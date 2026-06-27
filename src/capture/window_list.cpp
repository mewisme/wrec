#include "window_list.h"

#include "cli.h"

#include "logging.h"

#include <Psapi.h>
#include <Windows.h>


#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool isShellWindow(HWND hwnd) {
  wchar_t className[256]{};
  if (GetClassNameW(hwnd, className, 256) == 0) {
    return false;
  }
  static const wchar_t *kBlocked[] = {
      L"Shell_TrayWnd",
      L"Progman",
      L"WorkerW",
      L"Shell_SecondaryTrayWnd",
  };
  for (const wchar_t *blocked : kBlocked) {
    if (_wcsicmp(className, blocked) == 0) {
      return true;
    }
  }
  return false;
}

bool isTopLevelWindow(HWND hwnd) { return GetAncestor(hwnd, GA_ROOT) == hwnd; }

std::wstring queryProcessPath(unsigned long pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return L"<unknown>";
  }
  wchar_t path[MAX_PATH]{};
  DWORD size = MAX_PATH;
  if (!QueryFullProcessImageNameW(process, 0, path, &size)) {
    CloseHandle(process);
    return L"<unknown>";
  }
  CloseHandle(process);
  return path;
}

bool shouldInclude(const WindowInfo &info, bool includeAll) {
  if (includeAll) {
    return info.width > 0 && info.height > 0;
  }
  if (!info.visible) {
    return false;
  }
  if (info.title.empty()) {
    return false;
  }
  if (info.width <= 0 || info.height <= 0) {
    return false;
  }
  return true;
}

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
  auto *windows = reinterpret_cast<std::vector<WindowInfo> *>(lParam);
  if (!IsWindow(hwnd) || !isTopLevelWindow(hwnd)) {
    return TRUE;
  }

  const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (exStyle & WS_EX_TOOLWINDOW) {
    return TRUE;
  }
  if (isShellWindow(hwnd)) {
    return TRUE;
  }

  WindowInfo info{};
  info.hwnd = hwnd;
  info.visible = IsWindowVisible(hwnd) != FALSE;

  wchar_t title[512]{};
  GetWindowTextW(hwnd, title, 512);
  info.title = title;

  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) {
    return TRUE;
  }
  info.width = rect.right - rect.left;
  info.height = rect.bottom - rect.top;

  unsigned long pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  info.pid = pid;
  info.exePath = queryProcessPath(pid);

  windows->push_back(std::move(info));
  return TRUE;
}

std::wstring jsonEscape(const std::wstring &text) {
  std::wstring out;
  out.reserve(text.size());
  for (wchar_t ch : text) {
    switch (ch) {
    case L'\\':
      out += L"\\\\";
      break;
    case L'"':
      out += L"\\\"";
      break;
    case L'\n':
      out += L"\\n";
      break;
    case L'\r':
      out += L"\\r";
      break;
    case L'\t':
      out += L"\\t";
      break;
    default:
      out += ch;
      break;
    }
  }
  return out;
}

bool titleContainsCaseInsensitive(const std::wstring &haystack,
                                  const std::wstring &needle) {
  if (needle.empty()) {
    return false;
  }
  std::wstring lowerHay = haystack;
  std::wstring lowerNeedle = needle;
  std::transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(),
                 ::towlower);
  std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
                 ::towlower);
  return lowerHay.find(lowerNeedle) != std::wstring::npos;
}

int windowArea(const WindowInfo &info) { return info.width * info.height; }

std::wstring truncateWide(const std::wstring &text, size_t maxChars) {
  if (text.size() <= maxChars) {
    return text;
  }
  if (maxChars <= 3) {
    return text.substr(0, maxChars);
  }
  return text.substr(0, maxChars - 3) + L"...";
}

std::wstring exeBasename(const std::wstring &path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

} // namespace

std::vector<WindowInfo> enumerateWindows(bool includeAll) {
  std::vector<WindowInfo> windows;
  EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&windows));
  windows.erase(std::remove_if(windows.begin(), windows.end(),
                               [&](const WindowInfo &info) {
                                 return !shouldInclude(info, includeAll);
                               }),
                windows.end());
  std::sort(windows.begin(), windows.end(),
            [](const WindowInfo &a, const WindowInfo &b) {
              return a.title < b.title;
            });
  return windows;
}

Status listWindows(const ListOptions &options) {
  const auto windows = enumerateWindows(options.all);
  if (options.json) {
    std::wostringstream json;
    json << L"[";
    for (size_t i = 0; i < windows.size(); ++i) {
      const auto &w = windows[i];
      if (i > 0) {
        json << L",";
      }
      json << L"{" << L"\"pid\":" << w.pid << L"," << L"\"hwnd\":"
           << reinterpret_cast<unsigned long long>(w.hwnd) << L","
           << L"\"exe\":\"" << jsonEscape(w.exePath) << L"\","
           << L"\"title\":\"" << jsonEscape(w.title) << L"\"," << L"\"width\":"
           << w.width << L"," << L"\"height\":" << w.height << L","
           << L"\"visible\":" << (w.visible ? L"true" : L"false") << L"}";
    }
    json << L"]\n";
    writeStdout(json.str());
    return Status::ok();
  }

  std::wostringstream header;
  header << std::left << std::setw(8) << L"PID" << std::setw(14) << L"HWND"
         << std::setw(28) << L"EXE" << std::setw(32) << L"TITLE"
         << std::setw(12) << L"SIZE" << L"VIS\n";
  writeStdout(header.str());

  for (const auto &w : windows) {
    std::wostringstream size;
    size << w.width << L'x' << w.height;
    const std::wstring exeShort = truncateWide(exeBasename(w.exePath), 26);
    const std::wstring titleShort = truncateWide(w.title, 30);
    std::wostringstream row;
    row << std::left << std::setw(8) << w.pid << std::setw(14)
        << reinterpret_cast<unsigned long long>(w.hwnd) << std::setw(28)
        << exeShort << std::setw(32) << titleShort << std::setw(12)
        << size.str() << (w.visible ? L"yes" : L"no") << L'\n';
    writeStdout(row.str());
  }
  return Status::ok();
}

