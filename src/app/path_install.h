#pragma once

#include "result.h"

#include <string>

struct InstallOptions;

Status installToPath(const InstallOptions &options);
Status uninstallFromPath(const InstallOptions &options);
std::wstring defaultInstallDir();
const char *detectInstallSource();
const char *installSourceLabel(const char *source);
