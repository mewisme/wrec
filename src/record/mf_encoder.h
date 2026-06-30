#pragma once

#include "result.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

struct AudioConfig {
  bool enabled = false;
};

class MfStartupGuard {
public:
  MfStartupGuard();
  ~MfStartupGuard();
  bool isOk() const { return ok_; }

private:
  bool ok_ = false;
};

class MfEncoder {
public:
  Status open(const std::wstring &path, uint32_t width, uint32_t height,
              int fps, int bitrate, const AudioConfig &audio);
  Status writeFrame(const std::vector<uint8_t> &bgra, int64_t timestamp100ns);
  Status finalize();

private:
  Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
  DWORD streamIndex_ = 0;
  int fps_ = 60;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool opened_ = false;
};
