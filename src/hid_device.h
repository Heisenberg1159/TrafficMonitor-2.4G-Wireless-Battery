// Minimal Win32 HID helpers: enumeration, overlapped I/O with timeouts.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct HidInterfaceInfo {
    std::wstring path;
    USHORT vendorId = 0;
    USHORT productId = 0;
    USHORT version = 0;
    USHORT usagePage = 0;
    USHORT usage = 0;
    USHORT inputLen = 0;
    USHORT outputLen = 0;
    USHORT featureLen = 0;
    std::wstring product;  // USB product string, may be empty
};

constexpr USHORT kAnyVendor = 0;

// Lists the present HID collections whose vendor id matches |vendorId|
// (kAnyVendor = all of them). Only collections that can be opened for
// attribute queries are returned; nothing is ever sent to the device here.
std::vector<HidInterfaceInfo> EnumerateHid(USHORT vendorId);

// One opened HID collection. All I/O is overlapped and bounded by a timeout,
// so a dongle that stops answering can never hang the caller.
class HidConnection {
public:
    HidConnection() = default;
    ~HidConnection();
    HidConnection(const HidConnection&) = delete;
    HidConnection& operator=(const HidConnection&) = delete;

    bool Open(const std::wstring& path);
    void Close();
    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

    // Drops input reports that were queued before our request was sent.
    void FlushInput();

    // Control transfer (SET_REPORT, output). |data[0]| is the report id.
    bool SetOutputReport(const unsigned char* data, DWORD len);

    // Feature reports (control transfers). |data[0]| is the report id; for
    // GetFeature set it before the call, the reply overwrites the buffer.
    bool SetFeature(const unsigned char* data, DWORD len);
    bool GetFeature(unsigned char* data, DWORD len);

    // Interrupt OUT transfer. |data[0]| is the report id (0 if unused).
    bool Write(const unsigned char* data, DWORD len, DWORD timeoutMs);

    // Reads one input report. Returns the number of bytes read, 0 on timeout,
    // -1 on a hard error (device removed, etc.).
    int Read(unsigned char* buf, DWORD len, DWORD timeoutMs);

    DWORD LastError() const { return lastError_; }

private:
    bool Wait(OVERLAPPED& ov, DWORD timeoutMs, DWORD& transferred);

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE readEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    DWORD lastError_ = 0;
};
