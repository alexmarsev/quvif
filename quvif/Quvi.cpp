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
	qc = quvi_setopt(q, QUVIOPT_FORMAT, "best");
	if (qc != QUVI_OK)
		throw qc;
	qc = quvi_setopt(q, QUVIOPT_CATEGORY, QUVIPROTO_HTTP);
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
	, m_qp(m_q, m_ourl)
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

	char* infoContentType = nullptr;
	qc = quvi_getprop(m_qp, QUVIPROP_MEDIACONTENTTYPE, &infoContentType);
	if (qc == QUVI_OK && infoContentType)
		m_contentType = infoContentType;

	double len = 0;
	qc = quvi_getprop(m_qp, QUVIPROP_MEDIACONTENTLENGTH, &len);
	if (qc == QUVI_OK && len > 0)
		m_contentLength = (uint64_t)len;

	qc = quvi_getinfo(m_q, QUVIINFO_CURL, &m_curl);
	if (qc != QUVI_OK || !m_curl)
		throw qc;
}

class QuviSimpleStreamBackend final : public QuviMediaBackend {
	uint64_t m_length;
	CURL* m_curl;

	static const size_t CachePacketSize = 64 * 1024;
	typedef std::array<char, CachePacketSize> CachePacket;
	std::vector<std::unique_ptr<CachePacket>> m_cache;

	std::thread m_worker;
	std::mutex m_workerMutex;
	bool m_bWorkerInactive = false;

	bool m_bDestroying = false;

	struct CurlCallbackData {
		QuviSimpleStreamBackend* owner;
		CachePacket packet;
		size_t storing = 0; // bytes
		size_t current = 0; // packet
		size_t undone = 0; // packets
		CurlCallbackData(QuviSimpleStreamBackend* owner) : owner(owner) { assert(owner); }
	};
	static size_t CurlCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
		auto& data = *static_cast<CurlCallbackData*>(userdata);
		const size_t gotnow = size * nmemb;

		assert(data.undone);
		// abort if the filter is being destroyed
		// or the server sends more data than it should
		if (!data.undone || data.owner->m_bDestroying)
			return gotnow + 1;

		const size_t topacket = std::min(data.packet.size() - data.storing, gotnow);

		// copy to current packet
		if (topacket) {
			memcpy(&data.packet[data.storing], ptr, topacket);
			data.storing += topacket;
		}

		// if the packet is complete
		if (data.storing == data.packet.size()) {
			// copy packet to cache
			// and decide whether to abort the transfer
			if (!data.owner->ToCache(data))
				return gotnow + 1;

			// remember possible stub
			if (const size_t tostub = gotnow - topacket) {
				memcpy(data.packet.data(), ptr + topacket, tostub);
				data.storing += tostub;
			}
		}

