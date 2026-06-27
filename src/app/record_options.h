#pragma once

#include "cli.h"
#include "result.h"

#include <string>

struct PresetValues {
  int fps;
  int bitrate;
};

Result<PresetValues> presetValues(const std::wstring &name);
void applyRecordPreset(RecordOptions &options);
std::wstring defaultOutputDir();
Result<std::wstring> resolveRecordOutputPath(const RecordOptions &options);
