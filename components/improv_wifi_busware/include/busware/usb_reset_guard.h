// SPDX-License-Identifier: MIT
//
// USB reset guard for ESP32 chips with a USB-Serial-JTAG port.
//
// A reset over the USB-Serial-JTAG port - a terminal or flasher toggling
// DTR/RTS, such as the hard reset after flashing - is a core reset.  Unlike
// esp_restart() it does not reset the modem subsystem first.  On the
// ESP32-C6 the Wi-Fi station afterwards found no access point in about 8 % of
// such resets and stayed that way until the next restart: the driver reports
// "Haven't to connect to a suitable AP now!" on every attempt and refuses
// scans while the station keeps connecting.  Restarting by software, or
// resetting just the modem subsystem, cured it; waiting did not, and neither
// did esp_wifi_stop()/esp_wifi_start().
//
// Call busware_usb_reset_guard() as the first statement of app_main() or
// setup(), before Wi-Fi is started.  After a USB reset it restarts the chip
// once by software (about 0.3 s), except while an OTA image still awaits its
// confirmation: the bootloader would take the next boot of such an image as a
// failure and roll it back.
//
// Enabled by default on the ESP32-C6 only, where it was measured.  Other
// targets with USB-Serial-JTAG: Kconfig BUSWARE_USB_RESET_GUARD (ESP-IDF) or
// -DBUSWARE_USB_RESET_GUARD=1 (Arduino).  On chips without USB-Serial-JTAG,
// and on the ESP8266, both functions do nothing.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Restarts the chip once by software if it was reset over the USB port.
// Returns otherwise.
void busware_usb_reset_guard(void);

// True if this boot is the guard's restart, i.e. the chip was reset over the
// USB port before (esp_reset_reason() reports a software reset then).
bool busware_usb_reset_guard_was_usb(void);

#ifdef __cplusplus
}
#endif
