## What

<!-- e.g. "Add Acme X1 mouse (receiver 1234:5678, protocol acme-mouse)" -->

## New device checklist

- [ ] Provider in `src/protocols/`, row in `registry.cpp`, line in `CMakeLists.txt`
- [ ] README "Supported devices" row added
- [ ] Only read commands are sent; replies are validated (id, length, checksum, 0–100)
- [ ] Tested on real hardware: online / charging / asleep / unplug-replug
- [ ] Polling does not wake the device (or explained below why it can't be avoided)
- [ ] `build.bat` builds without warnings

**Protocol source:** <!-- link, or "captured from <app> <version>" -->

**`WirelessBatteryCli --list <vid>`**

```
```

**`WirelessBatteryCli 10 2`** (covering the states above)

```
```
