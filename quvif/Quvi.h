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

#pragma once

#include <curl/curl.h>
#include <quvi.h>

#include <array>
#include <future>
#include <list>
#include <mutex>
#include <string>

class QuviMediaInfo {
	class Quvi final {
		quvi_t q;
	public:
		Quvi();
		~Quvi() { quvi_close(&q); }
		operator quvi_t&() { return q; }
	};

	class QuviParse final {
		quvi_media_t qm;
	public:
		QuviParse(Quvi& q, const std::wstring& url);
		~QuviParse() { quvi_parse_close(&qm); }
		operator quvi_media_t&() { return qm; }
	};

	std::wstring m_ourl;
	std::wstring m_url;
	std::string m_murl;
	std::wstring m_title;
	uint64_t m_length;

	Quvi m_q;
	QuviParse m_qp;

protected:
	CURL* m_curl;

public:
	QuviMediaInfo(std::wstring&& url);
	virtual ~QuviMediaInfo() {}

	const std::wstring& GetOriginalUrl() const { return m_ourl; }
	const std::wstring& GetUrl() const { return m_url; }
	const std::string& GetMultibyteUrl() const { return m_murl; }
	const std::wstring& GetTitle() const { return m_title; }
	uint64_t GetLength() const { return m_length; }
};

class QuviMedia final : public QuviMediaInfo {
public:
	static const size_t CachePacketSize = 64 * 1024;

private:
	typedef std::array<char, CachePacketSize> CachePacket;
	std::vector<std::unique_ptr<CachePacket>> m_cache;

	std::thread m_worker;
	std::mutex m_workerMutex;
	bool m_bWorkerInactive;

	std::list<std::pair<size_t, std::promise<void>>> m_promises;
	std::future<void> Promise(size_t index);

	bool m_bDestroying;

	struct CurlCallbackData {
		QuviMedia* owner;
		CachePacket packet;
		size_t storing; // bytes
		size_t current; // packet
		size_t undone; // packets
		CurlCallbackData(QuviMedia* owner) 
			: owner(owner), storing(0), current(0), undone(0)
		{
			assert(owner);
		}
	};

	static size_t CurlCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

	bool ToCache(CurlCallbackData& data);

	void Loop();

public:
	QuviMedia(std::wstring&& url);
	~QuviMedia();

	bool Get(uint64_t offset, size_t length, char* dest);
};
