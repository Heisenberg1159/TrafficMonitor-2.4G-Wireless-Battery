// Renders the plugin's real DrawItem(HDC) path to a PNG for review, emulating
// TrafficMonitor V1.86's two taskbar back ends:
//   GDI  : opaque DIB filled with the taskbar color
//   D2D  : GDI-interop DIB pre-filled with alpha 1/255; afterwards alpha 1/255
//          -> transparent, alpha 0 -> opaque (host shader), then composited
//   RenderPreview.exe out.png ["@Microsoft JhengHei UI"] [pt]
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "gdiplus_drawer.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {

int SaveClsid(const wchar_t* mime, CLSID* clsid) {
    UINT n = 0, size = 0;
    GetImageEncodersSize(&n, &size);
    std::vector<BYTE> buf(size);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(n, size, codecs);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(codecs[i].MimeType, mime) == 0) { *clsid = codecs[i].Clsid; return 0; }
    return -1;
}

DeviceStatus Make(LinkState st, int level, bool charging) {
    DeviceStatus s;
    s.state = st;
    s.level = level;
    s.charging = charging;
    s.hasReading = level >= 0;
    return s;
}

struct Case { const wchar_t* name; DeviceKind kind; DeviceStatus s; };

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = argc > 1 ? argv[1] : L"preview.png";
    const wchar_t* face = argc > 2 ? argv[2] : L"@Microsoft JhengHei UI";
    int pt = argc > 3 ? _wtoi(argv[3]) : 8;

    GdiplusStartupInput in;
    ULONG_PTR token;
    GdiplusStartup(&token, &in, nullptr);
    {
        std::vector<Case> cases = {
            {L"在线 85%", DeviceKind::Mouse, Make(LinkState::Online, 85, false)},
            {L"充电 23%", DeviceKind::Keyboard, Make(LinkState::Online, 23, true)},
            {L"中等 45%", DeviceKind::Mouse, Make(LinkState::Online, 45, false)},
            {L"低电 12%", DeviceKind::Keyboard, Make(LinkState::Online, 12, false)},
            {L"休眠 85%", DeviceKind::Mouse, Make(LinkState::Asleep, 85, false)},
            {L"未连接", DeviceKind::Keyboard, Make(LinkState::NotFound, -1, false)},
        };
        struct Row { bool dark; bool d2d; int dpi; };
        std::vector<Row> rows;
        for (int dark = 1; dark >= 0; --dark)
            for (int d2d = 0; d2d < 2; ++d2d)
                for (int dpi : {96, 144}) rows.push_back({dark != 0, d2d != 0, dpi});

        const int W = 900, labelW = 150;
        std::vector<int> rowH;
        for (auto& r : rows) rowH.push_back(r.dpi == 96 ? 26 : 38);
        int H = 0;
        for (int h : rowH) H += h + 2;
        Bitmap canvas(W, H, PixelFormat32bppARGB);
        Graphics cg(&canvas);
        cg.Clear(Color(255, 128, 128, 128));
        Font labelFont(L"Microsoft YaHei UI", 9);
        SolidBrush labelBrush(Color(255, 255, 255, 255));

        HDC screen = GetDC(nullptr);
        int top = 0;
        for (size_t ri = 0; ri < rows.size(); ++ri) {
            const Row& r = rows[ri];
            const int h = rowH[ri];
            COLORREF bg = r.dark ? RGB(32, 32, 32) : RGB(243, 243, 243);
            COLORREF fg = r.dark ? RGB(255, 255, 255) : RGB(0, 0, 0);

            BITMAPINFO bi{};
            bi.bmiHeader = {sizeof(BITMAPINFOHEADER), W - labelW, -h, 1, 32, BI_RGB};
            void* bits = nullptr;
            HDC dc = CreateCompatibleDC(screen);
            HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            HGDIOBJ oldBmp = SelectObject(dc, dib);
            auto* px = static_cast<DWORD*>(bits);
            const int n = (W - labelW) * h;
            for (int i = 0; i < n; ++i)  // D2D interop: transparent black with alpha 1
                px[i] = r.d2d ? 0x01000000 : (0xFF000000 | (GetRValue(bg) << 16) | (GetGValue(bg) << 8) | GetBValue(bg));

            HFONT font = CreateFontW(-MulDiv(pt, r.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, face);
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetTextColor(dc, fg);
            SIZE sz{};
            GetTextExtentPoint32W(dc, L"100%", 4, &sz);  // same as plugin GetItemWidthEx
            int itemW = BatteryItemWidth(sz.cx, sz.cy);
            int x = 6;
            for (const auto& c : cases) {
                RenderStyle style{fg, fg, r.dark};
                DrawBatteryItemToDC(dc, c.kind, c.s, style, x, 0, itemW, h);
                x += itemW + 22;
            }
            GdiFlush();
            if (r.d2d) {  // host shader, then composite onto the taskbar color
                for (int i = 0; i < n; ++i) {
                    DWORD p = px[i];
                    BYTE a = p >> 24;
                    if (a == 1) a = 0, p &= 0x00FFFFFF;
                    else if (a == 0) a = 255;
                    auto blend = [&](int sh, BYTE b) {
                        int src = (p >> sh) & 0xFF;  // premultiplied
                        return static_cast<DWORD>((std::min)(255, src + b * (255 - a) / 255)) << sh;
                    };
                    px[i] = 0xFF000000 | blend(16, GetRValue(bg)) | blend(8, GetGValue(bg)) | blend(0, GetBValue(bg));
                }
            } else {
                for (int i = 0; i < n; ++i) px[i] |= 0xFF000000;
            }
            Bitmap rowBmp(W - labelW, h, (W - labelW) * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
            cg.DrawImage(&rowBmp, labelW, top);
            std::wstring label = std::wstring(r.dark ? L"Dark" : L"Light") + (r.d2d ? L" transparent " : L" GDI ") +
                                 std::to_wstring(r.dpi * 100 / 96) + L"%";
            cg.DrawString(label.c_str(), -1, &labelFont, PointF(4, (REAL)top + h / 2 - 9), &labelBrush);
            SelectObject(dc, oldFont);
            SelectObject(dc, oldBmp);
            DeleteObject(font);
            DeleteObject(dib);
            DeleteDC(dc);
            top += h + 2;
        }
        ReleaseDC(nullptr, screen);
        CLSID png;
        SaveClsid(L"image/png", &png);
        canvas.Save(out, &png, nullptr);
    }
    GdiplusShutdown(token);
    return 0;
}
