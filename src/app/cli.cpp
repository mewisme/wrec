#include "cli.h"

#include "gui.h"
#include "logging.h"
#include "path_install.h"
#include "record_options.h"
#include "recorder.h"
#include "scene.h"
#include "window_list.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <sstream>
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

bool parseCanvasSize(const std::wstring &text, uint32_t &width,
                     uint32_t &height) {
  const size_t xPos = text.find(L'x');
  if (xPos == std::wstring::npos) {
    const size_t XPos = text.find(L'X');
    if (XPos == std::wstring::npos) {
      return false;
    }
    width = static_cast<uint32_t>(parseInt(text.substr(0, XPos)));
    height = static_cast<uint32_t>(parseInt(text.substr(XPos + 1)));
    return width > 0 && height > 0;
  }
  width = static_cast<uint32_t>(parseInt(text.substr(0, xPos)));
  height = static_cast<uint32_t>(parseInt(text.substr(xPos + 1)));
  return width > 0 && height > 0;
}

Result<CustomSourceSpec> parseCustomSourceSpec(const std::wstring &text) {
  CustomSourceSpec spec{};
  std::wstringstream ss(text);
  std::wstring token;
  while (std::getline(ss, token, L',')) {
    const size_t eq = token.find(L'=');
    if (eq == std::wstring::npos) {
      return Result<CustomSourceSpec>::fail("Invalid --source entry: " +
                                            wideToUtf8(text));
    }
    const std::wstring key = token.substr(0, eq);
    const std::wstring value = token.substr(eq + 1);
    if (equalsIgnoreCase(key, L"hwnd")) {
      spec.hwnd = parseU64(value);
    } else if (equalsIgnoreCase(key, L"pid")) {
      spec.pid = parseU32(value);
    } else if (equalsIgnoreCase(key, L"title")) {
      spec.title = value;
    } else if (equalsIgnoreCase(key, L"x")) {
      spec.x = parseInt(value);
    } else if (equalsIgnoreCase(key, L"y")) {
      spec.y = parseInt(value);
    } else if (equalsIgnoreCase(key, L"w")) {
      spec.w = parseInt(value);
    } else if (equalsIgnoreCase(key, L"h")) {
      spec.h = parseInt(value);
    } else if (equalsIgnoreCase(key, L"scale")) {
      const auto mode = parseScaleModeStrict(value);
      if (!mode.isOk()) {
        return Result<CustomSourceSpec>::fail(mode.error());
      }
      spec.scale = mode.value();
    } else {
      return Result<CustomSourceSpec>::fail("Unknown --source key: " +
                                            wideToUtf8(key));
    }
  }
  if (spec.hwnd == 0 && spec.pid == 0 && spec.title.empty()) {
    return Result<CustomSourceSpec>::fail(
        "--source requires hwnd, pid, or title");
  }
  if (spec.w <= 0 || spec.h <= 0) {
    return Result<CustomSourceSpec>::fail("--source requires positive w and h");
  }
  return Result<CustomSourceSpec>::ok(std::move(spec));
}

