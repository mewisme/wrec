#include "compositor.h"

#include "source_frame.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace {

void fillSolid(std::vector<uint8_t> &output, uint32_t canvasW, uint32_t canvasH,
               uint32_t argb) {
  const size_t pixels = static_cast<size_t>(canvasW) * canvasH;
  if (output.size() != pixels * 4) {
    output.assign(pixels * 4, 0);
  }
  const uint8_t b = static_cast<uint8_t>(argb & 0xFF);
  const uint8_t g = static_cast<uint8_t>((argb >> 8) & 0xFF);
  const uint8_t r = static_cast<uint8_t>((argb >> 16) & 0xFF);
  const uint8_t a = static_cast<uint8_t>((argb >> 24) & 0xFF);
  for (size_t i = 0; i < pixels; ++i) {
    uint8_t *px = output.data() + i * 4;
    px[0] = b;
    px[1] = g;
    px[2] = r;
    px[3] = a;
  }
}

void blitTile1to1(const SourceFrameView &frame, std::vector<uint8_t> &output,
                  uint32_t canvasW, int destX, int destY, int destW,
                  int destH) {
  for (int y = 0; y < destH; ++y) {
    const int dstY = destY + y;
    if (dstY < 0) {
      continue;
    }
    const uint8_t *srcRow =
        frame.data + static_cast<size_t>(y) * frame.rowPitch;
    std::memcpy(output.data() + (static_cast<size_t>(dstY) * canvasW +
                                 static_cast<size_t>(destX)) *
                                    4,
                srcRow, static_cast<size_t>(destW) * 4);
  }
}

bool tryComposeTile1to1(const Scene &scene,
                        const std::vector<SourceFrameView> &frames,
                        std::vector<uint8_t> &output) {
  if (scene.sources.empty()) {
    return false;
  }

  size_t coveredPixels = 0;
  for (const SceneSource &src : scene.sources) {
    if (src.captureIndex >= frames.size()) {
      return false;
    }
    const SourceFrameView &frame = frames[src.captureIndex];
    if (frame.state != SourceState::Active || frame.data == nullptr) {
      return false;
    }
    if (src.scale != ScaleMode::Stretch || src.w <= 0 || src.h <= 0) {
      return false;
    }
    if (frame.width != static_cast<uint32_t>(src.w) ||
        frame.height != static_cast<uint32_t>(src.h)) {
      return false;
    }
    coveredPixels += static_cast<size_t>(src.w) * static_cast<size_t>(src.h);
  }

  const size_t canvasPixels =
      static_cast<size_t>(scene.canvasWidth) * scene.canvasHeight;
  if (coveredPixels != canvasPixels) {
    return false;
  }

  const size_t bytes = canvasPixels * 4;
  if (output.size() != bytes) {
    output.resize(bytes);
  }

  for (const SceneSource &src : scene.sources) {
    const SourceFrameView &frame = frames[src.captureIndex];
    blitTile1to1(frame, output, scene.canvasWidth, src.x, src.y, src.w, src.h);
  }
  return true;
}

} // namespace

BlitRect computeBlitRect(uint32_t srcW, uint32_t srcH, int destW, int destH,
                         ScaleMode mode) {
  BlitRect r{};
  if (srcW == 0 || srcH == 0 || destW <= 0 || destH <= 0) {
    return r;
  }

  if (mode == ScaleMode::Stretch) {
    r.offsetX = 0;
    r.offsetY = 0;
    r.width = destW;
    r.height = destH;
    r.scaleX = static_cast<double>(destW) / srcW;
    r.scaleY = static_cast<double>(destH) / srcH;
    return r;
  }

  if (mode == ScaleMode::Fill) {
    const double scale = (std::max)(static_cast<double>(destW) / srcW,
                                    static_cast<double>(destH) / srcH);
    r.width = destW;
    r.height = destH;
    r.scaleX = scale;
    r.scaleY = scale;
    const int scaledW = static_cast<int>(srcW * scale);
    const int scaledH = static_cast<int>(srcH * scale);
    r.offsetX = (destW - scaledW) / 2;
    r.offsetY = (destH - scaledH) / 2;
    return r;
  }

  // Fit
  const double scale = (std::min)(static_cast<double>(destW) / srcW,
                                  static_cast<double>(destH) / srcH);
  r.width = static_cast<int>(srcW * scale);
  r.height = static_cast<int>(srcH * scale);
  r.offsetX = (destW - r.width) / 2;
  r.offsetY = (destH - r.height) / 2;
  r.scaleX = scale;
  r.scaleY = scale;
  return r;
}

