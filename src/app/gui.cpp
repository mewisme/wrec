#include "gui.h"

#include "cli.h"
#include "logging.h"
#include "path_install.h"
#include "record_options.h"
#include "recorder.h"
#include "window_list.h"

#include <Windows.h>
#include <winrt/base.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"wrec_gui";
constexpr int kBaseClientWidth = 640;
constexpr int kBaseClientHeight = 540;

int guiDpi(HWND hwnd) {
  const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
  return dpi != 0 ? static_cast<int>(dpi) : 96;
}

int guiPx(int value, int dpi) { return MulDiv(value, dpi, 96); }

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_RECORD_DONE = WM_APP + 2;

enum ControlId : int {
  IDC_LIST = 1001,
  IDC_REFRESH,
  IDC_SHOW_ALL,
  IDC_SELECTED,
  IDC_OUTPUT_FILE,
  IDC_BROWSE_FILE,
  IDC_OUTPUT_DIR,
  IDC_BROWSE_DIR,
  IDC_PRESET,
  IDC_FPS,
  IDC_BITRATE,
  IDC_CURSOR,
  IDC_HOTKEYS,
  IDC_START_PAUSED,
  IDC_SPEED,
  IDC_START,
  IDC_STOP,
  IDC_OPEN_RECENT,
  IDC_STATUS,
  IDC_INSTALL_DIR,
  IDC_INSTALL,
  IDC_UNINSTALL,
};

struct RecordDonePayload {
  bool ok = false;
  std::string error;
  std::wstring outputPath;
};

struct GuiState {
  HWND hwnd = nullptr;
  std::thread recordThread;
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> recording{false};
  std::wstring lastRecordedPath;
  bool fpsUserEdited = false;
  bool bitrateUserEdited = false;
  bool updatingPresetFields = false;
};

GuiState *g_state = nullptr;

std::wstring utf8ToWide(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      wide.data(), size);
  return wide;
}

std::wstring getWindowTextString(HWND control) {
  const int len = GetWindowTextLengthW(control);
  if (len <= 0) {
    return {};
  }
  std::wstring text(static_cast<size_t>(len + 1), L'\0');
  GetWindowTextW(control, text.data(), len + 1);
  text.resize(static_cast<size_t>(len));
  return text;
}

void setWindowText(HWND control, const std::wstring &text) {
  SetWindowTextW(control, text.c_str());
}

void setStatus(const std::wstring &text) {
  if (g_state != nullptr && g_state->hwnd != nullptr) {
    setWindowText(GetDlgItem(g_state->hwnd, IDC_STATUS), text);
  }
}

HWND createLabel(HWND parent, const wchar_t *text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h,
                         parent, nullptr, nullptr, nullptr);
}

HWND createEdit(HWND parent, int id, int x, int y, int w, int h,
                DWORD extraStyle = 0) {
  return CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extraStyle, x, y, w, h, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}

HWND createButton(HWND parent, int id, const wchar_t *text, int x, int y, int w,
                  int h) {
  return CreateWindowExW(
      0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h,
      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
      nullptr);
}

HWND createCheck(HWND parent, int id, const wchar_t *text, int x, int y, int w,
                 int h, bool checked) {
  return CreateWindowExW(
      0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | (checked ? BST_CHECKED : 0), x,
      y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      nullptr, nullptr);
}

