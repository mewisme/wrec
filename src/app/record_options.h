#pragma once

#include "cli.h"
#include "record_preset.h"
#include "result.h"

#include <string>

struct PresetValues {
  int fps;
  int bitrate;
};

Result<PresetValues> presetValues(RecordPreset preset);
void applyRecordPreset(RecordOptions &options);
std::wstring defaultOutputDir();
Result<std::wstring> resolveRecordOutputPath(const RecordOptions &options);
