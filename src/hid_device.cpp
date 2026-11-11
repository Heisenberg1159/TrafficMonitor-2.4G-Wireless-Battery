#include "hid_device.h"

#include <hidsdi.h>
#include <setupapi.h>

#include <cwctype>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace {

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool QueryInterface(const std::wstring& path, HidInterfaceInfo& info) {
    // Access 0: attribute/caps queries work even on collections that Windows
    // holds exclusively (keyboard, mouse), and we never disturb them.
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = false;
    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof(attr);
    PHIDP_PREPARSED_DATA pp = nullptr;
    if (HidD_GetAttributes(h, &attr) && HidD_GetPreparsedData(h, &pp)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
            info.path = path;
            info.vendorId = attr.VendorID;
            info.productId = attr.ProductID;
            info.version = attr.VersionNumber;
            info.usagePage = caps.UsagePage;
            info.usage = caps.Usage;
            info.inputLen = caps.InputReportByteLength;
            info.outputLen = caps.OutputReportByteLength;
            info.featureLen = caps.FeatureReportByteLength;
            wchar_t name[128] = {};
            if (HidD_GetProductString(h, name, sizeof(name))) info.product = name;
            ok = true;
        }
        HidD_FreePreparsedData(pp);
    }
    CloseHandle(h);
    return ok;
}

}  // namespace

std::vector<HidInterfaceInfo> EnumerateHid(USHORT vendorId) {
    std::vector<HidInterfaceInfo> result;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return result;

    wchar_t vidTag[16];
    swprintf_s(vidTag, L"vid_%04x", vendorId);

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifData); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;
        std::vector<BYTE> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifData, detail, needed, nullptr, nullptr)) continue;

        std::wstring path = detail->DevicePath;
        // Cheap filter on the path before opening anything.
        if (vendorId != kAnyVendor && ToLower(path).find(vidTag) == std::wstring::npos) continue;

        HidInterfaceInfo info;
        if (QueryInterface(path, info) && (vendorId == kAnyVendor || info.vendorId == vendorId))
            result.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

HidConnection::~HidConnection() { Close(); }

bool HidConnection::Open(const std::wstring& path) {
    Close();
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        lastError_ = GetLastError();
        return false;
    }
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readEvent_ || !writeEvent_) {
        lastError_ = GetLastError();
        Close();
        return false;
    }
    return true;
}

void HidConnection::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    if (readEvent_) CloseHandle(readEvent_), readEvent_ = nullptr;
    if (writeEvent_) CloseHandle(writeEvent_), writeEvent_ = nullptr;
}

void HidConnection::FlushInput() {
    if (IsOpen()) HidD_FlushQueue(handle_);
}

bool HidConnection::SetOutputReport(const unsigned char* data, DWORD len) {
    if (!IsOpen()) return false;
    if (!HidD_SetOutputReport(handle_, const_cast<unsigned char*>(data), len)) {
        lastError_ = GetLastError();
        return false;
    }
    return true;
}

bool HidConnection::SetFeature(const unsigned char* data, DWORD len) {
    if (!IsOpen()) return false;
    if (!HidD_SetFeature(handle_, const_cast<unsigned char*>(data), len)) {
        lastError_ = GetLastError();
        return false;
    }
    return true;
}

bool HidConnection::GetFeature(unsigned char* data, DWORD len) {
    if (!IsOpen()) return false;
    if (!HidD_GetFeature(handle_, data, len)) {
        lastError_ = GetLastError();
        return false;
    }
    return true;
}

bool HidConnection::Wait(OVERLAPPED& ov, DWORD timeoutMs, DWORD& transferred) {
    transferred = 0;
    DWORD w = WaitForSingleObject(ov.hEvent, timeoutMs);
    if (w == WAIT_OBJECT_0) {
        if (GetOverlappedResult(handle_, &ov, &transferred, FALSE)) return true;
        lastError_ = GetLastError();
        return false;
    }
    // Timed out: cancel and wait for the cancellation to finish so the kernel
    // never writes into a buffer the caller has already released.
    CancelIoEx(handle_, &ov);
    GetOverlappedResult(handle_, &ov, &transferred, TRUE);
    transferred = 0;
    lastError_ = WAIT_TIMEOUT;
    return false;
}

bool HidConnection::Write(const unsigned char* data, DWORD len, DWORD timeoutMs) {
    if (!IsOpen()) return false;
    OVERLAPPED ov{};
    ov.hEvent = writeEvent_;
    ResetEvent(writeEvent_);
    if (!WriteFile(handle_, data, len, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
        lastError_ = GetLastError();
        return false;
    }
    DWORD n = 0;
    return Wait(ov, timeoutMs, n) && n == len;
}

int HidConnection::Read(unsigned char* buf, DWORD len, DWORD timeoutMs) {
    if (!IsOpen()) return -1;
    OVERLAPPED ov{};
    ov.hEvent = readEvent_;
    ResetEvent(readEvent_);
    if (!ReadFile(handle_, buf, len, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
        lastError_ = GetLastError();
        return -1;
    }
    DWORD n = 0;
    if (Wait(ov, timeoutMs, n)) return static_cast<int>(n);
    return lastError_ == WAIT_TIMEOUT ? 0 : -1;
}
