// Logitech Lightspeed 2.4G mice (receiver VID 0x046D): G304 (receiver C53F), ...
//
// Vendor collection: usage page 0xFF00, 20-byte input/output reports.
// HID++ 2.0 long report 0x11 (Lightspeed receivers reject the 7-byte short 0x10):
//   request : 11 <dev> <featureIndex> <function|swId> <params..>  (20 bytes)
//   reply   : 11 <dev> <featureIndex> <function|swId> <payload..> (16-byte payload)
//   error   : 11 <dev> 8F|FF <reqIndex> <reqFunc> <code> ..
//   swId 8 in the low nibble of byte 3 tells replies from notifications; the
//   reply may carry dev ^ 0xFF instead of dev.
// The receiver rejects requests while the mouse sleeps (error frame, a few ms);
// the next poll is the retry, never a loop inside Query().
// Root feature 0x0000 function 0 maps a feature id to its per-device index
// (0 = unsupported). Read-only features used:
//   0x1000 Battery Status  fn 0: payload[0] = level %, payload[2] = status.
//          The G304 runs on AA cells: the reported level is used as-is, no
//          voltage curve. Charging when status is 1/2/4, not 0/3/5/6/7.
//   0x1001 Battery Voltage fn 0: payload[0..1] = millivolts BE (tooltip only).
//   0x0005 Device Name     fn 1: payload = UTF-8 name (first 16-byte chunk).
// Device indexes are probed in the order 1, 2, 0xFF.
// Protocol facts: HHHHHH233333/2.4G-battery-viewer (battery_viewer.py /
// hid_win.py), whose author tested the G304 Lightspeed route on real hardware.
#include "protocols.h"
#include "provider_base.h"

#include <cstring>
#include <cwctype>

namespace {

constexpr USHORT kVid = 0x046D;
constexpr DWORD kLen = 20;
constexpr unsigned char kReportId = 0x11;
constexpr unsigned char kSwId = 0x08;
constexpr int kDevNums[] = {1, 2, 0xFF};

class LogitechMouseProvider : public ProviderBase {
    USHORT VendorId() const override { return kVid; }

    bool Matches(const HidInterfaceInfo& i) const override {
        return i.usagePage == 0xFF00 && i.inputLen == kLen && i.outputLen >= kLen;
    }

    // One HID++ request/reply. Returns the 16-byte payload length on success,
    // -1 on timeout / write failure, -2 on a HID++ error frame.
    int Transact(HidConnection& conn, int dev, int featureIndex, int function,
                 const unsigned char* params, int paramsLen, unsigned char* payload) {
        unsigned char req[kLen] = {kReportId, static_cast<unsigned char>(dev),
                                   static_cast<unsigned char>(featureIndex),
                                   static_cast<unsigned char>((function & 0xF0) | kSwId)};
        if (paramsLen > 0) memcpy(req + 4, params, static_cast<size_t>(paramsLen));
        conn.FlushInput();
        if (!conn.SetOutputReport(req, kLen) && !conn.Write(req, kLen, kWriteTimeoutMs)) return -1;
        unsigned char reply[kLen] = {};
        int n = ReadMatching(conn, reply, kLen, [dev, featureIndex](const unsigned char* p, int len) {
            if (len != static_cast<int>(kLen) || p[0] != kReportId) return false;
            if (p[1] != dev && p[1] != (dev ^ 0xFF)) return false;
            if (p[2] == 0x8F || p[2] == 0xFF) return true;  // error frame, caller inspects
            return p[2] == featureIndex && (p[3] & 0x0F) == kSwId;
        });
        if (n != static_cast<int>(kLen)) return -1;
        if (reply[2] == 0x8F || reply[2] == 0xFF) return -2;
        memcpy(payload, reply + 4, kLen - 4);
        return static_cast<int>(kLen - 4);
    }

    // Root feature 0x0000: feature id -> per-device index (0 = unsupported).
    int FeatureIndex(HidConnection& conn, int dev, int feature) {
        unsigned char params[3] = {static_cast<unsigned char>(feature >> 8),
                                   static_cast<unsigned char>(feature), 0};
        unsigned char payload[kLen - 4] = {};
        int n = Transact(conn, dev, 0, 0, params, 3, payload);
        return n < 0 ? n : payload[0];
    }

