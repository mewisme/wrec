#include "path_install.h"

#include "cli.h"
#include "logging.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::wstring getEnvVar(const wchar_t *name) {
  wchar_t buf[MAX_PATH * 4]{};
  const DWORD len =
      GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
  if (len == 0 || len >= std::size(buf)) {
    return {};
  }
  return buf;
}

std::wstring normalizeDir(std::wstring path) {
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  return path;
}

bool equalsPathIgnoreCase(const std::wstring &a, const std::wstring &b) {
  return _wcsicmp(normalizeDir(a).c_str(), normalizeDir(b).c_str()) == 0;
}

Result<std::wstring> readUserPath() {
  wchar_t buf[32767]{};
  DWORD size = static_cast<DWORD>(std::size(buf));
  const LSTATUS status =
      RegGetValueW(HKEY_CURRENT_USER, L"Environment", L"Path",
                   RRF_RT_REG_EXPAND_SZ | RRF_RT_REG_SZ, nullptr, buf, &size);
  if (status != ERROR_SUCCESS) {
    return Result<std::wstring>::fail("Failed to read user PATH from registry");
  }
  return Result<std::wstring>::ok(std::wstring(buf));
}

Status writeUserPath(const std::wstring &path) {
  const LSTATUS status = RegSetKeyValueW(
      HKEY_CURRENT_USER, L"Environment", L"Path", REG_EXPAND_SZ, path.c_str(),
      static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
  if (status != ERROR_SUCCESS) {
    return Status::fail("Failed to write user PATH to registry");
  }

  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                      reinterpret_cast<LPARAM>(L"Environment"),
                      SMTO_ABORTIFHUNG, 5000, nullptr);
  return Status::ok();
}

std::vector<std::wstring> splitPathList(const std::wstring &pathList) {
  std::vector<std::wstring> parts;
  std::wstring current;
  for (wchar_t ch : pathList) {
    if (ch == L';') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current += ch;
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

std::wstring joinPathList(const std::vector<std::wstring> &parts) {
  std::wstring out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out += L';';
    }
    out += parts[i];
  }
  return out;
}

Result<std::wstring> currentExePath() {
  wchar_t buf[MAX_PATH]{};
  const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return Result<std::wstring>::fail(
        "Failed to resolve current executable path");
  }
  return Result<std::wstring>::ok(std::wstring(buf));
}

Status ensureDirectory(const std::wstring &dir) {
  std::error_code ec;
  fs::create_directories(fs::path(dir), ec);
  if (ec) {
    return Status::fail("Failed to create directory: " + wideToUtf8(dir));
  }
  return Status::ok();
}

Status copyCurrentExeTo(const std::wstring &destExe) {
  const auto src = currentExePath();
  if (!src.isOk()) {
    return Status::fail(src.error());
  }

  if (_wcsicmp(src.value().c_str(), destExe.c_str()) == 0) {
    return Status::ok();
  }

  if (!CopyFileW(src.value().c_str(), destExe.c_str(), FALSE)) {
    return Status::fail("CopyFile failed: " +
                        formatHresult(static_cast<long>(GetLastError())));
  }
  return Status::ok();
}

} // namespace

std::wstring defaultInstallDir() {
  const std::wstring home = getEnvVar(L"USERPROFILE");
  if (home.empty()) {
    return L"C:\\Users\\Public\\.local\\bin";
  }
  return normalizeDir(home + L"\\.local\\bin");
}

const char *detectInstallSource() {
  const auto pathResult = currentExePath();
  if (!pathResult.isOk()) {
    return "unknown";
  }

  std::wstring path = pathResult.value();
  std::transform(path.begin(), path.end(), path.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });

  // ponytail: path heuristics only; upgrade path is a sidecar marker at install
  // time
  if (path.find(L"\\scoop\\apps\\wrec\\") != std::wstring::npos ||
      path.find(L"\\scoop\\shims\\wrec.exe") != std::wstring::npos) {
    return "scoop";
  }
  if (path.find(L"\\microsoft\\winget\\") != std::wstring::npos) {
    return "winget";
  }

  const std::wstring destExe = defaultInstallDir() + L"\\wrec.exe";
  if (equalsPathIgnoreCase(pathResult.value(), destExe)) {
    return "install";
  }

  return "portable";
}

