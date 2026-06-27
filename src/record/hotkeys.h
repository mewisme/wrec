#pragma once

#include "result.h"

#include <Windows.h>

#include <atomic>

enum class HotkeyAction { None, StopOrStart, PauseToggle, Quit };

HotkeyAction hotkeyActionFromId(int id);

Status registerHotKeys(HWND hwnd = nullptr);
void unregisterHotKeys(HWND hwnd = nullptr);

HotkeyAction consumePendingHotkey(std::atomic<int> *pending);

class HotkeyManager {
public:
  Status registerAll();
  void unregisterAll();
  HotkeyAction pollAction();
  void waitForMessagesMs(int ms);

private:
  bool registered_ = false;
};
