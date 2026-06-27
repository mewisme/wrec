#pragma once

#include "result.h"

#include <string>

struct ListOptions {
  bool all = false;
  bool json = false;
  bool verbose = false;
};

struct RecordOptions {
  unsigned long long hwnd = 0;
  unsigned long pid = 0;
  std::wstring title;
  std::wstring outputPath;
  std::wstring outputDir;
  std::wstring preset = L"medium";
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
};

struct InstallOptions {
  std::wstring dir;
  bool verbose = false;
};

struct ParsedCommand {
  enum class Kind {
    Help,
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
