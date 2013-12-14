/*
 * This file is part of quvif.
 *
 * Copyright (C) 2013 Alex Marsev
 *
 * quvif is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * quvif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "stdafx.h"
#include "Quvi.h"

std::wstring WideFromMultibyte(const char* src) {
	std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>> convert;
	return convert.from_bytes(src);
}

std::string MultibyteFromWide(const wchar_t* src) {
	std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>> convert;
	return convert.to_bytes(src);
}

QuviMediaInfo::Quvi::Quvi() {
	QUVIcode qc = quvi_init(&q);
	if (qc != QUVI_OK)
		throw qc;
}

QuviMediaInfo::QuviParse::QuviParse(Quvi& q, const std::wstring& url) {
	std::string murl(MultibyteFromWide(url.c_str()));
	QUVIcode qc = quvi_parse(q, murl.empty() ? "" : &murl[0], &qm);
	if (qc != QUVI_OK)
		throw qc;
}

QuviMediaInfo::QuviMediaInfo(std::wstring&& url)
	: m_ourl(std::move(url))
	, m_length(0)
	, m_qp(m_q, m_ourl)
	, m_curl(nullptr)
{
	QUVIcode qc;

	char* infoUrl = nullptr;
	qc = quvi_getprop(m_qp, QUVIPROP_MEDIAURL, &infoUrl);
	if (qc != QUVI_OK || !infoUrl)
		throw qc;
	m_url = WideFromMultibyte(infoUrl);
	m_murl = infoUrl;

	char* infoTitle = nullptr;
	qc = quvi_getprop(m_qp, QUVIPROP_PAGETITLE, &infoTitle);
	if (qc == QUVI_OK && infoTitle) {
		// not nice to expect the page to be encoded in utf-8, but quvi doesn't leave much choice
		std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
		m_title = convert.from_bytes(infoTitle);
	}

	double len = 0;
	qc = quvi_getprop(m_qp, QUVIPROP_MEDIACONTENTLENGTH, &len);
	if (qc == QUVI_OK && len > 0)
		m_length = (uint64_t)len;

	qc = quvi_getinfo(m_q, QUVIINFO_CURL, &m_curl);
	if (qc != QUVI_OK || !m_curl)
		throw qc;
}

std::future<void> QuviMedia::Promise(size_t index) {
	assert(std::try_lock(m_workerMutex) == 0); // expects outside lock

	m_promises.emplace_back(index, std::promise<void>());

	if (m_bWorkerInactive) { // restart worker thread if needed
		m_worker.detach();
		m_worker = std::thread(std::bind(&QuviMedia::Loop, this));
		m_bWorkerInactive = false;
	}

	return m_promises.back().second.get_future();
}

size_t QuviMedia::CurlCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	auto& data = *static_cast<CurlCallbackData*>(userdata);
	const size_t gotnow = size * nmemb;

	assert(data.undone);
	if (!data.undone || data.owner->m_bDestroying)
		return gotnow + 1;

	const size_t topacket = std::min(data.packet.size() - data.storing, gotnow);

	if (topacket) {
		memcpy(&data.packet[data.storing], ptr, topacket);
		data.storing += topacket;
	}

	if (data.storing == data.packet.size()) {
		// copy packet to cache
		if (!data.owner->ToCache(data))
			return gotnow + 1;

		// and remember possible stub
		if (const size_t tostub = gotnow - topacket) {
			memcpy(data.packet.data(), ptr + topacket, tostub);
			data.storing += tostub;
		}
	}

	return gotnow;
}

bool QuviMedia::ToCache(CurlCallbackData& data) {
	std::lock_guard<std::mutex> lock(m_workerMutex);

	// place into cache
	assert(!m_cache[data.current]);
	m_cache[data.current] = std::make_unique<CachePacket>(data.packet);

	// fulfill promises
	for (auto it = m_promises.begin(); it != m_promises.end();) {
		if (it->first == data.current) {
			it->second.set_value();
			it = m_promises.erase(it);
		} else {
			it++;
		}
	}

	// update data state
	data.storing = 0;
	data.current++;
	assert(data.undone > 0);
	data.undone--;

	// return false if next promise requires a jump
	if (data.undone && !m_promises.empty()) {
		// TODO: don't do this if the server doesn't support http range requests
		const size_t next = m_promises.front().first;
		assert(!m_cache[next]);
		if (next < data.current || next > data.current + 64) {
			assert(data.current + data.undone > next);
			return false;
		}
	}

	return true;
}

void QuviMedia::Loop() {
	static_assert(CURL_MAX_WRITE_SIZE < CachePacketSize, "the packet cannot hold...");

	CurlCallbackData data(this);
	curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, CurlCallback);
	curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &data);

	auto setRange = [&]() -> bool {
		size_t left = 0, right = 0;
		{
			std::lock_guard<std::mutex> lock(m_workerMutex);
			
			if (m_promises.empty()) {
				// find first missing packet
				for (; left < m_cache.size() && m_cache[left]; ++left);
			} else {
				// or use first unfulfilled promise
				left += m_promises.front().first;
				assert(!m_cache[left]);
			}

			// got it all
			if (left == m_cache.size())
				return false;

			// determine how far to go
			for (right = left + 1; right < m_cache.size() && !m_cache[right]; ++right);
		}
		assert(left < right);
		assert(right <= m_cache.size());

		// convert to byte indexes
		const uint64_t leftb = (uint64_t)left * CachePacketSize;
		const uint64_t rightb = std::min((uint64_t)right * CachePacketSize - 1, GetLength() - 1);
		assert(leftb <= rightb);
		assert(rightb < GetLength());

		// set http range
		const std::string range = std::to_string(leftb) + "-" + std::to_string(rightb);
		curl_easy_setopt(m_curl, CURLOPT_RANGE, range.c_str());

		// set callback data
		assert(!data.storing);
		data.current = left;
		data.undone = right - left;

		return true;
	};

	while (!m_bDestroying && setRange()) {
		const CURLcode cc = curl_easy_perform(m_curl);
		if (cc == CURLE_OK && data.undone) {
			// copy possible eof stub
			assert(data.undone == 1);
			ToCache(data);
			assert(data.current == m_cache.size());
		}
	}

	// TODO: try to lower curl timeout on destruction
}

QuviMedia::QuviMedia(std::wstring&& url)
	: QuviMediaInfo(std::move(url))
	, m_bWorkerInactive(false)
	, m_bDestroying(false)
{
	size_t len = (size_t)(GetLength() / CachePacketSize);
	if (GetLength() - len * CachePacketSize)
		len++;
	m_cache.resize(len);
	m_worker = std::thread(std::bind(&QuviMedia::Loop, this));
}

QuviMedia::~QuviMedia() {
	m_bDestroying = true;
	m_worker.join();
}


bool QuviMedia::Get(uint64_t offset, size_t length, char* dest) {
	assert(offset >= 0 && length > 0 && dest);
	while (length > 0) {
		const size_t packetindex = (size_t)(offset / QuviMedia::CachePacketSize);
		const size_t packetoffset = (size_t)(offset - packetindex * QuviMedia::CachePacketSize);
		const size_t tocopy = std::min(length, QuviMedia::CachePacketSize - packetoffset);
		assert(packetoffset < QuviMedia::CachePacketSize);
		assert(tocopy <= QuviMedia::CachePacketSize);
		assert(packetoffset + tocopy <= QuviMedia::CachePacketSize);

		const auto& packet = m_cache[packetindex];
		std::future<void> ft;

		{
			std::lock_guard<std::mutex> lock(m_workerMutex);
			if (!packet)
				ft = Promise(packetindex);
		}
		
		// block until the promise is fulfilled
		if (ft.valid())
			ft.get();
		
		{
			std::lock_guard<std::mutex> lock(m_workerMutex);
			memcpy(dest, packet->data() + packetoffset, tocopy);
		}

		offset += tocopy;
		length -= tocopy;
		dest += tocopy;
	}

	// TODO: handle errors
	return true;
}
