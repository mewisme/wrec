#pragma once

#include "capture_perf.h"
#include "d3d_device.h"
#include "result.h"
#include "source_frame.h"
#include "window_info.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

class CaptureSource {
public:
  CaptureSource(D3dDevice &device, WindowInfo target);
  ~CaptureSource();

  Status start();
  void stop();
  void poll();
  SourceFrameView latestFrame() const;
  bool isClosed() const;
  uint64_t frameGeneration() const;
  CapturePollMetrics consumePollMetrics();
  const WindowInfo &info() const { return info_; }
  HWND hwnd() const { return info_.hwnd; }

private:
  bool checkOcclusionCached();
  Status ensureStaging(uint32_t width, uint32_t height);
  Status copyTextureToCpu(ID3D11Texture2D *texture, uint32_t width,
                          uint32_t height);
  void updateFromPrintWindow();

  D3dDevice &device_;
  WindowInfo info_;
  class WgcCapture *capture_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  uint32_t stagingWidth_ = 0;
  uint32_t stagingHeight_ = 0;

  mutable std::mutex mutex_;
  std::vector<uint8_t> cpuBuffer_;
  uint32_t frameWidth_ = 0;
  uint32_t frameHeight_ = 0;
  SourceState state_ = SourceState::NoFrameYet;
  uint64_t frameGeneration_ = 0;
  bool wgcPending_ = false;
  ID3D11Texture2D *pendingTexture_ = nullptr;
  uint32_t pendingWidth_ = 0;
  uint32_t pendingHeight_ = 0;

  bool occludedCached_ = false;
  bool occlusionInitialized_ = false;
  std::chrono::steady_clock::time_point lastOcclusionCheckAt_{};
  std::chrono::steady_clock::time_point lastPrintWindowAt_{};
  static constexpr std::chrono::milliseconds kPrintWindowInterval{250};
  static constexpr std::chrono::milliseconds kOcclusionCheckVisibleInterval{
      150};
  static constexpr std::chrono::milliseconds kOcclusionCheckOccludedInterval{
      250};

  CapturePollMetrics lastPollMetrics_{};
};
