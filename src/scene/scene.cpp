#include "scene.h"

#include "cli.h"
#include "logging.h"
#include "window_list.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

bool equalsIgnoreCase(const std::wstring &a, const std::wstring &b) {
  if (a.size() != b.size()) {
    return false;
  }
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

uint32_t evenDown(uint32_t v) { return v & ~1u; }

int evenDownInt(int v) { return v & ~1; }

uint32_t maxSourceWidth(const std::vector<SourceDimensions> &dims) {
  uint32_t m = 0;
  for (const auto &d : dims) {
    if (d.hasFrame) {
      m = (std::max)(m, d.width);
    }
  }
  return m;
}

uint32_t maxSourceHeight(const std::vector<SourceDimensions> &dims) {
  uint32_t m = 0;
  for (const auto &d : dims) {
    if (d.hasFrame) {
      m = (std::max)(m, d.height);
    }
  }
  return m;
}

uint32_t sumSourceWidths(const std::vector<SourceDimensions> &dims) {
  uint32_t s = 0;
  for (const auto &d : dims) {
    if (d.hasFrame) {
      s += d.width;
    }
  }
  return s;
}

uint32_t sumSourceHeights(const std::vector<SourceDimensions> &dims) {
  uint32_t s = 0;
  for (const auto &d : dims) {
    if (d.hasFrame) {
      s += d.height;
    }
  }
  return s;
}

void applyFallbackDims(std::vector<SourceDimensions> &dims,
                       const std::vector<WindowInfo> &targets) {
  for (size_t i = 0; i < dims.size() && i < targets.size(); ++i) {
    if (!dims[i].hasFrame) {
      dims[i].width = static_cast<uint32_t>((std::max)(targets[i].width, 1));
      dims[i].height = static_cast<uint32_t>((std::max)(targets[i].height, 1));
      dims[i].hasFrame = true;
    }
  }
}

} // namespace

Result<ScaleMode> parseScaleModeStrict(const std::wstring &text) {
  if (equalsIgnoreCase(text, L"fit")) {
    return Result<ScaleMode>::ok(ScaleMode::Fit);
  }
  if (equalsIgnoreCase(text, L"fill")) {
    return Result<ScaleMode>::ok(ScaleMode::Fill);
  }
  if (equalsIgnoreCase(text, L"stretch")) {
    return Result<ScaleMode>::ok(ScaleMode::Stretch);
  }
  return Result<ScaleMode>::fail("scale must be fit, fill, or stretch");
}

const char *scaleModeName(ScaleMode mode) {
  switch (mode) {
  case ScaleMode::Fill:
    return "fill";
  case ScaleMode::Stretch:
    return "stretch";
  case ScaleMode::Fit:
  default:
    return "fit";
  }
}

Result<LayoutKind> parseLayoutKind(const std::wstring &text) {
  if (equalsIgnoreCase(text, L"auto")) {
    return Result<LayoutKind>::ok(LayoutKind::Auto);
  }
  if (equalsIgnoreCase(text, L"grid")) {
    return Result<LayoutKind>::ok(LayoutKind::Grid);
  }
  if (equalsIgnoreCase(text, L"horizontal")) {
    return Result<LayoutKind>::ok(LayoutKind::Horizontal);
  }
  if (equalsIgnoreCase(text, L"vertical")) {
    return Result<LayoutKind>::ok(LayoutKind::Vertical);
  }
  if (equalsIgnoreCase(text, L"focus")) {
    return Result<LayoutKind>::ok(LayoutKind::Focus);
  }
  if (equalsIgnoreCase(text, L"custom")) {
    return Result<LayoutKind>::ok(LayoutKind::Custom);
  }
  return Result<LayoutKind>::fail(
      "layout must be auto, grid, horizontal, vertical, focus, or custom");
}

const char *layoutKindName(LayoutKind kind) {
  switch (kind) {
  case LayoutKind::Grid:
    return "grid";
  case LayoutKind::Horizontal:
    return "horizontal";
  case LayoutKind::Vertical:
    return "vertical";
  case LayoutKind::Focus:
    return "focus";
  case LayoutKind::Custom:
    return "custom";
  case LayoutKind::Auto:
  default:
    return "auto";
  }
}

