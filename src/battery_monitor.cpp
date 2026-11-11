#include "battery_monitor.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr wchar_t kSection[] = L"config";
constexpr LONGLONG kMaxLogBytes = 256 * 1024;
constexpr DWORD kFirstReadingRetryMs = 10 * 1000;

const wchar_t* KindName(int i) { return i == 0 ? L"mouse" : L"keyboard"; }

const wchar_t* StateName(LinkState s) {
    switch (s) {
        case LinkState::NotFound: return L"not-found";
        case LinkState::NoAnswer: return L"no-answer";
        case LinkState::Asleep: return L"asleep";
        case LinkState::Online: return L"online";
    }
    return L"?";
}

// INI written by WritePrivateProfileStringW stays ANSI unless the file already
// starts with a UTF-16 BOM; create it that way so the charging mark survives.
void EnsureUnicodeIni(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const wchar_t text[] =
        L"﻿; 无线设备电量插件配置。显示样式和轮询间隔可在插件的\"选项\"对话框中修改；\r\n"
        L"; 直接编辑本文件后，右键菜单选择\"立即刷新电量\"生效\r\n"
        L"[config]\r\n"
        L"; 显示样式：icon = 图标（设备图标 + 电池 + 百分比），text = 纯文字\r\n"
        L"display_style=icon\r\n"
        L"; 轮询间隔（秒，10~3600）。接收器直接返回缓存值，不会唤醒鼠标/键盘。\r\n"
        L"poll_interval_sec=60\r\n"
        L"; 低电量提醒阈值（%），0 表示不提醒\r\n"
        L"low_battery_warn=20\r\n"
        L"; 充电中时数值后追加的标记\r\n"
        L"charging_mark=⚡\r\n"
        L"; 设备休眠/离线时（显示上次读数）数值后追加的标记，默认不追加\r\n"
        L"sleep_mark=\r\n"
        L"; 1 = 把每次轮询结果写入 WirelessBattery.log，用于排查问题\r\n"
        L"debug_log=0\r\n";
    DWORD written = 0;
    WriteFile(h, text, static_cast<DWORD>((sizeof(text) - sizeof(wchar_t))), &written, nullptr);
    CloseHandle(h);
}

std::wstring ReadString(const std::wstring& path, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(kSection, key, def, buf, static_cast<DWORD>(std::size(buf)), path.c_str());
    return buf;
}

}  // namespace

BatteryMonitor::BatteryMonitor() {
    for (int i = 0; i < static_cast<int>(DeviceKind::Count); ++i) {
        providers_[i] = CreateProviderFor(static_cast<DeviceKind>(i));
    }
}

BatteryMonitor::~BatteryMonitor() {
    // Only signal. The worker owns a reference on this DLL, so the module (and
    // this static object) cannot go away while it runs; at process exit the
    // thread is already gone. Never wait here: we may hold the loader lock.
    if (stopEvent_) SetEvent(stopEvent_);
}

void BatteryMonitor::Start(const std::wstring& configDir) {
    if (started_) return;
    started_ = true;

    std::wstring dir = configDir;
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    configPath_ = dir + L"WirelessBattery.ini";
    logPath_ = dir + L"WirelessBattery.log";
    EnsureUnicodeIni(configPath_);
    LoadConfig();

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    refreshEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&ThreadMain), &self);
    HANDLE thread = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else if (self) {
        FreeLibrary(self);
    }
}

void BatteryMonitor::RequestRefresh() {
    if (refreshEvent_) SetEvent(refreshEvent_);
}

void BatteryMonitor::SaveOptions(bool iconStyle, int pollSeconds) {
    const wchar_t* p = configPath_.c_str();
    WritePrivateProfileStringW(kSection, L"display_style", iconStyle ? L"icon" : L"text", p);
    WritePrivateProfileStringW(kSection, L"poll_interval_sec", std::to_wstring(pollSeconds).c_str(), p);
    LoadConfig();
    RequestRefresh();
}

DeviceStatus BatteryMonitor::Get(DeviceKind kind) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_[static_cast<int>(kind)];
}

MonitorConfig BatteryMonitor::Config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

DWORD WINAPI BatteryMonitor::ThreadMain(LPVOID self) {
    static_cast<BatteryMonitor*>(self)->Run();
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThreadMain), &module);
    FreeLibraryAndExitThread(module, 0);
}

void BatteryMonitor::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    HANDLE events[2] = {stopEvent_, refreshEvent_};
    for (;;) {
        PollOnce();
        DWORD waitMs = static_cast<DWORD>(Config().pollSeconds) * 1000;
        {
            // Receiver present but no reading yet (e.g. mouse asleep at startup):
            // retry sooner so the value shows up quickly.
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& s : status_) {
                if (s.state != LinkState::NotFound && !s.hasReading) waitMs = std::min<DWORD>(waitMs, kFirstReadingRetryMs);
            }
        }
        DWORD w = WaitForMultipleObjects(2, events, FALSE, waitMs);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_OBJECT_0 + 1) LoadConfig();  // manual refresh re-reads the ini
    }
}

void BatteryMonitor::PollOnce() {
    for (int i = 0; i < static_cast<int>(DeviceKind::Count); ++i) {
        BatteryReading r = providers_[i]->Poll();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            DeviceStatus& s = status_[i];
            s.state = r.state;
            if (!r.deviceName.empty()) s.name = r.deviceName;
            if (r.state == LinkState::Online) {
                s.level = r.level;
                s.charging = r.charging;
                s.millivolts = r.millivolts;
                GetLocalTime(&s.lastReading);
                s.hasReading = true;
            } else if (r.state == LinkState::NotFound) {
                s = DeviceStatus{};  // receiver unplugged: forget stale values
            }
        }
        if (Config().debugLog) {
            wchar_t line[512];
            swprintf_s(line, L"%s %s [%S] level=%d charging=%d mV=%d name=\"%s\" %s", KindName(i),
                       StateName(r.state), r.protocol, r.level, r.charging ? 1 : 0, r.millivolts,
                       r.deviceName.c_str(), r.detail.c_str());
            Log(line);
        }
    }
}

void BatteryMonitor::LoadConfig() {
    MonitorConfig c;
    const wchar_t* p = configPath_.c_str();
    c.pollSeconds = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"poll_interval_sec", 60, p)), 10, 3600);
    c.lowBatteryWarn = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"low_battery_warn", 20, p)), 0, 100);
    c.debugLog = GetPrivateProfileIntW(kSection, L"debug_log", 0, p) != 0;
    c.chargingMark = ReadString(configPath_, L"charging_mark", L"⚡");
    c.sleepMark = ReadString(configPath_, L"sleep_mark", L"");
    c.iconStyle = _wcsicmp(ReadString(configPath_, L"display_style", L"icon").c_str(), L"text") != 0;
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = c;
}

void BatteryMonitor::Log(const std::wstring& line) {
    HANDLE h = CreateFileW(logPath_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > kMaxLogBytes) {  // keep the log small
        CloseHandle(h);
        h = CreateFileW(logPath_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t prefix[32];
    swprintf_s(prefix, L"%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring full = prefix + line + L"\r\n";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), static_cast<int>(full.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, full.c_str(), static_cast<int>(full.size()), utf8.data(), bytes, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
}
