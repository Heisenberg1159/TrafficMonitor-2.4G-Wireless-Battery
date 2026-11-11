# Adding a device

This guide takes you from "my mouse/keyboard isn't supported" to a working pull request. You need the device,
its USB receiver, a Windows PC and, if the protocol is not documented anywhere, the vendor's configuration
software.

Rather let an AI assistant drive? Use [AI_PROMPT.md](AI_PROMPT.md). It follows the same steps.

- [1. Look at the receiver](#1-look-at-the-receiver)
- [2. Find the protocol](#2-find-the-protocol)
- [3. Make sure polling is free](#3-make-sure-polling-is-free)
- [4. Write the provider](#4-write-the-provider)
- [5. Test](#5-test)
- [6. Open the pull request](#6-open-the-pull-request)
- [Safety rules](#safety-rules)

---

## 1. Look at the receiver

Build the project (`build.bat`), plug in the receiver, then run:

```bat
build\WirelessBatteryCli.exe --list          :: every HID collection on the PC
build\WirelessBatteryCli.exe --list 3554     :: only one vendor id (hex)
```

`--list` only reads USB descriptors and never sends anything to a device. Example output for a VGN F1 SE
receiver:

```
VID:PID   UsagePage:Usage  in  out feat  product
3554:FB50   0001:0002       8   0    0  "VGN DragonFly F1 SE"      <- mouse input (owned by Windows)
3554:FB50 * FF02:0002      17  17    0  "VGN DragonFly F1 SE"      <- vendor channel, 17-byte in/out
3554:FB50 * FF03:0000       8   0    0  "VGN DragonFly F1 SE"
3554:FB50 * FF04:0002       0   0    8  "VGN DragonFly F1 SE"
```

- Find your receiver by its product string, or unplug/replug it and compare the lists.
- Collections marked `*` use a vendor-defined usage page (`0xFF00`–`0xFFFF`). The battery protocol
  almost always lives on one of them. Note the **usage page, usage and report lengths**. Together they
  identify the right collection.
- Keyboard/mouse input collections (`0001:0006`, `0001:0002`) are owned by Windows and cannot be opened.
  That is fine, we never need them.
- Some vendors (MCHOSE glw) put the channel on usage page `0001` usage `0000` instead. Then the report lengths
  (e.g. 65/65) are the giveaway.

## 2. Find the protocol

### 2a. Search first

Check the projects in the README's [help wanted](../README.md#help-wanted-protocols-that-are-documented-but-not-implemented-yet)
table, plus [libratbag](https://github.com/libratbag/libratbag), [Solaar](https://github.com/pwr-Solaar/Solaar)
(Logitech) and [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB). Search GitHub for your receiver's
`VID:PID`, e.g. `"3554" "FB50"`. Many cheap 2.4G peripherals share a handful of chip vendors (Compx, Nordic,
Telink, Beken...), so one protocol often covers several brands.

You may reuse *facts* about a protocol (bytes, offsets, checksums). Do not copy code from projects whose license
is incompatible with MIT (e.g. GPL).

### 2b. Capture it yourself

1. Install [Wireshark](https://www.wireshark.org/) with **USBPcap** (tick it in the installer) and reboot.
2. Start capturing on the USBPcap root hub your receiver is attached to. If unsure, try each one and move the
   mouse until packets appear.
3. Open the vendor software and go to the page that shows battery. Let it refresh a few times, then stop.
4. Find the device address of your receiver: the middle number of `usb.src`/`usb.dst`, e.g. `7` in `1.7.2`.
   Filter on it with `usb.device_address == 7`. Keep your hands off the mouse/keyboard while capturing, so
   normal input reports don't flood the list.
5. Look for a **request** followed by a **reply**:
   - `URB_CONTROL out` + `SET_REPORT` (bmRequestType `0x21`, bRequest `0x09`): output or feature report via
     control transfer → `conn.SetOutputReport()` / `conn.SetFeature()`.
     `wValue` high byte = report type (`02` output, `03` feature), low byte = report id.
   - `URB_INTERRUPT out` on the vendor interface → `conn.Write()`.
   - Reply on `URB_INTERRUPT in` → `conn.Read()` / `ReadMatching()`, or a `GET_REPORT` feature reply →
     `conn.GetFeature()`.
   - Some receivers send status **unsolicited**, e.g. every few seconds. Then you only need to read, no
     request at all.
6. **Locate the battery byte.** Note the level the vendor app shows, then change something and capture again:
   - plug in the charging cable → a charging flag flips (often bit 7, or a separate byte),
   - use the device for a while → the level byte goes down by a few %,
   - turn the device off → a "connected/online" byte flips.
   Bytes that track the app's number are the level. A byte pair around `0x0E00`–`0x1100` is usually
   millivolts (3.6–4.3 V).
7. **Work out integrity checks.** Most protocols have a checksum. Try sum of bytes, `0x55 - sum`, XOR, and
   one's complement over different ranges until every captured packet verifies. Check fixed headers too
   (e.g. request `55`, reply `AA`).

Save a few hex dumps (request + reply, online/asleep/charging) for the PR.

## 3. Make sure polling is free

This plugin polls every 10–3600 s for as long as the PC runs, so the query must be answered by the
**receiver** and must not wake the device. Test it:

- Leave the device untouched until it sleeps (usually 1–10 min; the LED goes off).
- Send the request (see step 5) and time it. `WirelessBatteryCli` prints the duration.
  - **A few ms with a valid reply** (stale value or an "offline" flag): answered from the receiver cache. 👍
  - **Tens of ms, or the device's LED lights up**: the request went over the air and woke it. Prefer a
    different command or an unsolicited status report. If there is no other way, say so in the PR. Such
    protocols must default to a long interval.
- Logitech HID++ battery features are answered by the device itself, not the receiver. They are still fine,
  because the receiver rejects requests while the device sleeps, but never retry in a loop.

## 4. Write the provider

Create `src/protocols/<vendor>_<kind>.cpp`. Start from this template, or copy
[`compx_mouse.cpp`](../src/protocols/compx_mouse.cpp) (output report via control transfer, two requests) or
[`mchose_keyboard.cpp`](../src/protocols/mchose_keyboard.cpp) (interrupt write, one request).

```cpp
// Acme 2.4G mice (receiver VID 0x1234): Acme X1, Acme X2 ...
//
// Collection: usage page 0xFF01, 32-byte input/output reports, report id 0x05.
// Request : 05 10 00 .. 00                  read battery
// Reply   : 05 10 <online> <level> <charging> ..
//   (document every byte you rely on, and how the checksum works)
#include "protocols.h"
#include "provider_base.h"

namespace {

class AcmeMouseProvider : public ProviderBase {
    static constexpr USHORT kVid = 0x1234;
    static constexpr DWORD kLen = 32;            // report length incl. report id
    static constexpr unsigned char kReportId = 0x05;
    static constexpr unsigned char kCmdBattery = 0x10;

    USHORT VendorId() const override { return kVid; }

    // Match on usage page AND report lengths. The PID usually varies between models.
    bool Matches(const HidInterfaceInfo& i) const override {
        return i.usagePage == 0xFF01 && i.inputLen == kLen && i.outputLen == kLen;
    }

    BatteryReading Query(HidConnection& conn) override {
        BatteryReading r;
        unsigned char req[kLen] = {kReportId, kCmdBattery};
        conn.FlushInput();                              // drop stale reports
        if (!conn.SetOutputReport(req, kLen)) {         // or conn.Write(req, kLen, kWriteTimeoutMs)
            r.state = LinkState::NoAnswer;
            r.detail = L"write failed";
            return r;
        }
        unsigned char reply[kLen] = {};
        int n = ReadMatching(conn, reply, kLen, [](const unsigned char* p, int len) {
            return len == static_cast<int>(kLen) && p[0] == kReportId && p[1] == kCmdBattery;  // + checksum!
        });
        if (n != static_cast<int>(kLen)) {
            r.state = LinkState::NoAnswer;
            r.detail = L"no reply";
            return r;
        }
        r.detail = Hex(reply, kLen);                    // shows up in CLI output and debug log
        if (reply[2] == 0) {                            // receiver says: device not linked
            r.state = LinkState::Asleep;
            return r;
        }
        if (reply[3] > 100) {                           // never trust a value you did not validate
            r.state = LinkState::NoAnswer;
            return r;
        }
        r.state = LinkState::Online;
        r.level = reply[3];
        r.charging = reply[4] != 0;
        return r;
    }
};

}  // namespace

std::unique_ptr<BatteryProvider> CreateAcmeMouseProvider() { return std::make_unique<AcmeMouseProvider>(); }
```

Then register it in three places:

```cpp
// src/protocols/protocols.h
std::unique_ptr<BatteryProvider> CreateAcmeMouseProvider();

// src/protocols/registry.cpp  (inside kProtocols)
{"acme-mouse", DeviceKind::Mouse, L"Acme 2.4G mouse receiver (VID 1234, usage page FF01)",
 &CreateAcmeMouseProvider},
```

```cmake
# CMakeLists.txt, battery_core sources
    src/protocols/acme_mouse.cpp
```

What the base class already does for you:

- finds the collection via `VendorId()` + `Matches()`, caches its path and re-enumerates after unplug/replug,
- opens the handle for each poll and closes it afterwards (no handle is kept open between polls),
- reports `NotFound` when the receiver is absent, so the taskbar hides the battery icon,
- `ReadMatching()` waits at most 300 ms and skips up to 16 unrelated reports.

States to return from `Query()`:

| State | When |
|---|---|
| `Online` | Valid fresh reading: set `level` (0–100), `charging`, optionally `millivolts` |
| `Asleep` | Receiver answered, but says the device is off/asleep/not linked. The UI keeps the last value, dimmed |
| `NoAnswer` | Timeout, bad checksum, out-of-range value. Anything you cannot trust |

## 5. Test

```bat
build.bat
build\WirelessBatteryCli.exe --protocols     :: your protocol is listed
build\WirelessBatteryCli.exe 10 2            :: 10 polls, 2 s apart
```

Check each of these and keep the output for the PR:

- [ ] device in use → `online`, level matches the vendor app (±1–2 %)
- [ ] charging cable plugged in → `charging`
- [ ] device asleep / switched off → `asleep` (or `no answer`), and the device does **not** wake up
- [ ] unplug the receiver → `not found`; plug it back → readings return without restarting
- [ ] each poll takes a few ms (≤ 300 ms worst case)
- [ ] with your receiver unplugged, the other protocols still work (no false match)

Then run it as a real plugin: `build.bat install "<TrafficMonitor folder>"` (exit TrafficMonitor first), set
`debug_log=1` in `WirelessBattery.ini`, and watch `WirelessBattery.log`.

## 6. Open the pull request

Include:

- the provider file, the registry row, the CMake line,
- a new row in the README **Supported devices** table (model, `VID:PID`, protocol id, ✅),
- `WirelessBatteryCli --list <vid>` output for your receiver,
- `WirelessBatteryCli 10 2` output covering online / asleep / charging,
- where the protocol came from (link, or "captured from <vendor app> <version>").

## Safety rules

These are non-negotiable, because the plugin runs unattended on many PCs:

1. **Read-only.** Only send commands you have seen the vendor app send *to read* state. Never send set/write/
   save/reset commands, and never "probe" unknown command ids. Some firmware treats unknown commands as writes.
2. **Bounded.** Every read and write must have a timeout. Use `ReadMatching()` and `Write(…, kWriteTimeoutMs)`.
   No infinite loops, no retries inside `Query()`; the next poll is the retry.
3. **Validated.** Check report id, command echo, length and checksum before using a reply. Reject levels
   outside 0–100.
4. **Quiet.** Do not open the device outside `Query()`. Do not keep handles open. Do not enumerate more often
   than the poll interval.
5. **Specific.** `Matches()` must not accept collections of other vendors' devices. Match the usage page *and*
   report lengths, not just the VID.
