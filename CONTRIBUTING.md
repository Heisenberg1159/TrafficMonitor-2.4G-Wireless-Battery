# Contributing

Thanks for helping! The most valuable contributions are **new devices**. Every receiver family someone adds
helps everyone else who owns one.

## New device

- Follow [docs/ADDING_A_DEVICE.md](docs/ADDING_A_DEVICE.md), or hand [docs/AI_PROMPT.md](docs/AI_PROMPT.md)
  to an AI coding assistant.
- One device family per PR.
- The PR must include real `WirelessBatteryCli` output (online / asleep / charging) from your hardware.
- Respect the [safety rules](docs/ADDING_A_DEVICE.md#safety-rules): read-only commands, bounded I/O,
  validated replies.

Don't have time to write code? [Open a device request](https://github.com/Heisenberg1159/TrafficMonitor-2.4G-Wireless-Battery/issues/new?template=new_device.md) with your
`WirelessBatteryCli --list` output and, ideally, a capture. Someone with the same device may pick it up.

## Other changes

Bug fixes, translations (the plugin UI is currently Chinese), UI polish and docs are welcome too. For larger
changes (new UI, new dependencies) please open an issue first.

## Code style

- C++17, Win32 API only, no new third-party dependencies (the DLL must stay a single file).
- Keep the existing formatting (4 spaces, ~120 columns) and comment *why*, not *what*.
- `build.bat` must build without warnings (`/W4`).
- Nothing may block TrafficMonitor's UI thread. Device I/O belongs on the worker thread only.
