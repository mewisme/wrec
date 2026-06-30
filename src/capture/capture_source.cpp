#include "capture_source.h"
#include "capture_printwindow.h"
#include "capture_wgc.h"
#include "logging.h"
#include <cstring>
namespace {
using Clock = std::chrono::steady_clock;
} // namespace
Status CaptureSource::ensureStaging(uint32_t width, uint32_t height) {
  if (staging_ && stagingWidth_ == width && stagingHeight_ == height) {
    return Status::ok();
  }
  staging_.Reset();
  stagingWidth_ = width;
  stagingHeight_ = height;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  const HRESULT hr =
      device_.device()->CreateTexture2D(&desc, nullptr, &staging_);
  if (FAILED(hr)) {
    return Status::fail("CreateTexture2D(staging) failed: " +
                        formatHresult(hr));
  }
  return Status::ok();
}
Status CaptureSource::copyTextureToCpu(ID3D11Texture2D *texture, uint32_t width,
                                       uint32_t height) {
  if (auto st = ensureStaging(width, height); !st.isOk()) {
    return st;
  }
  device_.context()->CopyResource(staging_.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT hr =
      device_.context()->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    return Status::fail("Map staging failed: " + formatHresult(hr));
  }
  const size_t needed = static_cast<size_t>(width) * height * 4;
  if (cpuBuffer_.size() != needed) {
    cpuBuffer_.resize(needed);
  }
  frameWidth_ = width;
  frameHeight_ = height;
  if (mapped.RowPitch == width * 4) {
    std::memcpy(cpuBuffer_.data(), mapped.pData, needed);
  } else {
    for (uint32_t y = 0; y < height; ++y) {
      std::memcpy(cpuBuffer_.data() + static_cast<size_t>(y) * width * 4,
                  static_cast<const uint8_t *>(mapped.pData) +
                      static_cast<size_t>(y) * mapped.RowPitch,
                  static_cast<size_t>(width) * 4);
    }
  }
  device_.context()->Unmap(staging_.Get(), 0);
  state_ = SourceState::Active;
  ++frameGeneration_;
  return Status::ok();
}
void CaptureSource::updateFromPrintWindow() {
  const auto result = captureWindowPrintWindow(info_.hwnd);
  if (!result.isOk()) {
    return;
  }
  const PrintWindowFrame &frame = result.value();
  const size_t needed = static_cast<size_t>(frame.width) * frame.height * 4;
  if (cpuBuffer_.size() != needed) {
    cpuBuffer_.resize(needed);
  }
  std::memcpy(cpuBuffer_.data(), frame.pixels.data(), needed);
  frameWidth_ = frame.width;
  frameHeight_ = frame.height;
  state_ = SourceState::Active;
  ++frameGeneration_;
}
bool CaptureSource::checkOcclusionCached() {
  const auto now = Clock::now();
  const auto interval = occludedCached_ ? kOcclusionCheckOccludedInterval
                                        : kOcclusionCheckVisibleInterval;
  if (occlusionInitialized_ && now - lastOcclusionCheckAt_ < interval) {
    return occludedCached_;
  }
  const auto t0 = Clock::now();
  occludedCached_ = isWindowOccluded(info_.hwnd);
  lastOcclusionCheckAt_ = now;
  occlusionInitialized_ = true;
  lastPollMetrics_.occlusionUs +=
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0)
          .count();
  ++lastPollMetrics_.occlusionProbeCount;
  return occludedCached_;
}
CaptureSource::CaptureSource(D3dDevice &device, WindowInfo target)
    : device_(device), info_(std::move(target)) {}
CaptureSource::~CaptureSource() { stop(); }
Status CaptureSource::start() {
  capture_ = new WgcCapture(device_, info_.hwnd);
  auto frameHandler = [this](const CapturedFrame &frame) {
    std::lock_guard lock(mutex_);
    if (capture_->targetClosed()) {
      if (state_ != SourceState::Closed) {
        state_ = SourceState::Closed;
        ++frameGeneration_;
      }
      return;
    }
    pendingTexture_ = frame.texture;
    pendingWidth_ = frame.width;
    pendingHeight_ = frame.height;
    wgcPending_ = true;
  };
  const Status st = capture_->start(std::move(frameHandler));
  if (!st.isOk()) {
    delete capture_;
    capture_ = nullptr;
    return st;
  }
  return Status::ok();
}
void CaptureSource::stop() {
  if (capture_ != nullptr) {
    capture_->stop();
    delete capture_;
    capture_ = nullptr;
  }
}
void CaptureSource::poll() {
  const auto pollStart = Clock::now();
  lastPollMetrics_ = {};
  if (capture_ == nullptr) {
    lastPollMetrics_.pollUs =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                              pollStart)
            .count();
    return;
  }
  if (capture_->targetClosed()) {
    std::lock_guard lock(mutex_);
    if (state_ != SourceState::Closed) {
      state_ = SourceState::Closed;
      ++frameGeneration_;
    }
    pendingTexture_ = nullptr;
    wgcPending_ = false;
    lastPollMetrics_.pollUs =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                              pollStart)
            .count();
    return;
  }
  if (checkOcclusionCached()) {
    std::lock_guard lock(mutex_);
    pendingTexture_ = nullptr;
    wgcPending_ = false;
    const auto now = Clock::now();
    if (now - lastPrintWindowAt_ < kPrintWindowInterval) {
      lastPollMetrics_.pollUs =
          std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                                pollStart)
              .count();
      return;
    }
    lastPrintWindowAt_ = now;
    ++lastPollMetrics_.printWindowCount;
    updateFromPrintWindow();
    lastPollMetrics_.pollUs =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                              pollStart)
            .count();
    return;
  }
  ID3D11Texture2D *texture = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  {
    std::lock_guard lock(mutex_);
    if (!wgcPending_ || pendingTexture_ == nullptr) {
      lastPollMetrics_.pollUs =
          std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                                pollStart)
              .count();
      return;
    }
    texture = pendingTexture_;
    width = pendingWidth_;
    height = pendingHeight_;
    wgcPending_ = false;
    pendingTexture_ = nullptr;
  }
  const auto copyStart = Clock::now();
  {
    std::lock_guard lock(mutex_);
    if (auto st = copyTextureToCpu(texture, width, height); !st.isOk()) {
      logMessage(LogLevel::Verbose, st.error());
    } else {
      ++lastPollMetrics_.wgcCopyCount;
    }
  }
  lastPollMetrics_.wgcCopyUs =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                            copyStart)
          .count();
  lastPollMetrics_.pollUs =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                            pollStart)
          .count();
}
SourceFrameView CaptureSource::latestFrame() const {
  std::lock_guard lock(mutex_);
  SourceFrameView view{};
  view.state = state_;
  view.generation = frameGeneration_;
  if (state_ == SourceState::Active && !cpuBuffer_.empty()) {
    view.data = cpuBuffer_.data();
    view.width = frameWidth_;
    view.height = frameHeight_;
    view.rowPitch = frameWidth_ * 4;
  }
  return view;
}
bool CaptureSource::isClosed() const {
  std::lock_guard lock(mutex_);
  return state_ == SourceState::Closed;
}
uint64_t CaptureSource::frameGeneration() const {
  std::lock_guard lock(mutex_);
  return frameGeneration_;
}
CapturePollMetrics CaptureSource::consumePollMetrics() {
  std::lock_guard lock(mutex_);
  CapturePollMetrics m = lastPollMetrics_;
  lastPollMetrics_ = {};
  return m;
}
