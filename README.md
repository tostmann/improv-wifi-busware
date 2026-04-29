# improv-wifi-busware

[Improv-Serial][improv-spec] WiFi provisioning library for ESP32-family
devices that share their serial port with regular application traffic.
Originally extracted from Busware's TUL/TUL32/CUL32/EUL32 firmware
projects, but carries no Busware-specific code — drop it into any ESP32
project that needs Improv-Serial.

[improv-spec]: https://www.improv-wifi.com/serial/

```
+----------------------+        +-----------------------+
| ESP Web Tools (web)  | <----> |  improv-wifi-busware  |
| / Improv Web Comp.   |  Improv|  (your firmware)      |
+----------------------+ Serial +-----------------------+
                                            |
                                            v
                                +-----------------------+
                                |  WiFiBackend          |
                                | (esp_wifi or Arduino) |
                                +-----------------------+
```

Key differences from the upstream [jnthas/Improv-WiFi-Library][upstream]
that motivated this fork:

| | upstream | improv-wifi-busware |
|---|---|---|
| **Transport coupling** | hard `Stream*` (Arduino) | `feedByte(uint8_t)` byte-feeder, no transport types in the public API |
| **WiFi coupling** | hard `<WiFi.h>` | pluggable `WiFiBackend`, ships `EspIdf*` and `Arduino*` |
| **ESP-IDF support** | Arduino-only | first-class IDF (`idf_component_register`, `esp_wifi_*`) |
| **Window after boot** | always-on | bounded **120 s** window, then hard silent — UART is yours again |
| **WiFi scan** | blocking 1–2 s | non-blocking, polled from `tick()` |
| **Frame robustness** | length truncation > 255 bytes, VLA, no bounds check on `WIFI_SETTINGS` parse | guarded against all of those |

[upstream]: https://github.com/jnthas/Improv-WiFi-Library

The hard 120 s post-boot shutdown is the load-bearing design choice: the
library is meant for devices whose UART continues to carry the
application's regular protocol after provisioning. Improv must not
listen forever; one Improv pass per power cycle, then out of the way.


## Try it (60 seconds)

Flash a pre-built ESP32-C6 test firmware via the browser:

```bash
git clone https://github.com/tostmann/improv-wifi-busware
cd improv-wifi-busware/html
python3 -m http.server 8000
# then open http://localhost:8000/ in Chrome / Edge / Opera
```

The landing page offers two installer buttons — one for an ESP-IDF build,
one for an Arduino-Core build of the same demo firmware. Both use the
library, both expose the Improv-Serial handshake, and both bring up a
small `hallo!` HTTP greeter on port 80 after WiFi provisioning succeeds.
Use that to verify your hardware before integrating the library into
your own firmware.

(Web Serial is locked to HTTPS or `http://localhost`. Plain HTTP from a
LAN IP will not work; the page detects this and tells the user.)


## Add it to your project

### PlatformIO (Arduino-Core)

```ini
; platformio.ini
[env:my_board]
platform  = espressif32
framework = arduino
lib_deps =
    Network
    WiFi
    https://github.com/tostmann/improv-wifi-busware.git
```

`Network` is needed because arduino-esp32 v3 split the network layer out
of `WiFi` — it must be listed explicitly, otherwise the linker fails on
`NetworkInterface::*` symbols. (The same workaround is used in
`examples/arduino-test/platformio.ini`.)

### ESP-IDF

Either add this repo as a submodule and point `EXTRA_COMPONENT_DIRS` at
the `components/` directory:

```cmake
# top-level CMakeLists.txt of your IDF project
set(EXTRA_COMPONENT_DIRS
    ${EXTRA_COMPONENT_DIRS}
    "${CMAKE_SOURCE_DIR}/path/to/improv-wifi-busware/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your_app)
```

Then in your component `REQUIRES` list (typically `main/CMakeLists.txt`):

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES improv_wifi_busware
)
```

### Arduino IDE

Copy `components/improv_wifi_busware/` into your sketchbook's
`libraries/` folder, then `#include <ImprovWiFiLibrary.h>`.


## Use it (modern API)

The transport-neutral core is what you should reach for in new code. It
never owns the serial transport; you feed bytes to it from whatever
driver you use (`Stream`, `usb_serial_jtag_*`, UART, BLE-NUS, …).

