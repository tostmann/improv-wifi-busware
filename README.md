# improv-wifi-busware

Improv-Serial WiFi provisioning library for Busware ESP32-based devices
(TUL, TUL32, CUL32, EUL32 and successors). Fork of
[jnthas/Improv-WiFi-Library](https://github.com/jnthas/Improv-WiFi-Library)
with hardware-aware fixes for ESP Web Tools compatibility and a
provisioning-window model designed for devices whose serial port carries
regular application traffic outside the provisioning phase.

## Status

Work in progress — extracted from `tostmann/ip4knx` on 2026-04-29 to become
the shared provisioning library across Busware ESP32 firmware projects.
The current sources are an unmodified copy of the version that ships in
ip4knx; refactoring towards the goals below is the next step in this repo.

## Goals

- **Bounded provisioning window after boot** (default 120 s). Device may
  be re-provisioned by rebooting; the serial port is otherwise free for
  normal application traffic.
- **Build for both Arduino/PlatformIO and ESP-IDF** out of the same
  sources. CULFW32 uses `idf_component_register`, ip4knx uses Arduino
  Core for ESP32 — both must consume this library without forks.
- **Non-blocking WiFi scan** so re-provisioning during normal operation
  does not stall application loops.
- **Single source of truth for `handleSerial` / `handleBuffer`** — today
  these two paths drift; the time-bounded check is only present in one
  of them.

## Layout

```
improv-wifi-busware/
├── components/
│   └── improv_wifi_busware/      ESP-IDF-style component, also picked up
│       ├── include/              by Arduino IDE / PlatformIO via the
│       ├── src/                  forwarders in the repo root.
│       ├── CMakeLists.txt        ESP-IDF (idf_component_register)
│       ├── library.properties    Arduino IDE
│       └── library.json          PlatformIO
└── README.md
```

The library lives in a subdirectory so the repo can grow tooling, tests
and examples around it without polluting what consumers vendor.

## Consuming

### ESP-IDF (CULFW32-style)

```cmake
set(EXTRA_COMPONENT_DIRS
    ${EXTRA_COMPONENT_DIRS}
    "${CMAKE_SOURCE_DIR}/../improv-wifi-busware/components")
```

### PlatformIO

```ini
lib_deps =
    https://github.com/tostmann/improv-wifi-busware.git
```

PlatformIO auto-discovers `library.json` inside the component directory.

### Arduino IDE

Drop `components/improv_wifi_busware/` into your `libraries/` folder.

## Credits

Built on top of [jnthas/Improv-WiFi-Library](https://github.com/jnthas/Improv-WiFi-Library)
(MIT). Improv-Serial protocol © Open Home Foundation
([improv-wifi.com/serial](https://www.improv-wifi.com/serial/)).
