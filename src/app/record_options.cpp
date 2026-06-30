#include "record_options.h"

#include "logging.h"
#include "record_preset.h"

#include <Windows.h>
#include <shlobj.h>

#include <iomanip>
#include <sstream>

namespace {

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

Result<RecordPreset> parseRecordPreset(const std::wstring &value) {
  if (wideEqualsIgnoreCase(value, L"low")) {
    return Result<RecordPreset>::ok(RecordPreset::Low);
  }
  if (wideEqualsIgnoreCase(value, L"medium")) {
    return Result<RecordPreset>::ok(RecordPreset::Medium);
  }
  if (wideEqualsIgnoreCase(value, L"high")) {
    return Result<RecordPreset>::ok(RecordPreset::High);
  }
  if (wideEqualsIgnoreCase(value, L"ultra")) {
    return Result<RecordPreset>::ok(RecordPreset::Ultra);
  }
  if (wideEqualsIgnoreCase(value, L"extreme")) {
    return Result<RecordPreset>::ok(RecordPreset::Extreme);
  }
  return Result<RecordPreset>::fail(
      "preset must be low, medium, high, ultra, or extreme");
}

const char *recordPresetName(RecordPreset preset) {
  switch (preset) {
  case RecordPreset::Low:
    return "low";
  case RecordPreset::Medium:
    return "medium";
  case RecordPreset::High:
    return "high";
  case RecordPreset::Ultra:
    return "ultra";
  case RecordPreset::Extreme:
    return "extreme";
  }
  return "medium";
}

Result<PresetValues> presetValues(RecordPreset preset) {
  switch (preset) {
  case RecordPreset::Low:
    return Result<PresetValues>::ok({24, 2'000'000});
  case RecordPreset::Medium:
    return Result<PresetValues>::ok({30, 5'000'000});
  case RecordPreset::High:
    return Result<PresetValues>::ok({45, 7'000'000});
  case RecordPreset::Ultra:
    return Result<PresetValues>::ok({60, 8'000'000});
  case RecordPreset::Extreme:
    return Result<PresetValues>::ok({60, 12'000'000});
  }
  return Result<PresetValues>::ok({30, 5'000'000});
}

void applyRecordPreset(RecordOptions &options) {
  const PresetValues values = presetValues(options.preset).value();
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
