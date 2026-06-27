#include "hotkeys.h"

#include "logging.h"
#include "result.h"

namespace {
constexpr int HOTKEY_STOP = 1;
constexpr int HOTKEY_PAUSE = 2;
constexpr int HOTKEY_QUIT = 3;

void ensureThreadMessageQueue() {
  MSG msg{};
  PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
}

HotkeyAction drainHotkeyMessages() {
  HotkeyAction action = HotkeyAction::None;
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_HOTKEY) {
      action = hotkeyActionFromId(static_cast<int>(msg.wParam));
    }
    // ponytail: drop other messages; this thread is not a UI message loop
  }
  return action;
}
} // namespace

HotkeyAction hotkeyActionFromId(int id) {
  switch (id) {
  case HOTKEY_STOP:
    return HotkeyAction::StopOrStart;
  case HOTKEY_PAUSE:
    return HotkeyAction::PauseToggle;
  case HOTKEY_QUIT:
    return HotkeyAction::Quit;
  default:
    return HotkeyAction::None;
  }
}

Status registerHotKeys(HWND hwnd) {
  if (hwnd == nullptr) {
    ensureThreadMessageQueue();
  }
  if (!RegisterHotKey(hwnd, HOTKEY_STOP, MOD_CONTROL | MOD_ALT, 'S')) {
    return Status::fail("RegisterHotKey Ctrl+Alt+S failed");
  }
  if (!RegisterHotKey(hwnd, HOTKEY_PAUSE, MOD_CONTROL | MOD_ALT, 'P')) {
    UnregisterHotKey(hwnd, HOTKEY_STOP);
    return Status::fail("RegisterHotKey Ctrl+Alt+P failed");
  }
  if (!RegisterHotKey(hwnd, HOTKEY_QUIT, MOD_CONTROL | MOD_ALT, 'Q')) {
    UnregisterHotKey(hwnd, HOTKEY_STOP);
    UnregisterHotKey(hwnd, HOTKEY_PAUSE);
    return Status::fail("RegisterHotKey Ctrl+Alt+Q failed");
  }
  return Status::ok();
}

void unregisterHotKeys(HWND hwnd) {
  UnregisterHotKey(hwnd, HOTKEY_STOP);
  UnregisterHotKey(hwnd, HOTKEY_PAUSE);
  UnregisterHotKey(hwnd, HOTKEY_QUIT);
}

HotkeyAction consumePendingHotkey(std::atomic<int> *pending) {
  if (pending == nullptr) {
    return HotkeyAction::None;
  }
  return static_cast<HotkeyAction>(
      pending->exchange(static_cast<int>(HotkeyAction::None)));
}

Status HotkeyManager::registerAll() {
  if (registered_) {
    return Status::ok();
  }
  const Status st = registerHotKeys(nullptr);
  if (st.isOk()) {
    registered_ = true;
  }
  return st;
}

void HotkeyManager::unregisterAll() {
  if (!registered_) {
    return;
  }
  unregisterHotKeys(nullptr);
  registered_ = false;
}

HotkeyAction HotkeyManager::pollAction() { return drainHotkeyMessages(); }

void HotkeyManager::waitForMessagesMs(int ms) {
  MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(ms),
                            QS_ALLINPUT);
}
