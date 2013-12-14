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

const AMOVIESETUP_MEDIATYPE sudPinTypes = {
	&MEDIATYPE_Stream, &MEDIASUBTYPE_NULL
};

const AMOVIESETUP_PIN sudPin = {
	L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, NULL, 1, &sudPinTypes
};

const AMOVIESETUP_FILTER sudQuviSource = {
	&__uuidof(CQuviSourceFilter), QuviSourceFilterName, MERIT_UNLIKELY, 1, &sudPin, CLSID_LegacyAmFilterCategory
};

CFactoryTemplate g_Templates[1] = {
	{ QuviSourceFilterName, &__uuidof(CQuviSourceFilter), CQuviSourceFilter::CreateInstance, nullptr, &sudQuviSource },
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI DllRegisterServer() {
	return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer() {
	return AMovieDllRegisterServer2(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE hDllHandle, DWORD dwReason, LPVOID lpReserved) {
	return DllEntryPoint(hDllHandle, dwReason, lpReserved);
}
