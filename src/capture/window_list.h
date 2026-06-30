#pragma once

#include "result.h"
#include "window_info.h"

#include <Windows.h>

#include <string>
#include <vector>

struct ListOptions;
struct RecordOptions;

Status listWindows(const ListOptions &options);
Result<WindowInfo> resolveTargetWindow(const RecordOptions &options);
Result<std::vector<WindowInfo>>
resolveTargetWindows(const RecordOptions &options);
std::vector<WindowInfo> enumerateWindows(bool includeAll);

bool hwndMatchesTarget(HWND hwnd, HWND target);
size_t indexOfMatchingTarget(HWND hwnd, const std::vector<WindowInfo> &targets);
