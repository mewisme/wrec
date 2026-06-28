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

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

enum class ManagerState { Armed, Recording, Paused, Stopping, Finished };

using Clock = std::chrono::steady_clock;

int64_t toTimestamp100ns(Clock::duration elapsed) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
         100;
}

void waitUntil(Clock::time_point target, HotkeyManager *hotkeys) {
  while (Clock::now() < target) {
    if (hotkeys != nullptr) {
      hotkeys->waitForMessagesMs(1);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

std::vector<SceneCursorSource>
buildCursorSources(const Scene &scene,
                   const std::vector<std::unique_ptr<CaptureSource>> &sources) {
  std::vector<SceneCursorSource> cursorSources;
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
    const SourceFrameView frame = sources[src.captureIndex]->latestFrame();
    cs.frameWidth = frame.width;
    cs.frameHeight = frame.height;
    cursorSources.push_back(cs);
  }
  return cursorSources;
}

} // namespace

Status runRecorderManager(const RecordOptions &options,
                          std::atomic<bool> *stopRequested,
                          std::atomic<int> *hotkeyPending,
                          std::string *saveSummary) {
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

  auto yieldForHotkey = [&]() {
    if (guiHotkeys) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else if (options.hotkeys) {
      hotkeys.waitForMessagesMs(1);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  FrameSynchronizer synchronizer;
  SceneCompositor compositor;
  MfEncoder encoder;
  std::vector<uint8_t> outputBuffer;
  Scene scene{};
  bool sceneReady = false;
  bool encoderOpened = false;
  int64_t framesWritten = 0;

  ManagerState state =
      options.startPaused ? ManagerState::Armed : ManagerState::Recording;

  Clock::time_point recordStart{};
  Clock::duration totalPaused{};
  Clock::time_point pauseStarted{};
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

    const SyncedFrames synced = synchronizer.snapshot(sources);

    if (!sceneReady) {
      const auto dims = collectDimensions(sources);
      const auto sceneResult = buildSceneFromOptions(options, dims);
      if (!sceneResult.isOk()) {
        if (sceneResult.error().find("Waiting for first frame") !=
            std::string::npos) {
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
      logMessage(LogLevel::Verbose,
                 "Scene canvas " + std::to_string(scene.canvasWidth) + "x" +
                     std::to_string(scene.canvasHeight) + " (" +
                     layoutKindName(scene.layout) + ')');
    }

    compositor.compose(scene, synced.frames, outputBuffer);

    const auto cursorSources = buildCursorSources(scene, sources);
    compositeCursorOnScene(outputBuffer, scene.canvasWidth, scene.canvasHeight,
                           options.cursor, cursorSources);

    if (!encoderOpened) {
      if (scene.canvasWidth == 0 || scene.canvasHeight == 0) {
        continue;
      }
      if (auto st = encoder.open(options.outputPath, scene.canvasWidth,
                                 scene.canvasHeight, options.fps,
                                 options.bitrate, AudioConfig{});
          !st.isOk()) {
        for (auto &s : sources) {
          s->stop();
        }
        if (options.hotkeys && !guiHotkeys) {
          hotkeys.unregisterAll();
        }
        return st;
      }
      encoderOpened = true;
      if (!clockStarted) {
        recordStart = Clock::now();
        clockStarted = true;
      }
      nextFrameTime = Clock::now();
      logMessage(LogLevel::Verbose, "Encoder opened at fixed " +
                                        std::to_string(scene.canvasWidth) +
                                        "x" +
                                        std::to_string(scene.canvasHeight));
    }

    if (state == ManagerState::Recording && encoderOpened) {
      const int64_t elapsed100ns = toTimestamp100ns(elapsedRecording());
      const int64_t timestamp = static_cast<int64_t>(
          static_cast<double>(elapsed100ns) * options.speed);
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
      ++framesWritten;

      nextFrameTime += frameDurationSteady;
      if (nextFrameTime < Clock::now()) {
        nextFrameTime = Clock::now();
      }
    } else {
      yieldForHotkey();
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
