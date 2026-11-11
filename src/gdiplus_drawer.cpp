#include "gdiplus_drawer.h"

#include <objidl.h>
#include <gdiplus.h>

#include <cstring>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

namespace {

using namespace Gdiplus;

Color ToColor(unsigned long c, unsigned char a) { return Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

void EnsureGdiplus() {
    // Started once on first draw (never from DllMain) and left running until
    // process exit; GDI+ reference-counts startups made by the host.
    static ULONG_PTR token = [] {
        ULONG_PTR t = 0;
        GdiplusStartupInput input;
        GdiplusStartup(&t, &input, nullptr);
        return t;
    }();
    (void)token;
}

class GdiplusDrawer : public IPluginDrawer {
public:
    GdiplusDrawer(Graphics& g, HDC measureDc, const Font& font) : g_(g), dc_(measureDc), font_(font) {
        g_.SetSmoothingMode(SmoothingModeAntiAlias);
        g_.SetPixelOffsetMode(PixelOffsetModeHalf);
        g_.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);  // no ClearType on a transparent buffer
    }

    int GetAPIVersion() override { return 1; }

    void DrawWindowText(int x, int y, int w, int h, const wchar_t* s, unsigned long color, Alignment align, bool,
                        unsigned char alpha) override {
        StringFormat f(StringFormat::GenericTypographic());
        f.SetLineAlignment(StringAlignmentCenter);
        f.SetAlignment(align == LEFT ? StringAlignmentNear : align == RIGHT ? StringAlignmentFar : StringAlignmentCenter);
        f.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        SolidBrush b(ToColor(color, alpha));
        g_.DrawString(s, -1, &font_, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), &f, &b);
    }

    void SetDrawRect(int, int, int, int) override {}

    void FillRect(int x, int y, int w, int h, unsigned long color, unsigned char alpha) override {
        SolidBrush b(ToColor(color, alpha));
        g_.SetSmoothingMode(SmoothingModeNone);  // crisp, pixel-aligned bars
        g_.FillRectangle(&b, x, y, w, h);
        g_.SetSmoothingMode(SmoothingModeAntiAlias);
    }

    void DrawRectOutLine(int x, int y, int w, int h, unsigned long color, int width, bool, unsigned char alpha,
                         int radius) override {
        Pen p(ToColor(color, alpha), (REAL)width);
        REAL o = width / 2.0f;
        RectF r(x + o, y + o, w - (REAL)width, h - (REAL)width);
        GraphicsPath path;
        REAL d = (REAL)(2 * radius);
        if (radius > 0 && d <= r.Width && d <= r.Height) {
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
            path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
            path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
            path.CloseFigure();
        } else {
            path.AddRectangle(r);
        }
        g_.DrawPath(&p, &path);
    }

    void DrawBitmap(void*, int, int, int, int, StretchMode, unsigned char) override {}
    void DrawIcon(void*, int, int, int, int) override {}

    void GetTextExtent(const wchar_t* s, int& w, int& h) override {
        SIZE sz{};
        GetTextExtentPoint32W(dc_, s, lstrlenW(s), &sz);
        w = sz.cx;
        h = sz.cy;
    }

    void DrawLine(int x1, int y1, int x2, int y2, unsigned long color, int width, bool, unsigned char alpha) override {
        Pen p(ToColor(color, alpha), (REAL)width);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        g_.DrawLine(&p, x1, y1, x2, y2);
    }

    void* GetHDC() override { return dc_; }

private:
    Graphics& g_;
    HDC dc_;
    const Font& font_;
};

}  // namespace

void DrawBatteryItemToDC(HDC hdc, DeviceKind kind, const DeviceStatus& s, const RenderStyle& style,
                         int x, int y, int w, int h) {
    if (!hdc || w <= 0 || h <= 0) return;
    EnsureGdiplus();

    // Host font, minus the '@' prefix of vertical font faces (e.g.
    // "@Microsoft JhengHei UI"), which would rotate glyphs.
    LOGFONTW lf{};
    HFONT hostFont = static_cast<HFONT>(GetCurrentObject(hdc, OBJ_FONT));
    if (!hostFont || !GetObjectW(hostFont, sizeof(lf), &lf)) GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
    if (lf.lfFaceName[0] == L'@') memmove(lf.lfFaceName, lf.lfFaceName + 1, sizeof(lf.lfFaceName) - sizeof(wchar_t));
    lf.lfEscapement = lf.lfOrientation = 0;
    HFONT font = CreateFontIndirectW(&lf);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!mem || !dib || !bits || !font) {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        if (font) DeleteObject(font);
        return;
    }
    std::memset(bits, 0, static_cast<size_t>(w) * h * 4);  // fully transparent
    HGDIOBJ oldBmp = SelectObject(mem, dib);
    HGDIOBJ oldFont = SelectObject(mem, font);

    {
        Bitmap canvas(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Graphics g(&canvas);
        Font gpFont(mem, font);
        if (gpFont.GetLastStatus() == Ok) {
            GdiplusDrawer drawer(g, mem, gpFont);
            DrawBatteryItem(&drawer, kind, s, style, 0, 0, w, h);
        }
        g.Flush(FlushIntentionSync);
    }

    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(hdc, x, y, w, h, mem, 0, 0, w, h, blend);

    SelectObject(mem, oldFont);
    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    DeleteDC(mem);
    DeleteObject(font);
}
