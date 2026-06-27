#pragma once

#include <Windows.h>

#include <string>

struct WindowInfo {
  unsigned long pid = 0;
  HWND hwnd = nullptr;
  std::wstring exePath;
  std::wstring title;
  int width = 0;
  int height = 0;
  bool visible = false;
};
