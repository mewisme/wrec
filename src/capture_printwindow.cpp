#include "capture_printwindow.h"

#include "logging.h"

#include <string>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

bool isSameWindowTree(HWND target, HWND other) {
  if (!other) {
    return false;
  }
  if (target == other) {
    return true;
  }
  for (HWND walk = other; walk != nullptr; walk = GetParent(walk)) {
    if (walk == target) {
      return true;
    }
  }
  for (HWND walk = target; walk != nullptr; walk = GetParent(walk)) {
    if (walk == other) {
      return true;
    }
  }
  return false;
}

bool probePointOnWindow(HWND target, POINT point) {
  const HWND at = WindowFromPoint(point);
  return isSameWindowTree(target, at);
}

} // namespace

bool isWindowOccluded(HWND hwnd) {
  if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
    return true;
  }

  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) {
    return true;
  }
  if (rect.right <= rect.left || rect.bottom <= rect.top) {
    return true;
  }

  const POINT probes[] = {
      {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2},
      {rect.left + 8, rect.top + 8},
      {rect.right - 8, rect.bottom - 8},
  };
  for (const POINT probe : probes) {
    if (probePointOnWindow(hwnd, probe)) {
      return false;
    }
  }
  return true;
}

Result<PrintWindowFrame> captureWindowPrintWindow(HWND hwnd) {
  if (!IsWindow(hwnd)) {
    return Result<PrintWindowFrame>::fail("Invalid HWND");
  }

  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) {
    return Result<PrintWindowFrame>::fail("GetWindowRect failed");
  }

  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) {
    return Result<PrintWindowFrame>::fail("Target window has zero size");
  }

  HDC screenDc = GetDC(nullptr);
  if (!screenDc) {
    return Result<PrintWindowFrame>::fail("GetDC failed");
  }

  HDC memDc = CreateCompatibleDC(screenDc);
  if (!memDc) {
    ReleaseDC(nullptr, screenDc);
    return Result<PrintWindowFrame>::fail("CreateCompatibleDC failed");
  }

  HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
  ReleaseDC(nullptr, screenDc);
  if (!bitmap) {
    DeleteDC(memDc);
    return Result<PrintWindowFrame>::fail("CreateCompatibleBitmap failed");
  }

  HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

  BOOL ok = PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT);
  if (!ok) {
    ok = PrintWindow(hwnd, memDc, 0);
  }
  if (!ok) {
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    return Result<PrintWindowFrame>::fail("PrintWindow failed");
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  PrintWindowFrame frame{};
  frame.width = static_cast<uint32_t>(width);
  frame.height = static_cast<uint32_t>(height);
  frame.pixels.assign(static_cast<size_t>(width) * height * 4, 0);

  if (GetDIBits(memDc, bitmap, 0, static_cast<UINT>(height),
                frame.pixels.data(), &info, DIB_RGB_COLORS) == 0) {
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    return Result<PrintWindowFrame>::fail("GetDIBits failed");
  }

  SelectObject(memDc, oldBitmap);
  DeleteObject(bitmap);
  DeleteDC(memDc);

  logMessage(LogLevel::Verbose, "PrintWindow capture " +
                                    std::to_string(frame.width) + "x" +
                                    std::to_string(frame.height));
  return Result<PrintWindowFrame>::ok(std::move(frame));
}