int countTargets(const RecordOptions &options) {
  return static_cast<int>(options.hwnds.size() + options.pids.size() +
                          options.titles.size() + options.customSources.size());
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
  for (int i = 2; i < argc; ++i) {
    const std::wstring arg = argv[i];
    try {
      if (isFlag(arg, L"--hwnd", L'w')) {
        cmd.record.hwnds.push_back(parseU64(requireValue(i, argc, argv, "-w")));
      } else if (isFlag(arg, L"--pid", L'p')) {
        cmd.record.pids.push_back(parseU32(requireValue(i, argc, argv, "-p")));
      } else if (isFlag(arg, L"--title", L't')) {
        cmd.record.titles.push_back(requireValue(i, argc, argv, "-t"));
      } else if (arg == L"--layout") {
        const auto layout =
            parseLayoutKind(requireValue(i, argc, argv, "--layout"));
        if (!layout.isOk()) {
          return Result<ParsedCommand>::fail(
              "--layout expects auto, grid, horizontal, vertical, focus, or "
              "custom");
        }
        cmd.record.layout = layout.value();
      } else if (arg == L"--canvas") {
        const std::wstring canvas = requireValue(i, argc, argv, "--canvas");
        if (!parseCanvasSize(canvas, cmd.record.canvasWidth,
                             cmd.record.canvasHeight)) {
          return Result<ParsedCommand>::fail(
              "--canvas expects WxH (e.g. 1920x1080)");
        }
      } else if (isFlag(arg, L"--source", L'S')) {
        const auto spec =
            parseCustomSourceSpec(requireValue(i, argc, argv, "-S"));
        if (!spec.isOk()) {
          return Result<ParsedCommand>::fail(spec.error());
        }
        cmd.record.customSources.push_back(spec.value());
        cmd.record.layout = LayoutKind::Custom;
      } else if (isFlag(arg, L"--out", L'o')) {
        cmd.record.outputPath = requireValue(i, argc, argv, "-o");
      } else if (isFlag(arg, L"--output-dir", L'd')) {
        cmd.record.outputDir = requireValue(i, argc, argv, "-d");
      } else if (arg == L"--preset") {
        const auto preset =
            parseRecordPreset(requireValue(i, argc, argv, "--preset"));
        if (!preset.isOk()) {
          return Result<ParsedCommand>::fail(
              "--preset expects low, medium, high, ultra, or extreme");
        }
        cmd.record.preset = preset.value();
      } else if (arg == L"--fps") {
        cmd.record.fps = parseInt(requireValue(i, argc, argv, "--fps"));
        cmd.record.fpsExplicit = true;
      } else if (arg == L"--bitrate") {
        cmd.record.bitrate = parseInt(requireValue(i, argc, argv, "--bitrate"));
        cmd.record.bitrateExplicit = true;
      } else if (arg == L"--compress") {
        const auto level =
            parseCompressLevel(requireValue(i, argc, argv, "--compress"));
        if (!level.isOk()) {
          return Result<ParsedCommand>::fail(
              "--compress expects off, small, medium, or aggressive");
        }
        cmd.record.compress = level.value();
      } else if (arg == L"--cursor") {
        if (!parseOnOff(requireValue(i, argc, argv, "--cursor"),
                        cmd.record.cursor)) {
          return Result<ParsedCommand>::fail("--cursor expects on or off");
        }
      } else if (arg == L"--audio") {
        const std::wstring audio = requireValue(i, argc, argv, "--audio");
        if (!equalsIgnoreCase(audio, L"none")) {
          return Result<ParsedCommand>::fail(
              "--audio only supports none for now");
        }
      } else if (arg == L"--hotkeys") {
        if (!parseOnOff(requireValue(i, argc, argv, "--hotkeys"),
                        cmd.record.hotkeys)) {
          return Result<ParsedCommand>::fail("--hotkeys expects on or off");
        }
      } else if (arg == L"--start-paused") {
        cmd.record.startPaused = true;
      } else if (arg == L"--speed") {
        cmd.record.speed = parseSpeed(requireValue(i, argc, argv, "--speed"));
      } else if (isFlag(arg, L"--verbose", L'v')) {
        cmd.record.verbose = true;
      } else if (arg == L"--json") {
        cmd.record.json = true;
      } else {
        return Result<ParsedCommand>::fail("Unknown option for record: " +
                                           wideToUtf8(arg));
      }
    } catch (const std::exception &ex) {
      return Result<ParsedCommand>::fail(ex.what());
    }
  }

  if (countTargets(cmd.record) < 1) {
    return Result<ParsedCommand>::fail(
        "Specify at least one of -w, -p, -t, or -S/--source");
  }
  if (!cmd.record.customSources.empty() &&
      (cmd.record.canvasWidth == 0 || cmd.record.canvasHeight == 0)) {
    return Result<ParsedCommand>::fail("Custom layout requires --canvas WxH");
  }
  if (cmd.record.layout == LayoutKind::Custom &&
      cmd.record.customSources.empty()) {
    return Result<ParsedCommand>::fail(
        "--layout custom requires one or more -S/--source entries");
  }
  const auto outputResult = resolveRecordOutputPath(cmd.record);
  if (!outputResult.isOk()) {
    return Result<ParsedCommand>::fail(outputResult.error());
  }
  cmd.record.outputPath = outputResult.value();
  applyRecordPreset(cmd.record);
  if (cmd.record.fps <= 0 || cmd.record.fps > 240) {
    return Result<ParsedCommand>::fail("--fps must be between 1 and 240");
  }
  if (cmd.record.bitrate <= 0) {
    return Result<ParsedCommand>::fail("--bitrate must be positive");
  }
  if (cmd.record.speed <= 0.0 || cmd.record.speed > 64.0) {
    return Result<ParsedCommand>::fail(
        "--speed must be between 0 and 64 (e.g. 1, 0.9, 2x)");
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

const char *wrecVersion() {
#ifndef WREC_VERSION
  return "dev";
#else
  return WREC_VERSION;
#endif
}

void printVersion() {
  std::cout << R"( __          _______  ______ _____ 
 \ \        / /  __ \|  ____/ ____|
  \ \  /\  / /| |__) | |__ | |     
   \ \/  \/ / |  _  /|  __|| |     
    \  /\  /  | | \ \| |___| |____ 
     \/  \/   |_|  \_\______\_____|

)";
  const char *ver = wrecVersion();
  if (std::strcmp(ver, "dev") == 0) {
    std::cout << "wrec " << ver << '\n';
  } else {
    std::cout << "wrec v" << ver << '\n';
  }
  std::cout << "\xC2\xA9 2026 Mew\n";
  std::cout << "Licensed under the MIT License\n";
  std::cout << "Installed via " << installSourceLabel(detectInstallSource())
            << '\n';
}