Status rejectIfPackageManaged(const char *command) {
  const char *src = detectInstallSource();
  if (std::strcmp(src, "winget") == 0 || std::strcmp(src, "scoop") == 0) {
    return Status::fail(std::string("wrec ") + command +
                        " is for manual ZIP installs only; this copy was "
                        "installed via " +
                        src);
  }
  return Status::ok();
}

Status installToPath(const InstallOptions &options) {
  if (auto st = rejectIfPackageManaged("install"); !st.isOk()) {
    return st;
  }

  const std::wstring dir =
      normalizeDir(options.dir.empty() ? defaultInstallDir() : options.dir);
  const std::wstring destExe = dir + L"\\wrec.exe";

  if (auto st = ensureDirectory(dir); !st.isOk()) {
    return st;
  }
  if (auto st = copyCurrentExeTo(destExe); !st.isOk()) {
    return st;
  }

  const auto pathResult = readUserPath();
  if (!pathResult.isOk()) {
    return Status::fail(pathResult.error());
  }

  auto parts = splitPathList(pathResult.value());
  const bool alreadyOnPath =
      std::any_of(parts.begin(), parts.end(), [&](const std::wstring &p) {
        return equalsPathIgnoreCase(p, dir);
      });

  if (!alreadyOnPath) {
    parts.push_back(dir);
    if (auto st = writeUserPath(joinPathList(parts)); !st.isOk()) {
      return st;
    }
  }

  logMessage(LogLevel::Info, "Installed wrec to " + wideToUtf8(destExe));
  if (!alreadyOnPath) {
    logMessage(LogLevel::Info, "Added to user PATH: " + wideToUtf8(dir));
  } else {
    logMessage(LogLevel::Info, "Already on user PATH: " + wideToUtf8(dir));
  }
  logMessage(LogLevel::Info,
             "Restart your terminal to use `wrec` from anywhere.");
  return Status::ok();
}

Status uninstallFromPath(const InstallOptions &options) {
  if (auto st = rejectIfPackageManaged("uninstall"); !st.isOk()) {
    return st;
  }

  const std::wstring dir =
      normalizeDir(options.dir.empty() ? defaultInstallDir() : options.dir);
  const std::wstring destExe = dir + L"\\wrec.exe";

  const auto pathResult = readUserPath();
  if (!pathResult.isOk()) {
    return Status::fail(pathResult.error());
  }

  auto parts = splitPathList(pathResult.value());
  const size_t before = parts.size();
  parts.erase(std::remove_if(parts.begin(), parts.end(),
                             [&](const std::wstring &p) {
                               return equalsPathIgnoreCase(p, dir);
                             }),
              parts.end());

  if (parts.size() != before) {
    if (auto st = writeUserPath(joinPathList(parts)); !st.isOk()) {
      return st;
    }
    logMessage(LogLevel::Info, "Removed from user PATH: " + wideToUtf8(dir));
  } else if (options.verbose) {
    logMessage(LogLevel::Verbose, "Install directory was not on user PATH");
  }

  const auto running = currentExePath();
  const bool runningFromInstall =
      running.isOk() && equalsPathIgnoreCase(running.value(), destExe);

  if (fs::exists(destExe)) {
    if (runningFromInstall) {
      logMessage(LogLevel::Info,
                 "Skipped deleting " + wideToUtf8(destExe) +
                     " (currently running). Delete manually after exit.");
    } else if (!DeleteFileW(destExe.c_str())) {
      return Status::fail("Failed to delete " + wideToUtf8(destExe));
    } else {
      logMessage(LogLevel::Info, "Deleted " + wideToUtf8(destExe));
    }
  }

  logMessage(LogLevel::Info, "Uninstall complete. Restart your terminal.");
  return Status::ok();
}
