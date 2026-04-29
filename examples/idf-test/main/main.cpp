// Minimal ESP-IDF test firmware for improv-wifi-busware.
//
// What it proves on the ESP32-C6 test target:
//   1. The library compiles + links under pure ESP-IDF (no Arduino).
//   2. Improv-Serial talks to ESP Web Tools and to tools/improv_client.py.
//   3. The 120 s window opens unconditionally on every boot, even if WiFi
//      credentials are already stored. After expiry the library is fully
//      silent on the UART (no parser response, no late state frames).
//
// Outside the Improv window, this app implements a tiny "STATUS" line
// protocol so the host-side test script can poll arm state without going
// through Improv:
//     "?\n"   -> "STATUS armed=<0|1> ms_left=<N>\n"
// where ms_left is sourced from improv.windowMsRemaining(now_ms) and is
// therefore the live countdown the library would also use to expire the
// window. After expiry the value is 0.

#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "driver/usb_serial_jtag.h"

#include "improv_wifi/improv_wifi.h"
#include "improv_wifi/idf_backend.h"

static const char* kGreeterHtml =
    "<!doctype html>\n"
    "<html lang=\"de\"><head><meta charset=\"utf-8\">\n"
    "<title>improv-wifi-busware (idf-test)</title>\n"
    "<style>\n"
    "body{font-family:-apple-system,Segoe UI,sans-serif;max-width:38em;"
    "margin:4em auto;padding:0 1em;color:#222;background:#fafafa}\n"
    "h1{color:#005f87}code{background:#eef;padding:0.1em 0.3em;border-radius:3px}\n"
    "footer{margin-top:3em;font-size:0.85em;color:#888}\n"
    "</style></head><body>\n"
    "<h1>hallo!</h1>\n"
    "<p>Du bist auf dem ESP32-C6, geflasht mit der "
    "<code>improv-wifi-busware</code>-Test-Firmware (ESP-IDF-Build).</p>\n"
    "<p>Die WiFi-Provisionierung lief gerade erfolgreich. Der Greeter "
    "auf Port 80 ist Teil dieses Tests &mdash; nichts Geheimes hier, "
    "nur ein Lebenszeichen.</p>\n"
    "<footer>improv-wifi-busware v0.2.0-wip &middot; ESP-IDF</footer>\n"
    "</body></html>\n";

static esp_err_t greeterRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kGreeterHtml, HTTPD_RESP_USE_STRLEN);
}

static httpd_handle_t startGreeter() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port    = 80;
    cfg.lru_purge_enable = true;
    httpd_handle_t srv = nullptr;
    if (httpd_start(&srv, &cfg) != ESP_OK) return nullptr;
    httpd_uri_t u = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = greeterRoot,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(srv, &u);
    return srv;
}

static const char* TAG = "improv_test";

namespace ipw = improv_wifi_busware;

static void writeAll(const uint8_t* d, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        const int rc = usb_serial_jtag_write_bytes(d + sent, n - sent, pdMS_TO_TICKS(100));
        if (rc <= 0) break;
        sent += static_cast<size_t>(rc);
    }
}

static void writeFn(const uint8_t* d, size_t n, void* /*user*/) { writeAll(d, n); }

static void onError(ipw::Error e, void* /*user*/) {
    ESP_LOGI(TAG, "improv error: 0x%02x", static_cast<unsigned>(e));
}

static void onConnected(const char* ssid, const char* /*pw*/, void* /*user*/) {
    ESP_LOGI(TAG, "improv connected to '%s'", ssid ? ssid : "?");
}

extern "C" void app_main() {
    // NVS is required by esp_wifi_init().
    esp_err_t er = nvs_flash_init();
    if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // USB Serial/JTAG console.
    usb_serial_jtag_driver_config_t jtag_cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    usb_serial_jtag_driver_install(&jtag_cfg);

    // Boot banner (one line, then quiet). ESP Web Tools' Improv parser
    // discards non-IMPROV bytes between '\n' boundaries, so this is safe
    // even though the bounded provisioning window is open right now.
    static const char kBanner[] = "hallo from improv-wifi-busware (idf)\n";
    writeAll(reinterpret_cast<const uint8_t*>(kBanner), sizeof(kBanner) - 1);

    static ipw::EspIdfWiFiBackend backend{};
    ipw::Config cfg;
    cfg.backend       = &backend;
    cfg.write         = &writeFn;
    cfg.userCtx       = nullptr;
    cfg.windowMs      = 120000;  // spec
    cfg.device.chipFamily      = ipw::ChipFamily::Esp32C6;
    cfg.device.firmwareName    = "improv-wifi-busware-test";
    cfg.device.firmwareVersion = "0.2.0-wip";
    cfg.device.deviceName      = "ImprovBusware-Test";
    cfg.device.deviceUrl       = nullptr;
    cfg.onError       = &onError;
    cfg.onConnected   = &onConnected;

    ipw::ImprovWiFi improv{cfg};

    // Status line buffer for the out-of-band "?" probe.
    char statusLine[128];
    bool statusPending = false;

    httpd_handle_t greeter = nullptr;

    while (true) {
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        improv.tick(now_ms);

        // Bring up the greeter as soon as we have an IP. Survives later
        // disconnects -- esp_http_server keeps the listening socket bound to
        // the netif via lwIP, so we do not stop+start on link flaps.
        if (!greeter && backend.isConnected()) {
            greeter = startGreeter();
            if (greeter) ESP_LOGW(TAG, "greeter listening on http://<ip>/");
        }

        uint8_t rx[64];
        const int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), pdMS_TO_TICKS(50));

        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                const uint8_t b = rx[i];
                if (improv.isArmed()) {
                    improv.feedByte(b);
                    continue;  // Improv lib owns the byte during the window.
                }
                // After the window: "?" → STATUS probe, everything else echoes
                // back so a user with the ESP Web Tools console can see the
                // device is alive.
                if (b == '?') {
                    statusPending = true;
                } else {
                    writeAll(&b, 1);
                }
            }
        }

        if (statusPending) {
            // "armed=1 ms_left=N" while window open; "armed=0 ms_left=0" after.
            // ms_left is the live countdown reported by the lib itself, so the
            // host-side test can cross-check it against its own wall-clock.
            const int len = std::snprintf(statusLine, sizeof(statusLine),
                "STATUS armed=%d ms_left=%u\n",
                static_cast<int>(improv.isArmed()),
                static_cast<unsigned>(improv.windowMsRemaining(now_ms)));
            if (len > 0) writeAll(reinterpret_cast<const uint8_t*>(statusLine),
                                  static_cast<size_t>(len));
            statusPending = false;
        }
    }
}
