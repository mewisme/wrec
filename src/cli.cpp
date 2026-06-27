#include "cli.h"

#include "logging.h"
#include "path_install.h"
#include "recorder.h"
#include "window_list.h"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool equalsIgnoreCase(const std::wstring &a, const std::wstring &b) {
  if (a.size() != b.size()) {
    return false;
  }
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool isFlag(const std::wstring &arg, const wchar_t *longForm,
            wchar_t shortForm) {
  if (arg == longForm) {
    return true;
  }
  return arg.size() == 2 && arg[0] == L'-' && arg[1] == shortForm;
}

bool parseOnOff(const std::wstring &value, bool &out) {
  if (equalsIgnoreCase(value, L"on")) {
    out = true;
    return true;
  }
  if (equalsIgnoreCase(value, L"off")) {
    out = false;
    return true;
  }
  return false;
}

bool needsValue(int index, int argc) { return index + 1 < argc; }

std::wstring requireValue(int &index, int argc, wchar_t *argv[],
                          const char *flag) {
  if (!needsValue(index, argc)) {
    throw std::runtime_error(std::string("Missing value for ") + flag);
  }
  return argv[++index];
}

unsigned long long parseU64(const std::wstring &text) {
  return std::stoull(text, nullptr, 0);
}

unsigned long parseU32(const std::wstring &text) {
  return static_cast<unsigned long>(std::stoul(text, nullptr, 0));
}

int parseInt(const std::wstring &text) { return std::stoi(text); }

double parseSpeed(const std::wstring &text) {
  std::wstring s = text;
  if (!s.empty() && (s.back() == L'x' || s.back() == L'X')) {
    s.pop_back();
  }
  return std::stod(s);
}

bool isListCommand(const std::wstring &sub) {
  return sub == L"list" || sub == L"l";
}

bool isRecordCommand(const std::wstring &sub) {
  return sub == L"record" || sub == L"rec" || sub == L"r";
}

Result<ParsedCommand> parseListArgs(ParsedCommand cmd, int argc,
                                    wchar_t *argv[]) {
  for (int i = 2; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (isFlag(arg, L"--all", L'a')) {
      cmd.list.all = true;
    } else if (isFlag(arg, L"--json", L'j')) {
      cmd.list.json = true;
    } else if (isFlag(arg, L"--verbose", L'v')) {
      cmd.list.verbose = true;
    } else {
      return Result<ParsedCommand>::fail("Unknown option for list: " +
                                         wideToUtf8(arg));
    }
  }
  return Result<ParsedCommand>::ok(std::move(cmd));
}

Result<ParsedCommand> parseRecordArgs(ParsedCommand cmd, int argc,
                                      wchar_t *argv[]) {
  int targetCount = 0;
  for (int i = 2; i < argc; ++i) {
    const std::wstring arg = argv[i];
    try {
      if (isFlag(arg, L"--hwnd", L'w')) {
        cmd.record.hwnd = parseU64(requireValue(i, argc, argv, "-w/--hwnd"));
        ++targetCount;
      } else if (isFlag(arg, L"--pid", L'p')) {
        cmd.record.pid = parseU32(requireValue(i, argc, argv, "-p/--pid"));
        ++targetCount;
      } else if (isFlag(arg, L"--title", L't')) {
        cmd.record.title = requireValue(i, argc, argv, "-t/--title");
        ++targetCount;
      } else if (isFlag(arg, L"--out", L'o')) {
        cmd.record.outputPath = requireValue(i, argc, argv, "-o/--out");
      } else if (isFlag(arg, L"--fps", L'f')) {
        cmd.record.fps = parseInt(requireValue(i, argc, argv, "-f/--fps"));
      } else if (isFlag(arg, L"--bitrate", L'b')) {
        cmd.record.bitrate =
            parseInt(requireValue(i, argc, argv, "-b/--bitrate"));
      } else if (isFlag(arg, L"--cursor", L'c')) {
        if (!parseOnOff(requireValue(i, argc, argv, "-c/--cursor"),
                        cmd.record.cursor)) {
          return Result<ParsedCommand>::fail("-c/--cursor expects on or off");
        }
      } else if (isFlag(arg, L"--audio", L'a')) {
        const std::wstring audio = requireValue(i, argc, argv, "-a/--audio");
        if (!equalsIgnoreCase(audio, L"none")) {
          return Result<ParsedCommand>::fail(
              "-a/--audio only supports none for now");
        }
      } else if (isFlag(arg, L"--hotkeys", L'k')) {
        if (!parseOnOff(requireValue(i, argc, argv, "-k/--hotkeys"),
                        cmd.record.hotkeys)) {
          return Result<ParsedCommand>::fail("-k/--hotkeys expects on or off");
        }
      } else if (isFlag(arg, L"--start-paused", L'P')) {
        cmd.record.startPaused = true;
      } else if (isFlag(arg, L"--speed", L's')) {
        cmd.record.speed =
            parseSpeed(requireValue(i, argc, argv, "-s/--speed"));
      } else if (isFlag(arg, L"--verbose", L'v')) {
        cmd.record.verbose = true;
      } else if (isFlag(arg, L"--json", L'j')) {
        cmd.record.json = true;
      } else {
        return Result<ParsedCommand>::fail("Unknown option for record: " +
                                           wideToUtf8(arg));
      }
    } catch (const std::exception &ex) {
      return Result<ParsedCommand>::fail(ex.what());
    }
  }

  if (targetCount != 1) {
    return Result<ParsedCommand>::fail(
        "Specify exactly one of -w/--hwnd, -p/--pid, or -t/--title");
  }
  if (cmd.record.outputPath.empty()) {
    return Result<ParsedCommand>::fail("-o/--out is required");
  }
  if (cmd.record.fps <= 0 || cmd.record.fps > 240) {
    return Result<ParsedCommand>::fail("-f/--fps must be between 1 and 240");
  }
  if (cmd.record.bitrate <= 0) {
    return Result<ParsedCommand>::fail("-b/--bitrate must be positive");
  }
  if (cmd.record.speed <= 0.0 || cmd.record.speed > 64.0) {
    return Result<ParsedCommand>::fail(
        "-s/--speed must be between 0 and 64 (e.g. 1, 0.9, 2x)");
  }
  return Result<ParsedCommand>::ok(std::move(cmd));
}