```cpp
#include <improv_wifi/improv_wifi.h>
#include <improv_wifi/idf_backend.h>   // or arduino_backend.h

namespace ipw = improv_wifi_busware;

static void writeFn(const uint8_t* d, size_t n, void*) {
    // Forward to your serial transport. For ESP-IDF USB CDC:
    usb_serial_jtag_write_bytes(d, n, pdMS_TO_TICKS(100));
}

static void onConnected(const char* ssid, const char*, void*) {
    ESP_LOGI("app", "wifi up on '%s'", ssid);
}

static ipw::EspIdfWiFiBackend backend{};

void app_main() {
    nvs_flash_init();
    usb_serial_jtag_driver_install(&jtag_cfg);

    ipw::Config cfg;
    cfg.backend                 = &backend;
    cfg.write                   = &writeFn;
    cfg.windowMs                = 120'000;     // default; override if you must
    cfg.device.chipFamily       = ipw::ChipFamily::Esp32C6;
    cfg.device.firmwareName     = "my-firmware";
    cfg.device.firmwareVersion  = "1.0.0";
    cfg.device.deviceName       = "MyDevice";
    cfg.device.deviceUrl        = nullptr;     // -> http://<ip>
    cfg.onConnected             = &onConnected;

    ipw::ImprovWiFi improv{cfg};

    while (true) {
        improv.tick(esp_timer_get_time() / 1000);

        uint8_t buf[64];
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0 && improv.isArmed()) {
            improv.feedBytes(buf, n);
        }
        // After improv.isArmed() goes false the bytes are yours again --
        // route them to your application's normal command parser.
    }
}
```

### Public API surface

```cpp
class ImprovWiFi {
    explicit ImprovWiFi(const Config& cfg);

    void feedByte(uint8_t b);                       // one byte from the wire
    void feedBytes(const uint8_t* data, size_t n);  // convenience
    void tick(uint32_t nowMs);                      // drive timer + scan
    bool isArmed() const;                           // window still open?
    bool isConnected();                             // proxy to backend
    void setDeviceInfo(const DeviceInfo& info);
};

struct Config {
    WiFiBackend* backend;        // required: see below
    WriteFn      write;          // required: void (*)(const uint8_t*, size_t, void*)
    void*        userCtx;        // forwarded to write/onError/onConnected
    uint32_t     windowMs;       // default 120'000
    DeviceInfo   device;         // chip family, firmware name/version, device name, optional URL
    OnErrorFn    onError;
    OnConnectFn  onConnected;
};

class WiFiBackend {  // implement your own or use the bundled ones
    virtual bool isConnected() = 0;
    virtual std::string currentIp() = 0;
    virtual bool tryConnect(const char* ssid, const char* password) = 0;

    static constexpr int kScanRunning = -1;
    static constexpr int kScanFailed  = -2;
    virtual void     startScan() = 0;
    virtual int      scanResult() = 0;            // -1 / -2 / count
    virtual ApRecord apRecord(int index) = 0;
    virtual void     clearScan() = 0;
};
```

The bundled backends:

- **`EspIdfWiFiBackend`** (`<improv_wifi/idf_backend.h>`): `esp_wifi_*`,
  `esp_event`, `esp_netif`. Initializes WiFi/netif/event-loop idempotently
  if the application has not done so yet.
- **`ArduinoWiFiBackend`** (`<improv_wifi/arduino_backend.h>`): wraps
  `<WiFi.h>` / `<ESP8266WiFi.h>`. Uses the non-blocking
  `WiFi.scanNetworks(true, ...)` + `WiFi.scanComplete()` flavour so a
  scan does not stall the application loop.

Roll your own backend if you manage WiFi differently (existing connection
manager, captive portal, BLE-NUS bridge, …) — implementing five virtual
methods is enough.

### Legacy API (for drop-in replacement of jnthas's lib)

If you have existing code written against
[jnthas/Improv-WiFi-Library][upstream]:

```cpp
#include "ImprovWiFiLibrary.h"

ImprovWiFi improvSerial(&Serial);

void setup() {
    Serial.begin(115200);
    improvSerial.setDeviceInfo(
        ImprovTypes::CF_ESP32_C6,
        "MyFirmware", "1.0.0", "MyDevice");
    improvSerial.onImprovConnected(onConnected);
}
void loop() { improvSerial.handleSerial(); }
```

…this still works. The `ImprovWiFi(Stream*)` class is preserved as a thin
Arduino-only facade over the new core; only your `lib_deps` line needs
to change.


## Lifecycle contract (read this before debugging)

The library opens its provisioning window **unconditionally** on every
boot, regardless of the device's WiFi state. The window:

1. Opens immediately when the application calls `tick()` for the first
   time (the lib uses *your* clock, not `millis()`, so it is hardened
   against `millis()` instability at construction time).
2. Stays open for `Config::windowMs` (default 120 s) of monotonic time.
   Successful provisioning does **not** shorten it — ESP Web Tools
   continues querying `GET_DEVICE_INFO` after the WiFi credentials are
   accepted, and we let it.
3. After expiry: the library is **hard-silent** on the wire. `feedByte`
   becomes a no-op, no `CURRENT_STATE`/`ERROR_STATE`/`RPC_RESPONSE`
   frames are written. The application owns the UART again until the
   next reboot.

