#ifdef ARDUINO

#include "improv_wifi/arduino_backend.h"

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #ifndef WIFI_AUTH_OPEN
    #define WIFI_AUTH_OPEN ENC_TYPE_NONE
  #endif
#else
  #include <WiFi.h>
#endif

namespace improv_wifi_busware {

ArduinoWiFiBackend::ArduinoWiFiBackend() : ArduinoWiFiBackend(Options{}) {}

ArduinoWiFiBackend::ArduinoWiFiBackend(const Options& opts) : opts_(opts) {}

bool ArduinoWiFiBackend::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

std::string ArduinoWiFiBackend::currentIp() {
    if (WiFi.status() != WL_CONNECTED) return {};
    char buf[16];
    const auto a = WiFi.localIP();
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             static_cast<unsigned>(a[0]), static_cast<unsigned>(a[1]),
             static_cast<unsigned>(a[2]), static_cast<unsigned>(a[3]));
    return std::string(buf);
}

bool ArduinoWiFiBackend::tryConnect(const char* ssid, const char* password) {
    if (!ssid) return false;
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect();
        delay(100);
    }
    WiFi.begin(ssid, password ? password : "");

    const uint32_t deadline = millis() + opts_.connectTimeoutMs;
    while (millis() < deadline) {
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(opts_.connectPollMs);
    }
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        return false;
    }
    return true;
}

void ArduinoWiFiBackend::startScan() {
    // Async scan: returns immediately. Result is polled via scanResult().
    // Returns WIFI_SCAN_RUNNING (-1) until done, then number of APs (>=0)
    // or WIFI_SCAN_FAILED (-2).
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    scanInFlight_ = true;
}

int ArduinoWiFiBackend::scanResult() {
    const int rc = WiFi.scanComplete();
    if (rc == WIFI_SCAN_RUNNING) return WiFiBackend::kScanRunning;
    if (rc == WIFI_SCAN_FAILED)  return WiFiBackend::kScanFailed;
    return rc;  // >= 0 -> number of APs
}

ApRecord ArduinoWiFiBackend::apRecord(int index) {
    ApRecord out;
    if (index < 0) return out;
    out.ssid      = std::string(WiFi.SSID(index).c_str());
    out.rssi      = WiFi.RSSI(index);
    out.needsAuth = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    return out;
}

void ArduinoWiFiBackend::clearScan() {
    if (scanInFlight_) {
        WiFi.scanDelete();
        scanInFlight_ = false;
    }
}

}  // namespace improv_wifi_busware

#endif  // ARDUINO
