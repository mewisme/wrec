#include "cursor_overlay.h"

#include "compositor.h"
#include "logging.h"

#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstring>
#include <vector>

struct RectI {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

bool getTargetBounds(HWND hwnd, RectI &out) {
  RECT rect{};
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                      sizeof(rect)))) {
    out.left = rect.left;
    out.top = rect.top;
    out.right = rect.right;
    out.bottom = rect.bottom;
    return true;
  }
  if (GetWindowRect(hwnd, &rect)) {
    out.left = rect.left;
    out.top = rect.top;
    out.right = rect.right;
    out.bottom = rect.bottom;
    return true;
  }
  return false;
}

void alphaBlendPixel(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb,
                     uint8_t sa) {
  const float a = sa / 255.0f;
  dst[0] = static_cast<uint8_t>(sb * a + dst[0] * (1.0f - a));
  dst[1] = static_cast<uint8_t>(sg * a + dst[1] * (1.0f - a));
  dst[2] = static_cast<uint8_t>(sr * a + dst[2] * (1.0f - a));
  dst[3] = 255;
}

bool readCursorBitmap(HCURSOR cursor, std::vector<uint8_t> &pixels, int &width,
                      int &height, int &hotX, int &hotY) {
  ICONINFO info{};
  if (!GetIconInfo(cursor, &info)) {
    return false;
  }

  hotX = static_cast<int>(info.xHotspot);
  hotY = static_cast<int>(info.yHotspot);

  BITMAP bm{};
  HBITMAP colorBmp = info.hbmColor ? info.hbmColor : info.hbmMask;
  if (!colorBmp || GetObject(colorBmp, sizeof(bm), &bm) == 0) {
    if (info.hbmMask) {
      DeleteObject(info.hbmMask);
    }
    if (info.hbmColor) {
      DeleteObject(info.hbmColor);
    }
    return false;
  }

  width = bm.bmWidth;
  height = info.hbmColor ? bm.bmHeight : bm.bmHeight / 2;

  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  pixels.assign(static_cast<size_t>(width) * height * 4, 0);
  void *bits = nullptr;
  HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
  RECT fill{0, 0, width, height};
  FillRect(mem, &fill, brush);
  DeleteObject(brush);

  if (info.hbmColor) {
    HDC src = CreateCompatibleDC(screen);
    HGDIOBJ oldSrc = SelectObject(src, info.hbmColor);
    BitBlt(mem, 0, 0, width, height, src, 0, 0, SRCCOPY);
    SelectObject(src, oldSrc);
    DeleteDC(src);
  } else if (info.hbmMask) {
    // Monochrome cursor: draw mask XOR then AND
    DrawIconEx(mem, 0, 0, cursor, width, height, 0, nullptr, DI_MASK);
    DrawIconEx(mem, 0, 0, cursor, width, height, 0, nullptr, DI_IMAGE);
  }

  SelectObject(mem, old);
  if (bits) {
    std::memcpy(pixels.data(), bits, pixels.size());
  }
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);

  if (info.hbmMask) {
    DeleteObject(info.hbmMask);
  }
  if (info.hbmColor) {
    DeleteObject(info.hbmColor);
  }
  return true;
}

bool cursorInsideTarget(const POINT &screenPos, const RectI &bounds, int hotX,
                        int hotY) {
  const int x = screenPos.x - hotX;
  const int y = screenPos.y - hotY;
  return x >= bounds.left && y >= bounds.top && x < bounds.right &&
         y < bounds.bottom;
}

void drawCursorAt(std::vector<uint8_t> &frameBuffer, uint32_t width,
                  uint32_t height, const std::vector<uint8_t> &cursorPixels,
                  int cursorW, int cursorH, int frameLeft, int frameTop) {
  for (int y = 0; y < cursorH; ++y) {
    const int dstY = frameTop + y;
    if (dstY < 0 || dstY >= static_cast<int>(height)) {
      continue;
    }
    for (int x = 0; x < cursorW; ++x) {
      const int dstX = frameLeft + x;
      if (dstX < 0 || dstX >= static_cast<int>(width)) {
        continue;
      }
      const uint8_t *src =
          cursorPixels.data() + (static_cast<size_t>(y) * cursorW + x) * 4;
      uint8_t *dst =
          frameBuffer.data() + (static_cast<size_t>(dstY) * width + dstX) * 4;
      alphaBlendPixel(dst, src[2], src[1], src[0], src[3]);
    }
  }
}

bool cursorNearAnySource(const POINT &pt,
                         const std::vector<SceneCursorSource> &sources) {
  for (const SceneCursorSource &src : sources) {
    if (!src.hwnd) {
      continue;
    }
    RECT rect{};
    if (!GetWindowRect(src.hwnd, &rect)) {
      continue;
    }
    if (pt.x >= rect.left && pt.y >= rect.top && pt.x < rect.right &&
        pt.y < rect.bottom) {
      return true;
    }
  }
  return false;
}

