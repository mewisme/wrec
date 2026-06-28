#include "post_compress.h"

#include "logging.h"

#include <Windows.h>

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

bool equalsIgnoreCase(const std::wstring &a, const std::wstring &b) {
  if (a.size() != b.size()) {
    return false;
  }
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring quoteArg(const std::wstring &arg) {
  std::wstring out;
  out.push_back(L'"');
  for (wchar_t ch : arg) {
    if (ch == L'"') {
      out.push_back(L'\\');
    }
    out.push_back(ch);
  }
  out.push_back(L'"');
  return out;
}

int targetBitrateBps(int sourceBitrate, CompressLevel level) {
  double factor = 1.0;
  switch (level) {
  case CompressLevel::Small:
    factor = 0.70;
    break;
  case CompressLevel::Medium:
    factor = 0.45;
    break;
  case CompressLevel::Aggressive:
    factor = 0.25;
    break;
  case CompressLevel::Off:
    return sourceBitrate;
  }
  const int target =
      static_cast<int>(static_cast<double>(sourceBitrate) * factor);
  return target < 100'000 ? 100'000 : target;
}

bool findFfmpeg(std::wstring &outPath) {
  wchar_t buffer[MAX_PATH]{};
  const DWORD found =
      SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buffer, nullptr);
  if (found == 0 || found >= MAX_PATH) {
    return false;
  }
  outPath = buffer;
  return true;
}

std::wstring tempPathNextTo(const std::wstring &inputPath) {
  const size_t slash = inputPath.find_last_of(L"\\/");
  const std::wstring dir =
      slash == std::wstring::npos ? L"." : inputPath.substr(0, slash + 1);
  const std::wstring base =
      slash == std::wstring::npos ? inputPath : inputPath.substr(slash + 1);
  const size_t dot = base.find_last_of(L'.');
  const std::wstring stem =
      dot == std::wstring::npos ? base : base.substr(0, dot);
  return dir + stem + L".wrec-compress.tmp.mp4";
}

bool fileSizeBytes(const std::wstring &path, uint64_t &size) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
    return false;
  }
  ULARGE_INTEGER li{};
  li.LowPart = data.nFileSizeLow;
  li.HighPart = data.nFileSizeHigh;
  size = li.QuadPart;
  return true;
}

std::string formatSizeMb(uint64_t bytes) {
  const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(2);
  ss << mb << " MB";
  return ss.str();
}

void deleteFileQuiet(const std::wstring &path) {
  if (!path.empty()) {
    DeleteFileW(path.c_str());
  }
}

} // namespace

Result<CompressLevel> parseCompressLevel(const std::wstring &value) {
  if (equalsIgnoreCase(value, L"off")) {
    return Result<CompressLevel>::ok(CompressLevel::Off);
  }
  if (equalsIgnoreCase(value, L"small")) {
    return Result<CompressLevel>::ok(CompressLevel::Small);
  }
  if (equalsIgnoreCase(value, L"medium")) {
    return Result<CompressLevel>::ok(CompressLevel::Medium);
  }
  if (equalsIgnoreCase(value, L"aggressive")) {
    return Result<CompressLevel>::ok(CompressLevel::Aggressive);
  }
  return Result<CompressLevel>::fail(
      "compress level must be off, small, medium, or aggressive");
}

const char *compressLevelName(CompressLevel level) {
  switch (level) {
  case CompressLevel::Off:
    return "off";
  case CompressLevel::Small:
    return "small";
  case CompressLevel::Medium:
    return "medium";
  case CompressLevel::Aggressive:
    return "aggressive";
  }
  return "off";
}

