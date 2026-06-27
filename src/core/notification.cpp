#include "notification.h"

#include <Windows.h>
#include <shellapi.h>

namespace {

constexpr UINT kTrayIconId = 1;

HWND messageHostWindow() {
  static HWND hwnd =
      CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                      GetModuleHandleW(nullptr), nullptr);
  return hwnd;
}

} // namespace

void notifyTrayBalloon(const std::wstring &title, const std::wstring &body) {
  const HWND hwnd = messageHostWindow();
  if (hwnd == nullptr) {
    return;
  }

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_ICON | NIF_TIP;
  nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcsncpy_s(nid.szTip, L"wrec", _TRUNCATE);

  if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
    return;
  }

  nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
  nid.dwInfoFlags = NIIF_INFO;
  wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
  Shell_NotifyIconW(NIM_DELETE, &nid);
}