namespace {

Result<WindowInfo> resolveHwnd(unsigned long long hwndValue) {
  HWND hwnd = reinterpret_cast<HWND>(hwndValue);
  if (!IsWindow(hwnd)) {
    return Result<WindowInfo>::fail("Invalid HWND");
  }
  WindowInfo info{};
  info.hwnd = hwnd;
  info.visible = IsWindowVisible(hwnd) != FALSE;
  wchar_t title[512]{};
  GetWindowTextW(hwnd, title, 512);
  info.title = title;
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  info.width = rect.right - rect.left;
  info.height = rect.bottom - rect.top;
  unsigned long pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  info.pid = pid;
  info.exePath = queryProcessPath(pid);
  return Result<WindowInfo>::ok(std::move(info));
}

Result<WindowInfo> resolvePid(unsigned long pid) {
  const auto windows = enumerateWindows(false);
  std::vector<WindowInfo> matches;
  for (const auto &w : windows) {
    if (w.pid == pid) {
      matches.push_back(w);
    }
  }
  if (matches.empty()) {
    return Result<WindowInfo>::fail("No visible titled window found for PID");
  }
  auto best = std::max_element(matches.begin(), matches.end(),
                               [](const WindowInfo &a, const WindowInfo &b) {
                                 return windowArea(a) < windowArea(b);
                               });
  return Result<WindowInfo>::ok(*best);
}

Result<WindowInfo> resolveTitle(const std::wstring &titleText) {
  const auto windows = enumerateWindows(false);
  std::vector<WindowInfo> matches;
  for (const auto &w : windows) {
    if (titleContainsCaseInsensitive(w.title, titleText)) {
      matches.push_back(w);
    }
  }
  if (matches.empty()) {
    return Result<WindowInfo>::fail("No window matched --title");
  }
  if (matches.size() > 1) {
    return Result<WindowInfo>::fail(
        "Multiple windows matched --title; use --hwnd");
  }
  return Result<WindowInfo>::ok(matches.front());
}

} // namespace

Result<std::vector<WindowInfo>>
resolveTargetWindows(const RecordOptions &options) {
  std::vector<WindowInfo> targets;

  if (!options.customSources.empty()) {
    for (const auto &spec : options.customSources) {
      if (spec.hwnd != 0) {
        const auto r = resolveHwnd(spec.hwnd);
        if (!r.isOk()) {
          return Result<std::vector<WindowInfo>>::fail(r.error());
        }
        targets.push_back(r.value());
      } else if (spec.pid != 0) {
        const auto r = resolvePid(spec.pid);
        if (!r.isOk()) {
          return Result<std::vector<WindowInfo>>::fail(r.error());
        }
        targets.push_back(r.value());
      } else if (!spec.title.empty()) {
        const auto r = resolveTitle(spec.title);
        if (!r.isOk()) {
          return Result<std::vector<WindowInfo>>::fail(r.error());
        }
        targets.push_back(r.value());
      } else {
        return Result<std::vector<WindowInfo>>::fail(
            "Custom --source requires hwnd, pid, or title");
      }
    }
    return Result<std::vector<WindowInfo>>::ok(std::move(targets));
  }

  for (unsigned long long hwnd : options.hwnds) {
    const auto r = resolveHwnd(hwnd);
    if (!r.isOk()) {
      return Result<std::vector<WindowInfo>>::fail(r.error());
    }
    targets.push_back(r.value());
  }
  for (unsigned long pid : options.pids) {
    const auto r = resolvePid(pid);
    if (!r.isOk()) {
      return Result<std::vector<WindowInfo>>::fail(r.error());
    }
    targets.push_back(r.value());
  }
  for (const auto &titleText : options.titles) {
    const auto r = resolveTitle(titleText);
    if (!r.isOk()) {
      return Result<std::vector<WindowInfo>>::fail(r.error());
    }
    targets.push_back(r.value());
  }

  if (targets.empty()) {
    return Result<std::vector<WindowInfo>>::fail(
        "Specify at least one of -w/--hwnd, -p/--pid, or -t/--title");
  }
  return Result<std::vector<WindowInfo>>::ok(std::move(targets));
}

Result<WindowInfo> resolveTargetWindow(const RecordOptions &options) {
  const auto many = resolveTargetWindows(options);
  if (!many.isOk()) {
    return Result<WindowInfo>::fail(many.error());
  }
  if (many.value().size() != 1) {
    return Result<WindowInfo>::fail(
        "Specify exactly one target, or use resolveTargetWindows for multiple");
  }
  return Result<WindowInfo>::ok(many.value().front());
}
