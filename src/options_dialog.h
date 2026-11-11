// Plugin options dialog (display style + poll interval).
#pragma once

#include <windows.h>

struct OptionsValues {
    bool iconStyle = true;
    int pollSeconds = 60;
};

constexpr int kMinPollSeconds = 10;
constexpr int kMaxPollSeconds = 3600;

// Modal dialog. Returns true when the user pressed OK; |values| then holds
// the new settings (already range checked).
bool RunOptionsDialog(HWND parent, OptionsValues& values);
