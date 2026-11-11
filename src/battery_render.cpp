#include "battery_render.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

// Level colors, chosen to read well on both light and dark taskbars.
constexpr unsigned long kGreen = RGB(52, 199, 89);
constexpr unsigned long kOrange = RGB(255, 159, 10);
constexpr unsigned long kRed = RGB(255, 69, 58);
constexpr unsigned long kCharge = RGB(48, 209, 88);

// Layout in "units": 1u = 1px for a 16px-high font (9pt at 96 DPI).
constexpr float kPad = 1.0f, kGlyph = 13.0f, kGap1 = 4.0f;
constexpr float kBodyW = 21.0f, kBodyH = 11.0f, kNub = 2.0f, kGap2 = 4.0f;

int R(float v) { return static_cast<int>(std::lround(v)); }
float Unit(int textH) { return (std::max)(0.75f, textH / 16.0f); }
unsigned char Scale(unsigned char a, float f) { return static_cast<unsigned char>(a * f); }

// Rounded outline with the radius clamped so the corner arcs never overlap
// (an oversized radius makes GDI/D2D draw stray spurs).
void Outline(IPluginDrawer* d, int x, int y, int w, int h, unsigned long c, int stroke, unsigned char a, int radius) {
    int maxRadius = ((std::min)(w, h) - stroke) / 2;
    d->DrawRectOutLine(x, y, w, h, c, stroke, false, a, (std::max)(0, (std::min)(radius, maxRadius)));
}

void DrawMouseGlyph(IPluginDrawer* d, float left, int cy, float u, int stroke, unsigned long c, unsigned char a) {
    int w = R(8 * u), h = R(12 * u);
    int x = R(left + (kGlyph * u - w) / 2), y = cy - h / 2;
    Outline(d, x, y, w, h, c, stroke, a, R(4 * u));
    int mx = x + w / 2;
    d->DrawLine(mx, y + R(2.2f * u), mx, y + R(4.8f * u), c, stroke, false, a);  // wheel
}

void DrawKeyboardGlyph(IPluginDrawer* d, float left, int cy, float u, int stroke, unsigned long c, unsigned char a) {
    int w = R(kGlyph * u), h = R(9 * u);
    int x = R(left), y = cy - h / 2;
    Outline(d, x, y, w, h, c, stroke, a, R(2 * u));
    int dot = (std::max)(1, R(1.3f * u));
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 4; ++col) {
            d->FillRect(x + R((2.6f + col * 2.6f) * u), y + R((2.2f + row * 2.1f) * u), dot, dot, c, a);
        }
    }
    d->FillRect(x + R(3.6f * u), y + R(6.4f * u), R(5.8f * u), dot, c, a);  // space bar
}

}  // namespace

int BatteryItemWidth(int textW, int textH) {
    float u = Unit(textH);
    return R((kPad + kGlyph + kGap1 + kBodyW + kNub + kGap2) * u) + textW + R(kPad * u);
}

void DrawBatteryItem(IPluginDrawer* d, DeviceKind kind, const DeviceStatus& s, const RenderStyle& style,
                     int x, int y, int w, int h) {
    int textW = 0, textH = 0;
    d->GetTextExtent(L"100%", textW, textH);
    const float u = Unit(textH);
    const int cy = y + h / 2;
    const bool online = s.state == LinkState::Online;
    // Dim the whole item when the value is stale (asleep) or missing.
    const unsigned char a = online ? 255 : (s.hasReading ? 130 : 90);
    const int stroke = (std::max)(1, R(1.1f * u));

    // Device glyph.
    float left = x + kPad * u;
    if (kind == DeviceKind::Mouse)
        DrawMouseGlyph(d, left, cy, u, stroke, style.iconColor, a);
    else
        DrawKeyboardGlyph(d, left, cy, u, stroke, style.iconColor, a);

    // No receiver plugged in: there is no battery to show, so skip the battery
    // icon and put a dimmed "--" right after the device glyph.
    if (s.state == LinkState::NotFound) {
        const int tx = R(left + (kGlyph + kGap1) * u);
        if (tx < x + w) d->DrawWindowText(tx, y, x + w - tx, h, L"--", style.textColor, IPluginDrawer::LEFT, false, a);
        return;
    }

    // Battery body + terminal nub.
    const int bx = R(left + (kGlyph + kGap1) * u);
    const int bw = R(kBodyW * u), bh = R(kBodyH * u);
    const int by = cy - bh / 2;
    Outline(d, bx, by, bw, bh, style.iconColor, stroke, Scale(a, 0.85f), R(3 * u));
    const int nubH = R(5 * u);
    d->FillRect(bx + bw + R(0.6f * u), cy - nubH / 2, (std::max)(1, R(1.6f * u)), nubH, style.iconColor, Scale(a, 0.85f));

    // Level fill.
    const int inset = stroke + (std::max)(1, R(1.0f * u));
    const int ix = bx + inset, iy = by + inset, iw = bw - 2 * inset, ih = bh - 2 * inset;
    if (s.hasReading && iw > 0 && ih > 0) {
        const bool charging = online && s.charging;
        unsigned long color = charging ? kCharge : s.level <= 20 ? kRed : s.level <= 50 ? kOrange : kGreen;
        int fw = s.level > 0 ? (std::max)(1, R(iw * s.level / 100.0f)) : 0;
        if (charging) d->FillRect(ix, iy, iw, ih, color, Scale(a, 0.30f));  // tinted track
        if (fw > 0) d->FillRect(ix, iy, fw, ih, color, a);

        if (charging) {  // lightning bolt across the body
            unsigned long bolt = style.darkMode ? RGB(255, 255, 255) : RGB(25, 25, 25);
            int bcx = bx + bw / 2;
            int top = by + R(1.6f * u), bottom = by + bh - R(1.6f * u);
            int dx = R(1.6f * u), dy = R(0.7f * u);
            int lw = (std::max)(1, R(1.4f * u));
            d->DrawLine(bcx + dx, top, bcx - dx, cy + dy, bolt, lw);
            d->DrawLine(bcx - dx, cy + dy, bcx + dx, cy - dy, bolt, lw);
            d->DrawLine(bcx + dx, cy - dy, bcx - dx, bottom, bolt, lw);
        }
    }

    // Percentage.
    const int tx = bx + bw + R((kNub + kGap2) * u);
    std::wstring text = s.hasReading ? std::to_wstring(s.level) + L"%" : L"--";
    if (tx < x + w) d->DrawWindowText(tx, y, x + w - tx, h, text.c_str(), style.textColor, IPluginDrawer::LEFT, false, a);
}