The window is *unconditional* on purpose: even a device that is already
provisioned and currently connected to WiFi opens its window on every
power cycle, so a user can re-provision (move to a new SSID, rotate
PSK, …) by simply unplugging and re-plugging the device — no factory
reset, no dedicated button, no console command.


## Examples

Two minimal but complete example firmwares ship in `examples/`. Both
target ESP32-C6 and use the same `<improv_wifi/...>` source tree:

- **`examples/idf-test/`** — pure ESP-IDF, consumed via
  `EXTRA_COMPONENT_DIRS`. The path firmware-stacks like CULFW32 follow.
- **`examples/arduino-test/`** — Arduino-Core via PlatformIO, consumed
  via `lib_deps = symlink://../../components/improv_wifi_busware`.
  The path projects like ip4knx follow.

Both:
- print `hallo from improv-wifi-busware (idf|arduino)` at boot,
- accept Improv-Serial provisioning during the 120 s window,
- serve a `<h1>hallo!</h1>` page on port 80 once WiFi is up,
- and answer a single-character probe (`?`) with `STATUS armed=…` so
  scripts can verify arm state out-of-band.

Build:

```bash
# ESP-IDF
cd examples/idf-test
. $IDF_PATH/export.sh
idf.py set-target esp32c6        # one-time
idf.py -p /dev/ttyACM0 build flash

# Arduino-Core (PlatformIO)
cd examples/arduino-test
pio run -e c6_arduino -t upload --upload-port /dev/ttyACM0
```


## Testing

Two host-side scripts live under `tools/` for use against a flashed
device, no browser needed:

- **`tools/improv_client.py`** — one-shot Improv-Serial client.

  ```bash
  tools/improv_client.py --port /dev/ttyACM0 --info
  tools/improv_client.py --port /dev/ttyACM0 --scan
  tools/improv_client.py --port /dev/ttyACM0 --validate \
                         --ssid 'MyWiFi' --password 'Secret123'
  ```

- **`tools/test_lifecycle.py`** — regression test for the bounded-window
  contract. Resets the device, probes Improv at t=5 s and t=70 s
  (expects responses), then probes at t=135 s (expects silence). Total
  runtime ~150 s; run after any change to the timer or the parser.

  ```bash
  tools/test_lifecycle.py --port /dev/ttyACM0
  ```

Always pass `--port` explicitly. Auto-detect on multi-board lab hosts
will frequently pick the wrong device.


## Repository layout

```
improv-wifi-busware/
├── components/
│   └── improv_wifi_busware/         the library payload
│       ├── include/
│       │   ├── ImprovTypes.h        legacy Arduino types (jnthas-compat)
│       │   ├── ImprovWiFiLibrary.h  legacy Arduino class facade
│       │   └── improv_wifi/         modern transport-neutral API
│       │       ├── improv_wifi.h
│       │       ├── types.h
│       │       ├── wifi_backend.h
│       │       ├── arduino_backend.h
│       │       └── idf_backend.h
│       ├── src/
│       │   ├── improv_wifi.cpp      core (no platform deps)
│       │   ├── backend_idf.cpp      ESP-IDF backend
│       │   ├── backend_arduino.cpp  Arduino backend
│       │   └── arduino_facade.cpp   legacy class wrapper
│       ├── CMakeLists.txt           ESP-IDF (idf_component_register)
│       ├── library.json             PlatformIO
│       └── library.properties       Arduino IDE
├── examples/
│   ├── idf-test/                    minimal ESP-IDF demo
│   └── arduino-test/                minimal Arduino-Core demo
├── tools/
│   ├── improv_client.py             host-side Improv-Serial client
│   └── test_lifecycle.py            regression test for the 120 s contract
├── html/                            ESP Web Tools landing page
└── README.md
```

The library lives one level deep in `components/` so the repo can grow
tooling, examples and tests around it without polluting what consumers
vendor.


## Compatibility

Tested end-to-end on ESP32-C6 (ESP-IDF 5.5 + arduino-esp32 3.x).
Should compile without modification on ESP32 / ESP32-S2 / ESP32-S3 /
ESP32-C3 — the protocol is platform-agnostic, only the bundled WiFi
backends pull in target-specific WiFi headers. ESP8266 is supported
through the Arduino-Core backend (`<ESP8266WiFi.h>`) but has not been
tested in this repo.


## License

MIT. See [`LICENSE`](LICENSE). Original Improv-WiFi-Library © 2023
Jonathas Amaral Barbosa ([@jnthas][upstream-author]); refactor and
Busware-specific work © 2026 Dirk Tostmann / Busware. The Improv-Serial
protocol itself is © Open Home Foundation, see [improv-wifi.com][improv-spec].

[upstream-author]: https://github.com/jnthas
