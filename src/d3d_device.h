#pragma once

#include "result.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <vector>

struct MappedFrame {
  const uint8_t *data = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowPitch = 0;
};

class D3dDevice {
public:
  Status initialize();
  ID3D11Device *device() const { return device_.Get(); }
  ID3D11DeviceContext *context() const { return context_.Get(); }

  Status ensureStaging(uint32_t width, uint32_t height);
  Status copyTexture(ID3D11Texture2D *source);
  Result<MappedFrame> mapStaging();
  void unmapStaging();

private:
  Status ensureStagingLocked(uint32_t width, uint32_t height);
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  uint32_t stagingWidth_ = 0;
  uint32_t stagingHeight_ = 0;
  mutable std::mutex mutex_;
};

void copyBgraToFixedBuffer(const MappedFrame &source,
                           std::vector<uint8_t> &dest, uint32_t destWidth,
                           uint32_t destHeight);
void scaleBgraToFixedBuffer(const MappedFrame &source,
                            std::vector<uint8_t> &dest, uint32_t destWidth,
                            uint32_t destHeight);
