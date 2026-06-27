#include "logging.h"

#include <Windows.h>
#include <comdef.h>
#include <iostream>
#include <sstream>

namespace {
bool g_verbose = false;
bool g_json = false;
} // namespace

void logSetVerbose(bool verbose) { g_verbose = verbose; }
void logSetJson(bool json) { g_json = json; }
bool logIsJson() { return g_json; }

void logMessage(LogLevel level, const std::string &message) {
  if (level == LogLevel::Verbose && !g_verbose) {
    return;
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