std::wstring exeBasename(const std::wstring &path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

void setupListColumns(HWND listView, int dpi) {
  ListView_SetExtendedListViewStyle(listView, LVS_EX_FULLROWSELECT);
  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH;
  col.pszText = const_cast<wchar_t *>(L"Title");
  col.cx = guiPx(220, dpi);
  ListView_InsertColumn(listView, 0, &col);
  col.pszText = const_cast<wchar_t *>(L"EXE");
  col.cx = guiPx(100, dpi);
  ListView_InsertColumn(listView, 1, &col);
  col.pszText = const_cast<wchar_t *>(L"Size");
  col.cx = guiPx(80, dpi);
  ListView_InsertColumn(listView, 2, &col);
  col.pszText = const_cast<wchar_t *>(L"PID");
  col.cx = guiPx(60, dpi);
  ListView_InsertColumn(listView, 3, &col);
  col.pszText = const_cast<wchar_t *>(L"HWND");
  col.cx = guiPx(90, dpi);
  ListView_InsertColumn(listView, 4, &col);
}

void updateSelectedWindowDisplay(HWND hwnd);

void refreshWindowList(HWND hwnd) {
  const HWND listView = GetDlgItem(hwnd, IDC_LIST);
  ListView_DeleteAllItems(listView);
  const bool showAll = IsDlgButtonChecked(hwnd, IDC_SHOW_ALL) == BST_CHECKED;
  const auto windows = enumerateWindows(showAll);
  int index = 0;
  for (const auto &window : windows) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = index;
    item.pszText = const_cast<wchar_t *>(window.title.c_str());
    item.lParam = reinterpret_cast<LPARAM>(window.hwnd);
    ListView_InsertItem(listView, &item);

    const std::wstring exe = exeBasename(window.exePath);
    ListView_SetItemText(listView, index, 1,
                         const_cast<wchar_t *>(exe.c_str()));

    std::wostringstream size;
    size << window.width << L'x' << window.height;
    const std::wstring sizeText = size.str();
    ListView_SetItemText(listView, index, 2,
                         const_cast<wchar_t *>(sizeText.c_str()));

    const std::wstring pidText = std::to_wstring(window.pid);
    ListView_SetItemText(listView, index, 3,
                         const_cast<wchar_t *>(pidText.c_str()));

    std::wostringstream hwndText;
    hwndText << std::hex << std::uppercase
             << reinterpret_cast<unsigned long long>(window.hwnd);
    const std::wstring hwndStr = hwndText.str();
    ListView_SetItemText(listView, index, 4,
                         const_cast<wchar_t *>(hwndStr.c_str()));
    ++index;
  }
  updateSelectedWindowDisplay(hwnd);
}

void updateSelectedWindowDisplay(HWND hwnd) {
  const HWND listView = GetDlgItem(hwnd, IDC_LIST);
  const HWND selectedLabel = GetDlgItem(hwnd, IDC_SELECTED);
  const int row = ListView_GetNextItem(listView, -1, LVNI_SELECTED);
  if (row < 0) {
    setWindowText(selectedLabel, L"Selected: (none)");
    return;
  }

  wchar_t title[512]{};
  wchar_t exe[256]{};
  wchar_t pid[32]{};
  ListView_GetItemText(listView, row, 0, title, 512);
  ListView_GetItemText(listView, row, 1, exe, 256);
  ListView_GetItemText(listView, row, 3, pid, 32);

  LVITEMW item{};
  item.mask = LVIF_PARAM;
  item.iItem = row;
  ListView_GetItem(listView, &item);

  std::wostringstream text;
  text << L"Selected: " << title << L"  (" << exe << L", PID " << pid
       << L", HWND 0x" << std::hex << std::uppercase
       << static_cast<unsigned long long>(item.lParam) << L')';
  setWindowText(selectedLabel, text.str());
}

std::wstring presetNameFromIndex(int index) {
  static const wchar_t *kNames[] = {L"low", L"medium", L"high", L"ultra",
                                    L"extreme"};
  if (index < 0 || index >= 5) {
    return L"medium";
  }
  return kNames[index];
}

void applyPresetFields(HWND hwnd) {
  if (g_state == nullptr) {
    return;
  }
  g_state->updatingPresetFields = true;
  const int index = static_cast<int>(
      SendMessageW(GetDlgItem(hwnd, IDC_PRESET), CB_GETCURSEL, 0, 0));
  RecordOptions options{};
  options.preset = presetNameFromIndex(index);
  applyRecordPreset(options);
  setWindowText(GetDlgItem(hwnd, IDC_FPS), std::to_wstring(options.fps));
  setWindowText(GetDlgItem(hwnd, IDC_BITRATE),
                std::to_wstring(options.bitrate));
  g_state->fpsUserEdited = false;
  g_state->bitrateUserEdited = false;
  g_state->updatingPresetFields = false;
}

bool browseSaveFile(HWND owner, std::wstring &path) {
  wchar_t fileBuffer[MAX_PATH]{};
  if (!path.empty() && path.size() < MAX_PATH) {
    wcscpy_s(fileBuffer, path.c_str());
  }

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"MP4 video (*.mp4)\0*.mp4\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = fileBuffer;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags =
      OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
  ofn.lpstrDefExt = L"mp4";

  EnableWindow(owner, FALSE);
  const BOOL ok = GetSaveFileNameW(&ofn);
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  if (!ok) {
    return false;
  }
  path = fileBuffer;
  return true;
}

