// Battery query protocols for 2.4G receivers - public interface.
//
// Every protocol only talks to a vendor-defined HID collection of the
// receiver and only sends *read* commands. The receivers answer from their own
// cache (round trip ~1-2 ms), so polling does not wake the mouse/keyboard
// radio and costs the peripheral no extra power.
//
// To add a device, see docs/ADDING_A_DEVICE.md. In short: write a provider in
// src/protocols/ and list it in src/protocols/registry.cpp.
#pragma once

#include <memory>
#include <string>
#include <vector>

enum class DeviceKind { Mouse = 0, Keyboard = 1, Count = 2 };

enum class LinkState {
    NotFound,  // no matching receiver plugged in
    NoAnswer,  // receiver present but did not answer / bad answer
    Asleep,    // receiver says the peripheral is offline (sleeping / off)
    Online,    // fresh reading
};

struct BatteryReading {
    LinkState state = LinkState::NotFound;
    int level = -1;         // 0..100, -1 = unknown
    bool charging = false;
    int millivolts = 0;     // 0 = not reported by the protocol
    std::wstring deviceName;
    std::wstring detail;    // diagnostic text (hex dump / error)
    const char* protocol = "";  // id of the protocol that produced it
};

class BatteryProvider {
public:
    virtual ~BatteryProvider() = default;
    // Called on a background thread, at most once per poll interval. Must
    // return within ~1 s even if the receiver stops answering.
    virtual BatteryReading Poll() = 0;
};

struct ProtocolInfo {
    const char* id;               // short stable id, e.g. "compx-mouse"
    DeviceKind kind;              // which taskbar item it feeds
    const wchar_t* description;   // shown by WirelessBatteryCli --protocols
    std::unique_ptr<BatteryProvider> (*create)();
};

// Every registered protocol, in priority order.
const std::vector<ProtocolInfo>& Protocols();

// One provider per device kind that tries all protocols of that kind and
// sticks to the first one whose receiver is plugged in.
std::unique_ptr<BatteryProvider> CreateProviderFor(DeviceKind kind);
