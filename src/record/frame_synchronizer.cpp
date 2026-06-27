#include "frame_synchronizer.h"

#include <memory>

SyncedFrames FrameSynchronizer::snapshot(
    const std::vector<std::unique_ptr<CaptureSource>> &sources) {
  SyncedFrames result{};
  result.frames.reserve(sources.size());
  for (const auto &source : sources) {
    source->poll();
    result.frames.push_back(source->latestFrame());
  }
  return result;
}
