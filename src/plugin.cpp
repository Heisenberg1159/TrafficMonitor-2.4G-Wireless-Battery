// TrafficMonitor plugin: battery level of 2.4G wireless mouse / keyboard.
#include <windows.h>
#include <shellapi.h>

#include <string>

#pragma warning(push)
#pragma warning(disable : 4100)  // upstream header: unreferenced parameters
#include "PluginInterface.h"
#pragma warning(pop)
#include "battery_monitor.h"
#include "battery_render.h"
#include "gdiplus_drawer.h"
#include "options_dialog.h"

#pragma comment(lib, "shell32.lib")

namespace {

BatteryMonitor& Monitor() {
    static BatteryMonitor monitor;
    return monitor;
}

// Colors the host passes right before each custom draw (OnExtenedInfo).
struct HostColors {
    bool taskbar = false;
    bool hasValue = false, hasLabel = false;
    unsigned long value = 0, label = 0;
} g_colors;

const wchar_t* KindLabel(DeviceKind k) { return k == DeviceKind::Mouse ? L"鼠标" : L"键盘"; }

std::wstring FormatTime(const SYSTEMTIME& t) {
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    return buf;
}

std::wstring ValueText(const DeviceStatus& s, const MonitorConfig& c) {
    if (!s.hasReading) return L"--";
    std::wstring v = std::to_wstring(s.level) + L"%";
    if (s.state == LinkState::Online) {
        if (s.charging) v += c.chargingMark;
    } else {
        v += c.sleepMark;
    }
    return v;
}

std::wstring TooltipLine(DeviceKind k, const DeviceStatus& s) {
    std::wstring name = s.name.empty() ? std::wstring(KindLabel(k)) : s.name;
    std::wstring line = name + L"：";
    switch (s.state) {
        case LinkState::NotFound:
            return std::wstring(KindLabel(k)) + L"：未检测到接收器";
        case LinkState::NoAnswer:
            line += L"接收器无响应";
            break;
        case LinkState::Asleep:
            line += L"休眠/离线";
            break;
        case LinkState::Online:
            line += std::to_wstring(s.level) + L"%";
            if (s.millivolts > 0) {
                wchar_t mv[32];
                swprintf_s(mv, L"，%.2fV", s.millivolts / 1000.0);
                line += mv;
            }
            line += s.charging ? L"，充电中" : L"，未充电";
            break;
    }
    if (s.state != LinkState::Online && s.hasReading) {
        line += L"（上次 " + std::to_wstring(s.level) + L"%" + (s.charging ? L" 充电中" : L"") + L"，" +
                FormatTime(s.lastReading) + L"）";
    } else if (s.hasReading) {
        line += L"（" + FormatTime(s.lastReading) + L"）";
    }
    return line;
}

class BatteryItem : public IPluginItem {
public:
    explicit BatteryItem(DeviceKind kind) : kind_(kind) {}

    const wchar_t* GetItemName() const override { return kind_ == DeviceKind::Mouse ? L"鼠标电量" : L"键盘电量"; }
    const wchar_t* GetItemId() const override {
        return kind_ == DeviceKind::Mouse ? L"WirelessBatteryMouse" : L"WirelessBatteryKeyboard";
    }
    const wchar_t* GetItemLableText() const override { return KindLabel(kind_); }
    const wchar_t* GetItemValueText() const override { return value_.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return L"100%⚡"; }
    // Text style: no usage bar either when the receiver is missing.
    int IsDrawResourceUsageGraph() const override {
        return (iconStyle_ || status_.state == LinkState::NotFound) ? 0 : 1;
    }
    float GetResourceUsageGraphValue() const override { return graph_; }

    bool IsCustomDraw() const override { return iconStyle_; }
    int GetItemWidth() const override { return BatteryItemWidth(30, 16); }  // 96 DPI fallback
    int GetItemWidthEx(void* hDC) const override {
        SIZE sz{};
        if (!hDC || !GetTextExtentPoint32W(static_cast<HDC>(hDC), L"100%", 4, &sz)) return 0;  // host falls back
        return BatteryItemWidth(sz.cx, sz.cy);
    }
    // DrawItem(HDC) is the only custom-draw hook in TrafficMonitor <= V1.86
    // (DrawItemEx/IPluginDrawer arrived later, and newer hosts still call
    // DrawItem first), so drawing here works everywhere and never twice.
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override {
        HDC dc = static_cast<HDC>(hDC);
        RenderStyle style;
        style.darkMode = dark_mode;
        COLORREF dcColor = dc ? GetTextColor(dc) : CLR_INVALID;
        unsigned long fallback = dcColor != CLR_INVALID ? dcColor : (dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0));
        style.textColor = g_colors.hasValue ? g_colors.value : fallback;
        style.iconColor = (g_colors.taskbar && g_colors.hasLabel) ? g_colors.label : style.textColor;
        DrawBatteryItemToDC(dc, kind_, status_, style, x, y, w, h);
    }

    int OnMouseEvent(MouseEventType type, int, int, void*, int) override {
        if (type == MT_DBCLICKED) {
            Monitor().RequestRefresh();
            return 1;
        }
        return 0;
    }

    // Called on the UI thread from DataRequired: copy the worker's snapshot.
    void Update(const DeviceStatus& s, const MonitorConfig& c) {
        status_ = s;
        iconStyle_ = c.iconStyle;
        value_ = ValueText(s, c);
        graph_ = s.hasReading ? s.level / 100.0f : 0.0f;
    }

private:
    DeviceKind kind_;
    DeviceStatus status_;
    bool iconStyle_ = true;
    std::wstring value_ = L"--";
    float graph_ = 0.0f;
};