void SceneCompositor::blitSource(const SourceFrameView &frame, ScaleMode mode,
                                 std::vector<uint8_t> &output, uint32_t canvasW,
                                 uint32_t canvasH, int destX, int destY,
                                 int destW, int destH) {
  if (frame.data == nullptr || frame.width == 0 || frame.height == 0) {
    return;
  }

  const BlitRect blit =
      computeBlitRect(frame.width, frame.height, destW, destH, mode);

  if (blit.scaleX == 1.0 && blit.scaleY == 1.0 && blit.offsetX == 0 &&
      blit.offsetY == 0 && blit.width == destW && blit.height == destH &&
      static_cast<uint32_t>(destW) == frame.width &&
      static_cast<uint32_t>(destH) == frame.height) {
    for (int y = 0; y < destH; ++y) {
      const int dstY = destY + y;
      if (dstY < 0 || dstY >= static_cast<int>(canvasH)) {
        continue;
      }
      const uint8_t *srcRow =
          frame.data + static_cast<size_t>(y) * frame.rowPitch;
      std::memcpy(output.data() + (static_cast<size_t>(dstY) * canvasW +
                                   static_cast<size_t>(destX)) *
                                      4,
                  srcRow, static_cast<size_t>(destW) * 4);
    }
    return;
  }

  const double scale = blit.scaleX;

  // ponytail: nearest-neighbor blit; upgrade to bilinear if quality matters
  for (int y = 0; y < blit.height; ++y) {
    const int dstY = destY + blit.offsetY + y;
    if (dstY < 0 || dstY >= static_cast<int>(canvasH)) {
      continue;
    }
    const uint32_t srcY =
        static_cast<uint32_t>((std::min)(y / scale, frame.height - 1.0));
    const uint8_t *srcRow =
        frame.data + static_cast<size_t>(srcY) * frame.rowPitch;
    for (int x = 0; x < blit.width; ++x) {
      const int dstX = destX + blit.offsetX + x;
      if (dstX < 0 || dstX >= static_cast<int>(canvasW)) {
        continue;
      }
      const uint32_t srcX =
          static_cast<uint32_t>((std::min)(x / scale, frame.width - 1.0));
      std::memcpy(output.data() + (static_cast<size_t>(dstY) * canvasW +
                                   static_cast<size_t>(dstX)) *
                                      4,
                  srcRow + static_cast<size_t>(srcX) * 4, 4);
    }
  }
}

void SceneCompositor::drawPlaceholder(std::vector<uint8_t> &output,
                                      uint32_t canvasW, uint32_t canvasH, int x,
                                      int y, int w, int h) {
  const uint8_t gray[] = {0x40, 0x40, 0x40, 0xFF};
  for (int py = y; py < y + h && py < static_cast<int>(canvasH); ++py) {
    if (py < 0) {
      continue;
    }
    for (int px = x; px < x + w && px < static_cast<int>(canvasW); ++px) {
      if (px < 0) {
        continue;
      }
      std::memcpy(
          output.data() +
              (static_cast<size_t>(py) * canvasW + static_cast<size_t>(px)) * 4,
          gray, 4);
    }
  }

  HDC screen = GetDC(nullptr);
  if (!screen) {
    return;
  }
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib) {
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return;
  }
  HGDIOBJ old = SelectObject(mem, dib);
  HBRUSH bg = CreateSolidBrush(RGB(0x40, 0x40, 0x40));
  RECT fill{0, 0, w, h};
  FillRect(mem, &fill, bg);
  DeleteObject(bg);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(255, 255, 255));
  HFONT font = CreateFontW(-MulDiv(14, GetDpiForSystem(), 96), 0, 0, 0,
                           FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  HGDIOBJ oldFont = SelectObject(mem, font);
  DrawTextW(mem, L"Window Closed", -1, &fill,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(mem, oldFont);
  DeleteObject(font);
  SelectObject(mem, old);

  if (bits) {
    const auto *src = static_cast<const uint8_t *>(bits);
    for (int py = 0; py < h; ++py) {
      const int dstY = y + py;
      if (dstY < 0 || dstY >= static_cast<int>(canvasH)) {
        continue;
      }
      for (int px = 0; px < w; ++px) {
        const int dstX = x + px;
        if (dstX < 0 || dstX >= static_cast<int>(canvasW)) {
          continue;
        }
        const uint8_t *sp = src + (static_cast<size_t>(py) * w + px) * 4;
        if (sp[3] == 0) {
          continue;
        }
        std::memcpy(output.data() + (static_cast<size_t>(dstY) * canvasW +
                                     static_cast<size_t>(dstX)) *
                                        4,
                    sp, 4);
      }
    }
  }
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
}

void SceneCompositor::compose(const Scene &scene,
                              const std::vector<SourceFrameView> &frames,
                              std::vector<uint8_t> &output) {
  const uint32_t canvasW = scene.canvasWidth;
  const uint32_t canvasH = scene.canvasHeight;

  if (tryComposeTile1to1(scene, frames, output)) {
    return;
  }

  if (scene.sources.size() == 1) {
    const SceneSource &src = scene.sources[0];
    if (src.captureIndex < frames.size()) {
      const SourceFrameView &frame = frames[src.captureIndex];
      if (frame.state == SourceState::Active && frame.data != nullptr &&
          src.x == 0 && src.y == 0 && src.w == static_cast<int>(canvasW) &&
          src.h == static_cast<int>(canvasH) &&
          src.scale == ScaleMode::Stretch && frame.width == canvasW &&
          frame.height == canvasH) {
        const size_t bytes = static_cast<size_t>(canvasW) * canvasH * 4;
        if (output.size() != bytes) {
          output.resize(bytes);
        }
        std::memcpy(output.data(), frame.data, bytes);
        return;
      }
    }
  }

  fillSolid(output, canvasW, canvasH, scene.backgroundArgb);

  for (const SceneSource &src : scene.sources) {
    if (src.captureIndex >= frames.size()) {
      continue;
    }
    const SourceFrameView &frame = frames[src.captureIndex];
    if (frame.state == SourceState::Closed ||
        frame.state == SourceState::NoFrameYet) {
      drawPlaceholder(output, canvasW, canvasH, src.x, src.y, src.w, src.h);
      continue;
    }
    blitSource(frame, src.scale, output, canvasW, canvasH, src.x, src.y, src.w,
               src.h);
  }
}
