// Loads WirelessBattery.dll the way TrafficMonitor does and drives its interface.
//   PluginHostTest.exe <dll> <seconds> <configDir>
#include <windows.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4100)
#include "PluginInterface.h"
#pragma warning(pop)

class FakeApp : public ITrafficMonitor {
public:
    explicit FakeApp(std::wstring dir) : dir_(std::move(dir)) {}
    int GetAPIVersion() override { return 1; }
    const wchar_t* GetVersion() override { return L"1.86"; }
    double GetMonitorValue(MonitorItem) override { return 0; }
    const wchar_t* GetMonitorValueString(MonitorItem, int) override { return L""; }
    void ShowNotifyMessage(const wchar_t* msg) override { wprintf(L"  [notify] %s\n", msg); }
    unsigned short GetLanguageId() const override { return 0x804; }
    const wchar_t* GetPluginConfigDir() const override { return dir_.c_str(); }
    int GetDPI(DPIType) const override { return 96; }
    unsigned int GetThemeColor() const override { return 0; }
    const wchar_t* GetStringRes(const wchar_t*, const wchar_t*) override { return L""; }
    void* GetMainWindowHwnd() override { return nullptr; }
    void* GetTaskbarWindowHwnd() override { return nullptr; }

private:
    std::wstring dir_;
};

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc < 4) return 2;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { wprintf(L"LoadLibrary failed %lu\n", GetLastError()); return 1; }
    using GetInstance = ITMPlugin* (*)();
    auto get = reinterpret_cast<GetInstance>(GetProcAddress(dll, "TMPluginGetInstance"));
    if (!get) { wprintf(L"export TMPluginGetInstance missing\n"); return 1; }
    ITMPlugin* plugin = get();
    wprintf(L"API %d, %s %s by %s\n", plugin->GetAPIVersion(), plugin->GetInfo(ITMPlugin::TMI_NAME),
            plugin->GetInfo(ITMPlugin::TMI_VERSION), plugin->GetInfo(ITMPlugin::TMI_AUTHOR));

    FakeApp app(argv[3]);
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, argv[3]);
    plugin->OnInitialize(&app);
    for (int i = 0; IPluginItem* item = plugin->GetItem(i); ++i)
        wprintf(L"item %d: %s id=%s label=%s sample=%s\n", i, item->GetItemName(), item->GetItemId(),
                item->GetItemLableText(), item->GetItemValueSampleText());
    for (int i = 0; i < plugin->GetCommandCount(); ++i) wprintf(L"command %d: %s\n", i, plugin->GetCommandName(i));

    int seconds = _wtoi(argv[2]);
    ULONGLONG start = GetTickCount64();
    for (int s = 0; s < seconds; ++s) {
        ULONGLONG t0 = GetTickCount64();
        plugin->DataRequired();  // TrafficMonitor calls this on its UI thread every second
        ULONGLONG cost = GetTickCount64() - t0;
        if (s % 5 == 0 || s == seconds - 1) {
            wprintf(L"t=%2llus DataRequired=%llums  %s=%s  %s=%s  graph=%.2f/%.2f\n", (t0 - start) / 1000, cost,
                    plugin->GetItem(0)->GetItemLableText(), plugin->GetItem(0)->GetItemValueText(),
                    plugin->GetItem(1)->GetItemLableText(), plugin->GetItem(1)->GetItemValueText(),
                    plugin->GetItem(0)->GetResourceUsageGraphValue(), plugin->GetItem(1)->GetResourceUsageGraphValue());
            wprintf(L"  tooltip: %s\n", plugin->GetTooltipInfo());
        }
        Sleep(1000);
    }
    // Custom draw through the V1.86 path: DrawItem(HDC) on a 32bpp DIB.
    for (int i = 0; IPluginItem* item = plugin->GetItem(i); ++i) {
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), 200, -24, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        SelectObject(dc, dib);
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        int width = item->GetItemWidthEx(dc);
        item->DrawItem(dc, 0, 0, width, 24, true);
        GdiFlush();
        int painted = 0;
        for (int p = 0; p < 200 * 24; ++p) painted += static_cast<DWORD*>(bits)[p] != 0;
        wprintf(L"DrawItem %s: custom=%d width=%d painted_pixels=%d\n", item->GetItemName(), item->IsCustomDraw(),
                width, painted);
        DeleteDC(dc);
        DeleteObject(dib);
    }
    return 0;  // exit without FreeLibrary, like a process shutdown with the worker alive
}
