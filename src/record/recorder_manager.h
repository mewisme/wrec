#pragma once

#include "cli.h"
#include "result.h"

#include <atomic>

Status runRecorderManager(const RecordOptions &options,
                          std::atomic<bool> *stopRequested = nullptr,
                          std::atomic<int> *hotkeyPending = nullptr);
