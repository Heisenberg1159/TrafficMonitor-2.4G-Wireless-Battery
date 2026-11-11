# Prompt: let an AI assistant add your device

Open this repository in an AI coding assistant that can run commands on your Windows PC (Claude Code, GitHub
Copilot agent mode, Cursor, Windsurf, ...). Plug in your receiver and paste everything inside the box below.
Fill in the first two lines.

The assistant will collect what it needs, ask you to do the physical steps (put the device to sleep, plug in
the charger, ...), write the provider and test it. Review the diff before opening a PR. You are responsible
for what you submit.

````text
My device: <brand + model, e.g. "Acme X1 wireless mouse">, connected through its 2.4G USB receiver.
Protocol source if I have one: <link / pasted hex dumps / "none, help me capture it">

You are working in the WirelessBattery repository, a TrafficMonitor plugin that shows the battery level of
2.4G mice and keyboards. Your task is to add support for my device by writing a new protocol provider.

Read these files first, fully, before doing anything else:
- docs/ADDING_A_DEVICE.md   (the procedure and the safety rules; follow them strictly)
- src/battery_protocols.h, src/protocols/provider_base.h, src/protocols/registry.cpp, src/hid_device.h
- src/protocols/compx_mouse.cpp and src/protocols/mchose_keyboard.cpp (reference implementations)

Then work through these steps. Stop and ask me whenever you need me to do something physical or when
you are unsure. Never guess.

1. Build with `build.bat`. Run `build\WirelessBatteryCli.exe --list` and identify my receiver's HID
   collections. If several devices match, ask me to unplug/replug the receiver and diff the lists. Report the
   VID:PID and the vendor-defined (`*`) collections with their usage page, usage and report lengths.

2. Establish the protocol, in this order:
   a. If I gave you a source, extract the exact request bytes, reply layout, checksum and report type from it.
   b. Otherwise search the projects listed in README.md (help wanted table and credits) and other public
      sources for this VID:PID or brand. Use facts only. Do not copy GPL or other MIT-incompatible code.
   c. Otherwise guide me through a USBPcap + Wireshark capture of the vendor's software (ADDING_A_DEVICE.md
      §2b). Ask me to export the relevant packets as hex, captured while the device is online, while charging,
      and while asleep. Derive the request, the reply layout and the checksum, and verify that the checksum
      holds for EVERY captured packet.
   Write down the protocol (report id, lengths, transfer type, request bytes, reply offsets, checksum) and show
   it to me before writing code.

3. Safety gate. Do not skip this:
   - Only use commands the vendor software sends to READ state. Never send write/set/save/reset commands, and
     never probe unknown command ids, not even "just to test".
   - Before any command is sent to my device, show me the exact bytes and which collection they go to, and wait
     for my OK.
   - Plan the "does polling wake the device" test from ADDING_A_DEVICE.md §3.

4. Implement:
   - new file src/protocols/<vendor>_<kind>.cpp, deriving from ProviderBase, with a header comment documenting
     the protocol bytes like the existing providers do;
   - factory declaration in src/protocols/protocols.h, a row in kProtocols in src/protocols/registry.cpp
     (stable kebab-case id), the source file in CMakeLists.txt;
   - Matches() must check usage page AND report lengths; validate report id, command echo, length, checksum and
     the 0..100 range; map states to Online / Asleep / NoAnswer exactly as the guide says; put a hex dump in
     r.detail; no retries or sleeps inside Query(); every I/O bounded by a timeout.
   - Do not modify the plugin UI, threading, the other providers or the HID layer unless strictly needed. If
     you need something new in HidConnection (e.g. another report type), explain why first.

5. Build and test with `build\WirelessBatteryCli.exe --protocols` and `build\WirelessBatteryCli.exe 10 2`. Ask me
   to put the device in each state: in use, charging, asleep/off, receiver unplugged then replugged. Confirm
   with me that the level matches the vendor app and that the asleep device did not wake up. Also confirm the
   existing devices are not matched by the new provider.

6. Update README.md: add a row to "Supported devices" (model, VID:PID, protocol id, ✅ only if tested on my
   hardware, else 🟡), and remove the device from "Help wanted" if it was listed there.

7. Finish with a PR description draft containing: what was added, protocol source, the `--list <vid>` output,
   the `10 2` test output for each state, and anything uncertain. Do not commit, push or open the PR yourself
   unless I ask.
````

## Tips

- Give the assistant the **exact** text output of commands and Wireshark hex. Do not use screenshots of
  hex dumps.
- If the assistant proposes sending a command you have never seen the vendor app send, say no.
- One device per PR keeps review easy.
