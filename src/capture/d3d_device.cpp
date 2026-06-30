#include "d3d_device.h"

#include "logging.h"

#include <mutex>

Status D3dDevice::initialize() {
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_11_0;
  const HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
      D3D11_SDK_VERSION, &device_, &chosen, &context_);
  if (FAILED(hr)) {
    return Status::fail("D3D11CreateDevice failed: " + formatHresult(hr));
  }
  return Status::ok();
}

Status D3dDevice::ensureStagingLocked(uint32_t width, uint32_t height) {
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

  const HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging_);
  if (FAILED(hr)) {
    return Status::fail("CreateTexture2D(staging) failed: " +
                        formatHresult(hr));
  }
  return Status::ok();
}

Status D3dDevice::ensureStaging(uint32_t width, uint32_t height) {
  std::lock_guard lock(mutex_);
  return ensureStagingLocked(width, height);
}

Status D3dDevice::copyTexture(ID3D11Texture2D *source) {
  std::lock_guard lock(mutex_);
  D3D11_TEXTURE2D_DESC desc{};
  source->GetDesc(&desc);
  const auto status = ensureStagingLocked(desc.Width, desc.Height);
  if (!status.isOk()) {
    return status;
  }
  context_->CopyResource(staging_.Get(), source);
  return Status::ok();
}

Result<MappedFrame> D3dDevice::mapStaging() {
  std::lock_guard lock(mutex_);
  if (!staging_) {
    return Result<MappedFrame>::fail("Staging texture not allocated");
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT hr =
      context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr)) {
    return Result<MappedFrame>::fail("Map staging failed: " +
                                     formatHresult(hr));
  }
  MappedFrame frame{};
  frame.data = static_cast<const uint8_t *>(mapped.pData);
  frame.width = stagingWidth_;
  frame.height = stagingHeight_;
  frame.rowPitch = mapped.RowPitch;
  return Result<MappedFrame>::ok(frame);
}

void D3dDevice::unmapStaging() {
  std::lock_guard lock(mutex_);
  if (staging_) {
    context_->Unmap(staging_.Get(), 0);
  }
}
