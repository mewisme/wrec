#include "hotkeys.h"

#include "logging.h"
#include "result.h"

namespace {
constexpr int HOTKEY_STOP = 1;
constexpr int HOTKEY_PAUSE = 2;
constexpr int HOTKEY_QUIT = 3;
} // namespace

Status HotkeyManager::registerAll() {
  if (!RegisterHotKey(nullptr, HOTKEY_STOP, MOD_CONTROL | MOD_ALT, 'S')) {
    return Status::fail("RegisterHotKey Ctrl+Alt+S failed");
  }
  if (!RegisterHotKey(nullptr, HOTKEY_PAUSE, MOD_CONTROL | MOD_ALT, 'P')) {
    UnregisterHotKey(nullptr, HOTKEY_STOP);
    return Status::fail("RegisterHotKey Ctrl+Alt+P failed");
  }
  if (!RegisterHotKey(nullptr, HOTKEY_QUIT, MOD_CONTROL | MOD_ALT, 'Q')) {
    UnregisterHotKey(nullptr, HOTKEY_STOP);
    UnregisterHotKey(nullptr, HOTKEY_PAUSE);
    return Status::fail("RegisterHotKey Ctrl+Alt+Q failed");
  }
  return Status::ok();
}

void HotkeyManager::unregisterAll() {
  UnregisterHotKey(nullptr, HOTKEY_STOP);
  UnregisterHotKey(nullptr, HOTKEY_PAUSE);
  UnregisterHotKey(nullptr, HOTKEY_QUIT);
}

HotkeyAction HotkeyManager::pollAction() {
  MSG msg{};
  if (!PeekMessageW(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
    return HotkeyAction::None;
  }
  switch (static_cast<int>(msg.wParam)) {
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
