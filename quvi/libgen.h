#pragma once

#include <string.h>

char* dirname(char *path) {
	char* sep = strrchr(path, '/');
	if (!sep) sep = strrchr(path, '\\');
	if (sep) *sep = '\0';
	return path;
}
