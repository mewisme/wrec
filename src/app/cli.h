#pragma once

#include "post_compress.h"
#include "record_preset.h"
#include "result.h"
#include "scene.h"

#include <string>
#include <vector>

struct ListOptions {
  bool all = false;
  bool json = false;
  bool verbose = false;
};

struct RecordOptions {
  std::vector<unsigned long long> hwnds;
  std::vector<unsigned long> pids;
  std::vector<std::wstring> titles;
  LayoutKind layout = LayoutKind::Auto;
  ScaleMode scale = ScaleMode::Fit;
  uint32_t canvasWidth = 0;
  uint32_t canvasHeight = 0;
  std::vector<CustomSourceSpec> customSources;
  std::wstring outputPath;
  std::wstring outputDir;
  RecordPreset preset = RecordPreset::Medium;
  int fps = 0;
  int bitrate = 0;
  bool fpsExplicit = false;
  bool bitrateExplicit = false;
  bool cursor = true;
  bool hotkeys = true;
  bool startPaused = false;
  double speed = 1.0;
  bool verbose = false;
  bool json = false;
  CompressLevel compress = CompressLevel::Off;
};

struct InstallOptions {
  std::wstring dir;
  bool verbose = false;
};

struct ParsedCommand {
  enum class Kind {
    Help,
    Version,
    List,
    Record,
    Install,
    Uninstall,
    Gui
  } kind = Kind::Help;
  ListOptions list{};
  RecordOptions record{};
  InstallOptions install{};
};

Result<ParsedCommand> parseCommandLine(int argc, wchar_t *argv[]);
int runCommand(const ParsedCommand &command);
void printUsage();
void printVersion();
const char *wrecVersion();
