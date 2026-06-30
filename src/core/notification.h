#pragma once

#include <Windows.h>

#include <string>

void notifyTrayBalloon(const std::wstring &title, const std::wstring &body);

bool trayIconInstall(HWND ownerHwnd, UINT callbackMsg);
void trayIconRemove();
void trayIconShowBalloon(const std::wstring &title, const std::wstring &body);
bool trayIconIsActive();
