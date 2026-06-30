#include "cli.h"
#include "gui.h"
#include "logging.h"

#include <Windows.h>

#include <winrt/base.h>

int wmain(int argc, wchar_t *argv[]) {
  if (isGuiLaunch(argc, argv)) {
    detachGuiConsole();
  }

  const auto parsed = parseCommandLine(argc, argv);
  if (!parsed.isOk()) {
    initConsoleEncoding();
    logMessage(LogLevel::Error, parsed.error());
    return 1;
  }

  initConsoleEncoding();
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const bool isGui = parsed.value().kind == ParsedCommand::Kind::Gui;
  winrt::init_apartment(isGui ? winrt::apartment_type::single_threaded
                              : winrt::apartment_type::multi_threaded);

  const int code = runCommand(parsed.value());
  winrt::uninit_apartment();
  return code;
}
