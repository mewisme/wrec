#pragma once

#include "result.h"

#include <atomic>
#include <string>

enum class CompressLevel { Off, Small, Medium, Aggressive };

Result<CompressLevel> parseCompressLevel(const std::wstring &value);
const char *compressLevelName(CompressLevel level);

Status postCompressMp4WithFfmpeg(const std::wstring &inputPath,
                                 int sourceBitrate, CompressLevel level,
                                 std::atomic<bool> *stopRequested = nullptr,
                                 std::string *saveSummary = nullptr);
