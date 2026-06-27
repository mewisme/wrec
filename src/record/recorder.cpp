#include "recorder.h"

#include "recorder_manager.h"

Status runRecorder(const RecordOptions &options,
                   std::atomic<bool> *stopRequested,
                   std::atomic<int> *hotkeyPending) {
  return runRecorderManager(options, stopRequested, hotkeyPending);
}