		return gotnow;
	}
	bool ToCache(CurlCallbackData& data) {
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

		// update curl callback data
		assert(data.storing == CachePacketSize || data.current + 1 == m_cache.size());
		data.storing = 0;
		data.current++;
		assert(data.undone > 0);
		data.undone--;

		// abort the transfer if next promise requires a jump
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
	void Loop() {
		static_assert(CURL_MAX_WRITE_SIZE < CachePacketSize, "the packet cannot hold...");

		CurlCallbackData data(this);
		curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, CurlCallback);
		curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &data);

		auto setRange = [&]() -> bool {
			size_t left = 0, right = 0;
			{
				std::lock_guard<std::mutex> lock(m_workerMutex);

				if (!m_promises.empty()) {
					// use first unfulfilled promise
					left += m_promises.front().first;
					assert(!m_cache[left]);
				} else {
					// or first missing packet
					for (; left < m_cache.size() && m_cache[left]; ++left);
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
			const uint64_t rightb = std::min((uint64_t)right * CachePacketSize - 1, m_length - 1);
			assert(leftb <= rightb);
			assert(rightb < m_length);

			// set up http range
			const std::string range = std::to_string(leftb) + "-" + std::to_string(rightb);
			curl_easy_setopt(m_curl, CURLOPT_RANGE, range.c_str());

			// set curl callback data
			assert(!data.storing);
			data.current = left;
			data.undone = right - left;

			return true;
		};

		// while there are things to download
		while (!m_bDestroying && setRange()) {
			// download the range
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

	std::list<std::pair<size_t, std::promise<void>>> m_promises;
	std::future<void> Promise(size_t index) {
		assert(std::try_lock(m_workerMutex) == 0); // expects outside lock

		m_promises.emplace_back(index, std::promise<void>());

		if (m_bWorkerInactive) { // restart worker thread if needed
			m_worker.detach();
			m_worker = std::thread(std::bind(&QuviSimpleStreamBackend::Loop, this));
			m_bWorkerInactive = false;
		}

		return m_promises.back().second.get_future();
	}

public:
	QuviSimpleStreamBackend(uint64_t length, CURL* curl, CURLSH* curlsh)
		: m_length(length)
		, m_curl(curl_easy_duphandle(curl))
	{
		assert(m_curl); // TODO: throw exception
		assert(curlsh); // TODO: throw exception
		curl_easy_setopt(m_curl, CURLOPT_SHARE, curlsh);

		size_t packets = (size_t)(m_length / CachePacketSize); // full
		if (m_length - packets * CachePacketSize) // eof stub
			packets++;
		m_cache.resize(packets);
		m_worker = std::thread(std::bind(&QuviSimpleStreamBackend::Loop, this));
	}
	~QuviSimpleStreamBackend() {
		m_bDestroying = true;
		m_worker.join();
		curl_easy_setopt(m_curl, CURLOPT_SHARE, nullptr);
		curl_easy_cleanup(m_curl);
	}

	virtual bool Get(uint64_t offset, size_t length, char* dest) override {
		assert(offset >= 0 && length > 0 && dest);
		while (length > 0) {
			const size_t packetindex = (size_t)(offset / CachePacketSize);
			const size_t packetoffset = (size_t)(offset - packetindex * CachePacketSize);
			const size_t tocopy = std::min(length, CachePacketSize - packetoffset);
			assert(packetoffset < CachePacketSize);
			assert(tocopy <= CachePacketSize);
			assert(packetoffset + tocopy <= CachePacketSize);

			// look in the cache
			const auto& packet = m_cache[packetindex];
			std::future<void> ft;

			{
				// request the packet if the cache doesn't have it
				std::lock_guard<std::mutex> lock(m_workerMutex);
				if (!packet)
					ft = Promise(packetindex);
			}

			// block until the promise is fulfilled
			if (ft.valid())
				ft.get();

			{
				// copy the packet
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

	virtual uint64_t GetCurrentLength() override {
		return m_length;
	}

	virtual uint64_t GetTotalLength() override {
		return m_length;
	}
};

void QuviMedia::CurlShareLockFunction(CURL* handle, curl_lock_data data, curl_lock_access access, void* userptr) {
	UNREFERENCED_PARAMETER(handle);
	if (access == CURL_LOCK_ACCESS_SINGLE) {
		auto& locks = *static_cast<CurlSharedLock*>(userptr);
		locks[data].lock();
	}
}

void QuviMedia::CurlShareUnlockFunction(CURL* handle, curl_lock_data data, void* userptr) {
	UNREFERENCED_PARAMETER(handle);
	auto& locks = *static_cast<CurlSharedLock*>(userptr);
	locks[data].unlock();
}

QuviMedia::QuviMedia(std::wstring&& url)
	: QuviMediaInfo(std::move(url))
	, m_curlsh(curl_share_init())
{
	assert(m_curlsh); // TODO: throw exception
	curl_share_setopt(m_curlsh, CURLSHOPT_LOCKFUNC, CurlShareLockFunction);
	curl_share_setopt(m_curlsh, CURLSHOPT_UNLOCKFUNC, CurlShareUnlockFunction);
	curl_share_setopt(m_curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
	curl_share_setopt(m_curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
	curl_share_setopt(m_curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
	curl_share_setopt(m_curlsh, CURLSHOPT_USERDATA, &m_curlshLock);

	curl_easy_setopt(m_curl, CURLOPT_SHARE, m_curlsh);
	// TODO: ensure that cookies are properly inherited

	if (GetContentType() == "video/vnd.mpeg.dash.mpd") {
		// TODO: implement
		assert(false);
	} else {
		assert(m_backends.empty());
		m_backends.emplace_back(std::make_unique<QuviSimpleStreamBackend>(GetContentLength(), m_curl, m_curlsh));
	}
}

QuviMedia::~QuviMedia() {
	curl_easy_setopt(m_curl, CURLOPT_SHARE, nullptr);
	m_backends.clear();
	curl_share_cleanup(m_curlsh);
}