void printUsage() {
  std::cout
      << "wrec " << wrecVersion()
      << " - Windows CLI screen recorder\n\n"
         "Usage:\n"
         "  wrec [gui]             Open GUI (default)\n"
         "  wrec list|l [-a] [-j] [-v]\n"
         "  wrec record|rec|r (-w <HWND> | -p <PID> | -t <text> | "
         "-S <spec>) [-o <file.mp4>] [options]\n"
         "  wrec install [-d <dir>] [-v]\n"
         "  wrec uninstall [-d <dir>] [-v]\n"
         "  wrec help, -h, --help\n"
         "  wrec -V, --version\n\n"
         "List options:\n"
         "  -a, --all              Include tool/invisible/shell windows\n"
         "  -j, --json             JSON output\n"
         "  -v, --verbose          Verbose logging\n\n"
         "Record options:\n"
         "  -w, --hwnd <HWND>      Target window handle (repeatable)\n"
         "  -p, --pid <PID>        Target process ID (repeatable)\n"
         "  -t, --title <text>     Partial window title match "
         "(repeatable)\n"
         "  -o, --out <file.mp4>   Output file (default: auto-named in "
         "output folder)\n"
         "  -d, --output-dir <dir> Output folder (default: Videos)\n"
         "  -v, --verbose          Verbose logging\n"
         "  -S, --source <spec>    Custom placement: "
         "hwnd=...,x=...,y=...,w=...,h=[,scale=fit|fill|stretch]\n"
         "      --layout <mode>    auto, grid, horizontal, vertical, focus, "
         "custom (default: auto)\n"
         "      --canvas <WxH>     Output canvas size (required for "
         "custom layout)\n"
         "      --preset <level>   Quality preset: low, medium, high, "
         "ultra, extreme (default: medium)\n"
         "      --fps <number>     Frame rate (overrides preset)\n"
         "      --bitrate <bps>    Video bitrate (overrides preset)\n"
         "      --compress <level> Post-record re-encode: off, small,\n"
         "                         medium, aggressive (default: off;\n"
         "                         requires ffmpeg in PATH)\n"
         "      --cursor on|off    Draw cursor overlay (default: on)\n"
         "      --audio none       Audio not supported yet\n"
         "      --hotkeys on|off   Global hotkeys (default: on)\n"
         "      --start-paused     Capture armed; press Ctrl+Alt+S to "
         "start writing\n"
         "      --speed <mult>     Playback speed multiplier (default: "
         "1, e.g. 0.9, 2x)\n"
         "      --json             JSON events on stderr\n\n"
         "Multi-window examples:\n"
         "  wrec r -t Chrome -t \"Visual Studio Code\" -o session.mp4\n"
         "  wrec r -t Chrome -t Notepad --layout horizontal -o dual.mp4\n"
         "  wrec r -t Chrome -t Notepad --layout focus -o focus.mp4\n"
         "  wrec r -w 0x123 -w 0x456 --layout grid -o grid.mp4\n"
         "  wrec r --canvas 1920x1080 -S hwnd=0x123,x=0,y=0,w=960,"
         "h=540 -o custom.mp4\n\n"
         "Install options:\n"
         "  -d, --dir <path>       Install directory (default: "
         "%USERPROFILE%\\.local\\bin)\n"
         "  -v, --verbose          Verbose logging\n"
         "  (manual ZIP only; not available for scoop installs)\n\n"
         "Hotkeys (when --hotkeys on):\n"
         "  Ctrl+Alt+S  Stop recording (or Start if --start-paused)\n"
         "  Ctrl+Alt+P  Pause/resume\n"
         "  Ctrl+Alt+Q  Quit and finalize output\n";
}