bool browseFolder(HWND owner, std::wstring &path) {
  std::wstring startPath = path;
  BROWSEINFOW bi{};
  bi.hwndOwner = owner;
  bi.lpszTitle = L"Select output folder";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
  if (!startPath.empty()) {
    bi.lParam = reinterpret_cast<LPARAM>(startPath.c_str());
    bi.lpfn = [](HWND hwnd, UINT msg, LPARAM lp, LPARAM data) -> int {
      (void)lp;
      if (msg == BFFM_INITIALIZED && data != 0) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
      }
      return 0;
    };
  }

  EnableWindow(owner, FALSE);
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  if (pidl == nullptr) {
    return false;
  }
  wchar_t folder[MAX_PATH]{};
  if (!SHGetPathFromIDListW(pidl, folder)) {
    CoTaskMemFree(pidl);
    return false;
  }
  CoTaskMemFree(pidl);
  path = folder;
  return true;
}

void setRecordingUi(HWND hwnd, bool recording) {
  EnableWindow(GetDlgItem(hwnd, IDC_START), !recording ? TRUE : FALSE);
  EnableWindow(GetDlgItem(hwnd, IDC_STOP), recording ? TRUE : FALSE);
  EnableWindow(GetDlgItem(hwnd, IDC_LIST), !recording ? TRUE : FALSE);
  EnableWindow(GetDlgItem(hwnd, IDC_REFRESH), !recording ? TRUE : FALSE);
  EnableWindow(GetDlgItem(hwnd, IDC_SHOW_ALL), !recording ? TRUE : FALSE);
  const bool canOpenRecent =
      !recording && g_state != nullptr && !g_state->lastRecordedPath.empty();
  EnableWindow(GetDlgItem(hwnd, IDC_OPEN_RECENT), canOpenRecent ? TRUE : FALSE);
}

void openRecentVideo(HWND hwnd) {
  if (g_state == nullptr || g_state->lastRecordedPath.empty()) {
    setStatus(L"No recent recording");
    return;
  }
  const DWORD attr = GetFileAttributesW(g_state->lastRecordedPath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    setStatus(L"Recent file not found");
    return;
  }
  const HINSTANCE result =
      ShellExecuteW(hwnd, L"open", g_state->lastRecordedPath.c_str(), nullptr,
                    nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    setStatus(L"Failed to open recent video");
  }
}

Result<RecordOptions> buildRecordOptions(HWND hwnd) {
  const HWND listView = GetDlgItem(hwnd, IDC_LIST);
  const int selected = ListView_GetNextItem(listView, -1, LVNI_SELECTED);
  if (selected < 0) {
    return Result<RecordOptions>::fail("Select a window to record");
  }

  LVITEMW item{};
  item.mask = LVIF_PARAM;
  item.iItem = selected;
  if (!ListView_GetItem(listView, &item)) {
    return Result<RecordOptions>::fail("Failed to read selected window");
  }

  RecordOptions options{};
  options.hwnd = static_cast<unsigned long long>(item.lParam);
  options.outputPath = getWindowTextString(GetDlgItem(hwnd, IDC_OUTPUT_FILE));
  options.outputDir = getWindowTextString(GetDlgItem(hwnd, IDC_OUTPUT_DIR));
  options.preset = presetNameFromIndex(static_cast<int>(
      SendMessageW(GetDlgItem(hwnd, IDC_PRESET), CB_GETCURSEL, 0, 0)));

  try {
    options.fps = std::stoi(getWindowTextString(GetDlgItem(hwnd, IDC_FPS)));
    options.bitrate =
        std::stoi(getWindowTextString(GetDlgItem(hwnd, IDC_BITRATE)));
  } catch (...) {
    return Result<RecordOptions>::fail("Invalid FPS or bitrate");
  }
  options.fpsExplicit = g_state != nullptr && g_state->fpsUserEdited;
  options.bitrateExplicit = g_state != nullptr && g_state->bitrateUserEdited;

  options.cursor = IsDlgButtonChecked(hwnd, IDC_CURSOR) == BST_CHECKED;
  options.hotkeys = IsDlgButtonChecked(hwnd, IDC_HOTKEYS) == BST_CHECKED;
  options.startPaused =
      IsDlgButtonChecked(hwnd, IDC_START_PAUSED) == BST_CHECKED;

  try {
    std::wstring speedText = getWindowTextString(GetDlgItem(hwnd, IDC_SPEED));
    if (!speedText.empty() &&
        (speedText.back() == L'x' || speedText.back() == L'X')) {
      speedText.pop_back();
    }
    options.speed = speedText.empty() ? 1.0 : std::stod(speedText);
  } catch (...) {
    return Result<RecordOptions>::fail("Invalid speed value");
  }

  if (const auto preset = presetValues(options.preset); !preset.isOk()) {
    return Result<RecordOptions>::fail(preset.error());
  }
  applyRecordPreset(options);

  if (options.fps <= 0 || options.fps > 240) {
    return Result<RecordOptions>::fail("FPS must be between 1 and 240");
  }
  if (options.bitrate <= 0) {
    return Result<RecordOptions>::fail("Bitrate must be positive");
  }
  if (options.speed <= 0.0 || options.speed > 64.0) {
    return Result<RecordOptions>::fail("Speed must be between 0 and 64");
  }

  const auto outputResult = resolveRecordOutputPath(options);
  if (!outputResult.isOk()) {
    return Result<RecordOptions>::fail(outputResult.error());
  }
  options.outputPath = outputResult.value();
  return Result<RecordOptions>::ok(std::move(options));
}

