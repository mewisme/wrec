#include "d3d_device.h"

#include "logging.h"

#include <algorithm>
#include <cstring>
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

void copyBgraToFixedBuffer(const MappedFrame &source,
                           std::vector<uint8_t> &dest, uint32_t destWidth,
                           uint32_t destHeight) {
  dest.assign(static_cast<size_t>(destWidth) * destHeight * 4, 0);
  const uint32_t copyWidth = (std::min)(source.width, destWidth);
  const uint32_t copyHeight = (std::min)(source.height, destHeight);
  for (uint32_t y = 0; y < copyHeight; ++y) {
    const uint8_t *srcRow =
        source.data + static_cast<size_t>(y) * source.rowPitch;
    uint8_t *dstRow = dest.data() + static_cast<size_t>(y) * destWidth * 4;
    std::memcpy(dstRow, srcRow, static_cast<size_t>(copyWidth) * 4);
  }
}

void scaleBgraToFixedBuffer(const MappedFrame &source,
                            std::vector<uint8_t> &dest, uint32_t destWidth,
                            uint32_t destHeight) {
  // ponytail: nearest-neighbor scale/letterbox into fixed output buffer;
  // upgrade to bilinear if quality matters
  dest.assign(static_cast<size_t>(destWidth) * destHeight * 4, 0);
  if (source.width == 0 || source.height == 0) {
    return;
  }

  const double scale =
      (std::min)(static_cast<double>(destWidth) / source.width,
                 static_cast<double>(destHeight) / source.height);
  const uint32_t scaledW = static_cast<uint32_t>(source.width * scale);
  const uint32_t scaledH = static_cast<uint32_t>(source.height * scale);
  const uint32_t offsetX = (destWidth - scaledW) / 2;
  const uint32_t offsetY = (destHeight - scaledH) / 2;

  for (uint32_t y = 0; y < scaledH; ++y) {
    const uint32_t srcY = static_cast<uint32_t>(y / scale);
    const uint8_t *srcRow =
        source.data + static_cast<size_t>(srcY) * source.rowPitch;
    uint8_t *dstRow = dest.data() +
                      static_cast<size_t>(offsetY + y) * destWidth * 4 +
                      offsetX * 4;
    for (uint32_t x = 0; x < scaledW; ++x) {
      const uint32_t srcX = static_cast<uint32_t>(x / scale);
      std::memcpy(dstRow + static_cast<size_t>(x) * 4,
                  srcRow + static_cast<size_t>(srcX) * 4, 4);
    }
  }
}
