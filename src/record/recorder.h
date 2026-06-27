#pragma once

#include "cli.h"
#include "result.h"

#include <atomic>

Status runRecorder(const RecordOptions &options,
                   std::atomic<bool> *stopRequested = nullptr,
                   std::atomic<int> *hotkeyPending = nullptr);
