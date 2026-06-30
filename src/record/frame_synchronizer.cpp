#include "frame_synchronizer.h"

#include <memory>

SyncedFrames FrameSynchronizer::snapshot(
    const std::vector<std::unique_ptr<CaptureSource>> &sources) {
  SyncedFrames result{};
  result.frames.reserve(sources.size());
  for (const auto &source : sources) {
    source->poll();
    const CapturePollMetrics m = source->consumePollMetrics();
    result.perf.totalPollUs += m.pollUs;
    result.perf.totalWgcCopyUs += m.wgcCopyUs;
    result.perf.totalOcclusionUs += m.occlusionUs;
    result.perf.wgcCopyCount += m.wgcCopyCount;
    result.perf.occlusionProbeCount += m.occlusionProbeCount;
    result.perf.printWindowCount += m.printWindowCount;
    result.frames.push_back(source->latestFrame());
  }
  return result;
}
