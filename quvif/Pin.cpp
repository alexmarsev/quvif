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
#include "Pin.h"
#include "Filter.h"
#include "Quvi.h"

CQuviOutputPin::CQuviOutputPin(CQuviSourceFilter* pFilter, size_t index, CCritSec* pLock, HRESULT* phr)
	: CBasePin(L"Quvi Output Pin", pFilter, pLock, phr, (L"Output" + std::to_wstring(index)).c_str(), PINDIR_OUTPUT)
	, m_pFilter(pFilter)
	, m_index(index)
{
}

STDMETHODIMP CQuviOutputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
	if (riid == IID_IAsyncReader) {
		m_bQueriedAsyncReader = true;
		return GetInterface(static_cast<IAsyncReader*>(this), ppv);
	} else
		return super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CQuviOutputPin::CheckMediaType(const CMediaType* pmt) {
	CheckPointer(pmt, E_POINTER);
	return CMediaType(CQuviSourceFilter::QuviMediaType) == *pmt ? S_OK : S_FALSE;
}

HRESULT CQuviOutputPin::GetMediaType(int iPosition, CMediaType* pMediaType) {
	CheckPointer(pMediaType, E_POINTER);
	if (iPosition < 0)
		return E_INVALIDARG;
	if (iPosition > 1)
		return VFW_S_NO_MORE_ITEMS;
	*pMediaType = CQuviSourceFilter::QuviMediaType;
	return S_OK;
}

HRESULT CQuviOutputPin::CheckConnect(IPin* pPin) {
	CheckPointer(pPin, E_POINTER);
	m_bQueriedAsyncReader = false;
	return super::CheckConnect(pPin);
}

HRESULT CQuviOutputPin::CompleteConnect(IPin* pReceivePin) {
	CheckPointer(pReceivePin, E_POINTER);
	if (!m_bQueriedAsyncReader)
		return VFW_E_NO_TRANSPORT;
	return super::CompleteConnect(pReceivePin);
}

STDMETHODIMP CQuviOutputPin::BeginFlush() {
	// TODO: write me
	return E_NOTIMPL;
}

STDMETHODIMP CQuviOutputPin::EndFlush() {
	// TODO: write me
	return E_NOTIMPL;
}

STDMETHODIMP CQuviOutputPin::Length(LONGLONG* pTotal, LONGLONG* pAvailable) {
	CheckPointer(pTotal, E_POINTER);
	CheckPointer(pAvailable, E_POINTER);

	// TODO: handle zero length correctly
	// TODO: do something more smart to actual length
	// TODO: support untrustworthy file lengths
	const auto& backend = m_pFilter->m_pQuvi->GetBackends()[m_index];
	*pTotal = backend->GetTotalLength();
	*pAvailable = backend->GetCurrentLength();

	return S_OK;
}

STDMETHODIMP CQuviOutputPin::Request(IMediaSample* pSample, DWORD_PTR dwUser) {
	// TODO: write me
	return E_NOTIMPL;
}

STDMETHODIMP CQuviOutputPin::RequestAllocator(IMemAllocator* pPreferred, ALLOCATOR_PROPERTIES* pProps, IMemAllocator** ppActual) {
	// TODO: write me
	return E_NOTIMPL;
}

STDMETHODIMP CQuviOutputPin::SyncRead(LONGLONG llPosition, LONG lLength, BYTE* pBuffer) {
	CheckPointer(pBuffer, E_POINTER);

	const auto& backend = m_pFilter->m_pQuvi->GetBackends()[m_index];

	const uint64_t filelen = backend->GetTotalLength();

	if (llPosition < 0 || (uint64_t)llPosition >= filelen || lLength <= 0)
		return E_INVALIDARG;

	HRESULT ret = S_OK;

	if ((uint64_t)llPosition + lLength > filelen) {
		ret = S_FALSE;
		lLength = (LONG)(filelen - llPosition);
		assert(lLength > 0);
	}

	if (!backend->Get((uint64_t)llPosition, (size_t)lLength, (char*)pBuffer))
		ret = E_FAIL;

	return ret;
}

STDMETHODIMP CQuviOutputPin::SyncReadAligned(IMediaSample* pSample) {
	// TODO: write me
	return E_NOTIMPL;
}

STDMETHODIMP CQuviOutputPin::WaitForNext(DWORD dwTimeout, IMediaSample** ppSample, DWORD_PTR* pdwUser) {
	// TODO: write me
	return E_NOTIMPL;
}
