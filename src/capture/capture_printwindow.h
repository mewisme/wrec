#pragma once

#include "result.h"

#include <Windows.h>

#include <cstdint>
#include <vector>

struct PrintWindowFrame {
  std::vector<uint8_t> pixels;
  uint32_t width = 0;
  uint32_t height = 0;
};

bool isWindowOccluded(HWND hwnd);
Result<PrintWindowFrame> captureWindowPrintWindow(HWND hwnd);
