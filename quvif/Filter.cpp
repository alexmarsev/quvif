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
#include "Filter.h"
#include "Pin.h"
#include "Quvi.h"

CQuviSourceFilter::CQuviSourceFilter(LPUNKNOWN pUnk, HRESULT* phr)
	: CBaseFilter(QuviSourceFilterName, pUnk, this, __uuidof(CQuviSourceFilter))
{
	if (phr)
		*phr = S_OK;
}

CQuviSourceFilter::~CQuviSourceFilter() {
}

const AM_MEDIA_TYPE CQuviSourceFilter::QuviMediaType = {
	MEDIATYPE_Stream, MEDIASUBTYPE_None, 1, 0, 1, FORMAT_None, nullptr, 0, nullptr
};

CUnknown* WINAPI CQuviSourceFilter::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
	CUnknown* ret = new(std::nothrow) CQuviSourceFilter(pUnk, phr);
	if (!ret && phr)
		*phr = E_OUTOFMEMORY;
	return ret;
}

STDMETHODIMP CQuviSourceFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
	if (riid == IID_IFileSourceFilter)
		return GetInterface(static_cast<IFileSourceFilter*>(this), ppv);
	else
		return super::NonDelegatingQueryInterface(riid, ppv);
}

int CQuviSourceFilter::GetPinCount() {
	return m_pins.size();
}

CBasePin* CQuviSourceFilter::GetPin(int n) {
	if (n < 0 || (size_t)n >= m_pins.size())
		return nullptr;
	return m_pins[n].get();
}

STDMETHODIMP CQuviSourceFilter::GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt) {
	CheckPointer(ppszFileName, E_POINTER);

	if (!m_pQuvi)
		return E_FAIL;

	// TODO: decide what exactly is appropriate to return as file name
	// TODO: add file extension
	const auto& name = m_pQuvi->GetTitle();
	const size_t namelen = sizeof(name[0]) * (name.length() + 1);
	*ppszFileName = static_cast<LPOLESTR>(CoTaskMemAlloc(namelen));
	if (!*ppszFileName)
		return E_OUTOFMEMORY;
	memcpy(*ppszFileName, name.data(), namelen);

	if (pmt)
		*pmt = QuviMediaType;

	return S_OK;
}

STDMETHODIMP CQuviSourceFilter::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt) {
	CheckPointer(pszFileName, E_POINTER);
	UNREFERENCED_PARAMETER(pmt);
	
	m_pQuvi.release();
	m_pins.clear();

	auto doBasicUrlCheck = [](const std::wstring& url) -> bool {
		const std::wstring http(L"http://");
		const std::wstring https(L"https://");
		return url.compare(0, http.size(), http) || url.compare(0, https.size(), https);
	};

	std::wstring url(pszFileName);
	DbgLog((LOG_TRACE, 2, L"trying to open %s", pszFileName));
	// do a basic url check first
	if (doBasicUrlCheck(url)) {
		try {
			// then try to init quvi
			m_pQuvi = std::make_unique<QuviMedia>(std::move(url));
		} catch (QUVIcode qc) {
			(qc); // silence unused variable warning in release builds
			DbgLog((LOG_TRACE, 1, L"opening %s failed, quvi code: %d", pszFileName, (int)qc));
		} catch (...) {
			DbgLog((LOG_TRACE, 1, L"opening %s failed, unknown exception", pszFileName));
		}
	} else {
		DbgLog((LOG_TRACE, 1, L"opening %s failed, it failed basic url check", pszFileName));
	}

	if (m_pQuvi) {
		try {
			// create output pins
			for (size_t i = 0; i < m_pQuvi->GetBackends().size(); i++) {
				HRESULT hr = S_OK;
				m_pins.emplace_back(std::make_unique<CQuviOutputPin>(this, i, this, &hr));
				if (hr != S_OK) {
					m_pins.clear();
					break;
				}
			}
		} catch (...) {
			m_pins.clear();
		}
		if (m_pins.empty())
			DbgLog((LOG_TRACE, 1, L"opening %s failed, unable to create output pins", pszFileName));
	}

	if (!m_pins.empty()) {
		DbgLog((LOG_TRACE, 1, L"sucessfully opened %s", pszFileName));
		return S_OK;
	} else {
		m_pQuvi.release();
		return E_FAIL;
	}
}