Result<Scene>
buildSceneFromOptions(const RecordOptions &options,
                      const std::vector<SourceDimensions> &dimsIn,
                      const std::vector<WindowInfo> *resolvedTargets) {
  if (dimsIn.empty()) {
    return Result<Scene>::fail("No capture sources");
  }

  std::vector<SourceDimensions> dims = dimsIn;
  if (resolvedTargets != nullptr) {
    applyFallbackDims(dims, *resolvedTargets);
  } else {
    const auto targetsResult = resolveTargetWindows(options);
    if (targetsResult.isOk()) {
      applyFallbackDims(dims, targetsResult.value());
    }
  }

  Scene scene{};
  scene.layout = options.layout;
  if (!options.customSources.empty()) {
    scene.layout = LayoutKind::Custom;
  }

  const size_t count = dims.size();
  const bool explicitCanvas =
      options.canvasWidth > 0 && options.canvasHeight > 0;

  if (scene.layout == LayoutKind::Custom) {
    if (!explicitCanvas) {
      return Result<Scene>::fail("Custom layout requires --canvas WxH");
    }
    scene.canvasWidth = evenDown(options.canvasWidth);
    scene.canvasHeight = evenDown(options.canvasHeight);
    scene.sources.reserve(options.customSources.size());
    for (size_t i = 0; i < options.customSources.size(); ++i) {
      const auto &spec = options.customSources[i];
      SceneSource src{};
      src.captureIndex = i;
      src.x = spec.x;
      src.y = spec.y;
      src.w = evenDownInt(spec.w);
      src.h = evenDownInt(spec.h);
      src.scale = spec.scale;
      scene.sources.push_back(src);
    }
    return Result<Scene>::ok(std::move(scene));
  }

  if (scene.layout == LayoutKind::Focus) {
    if (explicitCanvas) {
      scene.canvasWidth = evenDown(options.canvasWidth);
      scene.canvasHeight = evenDown(options.canvasHeight);
    } else {
      scene.canvasWidth = evenDown(maxSourceWidth(dims));
      scene.canvasHeight = evenDown(maxSourceHeight(dims));
    }
    if (scene.canvasWidth == 0 || scene.canvasHeight == 0) {
      return Result<Scene>::fail("Invalid canvas dimensions");
    }
    SceneSource src{};
    src.captureIndex = 0;
    src.x = 0;
    src.y = 0;
    src.w = static_cast<int>(scene.canvasWidth);
    src.h = static_cast<int>(scene.canvasHeight);
    src.scale = options.scale;
    scene.sources.push_back(src);
    return Result<Scene>::ok(std::move(scene));
  }

  if (count == 1 && !explicitCanvas && scene.layout == LayoutKind::Auto) {
    const auto &d = dims[0];
    scene.canvasWidth = evenDown(d.width);
    scene.canvasHeight = evenDown(d.height);
    if (scene.canvasWidth == 0 || scene.canvasHeight == 0) {
      return Result<Scene>::fail("Waiting for first frame dimensions");
    }
    SceneSource src{};
    src.captureIndex = 0;
    src.x = 0;
    src.y = 0;
    src.w = static_cast<int>(scene.canvasWidth);
    src.h = static_cast<int>(scene.canvasHeight);
    src.scale = options.scale;
    scene.sources.push_back(src);
    return Result<Scene>::ok(std::move(scene));
  }

  LayoutKind layout = scene.layout;
  if (layout == LayoutKind::Auto) {
    layout = count <= 1
                 ? LayoutKind::Auto
                 : (count <= 3 ? LayoutKind::Horizontal : LayoutKind::Grid);
  }

  if (explicitCanvas) {
    scene.canvasWidth = evenDown(options.canvasWidth);
    scene.canvasHeight = evenDown(options.canvasHeight);
  } else if (layout == LayoutKind::Horizontal) {
    scene.canvasWidth = evenDown(sumSourceWidths(dims));
    scene.canvasHeight = evenDown(maxSourceHeight(dims));
  } else if (layout == LayoutKind::Vertical) {
    scene.canvasWidth = evenDown(maxSourceWidth(dims));
    scene.canvasHeight = evenDown(sumSourceHeights(dims));
  } else {
    const size_t cols =
        static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const size_t rows = (count + cols - 1) / cols;
    const uint32_t cellW = evenDown(maxSourceWidth(dims));
    const uint32_t cellH = evenDown(maxSourceHeight(dims));
    scene.canvasWidth = evenDown(static_cast<uint32_t>(cols) * cellW);
    scene.canvasHeight = evenDown(static_cast<uint32_t>(rows) * cellH);
  }

  if (scene.canvasWidth == 0 || scene.canvasHeight == 0) {
    return Result<Scene>::fail("Invalid canvas dimensions");
  }

  scene.sources.reserve(count);
  if (layout == LayoutKind::Horizontal) {
    int x = 0;
    const int h = static_cast<int>(scene.canvasHeight);
    if (explicitCanvas) {
      const int cellW =
          static_cast<int>(scene.canvasWidth) / static_cast<int>(count);
      for (size_t i = 0; i < count; ++i) {
        SceneSource src{};
        src.captureIndex = i;
        src.x = static_cast<int>(i) * cellW;
        src.y = 0;
        src.w = evenDownInt(cellW);
        src.h = h;
        src.scale = options.scale;
        scene.sources.push_back(src);
      }
    } else {
      for (size_t i = 0; i < count; ++i) {
        const int w = evenDownInt(static_cast<int>(dims[i].width));
        SceneSource src{};
        src.captureIndex = i;
        src.x = x;
        src.y = 0;
        src.w = w;
        src.h = h;
        src.scale = options.scale;
        scene.sources.push_back(src);
        x += w;
      }
    }
  } else if (layout == LayoutKind::Vertical) {
    int y = 0;
    const int w = static_cast<int>(scene.canvasWidth);
    if (explicitCanvas) {
      const int cellH =
          static_cast<int>(scene.canvasHeight) / static_cast<int>(count);
      for (size_t i = 0; i < count; ++i) {
        SceneSource src{};
        src.captureIndex = i;
        src.x = 0;
        src.y = static_cast<int>(i) * cellH;
        src.w = w;
        src.h = evenDownInt(cellH);
        src.scale = options.scale;
        scene.sources.push_back(src);
      }
    } else {
      for (size_t i = 0; i < count; ++i) {
        const int h = evenDownInt(static_cast<int>(dims[i].height));
        SceneSource src{};
        src.captureIndex = i;
        src.x = 0;
        src.y = y;
        src.w = w;
        src.h = h;
        src.scale = options.scale;
        scene.sources.push_back(src);
        y += h;
      }
    }
  } else {
    const size_t cols =
        static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const size_t rows = (count + cols - 1) / cols;
    int cellW = 0;
    int cellH = 0;
    if (explicitCanvas) {
      cellW = evenDownInt(static_cast<int>(scene.canvasWidth) /
                          static_cast<int>(cols));
      cellH = evenDownInt(static_cast<int>(scene.canvasHeight) /
                          static_cast<int>(rows));
    } else {
      cellW = evenDownInt(static_cast<int>(maxSourceWidth(dims)));
      cellH = evenDownInt(static_cast<int>(maxSourceHeight(dims)));
    }
    for (size_t i = 0; i < count; ++i) {
      const size_t col = i % cols;
      const size_t row = i / cols;
      SceneSource src{};
      src.captureIndex = i;
      src.x = static_cast<int>(col) * cellW;
      src.y = static_cast<int>(row) * cellH;
      src.w = cellW;
      src.h = cellH;
      src.scale = options.scale;
      scene.sources.push_back(src);
    }
  }

  return Result<Scene>::ok(std::move(scene));
}
