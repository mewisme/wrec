#pragma once

#include "scene.h"

#include <Windows.h>

#include <cstdint>
#include <vector>

enum class CursorOutsidePolicy { Hide, DrawAlways };

struct CursorOverlayOptions {
  HWND targetWindow = nullptr;
  bool enabled = true;
  CursorOutsidePolicy outsidePolicy = CursorOutsidePolicy::Hide;
};

struct SceneCursorSource {
  HWND hwnd = nullptr;
  int destX = 0;
  int destY = 0;
  int destW = 0;
  int destH = 0;
  ScaleMode scale = ScaleMode::Fit;
  uint32_t frameWidth = 0;
  uint32_t frameHeight = 0;
};

// Owned by the recording thread (RecorderManager); not shared across threads.
struct CursorOverlayCache {
  HCURSOR cursor = nullptr;
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  int hotX = 0;
  int hotY = 0;
  POINT lastScreenPos{};
  bool lastShowing = false;
  bool lastDrawn = false;
};

bool cursorOverlayNeedsRedraw(bool enabled,
                              const std::vector<SceneCursorSource> &sources,
                              CursorOverlayCache &cache);

void compositeCursor(std::vector<uint8_t> &frameBuffer, uint32_t width,
                     uint32_t height, const CursorOverlayOptions &options);

void compositeCursorOnScene(std::vector<uint8_t> &frameBuffer, uint32_t width,
                            uint32_t height, bool enabled,
                            const std::vector<SceneCursorSource> &sources,
                            CursorOverlayCache &cache);
