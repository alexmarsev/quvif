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

#include <string>
#include <vector>

class QuviMediaInfo {
	class Quvi final {
		quvi_t q = {};
	public:
		Quvi();
		~Quvi() { quvi_close(&q); }
		operator quvi_t&() { return q; }
	};

	class QuviParse final {
		quvi_media_t qm = {};
	public:
		QuviParse(Quvi& q, const std::wstring& url);
		~QuviParse() { quvi_parse_close(&qm); }
		operator quvi_media_t&() { return qm; }
	};

	std::wstring m_ourl;
	std::wstring m_url;
	std::string m_murl;
	std::wstring m_title;
	std::string m_contentType;
	uint64_t m_contentLength = 0;

	Quvi m_q;
	QuviParse m_qp;

protected:
	CURL* m_curl = nullptr;

public:
	QuviMediaInfo(std::wstring&& url);
	virtual ~QuviMediaInfo() {}

	const std::wstring& GetOriginalUrl() const { return m_ourl; }
	const std::wstring& GetUrl() const { return m_url; }
	const std::string& GetMultibyteUrl() const { return m_murl; }
	const std::wstring& GetTitle() const { return m_title; }
	const std::string& GetContentType() const { return m_contentType; }
	uint64_t GetContentLength() const { return m_contentLength; }
};

class QuviMediaBackend {
public:
	virtual ~QuviMediaBackend() {};
	virtual bool Get(uint64_t offset, size_t length, char* dest) = 0;
	virtual uint64_t GetCurrentLength() = 0;
	virtual uint64_t GetTotalLength() = 0;
};

class QuviMedia final : public QuviMediaInfo {
	std::vector<std::unique_ptr<QuviMediaBackend>> m_backends;

	CURLSH* m_curlsh;
	static void CurlShareLockFunction(CURL* handle, curl_lock_data data, curl_lock_access access, void* userptr);
	static void CurlShareUnlockFunction(CURL* handle, curl_lock_data data, void* userptr);
	typedef std::array<std::mutex, CURL_LOCK_DATA_LAST> CurlSharedLock;
	CurlSharedLock m_curlshLock;

public:
	QuviMedia(std::wstring&& url);
	~QuviMedia();

	const std::vector<std::unique_ptr<QuviMediaBackend>>& GetBackends() { return m_backends; }
};
