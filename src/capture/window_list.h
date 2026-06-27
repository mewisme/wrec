#pragma once

#include "cli.h"
#include "result.h"

#include <Windows.h>

#include <string>
#include <vector>

struct WindowInfo {
  unsigned long pid = 0;
  HWND hwnd = nullptr;
  std::wstring exePath;
  std::wstring title;
  int width = 0;
  int height = 0;
  bool visible = false;
};

Status listWindows(const ListOptions &options);
Result<WindowInfo> resolveTargetWindow(const RecordOptions &options);
std::vector<WindowInfo> enumerateWindows(bool includeAll);