    bool ReadBattery(HidConnection& conn, int dev, BatteryReading& r) {
        unsigned char payload[kLen - 4] = {};
        int n = Transact(conn, dev, batteryIdx_, 0, nullptr, 0, payload);
        if (n < 0) {
            // Receiver rejected the request or went silent: the mouse is
            // offline / fell asleep between the two requests.
            r.state = LinkState::Asleep;
            r.detail = n == -2 ? L"battery: rejected (mouse offline?)" : L"battery: no reply";
            return false;
        }
        r.detail = L"battery: " + Hex(payload, kLen - 4);
        if (payload[0] < 1 || payload[0] > 100) {
            r.state = LinkState::NoAnswer;
            return false;
        }
        r.state = LinkState::Online;
        r.level = payload[0];
        const int status = payload[2];
        r.charging = status != 0 && status != 3 && status != 5 && status != 6 && status != 7;
        return true;
    }

    void ReadVoltage(HidConnection& conn, int dev, BatteryReading& r) {
        if (voltageIdx_ < 0) voltageIdx_ = FeatureIndex(conn, dev, 0x1001);
        if (voltageIdx_ <= 0) return;
        unsigned char payload[kLen - 4] = {};
        if (Transact(conn, dev, voltageIdx_, 0, nullptr, 0, payload) < 0) return;
        const int mv = (payload[0] << 8) | payload[1];
        if (mv > 0) r.millivolts = mv;
    }

    void ReadName(HidConnection& conn, int dev, BatteryReading& r) {
        if (deviceName_.empty()) {
            if (nameIdx_ < 0) nameIdx_ = FeatureIndex(conn, dev, 0x0005);
            if (nameIdx_ > 0) {
                unsigned char payload[kLen - 4] = {};
                if (Transact(conn, dev, nameIdx_, 0x10, nullptr, 0, payload) > 0) {
                    int len = 0;
                    while (len < static_cast<int>(kLen - 4) && payload[len] != 0) ++len;
                    wchar_t wide[32] = {};
                    int n = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(payload), len,
                                                wide, 31);
                    if (n > 0) deviceName_.assign(wide, static_cast<size_t>(n));
                    while (!deviceName_.empty() && iswspace(deviceName_.back())) deviceName_.pop_back();
                }
            }
        }
        if (!deviceName_.empty()) r.deviceName = deviceName_;
    }

    BatteryReading Query(HidConnection& conn) override {
        BatteryReading r;
        const ULONGLONG start = GetTickCount64();
        int dev = cachedDev_;
        if (dev < 0) {
            bool sawError = false;
            for (int d : kDevNums) {
                int idx = FeatureIndex(conn, d, 0x1000);
                if (idx == -2) {
                    sawError = true;
                    continue;
                }
                if (idx <= 0) continue;  // no reply / feature unsupported: next index
                batteryIdx_ = idx;
                dev = d;
                break;
            }
            if (dev < 0) {
                // A Lightspeed receiver is a bridge: with the mouse offline it
                // rejects or stays silent instead of reporting a link status,
                // so both outcomes mean "device offline", not a broken receiver.
                r.state = LinkState::Asleep;
                r.detail = sawError ? L"receiver rejected every device index (mouse offline?)"
                                    : L"no reply (mouse offline?)";
                return r;
            }
        }
        if (!ReadBattery(conn, dev, r)) {
            cachedDev_ = -1;  // stale route: re-probe on the next poll
            return r;
        }
        cachedDev_ = dev;
        // Optional extras; keep the whole poll inside its ~1 s budget.
        if (GetTickCount64() - start < 500) ReadVoltage(conn, dev, r);
        if (GetTickCount64() - start < 500) ReadName(conn, dev, r);
        return r;
    }

    int cachedDev_ = -1;
    int batteryIdx_ = 0;
    int voltageIdx_ = -1;  // -1 unknown, 0 unsupported, >0 feature index
    int nameIdx_ = -1;
    std::wstring deviceName_;
};

}  // namespace

std::unique_ptr<BatteryProvider> CreateLogitechMouseProvider() {
    return std::make_unique<LogitechMouseProvider>();
}