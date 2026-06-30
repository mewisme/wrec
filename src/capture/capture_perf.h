#pragma once

#include <cstdint>

struct CapturePollMetrics {
  int64_t pollUs = 0;
  int64_t wgcCopyUs = 0;
  int64_t occlusionUs = 0;
  int wgcCopyCount = 0;
  int occlusionProbeCount = 0;
  int printWindowCount = 0;
};

struct SnapshotPerfMetrics {
  int64_t totalPollUs = 0;
  int64_t totalWgcCopyUs = 0;
  int64_t totalOcclusionUs = 0;
  int wgcCopyCount = 0;
  int occlusionProbeCount = 0;
  int printWindowCount = 0;
};
