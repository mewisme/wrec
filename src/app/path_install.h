#pragma once

#include "result.h"

#include <string>

struct InstallOptions;

Status installToPath(const InstallOptions &options);
Status uninstallFromPath(const InstallOptions &options);
Status ensureAppShortcuts(const std::wstring &exePath);
Status removeAppShortcuts();
Result<std::wstring> currentExePath();
std::wstring defaultInstallDir();
const char *detectInstallSource();
const char *installSourceLabel(const char *source);