Result<ParsedCommand> parseCommandLine(int argc, wchar_t *argv[]) {
  ParsedCommand cmd;
  if (argc < 2) {
    cmd.kind = ParsedCommand::Kind::Gui;
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

  if (sub == L"gui") {
    cmd.kind = ParsedCommand::Kind::Gui;
    return Result<ParsedCommand>::ok(std::move(cmd));
  }

  if (sub == L"uninstall") {
    cmd.kind = ParsedCommand::Kind::Uninstall;
    return parseInstallArgs(std::move(cmd), argc, argv, true);
  }

  if (sub == L"--help" || sub == L"-h" || sub == L"help") {
    cmd.kind = ParsedCommand::Kind::Help;
    return Result<ParsedCommand>::ok(std::move(cmd));
  }

  if (sub == L"--version" || sub == L"-V") {
    cmd.kind = ParsedCommand::Kind::Version;
    return Result<ParsedCommand>::ok(std::move(cmd));
  }

  return Result<ParsedCommand>::fail("Unknown command: " + wideToUtf8(sub));
}

namespace {

std::atomic<bool> *g_cliStopRequested = nullptr;

BOOL WINAPI cliConsoleCtrlHandler(DWORD ctrlType) {
  if (g_cliStopRequested == nullptr) {
    return FALSE;
  }
  switch (ctrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
    g_cliStopRequested->store(true);
    logMessage(LogLevel::Info, "Stopping and saving...");
    return TRUE;
  default:
    return FALSE;
  }
}

void configureLogging(const ParsedCommand &command) {
  switch (command.kind) {
  case ParsedCommand::Kind::List:
    logSetVerbose(command.list.verbose);
    logSetJson(command.list.json);
    break;
  case ParsedCommand::Kind::Record:
    logSetVerbose(command.record.verbose);
    logSetJson(command.record.json);
    break;
  case ParsedCommand::Kind::Install:
  case ParsedCommand::Kind::Uninstall:
    logSetVerbose(command.install.verbose);
    break;
  case ParsedCommand::Kind::Gui:
    logSetVerbose(true);
    logSetJson(false);
    break;
  default:
    logSetVerbose(false);
    logSetJson(false);
    break;
  }
}

void logAppVersion() {
  if (logIsJson()) {
    logJsonEvent("version",
                 "\"version\":\"" + std::string(wrecVersion()) + '"');
  } else {
    logMessage(LogLevel::Info, std::string("wrec ") + wrecVersion());
  }
}

} // namespace

int runCommand(const ParsedCommand &command) {
  configureLogging(command);
  if (command.kind == ParsedCommand::Kind::Gui) {
    hideConsoleIfStandaloneLaunch();
  }
  logAppVersion();
  switch (command.kind) {
  case ParsedCommand::Kind::Help:
    printUsage();
    return 0;
  case ParsedCommand::Kind::Version:
    printVersion();
    return 0;
  case ParsedCommand::Kind::List: {
    const auto result = listWindows(command.list);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Record: {
    std::atomic<bool> stopRequested{false};
    g_cliStopRequested = &stopRequested;
    SetConsoleCtrlHandler(cliConsoleCtrlHandler, TRUE);
    const auto result = runRecorder(command.record, &stopRequested);
    SetConsoleCtrlHandler(nullptr, FALSE);
    g_cliStopRequested = nullptr;
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Install: {
    const auto result = installToPath(command.install);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Uninstall: {
    const auto result = uninstallFromPath(command.install);
    if (!result.isOk()) {
      logMessage(LogLevel::Error, result.error());
      return 1;
    }
    return 0;
  }
  case ParsedCommand::Kind::Gui:
    return runGui();
  }
  return 1;
}
