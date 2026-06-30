#include "recorder_manager.h"

#include "capture_source.h"
#include "capture_wgc.h"
#include "compositor.h"
#include "cursor_overlay.h"
#include "d3d_device.h"
#include "frame_synchronizer.h"
#include "hotkeys.h"
#include "logging.h"
#include "mf_encoder.h"
#include "notification.h"
#include "post_compress.h"
#include "scene.h"
#include "window_list.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace {

enum class ManagerState { Armed, Recording, Paused, Stopping, Finished };

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

constexpr int kVerboseTimingInterval = 300;
constexpr int kPausePollMs = 250;

int64_t toTimestamp100ns(Clock::duration elapsed) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
         100;
}

int adaptiveWaitMs(Clock::time_point target) {
  const auto now = Clock::now();
  if (now >= target) {
    return 1;
  }
  const auto remainingMs = std::chrono::duration_cast<Ms>(target - now).count();
  return static_cast<int>((std::min)((std::max)(remainingMs, 1LL), 16LL));
}

void waitUntil(Clock::time_point target, HotkeyManager *hotkeys) {
  while (Clock::now() < target) {
    const int waitMs = adaptiveWaitMs(target);
    if (hotkeys != nullptr) {
      hotkeys->waitForMessagesMs(waitMs);
    } else {
      std::this_thread::sleep_for(Ms(waitMs));
    }
  }
}

std::vector<SourceDimensions>
collectDimensions(const std::vector<std::unique_ptr<CaptureSource>> &sources) {
  std::vector<SourceDimensions> dims;
  dims.reserve(sources.size());
  for (const auto &source : sources) {
    SourceDimensions d{};
    const SourceFrameView frame = source->latestFrame();
    if (frame.state == SourceState::Active) {
      d.width = frame.width;
      d.height = frame.height;
      d.hasFrame = true;
    }
    dims.push_back(d);
  }
  return dims;
}

void initCursorSources(
    std::vector<SceneCursorSource> &cursorSources, const Scene &scene,
    const std::vector<std::unique_ptr<CaptureSource>> &sources) {
  cursorSources.clear();
  cursorSources.reserve(scene.sources.size());
  for (const SceneSource &src : scene.sources) {
    if (src.captureIndex >= sources.size()) {
      continue;
    }
    SceneCursorSource cs{};
    cs.hwnd = sources[src.captureIndex]->hwnd();
    cs.destX = src.x;
    cs.destY = src.y;
    cs.destW = src.w;
    cs.destH = src.h;
    cs.scale = src.scale;
    cursorSources.push_back(cs);
  }
}

void updateCursorSourceFrames(std::vector<SceneCursorSource> &cursorSources,
                              const Scene &scene,
                              const std::vector<SourceFrameView> &frames) {
  size_t out = 0;
  for (const SceneSource &src : scene.sources) {
    if (src.captureIndex >= frames.size()) {
      continue;
    }
    if (out >= cursorSources.size()) {
      break;
    }
    const SourceFrameView &frame = frames[src.captureIndex];
    cursorSources[out].frameWidth = frame.width;
    cursorSources[out].frameHeight = frame.height;
    ++out;
  }
}

std::string formatMs(int64_t micros) {
  const int64_t whole = micros / 1000;
  const int64_t tenth = (micros % 1000) / 100;
  return std::to_string(whole) + '.' + std::to_string(tenth) + "ms";
}

bool sourcesChanged(const SyncedFrames &synced,
                    const std::vector<uint64_t> &lastGens) {
  if (synced.frames.size() != lastGens.size()) {
    return true;
  }
  for (size_t i = 0; i < synced.frames.size(); ++i) {
    if (synced.frames[i].generation != lastGens[i]) {
      return true;
    }
  }
  return false;
}

void storeGenerations(const SyncedFrames &synced,
                      std::vector<uint64_t> &lastGens) {
  lastGens.resize(synced.frames.size());
  for (size_t i = 0; i < synced.frames.size(); ++i) {
    lastGens[i] = synced.frames[i].generation;
  }
}

struct FocusState {
  HWND lastForeground = nullptr;
  size_t activeIndex = 0;
  size_t lastComposedIndex = SIZE_MAX;
};

