#include "recorder.h"

#include "recorder_manager.h"

Status runRecorder(const RecordOptions &options,
                   std::atomic<bool> *stopRequested,
                   std::atomic<int> *hotkeyPending, std::string *saveSummary) {
  return runRecorderManager(options, stopRequested, hotkeyPending, saveSummary);
}
