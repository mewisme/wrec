#pragma once

#include <cstdint>

enum class SourceState { NoFrameYet, Active, Closed };

struct SourceFrameView {
  SourceState state = SourceState::NoFrameYet;
  const uint8_t *data = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowPitch = 0;
};