bool cursorOverlayNeedsRedraw(bool enabled,
                              const std::vector<SceneCursorSource> &sources,
                              CursorOverlayCache &cache) {
  if (!enabled || sources.empty()) {
    return false;
  }

  CURSORINFO ci{};
  ci.cbSize = sizeof(ci);
  if (!GetCursorInfo(&ci)) {
    return cache.lastDrawn;
  }

  const bool showing = (ci.flags & CURSOR_SHOWING) != 0;
  const bool cursorNear = showing && cursorNearAnySource(ci.ptScreenPos, sources);
  const bool changed =
      ci.hCursor != cache.cursor || ci.ptScreenPos.x != cache.lastScreenPos.x ||
      ci.ptScreenPos.y != cache.lastScreenPos.y || showing != cache.lastShowing ||
      cursorNear != cache.lastDrawn;

  cache.lastScreenPos = ci.ptScreenPos;
  cache.lastShowing = showing;
  return changed;
}

void compositeCursor(std::vector<uint8_t> &frameBuffer, uint32_t width,
                     uint32_t height, const CursorOverlayOptions &options) {
  if (!options.enabled || !options.targetWindow) {
    return;
  }

  CURSORINFO ci{};
  ci.cbSize = sizeof(ci);
  if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)) {
    return;
  }

  RectI bounds{};
  if (!getTargetBounds(options.targetWindow, bounds)) {
    return;
  }

  std::vector<uint8_t> cursorPixels;
  int cursorW = 0;
  int cursorH = 0;
  int hotX = 0;
  int hotY = 0;
  if (!readCursorBitmap(ci.hCursor, cursorPixels, cursorW, cursorH, hotX,
                        hotY)) {
    return;
  }

  if (options.outsidePolicy == CursorOutsidePolicy::Hide &&
      !cursorInsideTarget(ci.ptScreenPos, bounds, hotX, hotY)) {
    return;
  }

  // Map screen position to frame-local coordinates
  const int frameLeft = ci.ptScreenPos.x - hotX - bounds.left;
  const int frameTop = ci.ptScreenPos.y - hotY - bounds.top;
  drawCursorAt(frameBuffer, width, height, cursorPixels, cursorW, cursorH,
               frameLeft, frameTop);
}

void compositeCursorOnScene(std::vector<uint8_t> &frameBuffer, uint32_t width,
                            uint32_t height, bool enabled,
                            const std::vector<SceneCursorSource> &sources,
                            CursorOverlayCache &cache) {
  if (!enabled || sources.empty()) {
    return;
  }

  CURSORINFO ci{};
  ci.cbSize = sizeof(ci);
  if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)) {
    return;
  }

  bool nearAnySource = false;
  for (const SceneCursorSource &src : sources) {
    if (!src.hwnd) {
      continue;
    }
    RECT rect{};
    if (!GetWindowRect(src.hwnd, &rect)) {
      continue;
    }
    if (ci.ptScreenPos.x >= rect.left && ci.ptScreenPos.y >= rect.top &&
        ci.ptScreenPos.x < rect.right && ci.ptScreenPos.y < rect.bottom) {
      nearAnySource = true;
      break;
    }
  }
  if (!nearAnySource) {
    cache.lastDrawn = false;
    return;
  }

  if (cache.cursor != ci.hCursor) {
    cache.cursor = ci.hCursor;
    if (!readCursorBitmap(ci.hCursor, cache.pixels, cache.width, cache.height,
                          cache.hotX, cache.hotY)) {
      cache.cursor = nullptr;
      return;
    }
  }

  const int cursorX = ci.ptScreenPos.x - cache.hotX;
  const int cursorY = ci.ptScreenPos.y - cache.hotY;

  for (auto it = sources.rbegin(); it != sources.rend(); ++it) {
    const SceneCursorSource &src = *it;
    if (!src.hwnd) {
      continue;
    }
    RectI bounds{};
    if (!getTargetBounds(src.hwnd, bounds)) {
      continue;
    }
    if (cursorX < bounds.left || cursorY < bounds.top ||
        cursorX >= bounds.right || cursorY >= bounds.bottom) {
      continue;
    }

    const int windowW = bounds.right - bounds.left;
    const int windowH = bounds.bottom - bounds.top;
    if (windowW <= 0 || windowH <= 0) {
      continue;
    }

    const uint32_t frameW =
        src.frameWidth > 0 ? src.frameWidth : static_cast<uint32_t>(windowW);
    const uint32_t frameH =
        src.frameHeight > 0 ? src.frameHeight : static_cast<uint32_t>(windowH);

    const double relX = static_cast<double>(cursorX - bounds.left) / windowW;
    const double relY = static_cast<double>(cursorY - bounds.top) / windowH;
    const double srcPx = relX * frameW;
    const double srcPy = relY * frameH;

    const BlitRect blit =
        computeBlitRect(frameW, frameH, src.destW, src.destH, src.scale);
    const int canvasLeft =
        src.destX + blit.offsetX + static_cast<int>(srcPx * blit.scaleX);
    const int canvasTop =
        src.destY + blit.offsetY + static_cast<int>(srcPy * blit.scaleY);

    drawCursorAt(frameBuffer, width, height, cache.pixels, cache.width,
                 cache.height, canvasLeft, canvasTop);
    cache.lastDrawn = true;
    return;
  }
  cache.lastDrawn = false;
}
