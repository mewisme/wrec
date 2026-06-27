#pragma once

#include "capture_source.h"

#include <memory>
#include <vector>

struct SyncedFrames {
  std::vector<SourceFrameView> frames;
};

class FrameSynchronizer {
public:
  SyncedFrames
  snapshot(const std::vector<std::unique_ptr<CaptureSource>> &sources);
};
