# `html/` — ESP Web Tools landing page

Self-contained ESP Web Tools install page for the two test firmwares that
ship with this repo. Drop the entire `html/` directory behind any web server
that speaks HTTPS (or use `http://localhost` for local testing).

```
html/
├── index.html              landing page with two install buttons
├── manifest-idf.json       ESP Web Tools manifest, ESP-IDF flavour
├── manifest-arduino.json   ESP Web Tools manifest, Arduino-Core flavour
└── firmware/
    ├── idf/factory.bin     merged bootloader + partitions + app, ESP-IDF
    └── arduino/factory.bin merged bootloader + partitions + app, Arduino
```

Both factory images target **ESP32-C6** and were produced with
`esptool merge-bin`. The Arduino flavour uses `--flash-size 4MB`, the IDF
flavour `--flash-size 2MB` (matches what each toolchain configured).

## Local serve

```bash
cd html
python3 -m http.server 8000
# then open http://localhost:8000/ in Chrome / Edge / Opera
```

ESP Web Tools needs the Web Serial API, which browsers gate behind a
*secure context* — i.e. HTTPS or `http://localhost`. Plain HTTP from a
LAN IP will not work; the landing page detects this and tells the user.

## Rebuilding the binaries

After changing source code, rebuild + re-merge.

**ESP-IDF flavour:**

```bash
cd examples/idf-test
. $IDF_PATH/export.sh           # bring `idf.py` into PATH
idf.py set-target esp32c6        # one-time
idf.py build
```

**Arduino-Core flavour (PlatformIO):**

```bash
cd examples/arduino-test
pio run -e c6_arduino
```

**Re-merge into `html/`:**

```bash
# from the repo root
esptool --chip esp32c6 merge-bin -o html/firmware/idf/factory.bin \
    --flash-mode dio --flash-size 2MB --flash-freq 80m \
    0x0     examples/idf-test/build/bootloader/bootloader.bin \
    0x8000  examples/idf-test/build/partition_table/partition-table.bin \
    0x10000 examples/idf-test/build/improv_wifi_busware_idf_test.bin

esptool --chip esp32c6 merge-bin -o html/firmware/arduino/factory.bin \
    --flash-mode dio --flash-size 4MB --flash-freq 80m \
    0x0     examples/arduino-test/.pio/build/c6_arduino/bootloader.bin \
    0x8000  examples/arduino-test/.pio/build/c6_arduino/partitions.bin \
    0x10000 examples/arduino-test/.pio/build/c6_arduino/firmware.bin
```