Status postCompressMp4WithFfmpeg(const std::wstring &inputPath,
                                 int sourceBitrate, CompressLevel level,
                                 std::atomic<bool> *stopRequested,
                                 std::string *saveSummary) {
  const auto setSummary = [&](const std::string &text) {
    if (saveSummary != nullptr) {
      *saveSummary = text;
    }
  };

  if (level == CompressLevel::Off) {
    return Status::ok();
  }

  std::wstring ffmpegPath;
  if (!findFfmpeg(ffmpegPath)) {
    const std::string notice =
        "Compression skipped: ffmpeg was not found in PATH. Install ffmpeg to "
        "use --compress.";
    logMessage(LogLevel::Info, notice);
    setSummary("Saved " + wideToUtf8(inputPath) +
               " (compression skipped: ffmpeg not in PATH)");
    return Status::ok();
  }

  const std::wstring tempPath = tempPathNextTo(inputPath);
  deleteFileQuiet(tempPath);

  uint64_t originalSize = 0;
  if (!fileSizeBytes(inputPath, originalSize)) {
    logMessage(LogLevel::Error,
               "Compression skipped: could not read output file size");
    setSummary("Saved " + wideToUtf8(inputPath) +
               " (compression skipped: file unreadable)");
    return Status::ok();
  }

  const int targetBps = targetBitrateBps(sourceBitrate, level);
  logMessage(LogLevel::Info,
             std::string("Starting post-recording compression (") +
                 compressLevelName(level) + ", target " +
                 std::to_string(targetBps) + " bps)");
  logMessage(LogLevel::Info, "Compressing output with ffmpeg...");

  const std::wstring cmd = quoteArg(ffmpegPath) + L" -y -i " +
                           quoteArg(inputPath) + L" -c:v libx264 -b:v " +
                           std::to_wstring(targetBps) +
                           L" -movflags +faststart -an " + quoteArg(tempPath);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};

  std::wstring cmdMutable = cmd;
  if (!CreateProcessW(nullptr, cmdMutable.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    logMessage(LogLevel::Error,
               "Compression failed: could not start ffmpeg (error " +
                   std::to_string(GetLastError()) + ')');
    setSummary("Saved " + wideToUtf8(inputPath) + " (compression failed)");
    return Status::ok();
  }

  CloseHandle(pi.hThread);

  bool cancelled = false;
  for (;;) {
    if (stopRequested != nullptr && stopRequested->load()) {
      cancelled = true;
      TerminateProcess(pi.hProcess, 1);
      break;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, 200);
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    if (wait == WAIT_FAILED) {
      break;
    }
  }

  DWORD exitCode = 1;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  CloseHandle(pi.hProcess);

  if (cancelled) {
    deleteFileQuiet(tempPath);
    logMessage(LogLevel::Info, "Compression cancelled; kept original file");
    setSummary("Saved " + wideToUtf8(inputPath) + " (compression cancelled)");
    return Status::ok();
  }

  if (exitCode != 0) {
    deleteFileQuiet(tempPath);
    logMessage(LogLevel::Error, "Compression failed: ffmpeg exited with code " +
                                    std::to_string(exitCode));
    setSummary("Saved " + wideToUtf8(inputPath) + " (compression failed)");
    return Status::ok();
  }

  uint64_t compressedSize = 0;
  if (!fileSizeBytes(tempPath, compressedSize) || compressedSize == 0) {
    deleteFileQuiet(tempPath);
    logMessage(LogLevel::Error,
               "Compression failed: ffmpeg produced no output");
    setSummary("Saved " + wideToUtf8(inputPath) + " (compression failed)");
    return Status::ok();
  }

  if (!MoveFileExW(tempPath.c_str(), inputPath.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    deleteFileQuiet(tempPath);
    logMessage(LogLevel::Error,
               "Compression failed: could not replace original file");
    setSummary("Saved " + wideToUtf8(inputPath) + " (compression failed)");
    return Status::ok();
  }

  const double reduction = originalSize > 0
                               ? (1.0 - static_cast<double>(compressedSize) /
                                            static_cast<double>(originalSize)) *
                                     100.0
                               : 0.0;
  std::ostringstream sizeMsg;
  sizeMsg.setf(std::ios::fixed);
  sizeMsg.precision(1);
  sizeMsg << formatSizeMb(originalSize) << " -> "
          << formatSizeMb(compressedSize) << " (" << reduction
          << "% reduction)";
  logMessage(LogLevel::Info, "Compression complete: " + sizeMsg.str());

  const std::string summary =
      "Saved compressed " + wideToUtf8(inputPath) + " (" + sizeMsg.str() + ')';
  setSummary(summary);
  return Status::ok();
}

#ifdef WREC_POST_COMPRESS_SELF_CHECK
namespace {
void postCompressSelfCheck() {
  assert(parseCompressLevel(L"off").isOk());
  assert(parseCompressLevel(L"SMALL").value() == CompressLevel::Small);
  assert(parseCompressLevel(L"bad").isOk() == false);
  assert(targetBitrateBps(5'000'000, CompressLevel::Small) == 3'500'000);
  assert(targetBitrateBps(5'000'000, CompressLevel::Medium) == 2'250'000);
  assert(targetBitrateBps(5'000'000, CompressLevel::Aggressive) == 1'250'000);
  assert(targetBitrateBps(100'000, CompressLevel::Aggressive) == 100'000);
}
} // namespace
static int kPostCompressSelfCheck = (postCompressSelfCheck(), 0);
#endif
