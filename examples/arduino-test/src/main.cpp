// Minimal Arduino-Core test firmware for improv-wifi-busware on ESP32-C6.
//
// Uses the legacy `<ImprovWiFiLibrary.h>` facade so this sketch matches the
// shape of ip4knx's main.cpp -- the same surface that downstream Arduino
// consumers rely on. Behaviour mirrors the IDF test:
//   - boot banner: "hallo from improv-wifi-busware (arduino)\n"
//   - 120 s window: lib owns the serial port, answers Improv RPCs.
//   - after expiry: any byte arriving on serial is echoed back, except '?'
//     which prints a one-line STATUS so host tooling can probe arm state.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "ImprovWiFiLibrary.h"

static ImprovWiFi improvSerial(&Serial);
static WebServer  greeter(80);
static bool       greeterUp = false;

static uint32_t bootMs = 0;
static bool     wasArmed = true;

static const char kGreeterHtml[] =
    "<!doctype html>\n"
    "<html lang=\"de\"><head><meta charset=\"utf-8\">\n"
    "<title>improv-wifi-busware (arduino-test)</title>\n"
    "<style>\n"
    "body{font-family:-apple-system,Segoe UI,sans-serif;max-width:38em;"
    "margin:4em auto;padding:0 1em;color:#222;background:#fafafa}\n"
    "h1{color:#7a3e00}code{background:#fee;padding:0.1em 0.3em;border-radius:3px}\n"
    "footer{margin-top:3em;font-size:0.85em;color:#888}\n"
    "</style></head><body>\n"
    "<h1>hallo!</h1>\n"
    "<p>Du bist auf dem ESP32-C6, geflasht mit der "
    "<code>improv-wifi-busware</code>-Test-Firmware (Arduino-Build).</p>\n"
    "<p>Die WiFi-Provisionierung lief gerade erfolgreich. Der Greeter "
    "auf Port 80 ist Teil dieses Tests &mdash; nichts Geheimes hier, "
    "nur ein Lebenszeichen.</p>\n"
    "<footer>improv-wifi-busware v0.2.0-wip &middot; Arduino-Core</footer>\n"
    "</body></html>\n";

static void greeterRoot() {
    greeter.send(200, "text/html; charset=utf-8", kGreeterHtml);
}

static void onConnected(const char* ssid, const char* /*pw*/) {
    Serial.print("connected to ");
    Serial.println(ssid);
}

static void onError(ImprovTypes::Error err) {
    Serial.print("improv error 0x");
    Serial.println(static_cast<unsigned>(err), HEX);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("hallo from improv-wifi-busware (arduino)");

    improvSerial.setDeviceInfo(
        ImprovTypes::CF_ESP32_C6,
        "improv-wifi-busware-arduino-test",
        "0.2.0-wip",
        "ImprovBusware-Arduino-Test"
    );
    improvSerial.onImprovConnected(onConnected);
    improvSerial.onImprovError(onError);
    bootMs = millis();
}

void loop() {
    // handleSerial() drives the timer and consumes bytes from Serial while
    // the window is open; after expiry it returns immediately without
    // touching Serial.available(), so the bytes are still ours below.
    improvSerial.handleSerial();

    // Bring the greeter up as soon as WiFi is connected; keep it alive across
    // the lib's window expiry.
    if (!greeterUp && WiFi.status() == WL_CONNECTED) {
        greeter.on("/", greeterRoot);
        greeter.begin();
        greeterUp = true;
        Serial.print("greeter listening on http://");
        Serial.println(WiFi.localIP());
    }
    if (greeterUp) greeter.handleClient();

    // Detect armed-edge so we don't print the same banner twice. Useful when
    // the user opens an ESP Web Tools console after the window expired.
    bool armed = false;
    // The facade exposes the underlying core via isArmed() in the new API
    // only. Approximate it by tracking elapsed time + the 120 s default.
    // (IMPROV_RUN_FOR is the lib's compile-time default, see the legacy header.)
    if (millis() - bootMs < IMPROV_RUN_FOR) armed = true;
    if (wasArmed && !armed) {
        Serial.println("[improv window closed -- echo on]");
        wasArmed = false;
    }

    if (!armed) {
        while (Serial.available()) {
            const int b = Serial.read();
            if (b < 0) break;
            if (b == '?') {
                Serial.print("STATUS armed=0 boot_ms=");
                Serial.println(static_cast<unsigned long>(millis() - bootMs));
            } else {
                Serial.write(static_cast<uint8_t>(b));
            }
        }
    }
}
