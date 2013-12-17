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

class CQuviSourceFilter;

class CQuviOutputPin final
	: public CBasePin
	, public IAsyncReader
{
public:
	CQuviOutputPin(CQuviSourceFilter* pFilter, size_t index, CCritSec* pLock, HRESULT* phr);

	DECLARE_IUNKNOWN;

	// CBasePin
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;
	HRESULT CheckMediaType(const CMediaType* pmt) override;
	HRESULT GetMediaType(int iPosition, CMediaType* pMediaType) override;
	HRESULT CheckConnect(IPin* pPin) override;
	HRESULT CompleteConnect(IPin* pReceivePin) override;

	// IAsyncReader
	STDMETHODIMP BeginFlush() override;
	STDMETHODIMP EndFlush() override;
	STDMETHODIMP Length(LONGLONG* pTotal, LONGLONG* pAvailable) override;
	STDMETHODIMP Request(IMediaSample* pSample, DWORD_PTR dwUser) override;
	STDMETHODIMP RequestAllocator(IMemAllocator* pPreferred, ALLOCATOR_PROPERTIES* pProps, IMemAllocator** ppActual) override;
	STDMETHODIMP SyncRead(LONGLONG llPosition, LONG lLength, BYTE* pBuffer) override;
	STDMETHODIMP SyncReadAligned(IMediaSample* pSample) override;
	STDMETHODIMP WaitForNext(DWORD dwTimeout, IMediaSample** ppSample, DWORD_PTR* pdwUser) override;

private:
	typedef CBasePin super;
	CQuviSourceFilter* m_pFilter;
	const size_t m_index;
	bool m_bQueriedAsyncReader = false;
};