void initFocusActive(Scene &scene,
                     std::vector<SceneCursorSource> &cursorSources,
                     const std::vector<std::unique_ptr<CaptureSource>> &sources,
                     const std::vector<WindowInfo> &targets,
                     FocusState &focus) {
  size_t initial = indexOfMatchingTarget(GetForegroundWindow(), targets);
  if (initial == targets.size()) {
    initial = 0;
  }
  focus.activeIndex = initial;
  focus.lastForeground = GetForegroundWindow();
  if (!scene.sources.empty()) {
    scene.sources[0].captureIndex = initial;
  }
  if (!cursorSources.empty()) {
    cursorSources[0].hwnd = sources[initial]->hwnd();
  }
}

bool applyFocusSwitch(
    Scene &scene, std::vector<SceneCursorSource> &cursorSources,
    const std::vector<std::unique_ptr<CaptureSource>> &sources,
    const std::vector<WindowInfo> &targets, FocusState &focus) {
  const HWND fg = GetForegroundWindow();
  if (fg == focus.lastForeground) {
    return false;
  }
  focus.lastForeground = fg;

  const size_t idx = indexOfMatchingTarget(fg, targets);
  if (idx == targets.size() || idx == focus.activeIndex) {
    return false;
  }

  focus.activeIndex = idx;
  if (!scene.sources.empty()) {
    scene.sources[0].captureIndex = idx;
  }
  if (!cursorSources.empty()) {
    cursorSources[0].hwnd = sources[idx]->hwnd();
  }
  logMessage(LogLevel::Verbose,
             "Focus switched to " + wideToUtf8(targets[idx].title) + " (" +
                 std::to_string(
                     reinterpret_cast<unsigned long long>(targets[idx].hwnd)) +
                 ')');
  return true;
}

} // namespace

