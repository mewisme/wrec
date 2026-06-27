#include "cursor_overlay.h"

#include "logging.h"

#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

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

} // namespace

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
