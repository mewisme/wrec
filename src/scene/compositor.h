#pragma once

#include "scene.h"
#include "source_frame.h"

#include <cstdint>
#include <vector>

struct BlitRect {
  int offsetX = 0;
  int offsetY = 0;
  int width = 0;
  int height = 0;
  double scaleX = 1.0;
  double scaleY = 1.0;
};

BlitRect computeBlitRect(uint32_t srcW, uint32_t srcH, int destW, int destH,
                         ScaleMode mode);

class SceneCompositor {
public:
  void compose(const Scene &scene, const std::vector<SourceFrameView> &frames,
               std::vector<uint8_t> &output);

private:
  void drawPlaceholder(std::vector<uint8_t> &output, uint32_t canvasW,
                       uint32_t canvasH, int x, int y, int w, int h);
  void blitSource(const SourceFrameView &frame, ScaleMode mode,
                  std::vector<uint8_t> &output, uint32_t canvasW,
                  uint32_t canvasH, int destX, int destY, int destW, int destH);
};
