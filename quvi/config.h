#pragma once

#include <direct.h>

#define __PRETTY_FUNCTION__ __FUNCTION__
#define SCRIPTSDIR "./scripts"
#define VERSIONFILE "version"
#define PACKAGE_VERSION "libquvi-0.4.1"
#ifdef _WIN64
#	define CANONICAL_TARGET "x86_64-pc-msvc"
#else
#	define CANONICAL_TARGET "i686-pc-msvc"
#endif
