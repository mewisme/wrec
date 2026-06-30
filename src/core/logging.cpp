#include "logging.h"

#include <Windows.h>
#include <comdef.h>
#include <cwchar>
#include <functional>
#include <iostream>
#include <sstream>

std::string wideToUtf8(const std::wstring &wide);

namespace {
bool g_verbose = false;
bool g_json = false;
LogGuiSink g_guiSink;

void writeConsoleStream(DWORD stdHandle, FILE *file, const std::wstring &text) {
  HANDLE handle = GetStdHandle(stdHandle);
  DWORD mode = 0;
  if (handle != INVALID_HANDLE_VALUE && handle != nullptr &&
      GetConsoleMode(handle, &mode) != 0) {
    DWORD written = 0;
    WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()),
                  &written, nullptr);
    return;
  }
  const std::string utf8 = wideToUtf8(text);
  fwrite(utf8.data(), 1, utf8.size(), file);
  fflush(file);
}
} // namespace

void initConsoleEncoding() {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
}

void writeStdout(const std::wstring &text) {
  writeConsoleStream(STD_OUTPUT_HANDLE, stdout, text);
}

void writeStderr(const std::wstring &text) {
  writeConsoleStream(STD_ERROR_HANDLE, stderr, text);
}

void logSetVerbose(bool verbose) { g_verbose = verbose; }
void logSetJson(bool json) { g_json = json; }
bool logIsJson() { return g_json; }
void logSetGuiSink(LogGuiSink sink) { g_guiSink = std::move(sink); }

void logMessage(LogLevel level, const std::string &message) {
  if (level == LogLevel::Verbose && !g_verbose) {
    return;
  }
  if (g_guiSink && level != LogLevel::Verbose) {
    g_guiSink(level, message);
  }
  if (g_json) {
    return;
  }
  std::ostream &out = (level == LogLevel::Error) ? std::cerr : std::cout;
  if (level == LogLevel::Error) {
    out << "error: ";
  } else if (level == LogLevel::Verbose) {
    out << "verbose: ";
  }
  out << message << '\n';
}

void logJsonEvent(const std::string &event, const std::string &extraFields) {
  if (!g_json) {
    return;
  }
  std::ostringstream oss;
  oss << "{\"event\":\"" << event << '"';
  if (!extraFields.empty()) {
    oss << ',' << extraFields;
  }
  oss << "}\n";
  std::cerr << oss.str();
}

std::string formatHresult(long hr) {
  _com_error err(hr);
  std::wstring wmsg = err.ErrorMessage();
  if (!wmsg.empty()) {
    return wideToUtf8(wmsg) + " (0x" +
           std::to_string(static_cast<unsigned long>(hr)) + ')';
  }
  return "HRESULT 0x" + std::to_string(static_cast<unsigned long>(hr));
}

std::string wideToUtf8(const std::wstring &wide) {
  if (wide.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring utf8ToWide(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      wide.data(), size);
  return wide;
}

bool wideEqualsIgnoreCase(const std::wstring &a, const std::wstring &b) {
  if (a.size() != b.size()) {
    return false;
  }
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