Result<ParsedCommand> parseInstallArgs(ParsedCommand cmd, int argc,
                                       wchar_t *argv[], bool uninstall) {
  (void)uninstall;
  for (int i = 2; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (isFlag(arg, L"--dir", L'd')) {
      cmd.install.dir = requireValue(i, argc, argv, "-d/--dir");
    } else if (isFlag(arg, L"--verbose", L'v')) {
      cmd.install.verbose = true;
    } else {
      return Result<ParsedCommand>::fail("Unknown option: " + wideToUtf8(arg));
    }
  }
  return Result<ParsedCommand>::ok(std::move(cmd));
}

} // namespace

void printUsage() {
  std::cout << "wrec - Windows CLI screen recorder\n\n"
               "Usage:\n"
               "  wrec list|l [-a] [-j] [-v]\n"
               "  wrec record|rec|r (-w <HWND> | -p <PID> | -t <text>) -o "
               "<file.mp4> [options]\n"
               "  wrec install [-d <dir>] [-v]\n"
               "  wrec uninstall [-d <dir>] [-v]\n\n"
               "List options:\n"
               "  -a, --all              Include tool/invisible/shell windows\n"
               "  -j, --json             JSON output\n"
               "  -v, --verbose          Verbose logging\n\n"
               "Record options:\n"
               "  -w, --hwnd <HWND>      Target window handle\n"
               "  -p, --pid <PID>        Target process ID\n"
               "  -t, --title <text>     Partial window title match\n"
               "  -o, --out <file.mp4>   Output file (required)\n"
               "  -f, --fps <number>     Frame rate (default: 60)\n"
               "  -b, --bitrate <bps>    Video bitrate (default: 8000000)\n"
               "  -c, --cursor on|off    Draw cursor overlay (default: on)\n"
               "  -a, --audio none       Audio not supported yet\n"
               "  -k, --hotkeys on|off   Global hotkeys (default: on)\n"
               "  -P, --start-paused     Capture armed; press Ctrl+Alt+S to "
               "start writing\n"
               "  -s, --speed <mult>     Playback speed multiplier (default: "
               "1, e.g. 0.9, 2x)\n"
               "  -v, --verbose          Verbose logging\n"
               "  -j, --json             JSON events on stderr\n\n"
               "Install options:\n"
               "  -d, --dir <path>       Install directory (default: "
               "%USERPROFILE%\\.local\\bin)\n"
               "  -v, --verbose          Verbose logging\n\n"
               "Hotkeys (when --hotkeys on):\n"
               "  Ctrl+Alt+S  Stop recording (or Start if --start-paused)\n"
               "  Ctrl+Alt+P  Pause/resume\n"
               "  Ctrl+Alt+Q  Quit and finalize output\n";
}

Result<ParsedCommand> parseCommandLine(int argc, wchar_t *argv[]) {
  ParsedCommand cmd;
  if (argc < 2) {
    cmd.kind = ParsedCommand::Kind::Help;
    return Result<ParsedCommand>::ok(std::move(cmd));
  }

  const std::wstring sub = argv[1];
  if (isListCommand(sub)) {
    cmd.kind = ParsedCommand::Kind::List;
    return parseListArgs(std::move(cmd), argc, argv);
  }

  if (isRecordCommand(sub)) {
    cmd.kind = ParsedCommand::Kind::Record;
    return parseRecordArgs(std::move(cmd), argc, argv);
  }

  if (sub == L"install") {
    cmd.kind = ParsedCommand::Kind::Install;
    return parseInstallArgs(std::move(cmd), argc, argv, false);
  }

  if (sub == L"uninstall") {
    cmd.kind = ParsedCommand::Kind::Uninstall;
    return parseInstallArgs(std::move(cmd), argc, argv, true);
  }

  if (sub == L"--help" || sub == L"-h" || sub == L"help") {
    cmd.kind = ParsedCommand::Kind::Help;
    return Result<ParsedCommand>::ok(std::move(cmd));
  }

  return Result<ParsedCommand>::fail("Unknown command: " + wideToUtf8(sub));
}

int runCommand(const ParsedCommand &command) {
  switch (command.kind) {
  case ParsedCommand::Kind::Help:
    printUsage();
    return 0;
  case ParsedCommand::Kind::List: {
    logSetVerbose(command.list.verbose);
    logSetJson(command.list.json);
    const auto result = listWindows(command.list);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Record: {
    logSetVerbose(command.record.verbose);
    logSetJson(command.record.json);
    const auto result = runRecorder(command.record);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Install: {
    logSetVerbose(command.install.verbose);
    const auto result = installToPath(command.install);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Uninstall: {
    logSetVerbose(command.install.verbose);
    const auto result = uninstallFromPath(command.install);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  }
  return 1;
}
