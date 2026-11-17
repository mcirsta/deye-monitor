# Deye Monitor — local energy dashboard

A desktop energy monitor written in **C++20 and Qt 6 Quick / QML**, with a native Wayland interface on Linux. It reads Deye inverter telemetry through a Solarman V5 logger on the local LAN. The backend, desktop UI, register profiles and command-line diagnostics are built into one executable.

Source repository: [mcirsta/deye-monitor](https://github.com/mcirsta/deye-monitor). Released under the [MIT license](LICENSE).

## Desktop build

Requires CMake 3.24+, Ninja, a C++20 compiler, and Qt 6.5+ with Core, Gui, Network, Qml, Quick, QuickControls2, Svg and Test. On Arch Linux the relevant packages are `base-devel cmake ninja qt6-base qt6-declarative qt6-wayland qt6-svg`.

```bash
make gui
./bin/deye-monitor-qt
# Explicit native Wayland launch:
make run-gui-wayland
# Register this checkout in the application menu and taskbar:
make install-desktop
```

The original sun-and-panels logo is [`assets/com.deye.monitor.svg`](assets/com.deye.monitor.svg). The same SVG is embedded for the Qt window icon and sidebar, and installed as a scalable desktop icon. `make install-desktop` installs the icon and `com.deye.monitor.desktop` under the user's XDG data directory (normally `~/.local/share`); its launcher points to this checkout's binary. The desktop entry matches [Qt's application ID](https://doc.qt.io/qt-6/qguiapplication.html#desktopFileName-prop) for taskbar association on Wayland. A regular `cmake --install build` also installs the SVG and launcher for the configured installation prefix.

Ninja uses its default parallelism. CMake builds the backend as the static `deye_core` library and links it into `bin/deye-monitor-qt`, together with the UI and embedded JSON register profiles. The executable uses the system Qt shared libraries and runs without helper processes. Qt Quick's Basic controls, a generic platform theme and Qt's bradient Wayland decorations are selected at startup. When a Wayland display is available, Linux defaults to the Wayland platform.

### Desktop interface

- Overview: solar, home, battery and grid power; battery charging/discharging is named prominently in the power card, SOC panel, with a small idle range around zero; animated dots show the direction of power flow; inverter-reported battery SOC, voltage, current and temperature.
- Telemetry: the profile's available electrical measurements, temperatures and daily / lifetime energy counters. The AM3 profile shows the read-only off-grid **Shutdown SOC** threshold in the Battery card, with an always-visible **Advanced battery settings** card beside it and an inverter-reported BMS summary. Its **AC output settings** card shows the configured output-voltage selection, measured inverter output, grid frequency, type and mode; **Grid voltage limits** shows the separate grid protection thresholds. These cards only read settings and never change them.
- The cutoff mapping and firmware evidence are recorded in [docs/battery-cutoff.md](docs/battery-cutoff.md).
- The output-voltage and related Grid Set1 register mapping is recorded in [docs/ac-output-settings.md](docs/ac-output-settings.md).
- History: solar, load, signed battery power and grid power for this session, with 5, 15 and 60 minute views. Up to 240 snapshots are retained in memory; actual duration depends on the refresh interval. Negative battery power means charging.
- Settings: **Scan network beside Logger IP address**, logger serial, TCP port, refresh interval, automatic or explicit inverter profile, display name and web credentials. Multiple scan results require a selection.
- **Save probe report…** appears only for startup support or connection/read problems, including when connecting a newly configured device. It is cleared once a complete telemetry snapshot succeeds; normal monitoring and later reconnections do not offer diagnostics. Open the dialog directly from the problem banner, enter the exact model and optional reference readings, then save the collected evidence to share manually. Device identifiers are omitted by default. There is no permanent entry in Settings. See [probe reports](docs/probe-reports.md) for optional additional reads and collection limits.
- Pause / resume and refresh controls. Offline readings retain their last values and display an offline banner with the time of the last successful reading.
- Charge ETA is shown only after a sustained, positive SOC trend while charging. SOC is reported by the inverter; it is not a verified average across battery packs.

Settings are stored by QSettings (`~/.config/DeyeMonitor/DeyeMonitor.ini` on Linux), with owner-only file permissions. Logger web credentials are stored locally in this file.

### Discovery and recovery

At startup, the monitor tries the saved logger address once. A failed connection immediately proceeds to discovery; an unanswered register request has a two-second deadline, and a saved-address HTTP lookup has a 1.5-second transfer timeout. It then sends the fixed read-only `AT+YZVER` query to UDP **6021**, once per LAN broadcast address. Each reply supplies a candidate IP, which must pass a logger `/status.html` identity check. Recovery matches the saved logger serial, MAC when available and inverter serial before resuming. A firmware-version reply alone is never accepted as device identity. With no saved address, discovery starts with the broadcast. The Settings scan uses the same broadcast-first discovery and displays each verified result immediately, allowing selection while the scan checks for other loggers.

The broadcast receive window is 1.5 seconds; outstanding status checks have a bounded extra verification window. Only if no matching logger is verified does discovery scan individual IPs. This fallback uses up to 128 concurrent TCP connection probes, followed by read-only `/status.html` requests, with a six-second deadline. For interface networks larger than /22, scanning is limited to the interface's local /24. Use the logger's web credentials in Settings. The UDP 6021 route works independently of the classic Solarman discovery service on UDP 48899, which was unavailable on the tested LSW-3 firmware.

Later failed register reads time out after two seconds and trigger the same broadcast-then-scan recovery. Failed discovery retries with a one-to-fifteen-second backoff. The new address is persisted as soon as the logger identity is verified. Unsupported or ambiguous profiles and identity changes during normal monitoring stop polling with a clear message.

Only Modbus **FC03 reads**, at most **32 registers per request**, are implemented. Responses are checked for Solarman checksum, logger serial, request sequence, RTU CRC, slave address, function and byte count. Fragmented / coalesced frames and unsolicited heartbeats are handled without acknowledgement or configuration writes. Use one local Modbus reader at a time.

### Inverter profiles

Profiles live in `profiles/` and are compiled into the executable:

| Profile | Automatic signature | Register map |
|---|---|---|
| `deye-am3` | 1 phase, 3 MPPT, valid rating in registers 16–18 | Deye SG02LP1-EU-AM3, including the SUN-10K model |
| `deye-p3` | 3 phase, 2 MPPT, valid rating in registers 20–22 | Original Deye P3 map |

Automatic selection rejects unknown and ambiguous signatures. Explicit profiles still require a valid serial, phase count and rating. A signature match identifies a register-map family; additional models need validation against their own documentation and telemetry.

A new profile defines identity / rating fields, valid phases and MPPT count, bounded read blocks, metrics, signedness, scaling and work modes. Add its JSON under `profiles/` and rebuild; the loader and Settings list discover it automatically. Include captured-frame decoding tests for each new register map. AM3 has mixed scaling: battery current 0.1 A, load / grid power 10 W, battery power 1 W.

AM3 solar energy uses its dedicated PV counters: register **108** for today's production and unsigned low/high registers **96–97** for lifetime production, each in 0.1 kWh. Registers 60 and 63–64 are general energy counters and are not used as PV production. These definitions follow the inspected [single-phase register map](https://github.com/davidrapan/ha-solarman/blob/ac1d88b83268beeb0511b8a1b7fc8e17deddc044/custom_components/solarman/inverter_definitions/deye_hybrid.yaml). Missing or invalid daily energy readings leave the overview footer blank; they are never substituted with load energy or estimated from session history.

### Diagnostics and tests

```bash
make test
./bin/deye-monitor-qt --scan --no-settings
./bin/deye-monitor-qt --snapshot --no-settings --host 192.168.1.100 --serial 1234567890
./bin/deye-monitor-qt --probe --no-settings --host 192.168.1.100 --serial 1234567890 --probe-model SUN-EXACT-MODEL
# Preview sample data without network access:
./bin/deye-monitor-qt --demo
```

`--snapshot` emits a timestamped JSON report including raw registers and wire frames. `--snapshot --demo --no-settings` produces a sample report without network access. Help, version and diagnostic modes work without a display server. Invalid command-line addresses, ports, serials and profiles are rejected before connection attempts. `--no-settings` avoids loading or saving local preferences. `--demo --capture /tmp/deye-preview.png --quit-after 2500` captures the application's own window. `--settings` opens the configuration window and directs the capture there.

Tests cover captured requests and replies, profile selection, AM3 scaling, corrupt and padded responses, fragmented streams, stale replies, heartbeats, immediate startup fallback, startup-only report offers, identity verification after resume, discovery, charge estimation, headless diagnostics and the QML navigation and settings workflow. Network tests use local simulated servers; UI tests use Qt's offscreen platform. Tests do not contact an inverter.

The application targets the desktop. Android packaging and a touch layout have not been implemented yet.