class WirelessBatteryPlugin : public ITMPlugin {
public:
    static WirelessBatteryPlugin& Instance() {
        static WirelessBatteryPlugin plugin;
        return plugin;
    }

    IPluginItem* GetItem(int index) override {
        if (index == 0) return &mouse_;
        if (index == 1) return &keyboard_;
        return nullptr;
    }

    void DataRequired() override {
        EnsureStarted();
        MonitorConfig c = Monitor().Config();
        DeviceStatus m = Monitor().Get(DeviceKind::Mouse);
        DeviceStatus k = Monitor().Get(DeviceKind::Keyboard);
        mouse_.Update(m, c);
        keyboard_.Update(k, c);
        tooltip_ = TooltipLine(DeviceKind::Mouse, m) + L"\n" + TooltipLine(DeviceKind::Keyboard, k);
        CheckLowBattery(DeviceKind::Mouse, m, c);
        CheckLowBattery(DeviceKind::Keyboard, k, c);
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
            case TMI_NAME: return L"无线设备电量";
            case TMI_DESCRIPTION:
                return L"显示 2.4G 无线鼠标（Compx 方案，如 VGN F1 系列）和键盘（迈从 K99 V3 等）的电量。"
                       L"直接查询接收器缓存，不唤醒设备、不增加功耗。";
            case TMI_AUTHOR: return L"Heisenberg1159";
            case TMI_COPYRIGHT: return L"Copyright (C) 2026 Heisenberg1159 and contributors";
            case TMI_VERSION: return L"1.1.0";
            case TMI_URL: return L"https://github.com/Heisenberg1159/TrafficMonitor-2.4G-Wireless-Battery";
            default: return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override { return tooltip_.c_str(); }

    // Host shows this from the item's right-click menu ("选项") and from the
    // plugin manager. The taskbar re-measures items every second, so a style
    // switch shows up right away without restarting TrafficMonitor.
    OptionReturn ShowOptionsDialog(void* hParent) override {
        EnsureStarted();
        MonitorConfig c = Monitor().Config();
        OptionsValues v{c.iconStyle, c.pollSeconds};
        if (!RunOptionsDialog(static_cast<HWND>(hParent), v)) return OR_OPTION_UNCHANGED;
        if (v.iconStyle == c.iconStyle && v.pollSeconds == c.pollSeconds) return OR_OPTION_UNCHANGED;
        Monitor().SaveOptions(v.iconStyle, v.pollSeconds);
        DataRequired();  // push the new style into the items before the host re-lays out
        return OR_OPTION_CHANGED;
    }

    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override {
        if (!data) return;
        switch (index) {
            case EI_CONFIG_DIR:
                if (configDir_.empty()) configDir_ = data;
                break;
            case EI_DRAW_TASKBAR_WND:
                g_colors.taskbar = wcscmp(data, L"1") == 0;
                break;
            case EI_VALUE_TEXT_COLOR:
                g_colors.value = wcstoul(data, nullptr, 10);
                g_colors.hasValue = true;
                break;
            case EI_LABEL_TEXT_COLOR:
                g_colors.label = wcstoul(data, nullptr, 10);
                g_colors.hasLabel = true;
                break;
            default:
                break;
        }
    }

    void OnInitialize(ITrafficMonitor* app) override {
        app_ = app;
        if (app_ && configDir_.empty()) {
            const wchar_t* dir = app_->GetPluginConfigDir();
            if (dir) configDir_ = dir;
        }
        EnsureStarted();
    }

    int GetCommandCount() override { return 2; }
    const wchar_t* GetCommandName(int i) override {
        return i == 0 ? L"立即刷新电量" : i == 1 ? L"打开电量插件配置文件" : nullptr;
    }
    void OnPluginCommand(int i, void*, void*) override {
        if (i == 0) {
            Monitor().RequestRefresh();
        } else if (i == 1) {
            EnsureStarted();
            ShellExecuteW(nullptr, L"open", L"notepad.exe", Monitor().ConfigPath().c_str(), nullptr, SW_SHOWNORMAL);
        }
    }

private:
    WirelessBatteryPlugin() : mouse_(DeviceKind::Mouse), keyboard_(DeviceKind::Keyboard) {}

    void EnsureStarted() {
        if (started_) return;
        started_ = true;
        std::wstring dir = configDir_;
        if (dir.empty()) {  // fall back to the folder of this DLL
            wchar_t path[MAX_PATH] = {};
            HMODULE self = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&Monitor), &self);
            GetModuleFileNameW(self, path, MAX_PATH);
            dir = path;
            dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
        }
        Monitor().Start(dir);
    }

    void CheckLowBattery(DeviceKind k, const DeviceStatus& s, const MonitorConfig& c) {
        bool& warned = warned_[static_cast<int>(k)];
        if (s.state != LinkState::Online || c.lowBatteryWarn <= 0) return;
        if (s.charging || s.level > c.lowBatteryWarn) {
            warned = false;
            return;
        }
        if (!warned && app_) {
            warned = true;
            std::wstring msg = std::wstring(KindLabel(k)) + L"电量低：" + std::to_wstring(s.level) + L"%，请及时充电";
            app_->ShowNotifyMessage(msg.c_str());
        }
    }

    BatteryItem mouse_;
    BatteryItem keyboard_;
    ITrafficMonitor* app_ = nullptr;
    std::wstring configDir_;
    std::wstring tooltip_;
    bool started_ = false;
    bool warned_[2] = {false, false};
};

}  // namespace

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() { return &WirelessBatteryPlugin::Instance(); }
