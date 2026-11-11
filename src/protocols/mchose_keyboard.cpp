// MCHOSE keyboards on the "glw"/CZ channel (receiver VID 0x3837): K99 V3 ...
//
// Collection: usage page 0x0001 / usage 0x0000, 64-byte reports without id.
// Request : 55 05 00 <chk> <len> <off_lo> <off_hi> <profile> 00 ..
//           chk = sum(bytes 4..7)                 (read function config)
// Reply   : AA 05 00 <chk> <len> 00 00 <profile> <data...>
//           chk = sum(bytes 4..63)
// Function config byte 32 = charging << 7 | level %.
#include "protocols.h"
#include "provider_base.h"

namespace {

class MchoseKeyboardProvider : public ProviderBase {
    static constexpr USHORT kVid = 0x3837;
    static constexpr DWORD kLen = 65;  // report id 0 + 64 bytes
    static constexpr unsigned char kFlagRequest = 0x55;
    static constexpr unsigned char kFlagReply = 0xAA;
    static constexpr unsigned char kCmdGetFuncConfig = 0x05;
    static constexpr unsigned char kChunk = 56;          // bytes per read
    static constexpr int kBatteryOffset = 32;            // inside func config
    static constexpr int kHeader = 8;                    // reply header length

    USHORT VendorId() const override { return kVid; }

    bool Matches(const HidInterfaceInfo& i) const override {
        return i.usagePage == 0x0001 && i.usage == 0x0000 && i.inputLen == kLen && i.outputLen == kLen;
    }

    BatteryReading Query(HidConnection& conn) override {
        BatteryReading r;
        // Read profile 0 function config, offset 0, 56 bytes. The battery
        // byte is mirrored in every profile block, so profile 0 is enough.
        unsigned char req[kLen] = {0x00, kFlagRequest, kCmdGetFuncConfig, 0x00, 0x00, kChunk, 0x00, 0x00, 0x00};
        req[4] = static_cast<unsigned char>(req[5] + req[6] + req[7] + req[8]);

        conn.FlushInput();
        if (!conn.Write(req, kLen, kWriteTimeoutMs)) {
            r.state = LinkState::NoAnswer;
            r.detail = L"write failed";
            return r;
        }
        unsigned char reply[kLen] = {};
        int n = ReadMatching(conn, reply, kLen, [](const unsigned char* p, int len) {
            if (len != static_cast<int>(kLen)) return false;
            const unsigned char* d = p + 1;  // skip report id
            if (d[0] != kFlagReply || d[1] != kCmdGetFuncConfig || d[4] != kChunk || d[5] != 0 || d[6] != 0) return false;
            unsigned sum = 0;
            for (int i = 4; i < 64; ++i) sum += d[i];
            return static_cast<unsigned char>(sum) == d[3];
        });
        if (n != static_cast<int>(kLen)) {
            r.state = LinkState::NoAnswer;
            r.detail = L"func config: no reply";
            return r;
        }
        const unsigned char* data = reply + 1 + kHeader;
        unsigned char b = data[kBatteryOffset];
        r.detail = L"func config[32]: " + Hex(&b, 1);
        int level = b & 0x7F;
        if (level > 100) {
            r.state = LinkState::NoAnswer;
            return r;
        }
        r.state = LinkState::Online;
        r.level = level;
        r.charging = (b & 0x80) != 0;
        return r;
    }
};

}  // namespace

std::unique_ptr<BatteryProvider> CreateMchoseKeyboardProvider() {
    return std::make_unique<MchoseKeyboardProvider>();
}
