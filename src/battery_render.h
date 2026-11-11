// Custom drawing of one battery item through TrafficMonitor's IPluginDrawer,
// which renders through GDI or Direct2D depending on the host window.
#pragma once

#pragma warning(push)
#pragma warning(disable : 4100)
#include "PluginInterface.h"
#pragma warning(pop)

#include "battery_monitor.h"

struct RenderStyle {
    unsigned long textColor = 0xFFFFFF;   // 0xBBGGRR, value text
    unsigned long iconColor = 0xFFFFFF;   // device glyph + battery outline
    bool darkMode = true;                 // dark background
};

// Width in pixels for a text height |textH| and a "100%" text width |textW|.
int BatteryItemWidth(int textW, int textH);

void DrawBatteryItem(IPluginDrawer* d, DeviceKind kind, const DeviceStatus& s, const RenderStyle& style,
                     int x, int y, int w, int h);
