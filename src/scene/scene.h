#pragma once

#include "result.h"

#include <cstdint>
#include <string>
#include <vector>

enum class ScaleMode { Fit, Fill, Stretch };
enum class LayoutKind { Auto, Grid, Horizontal, Vertical, Custom };

struct CustomSourceSpec {
  unsigned long long hwnd = 0;
  unsigned long pid = 0;
  std::wstring title;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  ScaleMode scale = ScaleMode::Fit;
};

struct SceneSource {
  size_t captureIndex = 0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  ScaleMode scale = ScaleMode::Fit;
};

struct Scene {
  uint32_t canvasWidth = 0;
  uint32_t canvasHeight = 0;
  uint32_t backgroundArgb = 0xFF000000;
  LayoutKind layout = LayoutKind::Auto;
  std::vector<SceneSource> sources;
};

struct SourceDimensions {
  uint32_t width = 0;
  uint32_t height = 0;
  bool hasFrame = false;
};

struct RecordOptions;

Result<ScaleMode> parseScaleModeStrict(const std::wstring &text);
const char *scaleModeName(ScaleMode mode);
Result<LayoutKind> parseLayoutKind(const std::wstring &text);
const char *layoutKindName(LayoutKind kind);

Result<Scene> buildSceneFromOptions(const RecordOptions &options,
                                    const std::vector<SourceDimensions> &dims);
