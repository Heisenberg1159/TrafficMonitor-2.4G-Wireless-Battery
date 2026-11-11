// GDI+ implementation of IPluginDrawer, plus the HDC entry point used by
// TrafficMonitor versions whose plugin API stops at DrawItem(HDC) (<= V1.86).
#pragma once

#include <windows.h>

#include "battery_render.h"

// Draws one item onto |hdc| (font and text color as selected by the host).
// Renders anti-aliased into a premultiplied-alpha buffer, then AlphaBlends it,
// so the destination alpha stays valid on TrafficMonitor's transparent
// (Direct2D + GDI interop) taskbar as well as on plain GDI windows.
void DrawBatteryItemToDC(HDC hdc, DeviceKind kind, const DeviceStatus& s, const RenderStyle& style,
                         int x, int y, int w, int h);
