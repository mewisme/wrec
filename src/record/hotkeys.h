#pragma once

#include "result.h"

#include <Windows.h>

enum class HotkeyAction { None, StopOrStart, PauseToggle, Quit };

class HotkeyManager {
public:
  Status registerAll();
  void unregisterAll();
  HotkeyAction pollAction();
};