Status runRecorder(const RecordOptions &options,
                   std::atomic<bool> *stopRequested,
                   std::atomic<int> *hotkeyPending, std::string *saveSummary) {
  const auto support = checkCaptureSupport();
  if (!support.isOk()) {
    return Status::fail(support.error());
  }

  const auto targetsResult = resolveTargetWindows(options);
  if (!targetsResult.isOk()) {
    return Status::fail(targetsResult.error());
  }
  const std::vector<WindowInfo> targets = targetsResult.value();

  for (const auto &target : targets) {
    logMessage(
        LogLevel::Info,
        "Recording target: " + wideToUtf8(target.title) + " (" +
            std::to_string(reinterpret_cast<unsigned long long>(target.hwnd)) +
            ')');
  }

  MfStartupGuard mfGuard;
  if (!mfGuard.isOk()) {
    return Status::fail("MFStartup failed");
  }

  D3dDevice device;
  if (auto st = device.initialize(); !st.isOk()) {
    return st;
  }

  std::vector<std::unique_ptr<CaptureSource>> sources;
  sources.reserve(targets.size());
  for (const auto &target : targets) {
    sources.push_back(std::make_unique<CaptureSource>(device, target));
  }

  for (auto &source : sources) {
    if (auto st = source->start(); !st.isOk()) {
      for (auto &s : sources) {
        s->stop();
      }
      return st;
    }
  }

  HotkeyManager hotkeys;
  const bool guiHotkeys = hotkeyPending != nullptr;
  if (options.hotkeys && !guiHotkeys) {
    if (auto st = hotkeys.registerAll(); !st.isOk()) {
      for (auto &s : sources) {
        s->stop();
      }
      return st;
    }
  }

  auto pollHotkey = [&]() -> HotkeyAction {
    if (guiHotkeys) {
      return consumePendingHotkey(hotkeyPending);
    }
    if (options.hotkeys) {
      return hotkeys.pollAction();
    }
    return HotkeyAction::None;
  };

  auto yieldForHotkey = [&](int ms = 1) {
    if (guiHotkeys) {
      std::this_thread::sleep_for(Ms(ms));
    } else if (options.hotkeys) {
      hotkeys.waitForMessagesMs(ms);
    } else {
      std::this_thread::sleep_for(Ms(ms));
    }
  };

  FrameSynchronizer synchronizer;
  SceneCompositor compositor;
  MfEncoder encoder;
  CursorOverlayCache cursorCache;
  std::vector<uint8_t> outputBuffer;
  std::vector<SceneCursorSource> cursorSources;
  Scene scene{};
  bool sceneReady = false;
  bool encoderOpened = false;
  int64_t framesWritten = 0;
  int64_t lateFrames = 0;
  int64_t timingPollUs = 0;
  int64_t timingComposeUs = 0;
  int64_t timingCursorUs = 0;
  int64_t timingEncodeUs = 0;
  int64_t timingWgcCopyUs = 0;
  int64_t timingOcclusionUs = 0;
  int64_t skippedComposeFrames = 0;
  int64_t printWindowCalls = 0;
  int64_t wgcCopyCalls = 0;
  int64_t occlusionProbes = 0;
  std::vector<uint64_t> lastComposedGens;
  bool hasComposedFrame = false;
  FocusState focusState{};

  ManagerState state =
      options.startPaused ? ManagerState::Armed : ManagerState::Recording;

  Clock::time_point recordStart{};
  Clock::duration totalPaused{};
  Clock::time_point pauseStarted{};
  Clock::time_point lastPausePoll{};
  bool clockStarted = false;

  if (state == ManagerState::Recording) {
    recordStart = Clock::now();
    clockStarted = true;
    logJsonEvent("recording");
    if (options.speed != 1.0) {
      logMessage(LogLevel::Verbose,
                 "Playback speed: " + std::to_string(options.speed) + "x");
    }
  } else {
    logJsonEvent("armed");
    logMessage(LogLevel::Info,
               "Armed (paused). Press Ctrl+Alt+S to start recording.");
  }

  const auto frameDuration = std::chrono::duration<double>(1.0 / options.fps);
  const auto frameDurationSteady =
      std::chrono::duration_cast<Clock::duration>(frameDuration);
  auto nextFrameTime = Clock::now();

  auto elapsedRecording = [&]() -> Clock::duration {
    const auto now = Clock::now();
    if (!clockStarted) {
      return Clock::duration::zero();
    }
    return now - recordStart - totalPaused;
  };

  auto openEncoderIfNeeded = [&]() -> Status {
    if (encoderOpened || scene.canvasWidth == 0 || scene.canvasHeight == 0) {
      return Status::ok();
    }
    if (auto st = encoder.open(options.outputPath, scene.canvasWidth,
                               scene.canvasHeight, options.fps, options.bitrate,
                               AudioConfig{});
        !st.isOk()) {
      return st;
    }
    encoderOpened = true;
    if (!clockStarted) {
      recordStart = Clock::now();
      clockStarted = true;
    }
    nextFrameTime = Clock::now();
    logMessage(LogLevel::Verbose, "Encoder opened at fixed " +
                                      std::to_string(scene.canvasWidth) + "x" +
                                      std::to_string(scene.canvasHeight));
    return Status::ok();
  };

  bool running = true;
  while (running && state != ManagerState::Finished) {
    if (stopRequested != nullptr && stopRequested->load()) {
      state = ManagerState::Stopping;
      running = false;
      break;
    }

    if (options.hotkeys) {
      switch (pollHotkey()) {
      case HotkeyAction::StopOrStart:
        if (state == ManagerState::Armed) {
          state = ManagerState::Recording;
          recordStart = Clock::now();
          totalPaused = {};
          clockStarted = true;
          nextFrameTime = recordStart;
          logMessage(LogLevel::Info, "Recording started");
          logJsonEvent("recording");
          notifyTrayBalloon(L"wrec", L"Recording started");
        } else {
          notifyTrayBalloon(L"wrec", L"Recording stopped");
          state = ManagerState::Stopping;
          running = false;
          if (stopRequested != nullptr) {
            stopRequested->store(true);
          }
        }
        break;
      case HotkeyAction::PauseToggle:
        if (state == ManagerState::Recording) {
          state = ManagerState::Paused;
          pauseStarted = Clock::now();
          lastPausePoll = Clock::now();
          logMessage(LogLevel::Info, "Paused");
          logJsonEvent("paused");
          notifyTrayBalloon(L"wrec", L"Paused");
        } else if (state == ManagerState::Paused) {
          totalPaused += Clock::now() - pauseStarted;
          state = ManagerState::Recording;
          nextFrameTime = Clock::now();
          logMessage(LogLevel::Info, "Resumed");
          logJsonEvent("resumed");
          notifyTrayBalloon(L"wrec", L"Resumed");
        }
        break;
      case HotkeyAction::Quit:
        notifyTrayBalloon(L"wrec", L"Recording stopped");
        state = ManagerState::Stopping;
        running = false;
        if (stopRequested != nullptr) {
          stopRequested->store(true);
        }
        break;
      default:
        break;
      }
    }

    if (state == ManagerState::Paused) {
      const auto now = Clock::now();
      if (now - lastPausePoll >= Ms(kPausePollMs)) {
        for (auto &source : sources) {
          source->poll();
        }
        lastPausePoll = now;
      }
      yieldForHotkey(16);
      continue;
    }

    if (state == ManagerState::Armed && sceneReady) {
      yieldForHotkey(16);
      continue;
    }

    if (state == ManagerState::Recording && encoderOpened &&
        Clock::now() < nextFrameTime) {
      HotkeyManager *const hotkeyPump =
          (options.hotkeys && !guiHotkeys) ? &hotkeys : nullptr;
      while (running && state == ManagerState::Recording && encoderOpened &&
             Clock::now() < nextFrameTime) {
        if (stopRequested != nullptr && stopRequested->load()) {
          state = ManagerState::Stopping;
          running = false;
          break;
        }
        if (options.hotkeys) {
          switch (pollHotkey()) {
          case HotkeyAction::StopOrStart:
            notifyTrayBalloon(L"wrec", L"Recording stopped");
            state = ManagerState::Stopping;
            running = false;
            if (stopRequested != nullptr) {
              stopRequested->store(true);
            }
            break;
          case HotkeyAction::PauseToggle:
            state = ManagerState::Paused;
            pauseStarted = Clock::now();
            lastPausePoll = Clock::now();
            logMessage(LogLevel::Info, "Paused");
            logJsonEvent("paused");
            notifyTrayBalloon(L"wrec", L"Paused");
            break;
          case HotkeyAction::Quit:
            notifyTrayBalloon(L"wrec", L"Recording stopped");
            state = ManagerState::Stopping;
            running = false;
            if (stopRequested != nullptr) {
              stopRequested->store(true);
            }
            break;
          default:
            break;
          }
        }
        if (!running || state != ManagerState::Recording) {
          break;
        }
        waitUntil(nextFrameTime, hotkeyPump);
      }
    }
    if (!running) {
      break;
    }

    const bool recordingFrame =
        state == ManagerState::Recording && encoderOpened;

    const auto tPollStart = Clock::now();
    const SyncedFrames synced = synchronizer.snapshot(sources);
    const auto tPollEnd = Clock::now();

    if (!sceneReady) {
      const auto dims = collectDimensions(sources);
      const auto sceneResult = buildSceneFromOptions(options, dims, &targets);
      if (!sceneResult.isOk()) {
        if (sceneResult.error().find("Waiting for first frame") !=
            std::string::npos) {
          yieldForHotkey();
          continue;
        }
        for (auto &s : sources) {
          s->stop();
        }
        if (options.hotkeys && !guiHotkeys) {
          hotkeys.unregisterAll();
        }
        return Status::fail(sceneResult.error());
      }
      scene = sceneResult.value();
      sceneReady = true;
      outputBuffer.assign(
          static_cast<size_t>(scene.canvasWidth) * scene.canvasHeight * 4, 0);
      initCursorSources(cursorSources, scene, sources);
      if (scene.layout == LayoutKind::Focus) {
        initFocusActive(scene, cursorSources, sources, targets, focusState);
      }
      logMessage(LogLevel::Verbose,
                 "Scene canvas " + std::to_string(scene.canvasWidth) + "x" +
                     std::to_string(scene.canvasHeight) + " (" +
                     layoutKindName(scene.layout) + ')');
      if (auto st = openEncoderIfNeeded(); !st.isOk()) {
        for (auto &s : sources) {
          s->stop();
        }
        if (options.hotkeys && !guiHotkeys) {
          hotkeys.unregisterAll();
        }
        return st;
      }
      if (state == ManagerState::Armed) {
        yieldForHotkey(16);
        continue;
      }
    }

    if (sceneReady && scene.layout == LayoutKind::Focus) {
      applyFocusSwitch(scene, cursorSources, sources, targets, focusState);
    }

    if (!recordingFrame) {
      yieldForHotkey(16);
      continue;
    }

    timingPollUs += std::chrono::duration_cast<std::chrono::microseconds>(
                        tPollEnd - tPollStart)
                        .count();
    timingWgcCopyUs += synced.perf.totalWgcCopyUs;
    timingOcclusionUs += synced.perf.totalOcclusionUs;
    printWindowCalls += synced.perf.printWindowCount;
    wgcCopyCalls += synced.perf.wgcCopyCount;
    occlusionProbes += synced.perf.occlusionProbeCount;

    updateCursorSourceFrames(cursorSources, scene, synced.frames);
    const bool focusDirty =
        scene.layout == LayoutKind::Focus && !scene.sources.empty() &&
        scene.sources[0].captureIndex != focusState.lastComposedIndex;
    const bool sourceDirty = !hasComposedFrame ||
                             sourcesChanged(synced, lastComposedGens) ||
                             focusDirty;
    const bool cursorDirty =
        options.cursor &&
        cursorOverlayNeedsRedraw(options.cursor, cursorSources, cursorCache);

    if (!sourceDirty && !cursorDirty) {
      ++skippedComposeFrames;
    } else {
      const auto tComposeStart = Clock::now();
      compositor.compose(scene, synced.frames, outputBuffer);
      const auto tComposeEnd = Clock::now();

      const auto tCursorStart = Clock::now();
      compositeCursorOnScene(outputBuffer, scene.canvasWidth,
                             scene.canvasHeight, options.cursor, cursorSources,
                             cursorCache);
      const auto tCursorEnd = Clock::now();

      timingComposeUs += std::chrono::duration_cast<std::chrono::microseconds>(
                             tComposeEnd - tComposeStart)
                             .count();
      timingCursorUs += std::chrono::duration_cast<std::chrono::microseconds>(
                            tCursorEnd - tCursorStart)
                            .count();
      storeGenerations(synced, lastComposedGens);
      hasComposedFrame = true;
      if (scene.layout == LayoutKind::Focus && !scene.sources.empty()) {
        focusState.lastComposedIndex = scene.sources[0].captureIndex;
      }
    }

    const int64_t elapsed100ns = toTimestamp100ns(elapsedRecording());
    const int64_t timestamp =
        static_cast<int64_t>(static_cast<double>(elapsed100ns) * options.speed);
    const auto tEncodeStart = Clock::now();
    if (auto st = encoder.writeFrame(outputBuffer, timestamp); !st.isOk()) {
      for (auto &s : sources) {
        s->stop();
      }
      if (options.hotkeys && !guiHotkeys) {
        hotkeys.unregisterAll();
      }
      encoder.finalize();
      return st;
    }
    const auto tEncodeEnd = Clock::now();
    timingEncodeUs += std::chrono::duration_cast<std::chrono::microseconds>(
                          tEncodeEnd - tEncodeStart)
                          .count();

    ++framesWritten;

    if (nextFrameTime < Clock::now()) {
      ++lateFrames;
    }
    nextFrameTime += frameDurationSteady;
    if (nextFrameTime < Clock::now()) {
      nextFrameTime = Clock::now();
    }

    if (framesWritten % kVerboseTimingInterval == 0) {
      const int64_t n = kVerboseTimingInterval;
      logMessage(LogLevel::Verbose,
                 "perf: fps=" + std::to_string(options.fps) +
                     " sources=" + std::to_string(sources.size()) +
                     " poll=" + formatMs(timingPollUs / n) +
                     " compose=" + formatMs(timingComposeUs / n) +
                     " cursor=" + formatMs(timingCursorUs / n) +
                     " encode=" + formatMs(timingEncodeUs / n) +
                     " late=" + std::to_string(lateFrames) +
                     " printwindow=" + std::to_string(printWindowCalls / n) +
                     " wgccopy=" + std::to_string(wgcCopyCalls / n) +
                     " occlusion=" + std::to_string(occlusionProbes / n) +
                     " skip=" + std::to_string(skippedComposeFrames));
      timingPollUs = 0;
      timingComposeUs = 0;
      timingCursorUs = 0;
      timingEncodeUs = 0;
      timingWgcCopyUs = 0;
      timingOcclusionUs = 0;
      lateFrames = 0;
      skippedComposeFrames = 0;
      printWindowCalls = 0;
      wgcCopyCalls = 0;
      occlusionProbes = 0;
    }
  }

  state = ManagerState::Finished;
  for (auto &s : sources) {
    s->stop();
  }
  if (options.hotkeys && !guiHotkeys) {
    hotkeys.unregisterAll();
  }

  if (encoderOpened) {
    if (auto st = encoder.finalize(); !st.isOk()) {
      return st;
    }
    logJsonEvent("finalized", "\"frames\":" + std::to_string(framesWritten));

    std::string summary;
    if (options.compress == CompressLevel::Off) {
      summary = "Saved " + wideToUtf8(options.outputPath);
    } else {
      postCompressMp4WithFfmpeg(options.outputPath, options.bitrate,
                                options.compress, stopRequested, &summary);
      if (summary.empty()) {
        summary = "Saved " + wideToUtf8(options.outputPath);
      }
    }
    logMessage(LogLevel::Info, summary);
    if (saveSummary != nullptr) {
      *saveSummary = std::move(summary);
    }
  }

  return Status::ok();
}
