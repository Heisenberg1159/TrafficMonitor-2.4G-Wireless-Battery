// Compx-based 2.4G mice (receiver VID 0x3554): VGN F1 series, Pulsar, ...
//
// Vendor collection: usage page 0xFF02, 17-byte input/output reports, id 0x08.
// Request : 08 <cmd> 00 .. 00 <chk>        chk = 0x55 - sum(bytes 0..15)
// Reply   : 08 <cmd> ..                    same checksum rule
//   cmd 0x03 link status : byte 6 = 1 when the mouse is linked
//   cmd 0x04 power       : byte 6 = level %, byte 7 = charging, bytes 8-9 = mV (BE)
#include "protocols.h"
#include "provider_base.h"

namespace {

class CompxMouseProvider : public ProviderBase {
    static constexpr USHORT kVid = 0x3554;
    static constexpr USHORT kUsagePage = 0xFF02;
    static constexpr DWORD kLen = 17;
    static constexpr unsigned char kReportId = 0x08;
    static constexpr unsigned char kCmdStatus = 0x03;
    static constexpr unsigned char kCmdPower = 0x04;

    USHORT VendorId() const override { return kVid; }

    bool Matches(const HidInterfaceInfo& i) const override {
        return i.usagePage == kUsagePage && i.inputLen == kLen && i.outputLen == kLen;
    }

    static unsigned char Checksum(const unsigned char* p) {
        unsigned sum = 0;
        for (DWORD i = 0; i < kLen - 1; ++i) sum += p[i];
        return static_cast<unsigned char>(0x55 - sum);
    }

    // Sends |cmd| and returns the 17-byte reply in |reply|, false on timeout.
    bool Transact(HidConnection& conn, unsigned char cmd, unsigned char (&reply)[kLen]) {
        unsigned char req[kLen] = {kReportId, cmd};
        req[kLen - 1] = Checksum(req);
        conn.FlushInput();
        if (!conn.SetOutputReport(req, kLen)) return false;
        int n = ReadMatching(conn, reply, kLen, [cmd](const unsigned char* p, int len) {
            return len == static_cast<int>(kLen) && p[0] == kReportId && p[1] == cmd && p[kLen - 1] == Checksum(p);
        });
        return n == static_cast<int>(kLen);
    }

    BatteryReading Query(HidConnection& conn) override {
        BatteryReading r;
        unsigned char reply[kLen] = {};
        if (!Transact(conn, kCmdStatus, reply)) {
            r.state = LinkState::NoAnswer;
            r.detail = L"status: no reply";
            return r;
        }
        if (reply[6] != 0x01) {  // 0 = mouse not linked (sleeping / switched off)
            r.state = LinkState::Asleep;
            r.detail = L"status: " + Hex(reply, kLen);
            return r;
        }
        if (!Transact(conn, kCmdPower, reply)) {
            // The receiver does not answer the power query while the link drops;
            // this happens when the mouse falls asleep between both requests.
            r.state = LinkState::Asleep;
            r.detail = L"power: no reply";
            return r;
        }
        r.detail = L"power: " + Hex(reply, kLen);
        if (reply[6] > 100 || reply[7] > 1) {
            r.state = LinkState::NoAnswer;
            return r;
        }
        r.state = LinkState::Online;
        r.level = reply[6];
        r.charging = reply[7] == 1;
        r.millivolts = (reply[8] << 8) | reply[9];
        return r;
    }
};

}  // namespace

std::unique_ptr<BatteryProvider> CreateCompxMouseProvider() { return std::make_unique<CompxMouseProvider>(); }
