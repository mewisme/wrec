#include "notification.h"

#include <Windows.h>
#include <shellapi.h>

namespace {

constexpr UINT kTrayIconId = 1;

HWND g_trayOwner = nullptr;
UINT g_trayCallbackMsg = 0;
HICON g_trayIcon = nullptr;

HWND messageHostWindow() {
  static HWND hwnd =
      CreateWindowExW(0, L"Message", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
  return hwnd;
}

HICON trayIconHandle() {
  if (g_trayIcon == nullptr) {
    g_trayIcon = LoadIconW(nullptr, IDI_APPLICATION);
  }
  return g_trayIcon;
}

void ephemeralTrayBalloon(HWND hwnd, const std::wstring &title,
                          const std::wstring &body) {
  if (hwnd == nullptr) {
    return;
  }

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(NOTIFYICONDATAW);
  nid.uVersion = NOTIFYICON_VERSION_4;
  nid.hWnd = hwnd;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_ICON | NIF_TIP;
  nid.hIcon = trayIconHandle();
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

} // namespace

bool trayIconInstall(HWND ownerHwnd, UINT callbackMsg) {
  if (ownerHwnd == nullptr) {
    return false;
  }

  const bool alreadyActive = trayIconIsActive();
  g_trayOwner = ownerHwnd;
  g_trayCallbackMsg = callbackMsg;

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(NOTIFYICONDATAW);
  nid.uVersion = NOTIFYICON_VERSION_4;
  nid.hWnd = ownerHwnd;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = callbackMsg;
  nid.hIcon = trayIconHandle();
  wcsncpy_s(nid.szTip, L"wrec", _TRUNCATE);

  return Shell_NotifyIconW(alreadyActive ? NIM_MODIFY : NIM_ADD, &nid) != FALSE;
}

void trayIconRemove() {
  if (g_trayOwner == nullptr) {
    return;
  }

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(NOTIFYICONDATAW);
  nid.hWnd = g_trayOwner;
  nid.uID = kTrayIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);

  g_trayOwner = nullptr;
  g_trayCallbackMsg = 0;
}

void trayIconShowBalloon(const std::wstring &title, const std::wstring &body) {
  if (!trayIconIsActive()) {
    return;
  }

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(NOTIFYICONDATAW);
  nid.uVersion = NOTIFYICON_VERSION_4;
  nid.hWnd = g_trayOwner;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
  nid.dwInfoFlags = NIIF_INFO;
  nid.hIcon = trayIconHandle();
  wcsncpy_s(nid.szTip, L"wrec", _TRUNCATE);
  wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool trayIconIsActive() { return g_trayOwner != nullptr; }

void notifyTrayBalloon(const std::wstring &title, const std::wstring &body) {
  if (trayIconIsActive()) {
    trayIconShowBalloon(title, body);
    return;
  }
  ephemeralTrayBalloon(messageHostWindow(), title, body);
}
