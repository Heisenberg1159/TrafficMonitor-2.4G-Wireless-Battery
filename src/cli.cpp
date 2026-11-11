// Console tool for debugging and for adding new devices.
//   WirelessBatteryCli.exe               one reading per device kind
//   WirelessBatteryCli.exe 10 2          10 rounds, 2 s apart
//   WirelessBatteryCli.exe --list        every HID collection of every device
//   WirelessBatteryCli.exe --list 3554   only vendor id 0x3554
//   WirelessBatteryCli.exe --protocols   registered protocols
// --list only reads descriptors; it never sends anything to a device.
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fcntl.h>
#include <io.h>

#include "battery_protocols.h"
#include "hid_device.h"

static const wchar_t* StateName(LinkState s) {
    switch (s) {
        case LinkState::NotFound: return L"not found";
        case LinkState::NoAnswer: return L"no answer";
        case LinkState::Asleep: return L"asleep";
        case LinkState::Online: return L"online";
    }
    return L"?";
}

static const wchar_t* KindName(DeviceKind k) { return k == DeviceKind::Mouse ? L"mouse" : L"keyboard"; }

static void List(USHORT vid) {
    auto all = EnumerateHid(vid);
    std::sort(all.begin(), all.end(), [](const HidInterfaceInfo& a, const HidInterfaceInfo& b) {
        if (a.vendorId != b.vendorId) return a.vendorId < b.vendorId;
        if (a.productId != b.productId) return a.productId < b.productId;
        return a.path < b.path;
    });
    wprintf(L"VID:PID   UsagePage:Usage  in  out feat  product\n");
    for (const auto& i : all) {
        // Vendor-defined pages (0xFF00+) are where battery protocols live.
        wprintf(L"%04X:%04X %c %04X:%04X   %3u %3u %4u  \"%s\"\n      %s\n", i.vendorId, i.productId,
                i.usagePage >= 0xFF00 ? L'*' : L' ', i.usagePage, i.usage, i.inputLen, i.outputLen, i.featureLen,
                i.product.c_str(), i.path.c_str());
    }
    wprintf(L"\n%zu collections. '*' = vendor-defined usage page (likely battery/config channel).\n", all.size());
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc > 1 && wcscmp(argv[1], L"--list") == 0) {
        List(argc > 2 ? static_cast<USHORT>(wcstoul(argv[2], nullptr, 16)) : kAnyVendor);
        return 0;
    }
    if (argc > 1 && wcscmp(argv[1], L"--protocols") == 0) {
        for (const auto& p : Protocols()) wprintf(L"%-22S %-9s %s\n", p.id, KindName(p.kind), p.description);
        return 0;
    }
    int rounds = argc > 1 ? _wtoi(argv[1]) : 1;
    int interval = argc > 2 ? _wtoi(argv[2]) : 2;
    if (rounds < 1) rounds = 1;

    std::unique_ptr<BatteryProvider> providers[] = {CreateProviderFor(DeviceKind::Mouse),
                                                    CreateProviderFor(DeviceKind::Keyboard)};
    for (int round = 0; round < rounds; ++round) {
        if (round) Sleep(interval * 1000);
        for (int k = 0; k < 2; ++k) {
            ULONGLONG t0 = GetTickCount64();
            BatteryReading r = providers[k]->Poll();
            ULONGLONG dt = GetTickCount64() - t0;
            wprintf(L"[%-8s] %-9s", KindName(static_cast<DeviceKind>(k)), StateName(r.state));
            if (r.state == LinkState::Online) {
                wprintf(L" %3d%% %s", r.level, r.charging ? L"charging" : L"discharging");
                if (r.millivolts) wprintf(L" %d mV", r.millivolts);
            }
            wprintf(L"  (%llu ms) %S \"%s\" | %s\n", dt, r.protocol, r.deviceName.c_str(), r.detail.c_str());
        }
    }
    return 0;
}
