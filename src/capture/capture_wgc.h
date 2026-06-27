#pragma once

#include "d3d_device.h"
#include "result.h"

#include <Windows.h>
#include <d3d11.h>

#include <atomic>
#include <functional>
#include <mutex>

struct CapturedFrame {
  ID3D11Texture2D *texture = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
};

using FrameCallback = std::function<void(const CapturedFrame &)>;

class WgcCapture {
public:
  WgcCapture(D3dDevice &device, HWND target);
  ~WgcCapture();

  Status start(FrameCallback onFrame);
  void stop();
  bool targetClosed() const { return targetClosed_.load(); }
  uint32_t contentWidth() const { return contentWidth_; }
  uint32_t contentHeight() const { return contentHeight_; }

private:
  struct Impl;
  Impl *impl_ = nullptr;
  D3dDevice &device_;
  HWND target_;
  std::atomic<bool> targetClosed_{false};
  uint32_t contentWidth_ = 0;
  uint32_t contentHeight_ = 0;
};

Status checkCaptureSupport();
