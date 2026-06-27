#include "record_options.h"

#include "logging.h"

#include <KnownFolders.h>
#include <Windows.h>
#include <shlobj.h>

#include <cwchar>
#include <iomanip>
#include <sstream>

namespace {

bool equalsIgnoreCase(const std::wstring &a, const std::wstring &b) {
  if (a.size() != b.size()) {
    return false;
  }
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool isAbsolutePath(const std::wstring &path) {
  return (path.size() >= 2 && path[1] == L':') ||
         (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
}

std::wstring resolveOutputDir(const std::wstring &dir) {
  if (!dir.empty()) {
    return dir;
  }
  return defaultOutputDir();
}

std::wstring joinPath(const std::wstring &dir, const std::wstring &file) {
  if (dir.empty()) {
    return file;
  }
  if (isAbsolutePath(file)) {
    return file;
  }
  if (dir.back() == L'\\' || dir.back() == L'/') {
    return dir + file;
  }
  return dir + L'\\' + file;
}

std::wstring makeAutoOutputName() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  std::wostringstream name;
  name << L"wrec-" << std::setfill(L'0') << std::setw(4) << st.wYear
       << std::setw(2) << st.wMonth << std::setw(2) << st.wDay << L'-'
       << std::setw(2) << st.wHour << std::setw(2) << st.wMinute << std::setw(2)
       << st.wSecond << L".mp4";
  return name.str();
}

Status ensureDirectoryExists(const std::wstring &dir) {
  if (dir.empty() || dir == L".") {
    return Status::ok();
  }
  const DWORD attr = GetFileAttributesW(dir.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    return Status::ok();
  }
  if (CreateDirectoryW(dir.c_str(), nullptr)) {
    return Status::ok();
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return Status::ok();
  }
  return Status::fail("Cannot create output directory: " + wideToUtf8(dir));
}

} // namespace

std::wstring defaultOutputDir() {
  PWSTR path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_DEFAULT, nullptr,
                                     &path)) &&
      path != nullptr) {
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
  }
  wchar_t home[MAX_PATH]{};
  const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return L"Videos";
  }
  return std::wstring(home) + L"\\Videos";
}

Result<PresetValues> presetValues(const std::wstring &name) {
  if (equalsIgnoreCase(name, L"low")) {
    return Result<PresetValues>::ok({24, 2'000'000});
  }
  if (equalsIgnoreCase(name, L"medium")) {
    return Result<PresetValues>::ok({30, 5'000'000});
  }
  if (equalsIgnoreCase(name, L"high")) {
    return Result<PresetValues>::ok({45, 7'000'000});
  }
  if (equalsIgnoreCase(name, L"ultra")) {
    return Result<PresetValues>::ok({60, 8'000'000});
  }
  if (equalsIgnoreCase(name, L"extreme")) {
    return Result<PresetValues>::ok({60, 12'000'000});
  }
  return Result<PresetValues>::fail(
      "Unknown preset: " + wideToUtf8(name) +
      " (expected low, medium, high, ultra, or extreme)");
}

void applyRecordPreset(RecordOptions &options) {
  const auto preset = presetValues(options.preset);
  if (!preset.isOk()) {
    return;
  }
  const PresetValues values = preset.value();
  if (!options.fpsExplicit) {
    options.fps = values.fps;
  }
  if (!options.bitrateExplicit) {
    options.bitrate = values.bitrate;
  }
}

Result<std::wstring> resolveRecordOutputPath(const RecordOptions &options) {
  const std::wstring dir = resolveOutputDir(options.outputDir);

  if (options.outputPath.empty()) {
    if (const auto st = ensureDirectoryExists(dir); !st.isOk()) {
      return Result<std::wstring>::fail(st.error());
    }
    return Result<std::wstring>::ok(joinPath(dir, makeAutoOutputName()));
  }

  std::wstring path = options.outputPath;
  if (!isAbsolutePath(path)) {
    path = joinPath(dir, path);
  }
  const size_t pos = path.find_last_of(L"\\/");
  const std::wstring parent =
      pos != std::wstring::npos ? path.substr(0, pos) : dir;
  if (const auto st = ensureDirectoryExists(parent); !st.isOk()) {
    return Result<std::wstring>::fail(st.error());
  }
  return Result<std::wstring>::ok(std::move(path));
}