void startRecording(HWND hwnd) {
  if (g_state == nullptr || g_state->recording.load()) {
    return;
  }
  const auto optionsResult = buildRecordOptions(hwnd);
  if (!optionsResult.isOk()) {
    setStatus(utf8ToWide(optionsResult.error()));
    return;
  }

  g_state->stopRequested.store(false);
  g_state->recording.store(true);
  setRecordingUi(hwnd, true);
  setStatus(L"Recording...");

  const RecordOptions options = optionsResult.value();
  if (g_state->recordThread.joinable()) {
    g_state->recordThread.join();
  }
  g_state->recordThread = std::thread([hwnd, options]() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const Status result = runRecorder(options, &g_state->stopRequested);
    winrt::uninit_apartment();
    auto *payload = new RecordDonePayload{};
    payload->ok = result.isOk();
    payload->outputPath = options.outputPath;
    if (!result.isOk()) {
      payload->error = result.error();
    }
    PostMessageW(hwnd, WM_APP_RECORD_DONE, 0,
                 reinterpret_cast<LPARAM>(payload));
  });
}

void stopRecording() {
  if (g_state != nullptr) {
    g_state->stopRequested.store(true);
    setStatus(L"Stopping...");
  }
}

void onRecordDone(HWND hwnd, RecordDonePayload *payload) {
  if (g_state == nullptr) {
    delete payload;
    return;
  }
  if (g_state->recordThread.joinable()) {
    g_state->recordThread.join();
  }
  g_state->recording.store(false);
  if (payload->ok) {
    if (!payload->outputPath.empty()) {
      g_state->lastRecordedPath = payload->outputPath;
    }
    setStatus(L"Saved " + payload->outputPath);
  } else {
    setStatus(L"Error: " + utf8ToWide(payload->error));
  }
  setRecordingUi(hwnd, false);
  delete payload;
}

