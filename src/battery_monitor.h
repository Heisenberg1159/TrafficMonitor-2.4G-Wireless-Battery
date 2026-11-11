// Background poller. All HID I/O runs on one low-priority worker thread so
// TrafficMonitor's UI thread never blocks on a device.
#pragma once

#include <windows.h>

#include <memory>
#include <mutex>
#include <string>

#include "battery_protocols.h"

struct DeviceStatus {
    LinkState state = LinkState::NotFound;
    int level = -1;          // last known level, survives sleep
    bool charging = false;   // last known charging flag
    int millivolts = 0;
    std::wstring name;
    SYSTEMTIME lastReading{};  // local time of the last Online reading
    bool hasReading = false;
};

struct MonitorConfig {
    int pollSeconds = 60;
    int lowBatteryWarn = 20;   // 0 disables the notification
    bool debugLog = false;
    bool iconStyle = true;     // display_style=icon (custom drawn) | text
    std::wstring chargingMark = L"⚡";
    std::wstring sleepMark;
};

class BatteryMonitor {
public:
    BatteryMonitor();
    ~BatteryMonitor();

    // |configDir| holds WirelessBattery.ini / .log. Safe to call once.
    void Start(const std::wstring& configDir);
    void RequestRefresh();
    // Writes the options to the ini, applies them at once and wakes the worker
    // so a new poll interval takes effect without waiting out the old one.
    void SaveOptions(bool iconStyle, int pollSeconds);

    DeviceStatus Get(DeviceKind kind) const;
    MonitorConfig Config() const;
    std::wstring ConfigPath() const { return configPath_; }

private:
    static DWORD WINAPI ThreadMain(LPVOID self);
    void Run();
    void PollOnce();
    void LoadConfig();
    void Log(const std::wstring& line);

    mutable std::mutex mutex_;
    DeviceStatus status_[static_cast<int>(DeviceKind::Count)];
    std::unique_ptr<BatteryProvider> providers_[static_cast<int>(DeviceKind::Count)];
    MonitorConfig config_;
    std::wstring configPath_;
    std::wstring logPath_;
    HANDLE stopEvent_ = nullptr;
    HANDLE refreshEvent_ = nullptr;
    bool started_ = false;
};
