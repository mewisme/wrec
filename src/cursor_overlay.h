#pragma once

#include <Windows.h>

#include <cstdint>
#include <vector>

enum class CursorOutsidePolicy { Hide, DrawAlways };

struct CursorOverlayOptions {
  HWND targetWindow = nullptr;
  bool enabled = true;
  CursorOutsidePolicy outsidePolicy = CursorOutsidePolicy::Hide;
};

void compositeCursor(std::vector<uint8_t> &frameBuffer, uint32_t width,
                     uint32_t height, const CursorOverlayOptions &options);