void createChildControls(HWND hwnd) {
  const int dpi = guiDpi(hwnd);
  const auto px = [&](int v) { return guiPx(v, dpi); };
  const int margin = px(8);
  const int innerW = px(kBaseClientWidth) - margin * 2;
  const int browseW = px(88);
  const int editW = innerW - px(88) - browseW;

  createButton(hwnd, IDC_REFRESH, L"Refresh", margin, px(8), px(88), px(26));
  createCheck(hwnd, IDC_SHOW_ALL, L"Show all windows", px(104), px(10), px(176),
              px(22), false);

  CreateWindowExW(0, WC_LISTVIEWW, L"",
                  WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                      LVS_SHOWSELALWAYS,
                  margin, px(36), innerW, px(160), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)),
                  nullptr, nullptr);

  CreateWindowExW(0, L"STATIC", L"Selected: (none)",
                  WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, margin, px(200),
                  innerW, px(18), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED)),
                  nullptr, nullptr);

  createLabel(hwnd, L"Output file:", margin, px(224), px(84), px(18));
  createEdit(hwnd, IDC_OUTPUT_FILE, px(92), px(220), editW, px(24));
  createButton(hwnd, IDC_BROWSE_FILE, L"Browse...", margin + px(92) + editW,
               px(218), browseW, px(26));

  createLabel(hwnd, L"Output dir:", margin, px(252), px(84), px(18));
  createEdit(hwnd, IDC_OUTPUT_DIR, px(92), px(248), editW, px(24));
  setWindowText(GetDlgItem(hwnd, IDC_OUTPUT_DIR), defaultOutputDir());
  createButton(hwnd, IDC_BROWSE_DIR, L"Browse...", margin + px(92) + editW,
               px(246), browseW, px(26));

  createLabel(hwnd, L"Preset:", margin, px(282), px(52), px(18));
  const HWND preset =
      CreateWindowExW(0, L"COMBOBOX", L"",
                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                      px(64), px(278), px(108), px(160), hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRESET)),
                      nullptr, nullptr);
  SendMessageW(preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"low"));
  SendMessageW(preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"medium"));
  SendMessageW(preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"high"));
  SendMessageW(preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ultra"));
  SendMessageW(preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"extreme"));
  SendMessageW(preset, CB_SETCURSEL, 1, 0);

  createLabel(hwnd, L"FPS:", px(184), px(282), px(36), px(18));
  createEdit(hwnd, IDC_FPS, px(220), px(278), px(52), px(24));
  createLabel(hwnd, L"Bitrate:", px(280), px(282), px(56), px(18));
  createEdit(hwnd, IDC_BITRATE, px(340), px(278), px(84), px(24));

  createCheck(hwnd, IDC_CURSOR, L"Cursor", margin, px(310), px(88), px(22),
              true);
  createCheck(hwnd, IDC_HOTKEYS, L"Hotkeys", px(104), px(310), px(92), px(22),
              true);
  createCheck(hwnd, IDC_START_PAUSED, L"Start paused", px(204), px(310),
              px(132), px(22), false);
  createLabel(hwnd, L"Speed:", px(344), px(312), px(52), px(18));
  createEdit(hwnd, IDC_SPEED, px(400), px(308), px(52), px(24));
  setWindowText(GetDlgItem(hwnd, IDC_SPEED), L"1");

  createButton(hwnd, IDC_START, L"Start Recording", margin, px(340), px(144),
               px(30));
  createButton(hwnd, IDC_STOP, L"Stop", px(160), px(340), px(88), px(30));
  createButton(hwnd, IDC_OPEN_RECENT, L"Play Recent", px(256), px(340), px(120),
               px(30));
  EnableWindow(GetDlgItem(hwnd, IDC_STOP), FALSE);
  EnableWindow(GetDlgItem(hwnd, IDC_OPEN_RECENT), FALSE);

  CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE, margin,
                  px(376), innerW, px(18), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
                  nullptr, nullptr);

  const int installBtnW = px(92);
  const int installEditW = innerW - px(84) - installBtnW;
  createLabel(hwnd, L"Install dir:", margin, px(418), px(80), px(18));
  createEdit(hwnd, IDC_INSTALL_DIR, px(88), px(414), installEditW, px(24));
  setWindowText(GetDlgItem(hwnd, IDC_INSTALL_DIR), defaultInstallDir());
  createButton(hwnd, IDC_INSTALL, L"Install", margin + px(88) + installEditW,
               px(412), installBtnW, px(28));
  createButton(hwnd, IDC_UNINSTALL, L"Uninstall",
               margin + px(88) + installEditW, px(444), installBtnW, px(28));

  setupListColumns(GetDlgItem(hwnd, IDC_LIST), dpi);
  applyPresetFields(hwnd);
  refreshWindowList(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    createChildControls(hwnd);
    return 0;
  case WM_COMMAND: {
    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);
    switch (id) {
    case IDC_REFRESH:
      refreshWindowList(hwnd);
      return 0;
    case IDC_SHOW_ALL:
      if (code == BN_CLICKED) {
        refreshWindowList(hwnd);
      }
      return 0;
    case IDC_BROWSE_FILE: {
      std::wstring path =
          getWindowTextString(GetDlgItem(hwnd, IDC_OUTPUT_FILE));
      if (browseSaveFile(hwnd, path)) {
        setWindowText(GetDlgItem(hwnd, IDC_OUTPUT_FILE), path);
      }
      return 0;
    }
    case IDC_BROWSE_DIR: {
      std::wstring path = getWindowTextString(GetDlgItem(hwnd, IDC_OUTPUT_DIR));
      if (browseFolder(hwnd, path)) {
        setWindowText(GetDlgItem(hwnd, IDC_OUTPUT_DIR), path);
      }
      return 0;
    }
    case IDC_PRESET:
      if (code == CBN_SELCHANGE) {
        applyPresetFields(hwnd);
      }
      return 0;
    case IDC_FPS:
    case IDC_BITRATE:
      if (code == EN_CHANGE && g_state != nullptr &&
          !g_state->updatingPresetFields) {
        if (id == IDC_FPS) {
          g_state->fpsUserEdited = true;
        } else {
          g_state->bitrateUserEdited = true;
        }
      }
      return 0;
    case IDC_START:
      startRecording(hwnd);
      return 0;
    case IDC_STOP:
      stopRecording();
      return 0;
    case IDC_OPEN_RECENT:
      openRecentVideo(hwnd);
      return 0;
    case IDC_INSTALL: {
      InstallOptions options{};
      options.dir = getWindowTextString(GetDlgItem(hwnd, IDC_INSTALL_DIR));
      const Status result = installToPath(options);
      setStatus(result.isOk()
                    ? L"Installed"
                    : L"Install failed: " + utf8ToWide(result.error()));
      return 0;
    }
    case IDC_UNINSTALL: {
      InstallOptions options{};
      options.dir = getWindowTextString(GetDlgItem(hwnd, IDC_INSTALL_DIR));
      const Status result = uninstallFromPath(options);
      setStatus(result.isOk()
                    ? L"Uninstalled"
                    : L"Uninstall failed: " + utf8ToWide(result.error()));
      return 0;
    }
    default:
      break;
    }
    break;
  }
  case WM_NOTIFY: {
    const auto *hdr = reinterpret_cast<LPNMHDR>(lParam);
    if (hdr->idFrom == IDC_LIST && hdr->code == LVN_ITEMCHANGED) {
      const auto *nm = reinterpret_cast<LPNMLISTVIEW>(lParam);
      if ((nm->uChanged & LVIF_STATE) != 0) {
        updateSelectedWindowDisplay(hwnd);
      }
    }
    return 0;
  }
  case WM_APP_LOG: {
    auto *text = reinterpret_cast<std::string *>(lParam);
    if (text != nullptr) {
      setStatus(utf8ToWide(*text));
      delete text;
    }
    return 0;
  }
  case WM_APP_RECORD_DONE:
    onRecordDone(hwnd, reinterpret_cast<RecordDonePayload *>(lParam));
    return 0;
  case WM_CLOSE:
    if (g_state != nullptr && g_state->recording.load()) {
      g_state->stopRequested.store(true);
      return 0;
    }
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int runGui() {
  const HRESULT oleHr = OleInitialize(nullptr);

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&icc);

  GuiState state{};
  g_state = &state;

  logSetGuiSink([](LogLevel level, const std::string &message) {
    if (g_state == nullptr || g_state->hwnd == nullptr) {
      return;
    }
    std::string line = message;
    if (level == LogLevel::Error) {
      line = "Error: " + line;
    }
    auto *heap = new std::string(std::move(line));
    PostMessageW(g_state->hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(heap));
  });

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  RegisterClassExW(&wc);

  const int dpi = guiDpi(nullptr);
  RECT rect{0, 0, guiPx(kBaseClientWidth, dpi), guiPx(kBaseClientHeight, dpi)};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;

  state.hwnd = CreateWindowExW(0, kWindowClass, L"wrec", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                               nullptr, nullptr, wc.hInstance, nullptr);
  if (state.hwnd == nullptr) {
    logSetGuiSink({});
    g_state = nullptr;
    if (SUCCEEDED(oleHr)) {
      OleUninitialize();
    }
    return 1;
  }

  ShowWindow(state.hwnd, SW_SHOW);
  UpdateWindow(state.hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (state.recording.load()) {
    state.stopRequested.store(true);
  }
  if (state.recordThread.joinable()) {
    state.recordThread.join();
  }

  logSetGuiSink({});
  g_state = nullptr;
  if (SUCCEEDED(oleHr)) {
    OleUninitialize();
  }
  return static_cast<int>(msg.wParam);
}
