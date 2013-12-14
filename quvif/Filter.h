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

#include <streams.h>
#include <memory>

class CQuviOutputPin;
class QuviMedia;

#define QuviSourceFilterName L"Quvi Source Filter"

class __declspec(uuid("6F24EFD1-07F1-4E01-B45E-2664B579D8E9"))
CQuviSourceFilter final
	: public CCritSec
	, public CBaseFilter
	, public IFileSourceFilter
	//, public IAMOpenProgress
	//, public IAMStreamSelect
{
	friend class CQuviOutputPin;

public:
	CQuviSourceFilter(LPUNKNOWN pUnk, HRESULT* phr);
	~CQuviSourceFilter();

	DECLARE_IUNKNOWN;

	static const AM_MEDIA_TYPE QuviMediaType;

	static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);

	// CBaseFilter
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
	int GetPinCount() override;
	CBasePin* GetPin(int n) override;

	// IFileSourceFilter
	STDMETHODIMP GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt) override;
	STDMETHODIMP Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt) override;

private:
	typedef CBaseFilter super;
	CQuviOutputPin* m_pPin;
	std::unique_ptr<QuviMedia> m_pQuvi;
};
