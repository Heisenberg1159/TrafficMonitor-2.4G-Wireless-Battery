#include "provider_base.h"

namespace {
constexpr int kMaxForeignReports = 16;  // unrelated reports tolerated per request
}

std::wstring Hex(const unsigned char* p, size_t n) {
    static const wchar_t* digits = L"0123456789ABCDEF";
    std::wstring s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += L' ';
        s += digits[p[i] >> 4];
        s += digits[p[i] & 15];
    }
    return s;
}

BatteryReading ProviderBase::Poll() {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (path_.empty() && !Locate()) {
            BatteryReading r;
            r.state = LinkState::NotFound;
            return r;
        }
        HidConnection conn;
        if (conn.Open(path_)) {
            BatteryReading r = Query(conn);
            r.deviceName = name_;
            if (r.state == LinkState::NoAnswer && conn.LastError() != WAIT_TIMEOUT) {
                path_.clear();  // hard I/O error: device probably re-enumerated
            }
            return r;
        }
        path_.clear();  // stale path, enumerate again
    }
    BatteryReading r;
    r.state = LinkState::NotFound;
    return r;
}

int ProviderBase::ReadMatching(HidConnection& conn, unsigned char* buf, DWORD len,
                               const std::function<bool(const unsigned char*, int)>& accept) {
    ULONGLONG deadline = GetTickCount64() + kReplyTimeoutMs;
    for (int i = 0; i < kMaxForeignReports; ++i) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return 0;
        int n = conn.Read(buf, len, static_cast<DWORD>(deadline - now));
        if (n <= 0) return n;
        if (accept(buf, n)) return n;
    }
    return 0;
}

bool ProviderBase::Locate() {
    for (const auto& info : EnumerateHid(VendorId())) {
        if (Matches(info)) {
            path_ = info.path;
            name_ = info.product;
            return true;
        }
    }
    return false;
}
