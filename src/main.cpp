#include "cli.h"
#include "logging.h"

#include <Windows.h>

#include <winrt/base.h>

int wmain(int argc, wchar_t *argv[]) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  const auto parsed = parseCommandLine(argc, argv);
  if (!parsed.isOk()) {
    logMessage(LogLevel::Error, parsed.error());
    winrt::uninit_apartment();
    return 1;
  }
  const int code = runCommand(parsed.value());
  winrt::uninit_apartment();
  return code;
}
