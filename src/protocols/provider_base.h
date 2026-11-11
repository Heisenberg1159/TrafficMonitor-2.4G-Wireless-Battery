// Shared plumbing for receiver protocols.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "battery_protocols.h"
#include "hid_device.h"

constexpr DWORD kWriteTimeoutMs = 500;
constexpr DWORD kReplyTimeoutMs = 300;

// "01 AB FF" style dump for BatteryReading::detail.
std::wstring Hex(const unsigned char* p, size_t n);

// Finds the receiver's vendor collection, remembers its path, re-enumerates
// when the receiver is unplugged / re-plugged, and opens/closes the handle
// around every query. A protocol only implements the three hooks below.
class ProviderBase : public BatteryProvider {
public:
    BatteryReading Poll() final;

protected:
    // USB vendor id of the receiver.
    virtual USHORT VendorId() const = 0;
    // True for the one HID collection that speaks the battery protocol
    // (usually a vendor usage page 0xFFxx with fixed report lengths).
    virtual bool Matches(const HidInterfaceInfo& info) const = 0;
    // Sends read-only request(s) and parses the reply. Set r.state to
    // Online / Asleep / NoAnswer; NotFound is handled by the base class.
    virtual BatteryReading Query(HidConnection& conn) = 0;

    // Waits up to kReplyTimeoutMs for the input report accepted by |accept|
    // and skips unrelated reports the receiver sends on the same collection.
    // Returns the report length, 0 on timeout, -1 on a hard error.
    static int ReadMatching(HidConnection& conn, unsigned char* buf, DWORD len,
                            const std::function<bool(const unsigned char*, int)>& accept);

private:
    bool Locate();

    std::wstring path_;
    std::wstring name_;
};
