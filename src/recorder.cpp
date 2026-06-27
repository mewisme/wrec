#include "recorder.h"

#include "capture_printwindow.h"
#include "capture_wgc.h"
#include "cursor_overlay.h"
#include "d3d_device.h"
#include "hotkeys.h"
#include "logging.h"
#include "mf_encoder.h"
#include "window_list.h"


#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

enum class RecorderState { Armed, Recording, Paused };

using Clock = std::chrono::steady_clock;

struct SharedFrame {
  std::mutex mutex;
  std::condition_variable cv;
  CapturedFrame frame{};
  bool hasFrame = false;
};

int64_t toTimestamp100ns(Clock::duration elapsed) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
         100;
}

void waitUntil(Clock::time_point target) {
  while (Clock::now() < target) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

} // namespace

Status runRecorder(const RecordOptions &options) {
  const auto support = checkCaptureSupport();
  if (!support.isOk()) {
    return Status::fail(support.error());
  }

  const auto targetResult = resolveTargetWindow(options);
  if (!targetResult.isOk()) {
    return Status::fail(targetResult.error());
  }
  const WindowInfo target = targetResult.value();

  logMessage(
      LogLevel::Info,
      "Recording target: " + wideToUtf8(target.title) + " (" +
          std::to_string(reinterpret_cast<unsigned long long>(target.hwnd)) +
          ')');

  MfStartupGuard mfGuard;
  if (!mfGuard.isOk()) {
    return Status::fail("MFStartup failed");
  }

  D3dDevice device;
  if (auto st = device.initialize(); !st.isOk()) {
    return st;
  }

  SharedFrame shared;
  WgcCapture capture(device, target.hwnd);

  auto frameHandler = [&](const CapturedFrame &frame) {
    std::lock_guard lock(shared.mutex);
    if (auto st = device.copyTexture(frame.texture); !st.isOk()) {
      return;
    }
    shared.frame = frame;
    shared.hasFrame = true;
    shared.cv.notify_one();
  };

  if (auto st = capture.start(frameHandler); !st.isOk()) {
    return st;
  }

  HotkeyManager hotkeys;
  if (options.hotkeys) {
    if (auto st = hotkeys.registerAll(); !st.isOk()) {
      capture.stop();
      return st;
    }
  }

  MfEncoder encoder;
  std::vector<uint8_t> outputBuffer;
  uint32_t outputWidth = 0;
  uint32_t outputHeight = 0;
  bool encoderOpened = false;
  int64_t framesWritten = 0;
  RecorderState state =
      options.startPaused ? RecorderState::Armed : RecorderState::Recording;

  Clock::time_point recordStart{};
  Clock::duration totalPaused{};
  Clock::time_point pauseStarted{};
  bool clockStarted = false;

  if (state == RecorderState::Recording) {
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
  while (running) {
    if (options.hotkeys) {
      switch (hotkeys.pollAction()) {
      case HotkeyAction::StopOrStart:
        if (state == RecorderState::Armed) {
          state = RecorderState::Recording;
          recordStart = Clock::now();
          totalPaused = {};
          clockStarted = true;
          nextFrameTime = recordStart;
          logMessage(LogLevel::Info, "Recording started");
          logJsonEvent("recording");
        } else {
          running = false;
        }
        break;
      case HotkeyAction::PauseToggle:
        if (state == RecorderState::Recording) {
          state = RecorderState::Paused;
          pauseStarted = Clock::now();
          logMessage(LogLevel::Info, "Paused");
          logJsonEvent("paused");
        } else if (state == RecorderState::Paused) {
          totalPaused += Clock::now() - pauseStarted;
          state = RecorderState::Recording;
          nextFrameTime = Clock::now();
          logMessage(LogLevel::Info, "Resumed");
          logJsonEvent("resumed");
        }
        break;
      case HotkeyAction::Quit:
        running = false;
        break;
      default:
        break;
      }
    }

    if (capture.targetClosed()) {
      logMessage(LogLevel::Info, "Stopping because target window closed");
      logJsonEvent("target_closed");
      running = false;
      break;
    }

    if (state == RecorderState::Recording && encoderOpened &&
        Clock::now() < nextFrameTime) {
      waitUntil(nextFrameTime);
    }

    const bool occluded = isWindowOccluded(target.hwnd);
    PrintWindowFrame printFrame{};
    MappedFrame src{};
    bool useStaging = false;

    if (occluded) {
      const auto printResult = captureWindowPrintWindow(target.hwnd);
      if (!printResult.isOk()) {
        continue;
      }
      printFrame = std::move(printResult.value());
      src.data = printFrame.pixels.data();
      src.width = printFrame.width;
      src.height = printFrame.height;
      src.rowPitch = printFrame.width * 4;
    } else {
      {
        std::unique_lock lock(shared.mutex);
        shared.cv.wait_for(lock, std::chrono::milliseconds(16),
                           [&] { return shared.hasFrame; });
        if (!shared.hasFrame) {
          continue;
        }
        shared.hasFrame = false;
      }

      if (state == RecorderState::Recording && encoderOpened &&
          Clock::now() < nextFrameTime) {
        continue;
      }

      auto mapped = device.mapStaging();
      if (!mapped.isOk()) {
        return Status::fail(mapped.error());
      }
      src = mapped.value();
      useStaging = true;
    }

    if (!encoderOpened) {
      outputWidth = src.width & ~1u;
      outputHeight = src.height & ~1u;
      if (outputWidth == 0 || outputHeight == 0) {
        if (useStaging) {
          device.unmapStaging();
        }
        continue;
      }
      if (auto st = encoder.open(options.outputPath, outputWidth, outputHeight,
                                 options.fps, options.bitrate, AudioConfig{});
          !st.isOk()) {
        if (useStaging) {
          device.unmapStaging();
        }
        return st;
      }
      outputBuffer.assign(static_cast<size_t>(outputWidth) * outputHeight * 4,
                          0);
      encoderOpened = true;
      if (!clockStarted) {
        recordStart = Clock::now();
        clockStarted = true;
      }
      nextFrameTime = Clock::now();
      logMessage(LogLevel::Verbose, "Encoder opened at fixed " +
                                        std::to_string(outputWidth) + "x" +
                                        std::to_string(outputHeight));
    }

    if (src.width == outputWidth && src.height == outputHeight) {
      copyBgraToFixedBuffer(src, outputBuffer, outputWidth, outputHeight);
    } else {
      logMessage(LogLevel::Verbose, "Target window resized; output remains " +
                                        std::to_string(outputWidth) + "x" +
                                        std::to_string(outputHeight));
      scaleBgraToFixedBuffer(src, outputBuffer, outputWidth, outputHeight);
    }
    if (useStaging) {
      device.unmapStaging();
    }

    CursorOverlayOptions cursorOpts{};
    cursorOpts.targetWindow = target.hwnd;
    cursorOpts.enabled = options.cursor;
    compositeCursor(outputBuffer, outputWidth, outputHeight, cursorOpts);

    if (state == RecorderState::Recording && encoderOpened) {
      const int64_t elapsed100ns = toTimestamp100ns(elapsedRecording());
      const int64_t timestamp = static_cast<int64_t>(
          static_cast<double>(elapsed100ns) * options.speed);
      if (auto st = encoder.writeFrame(outputBuffer, timestamp); !st.isOk()) {
        capture.stop();
        if (options.hotkeys) {
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
    }
  }

  capture.stop();
  if (options.hotkeys) {
    hotkeys.unregisterAll();
  }

  if (encoderOpened) {
    if (auto st = encoder.finalize(); !st.isOk()) {
      return st;
    }
    logMessage(LogLevel::Info, "Saved " + wideToUtf8(options.outputPath));
    logJsonEvent("finalized", "\"frames\":" + std::to_string(framesWritten));
  }

  return Status::ok();
}
