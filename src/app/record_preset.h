#pragma once

#include "result.h"

#include <string>

enum class RecordPreset { Low, Medium, High, Ultra, Extreme };

Result<RecordPreset> parseRecordPreset(const std::wstring &value);
const char *recordPresetName(RecordPreset preset);
