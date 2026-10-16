// SPDX-License-Identifier: MIT
//
// USB reset guard, see include/busware/usb_reset_guard.h.

#include "busware/usb_reset_guard.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#endif

// ESP-IDF: Kconfig BUSWARE_USB_RESET_GUARD.  Arduino: -DBUSWARE_USB_RESET_GUARD,
// default on for the ESP32-C6 only.
#if (defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)) && SOC_USB_SERIAL_JTAG_SUPPORTED
#  if defined(ARDUINO)
#    ifndef BUSWARE_USB_RESET_GUARD
#      if CONFIG_IDF_TARGET_ESP32C6
#        define BUSWARE_USB_RESET_GUARD 1
#      else
#        define BUSWARE_USB_RESET_GUARD 0
#      endif
#    endif
#  elif defined(CONFIG_BUSWARE_USB_RESET_GUARD)
#    define BUSWARE_USB_RESET_GUARD 1
#  else
#    define BUSWARE_USB_RESET_GUARD 0
#  endif
#else
#  undef BUSWARE_USB_RESET_GUARD
#  define BUSWARE_USB_RESET_GUARD 0
#endif

#if BUSWARE_USB_RESET_GUARD

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "usb_reset_guard";

// Survives the software restart (RTC memory is not initialized on boot).
#define GUARD_MAGIC 0x55534230u
static RTC_NOINIT_ATTR uint32_t s_marker;
static bool s_was_usb;

extern "C" void busware_usb_reset_guard(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    s_was_usb = reason == ESP_RST_SW && s_marker == GUARD_MAGIC;
    s_marker = 0;
    // ESP_RST_USB covers the serial and the JTAG side of the port.
    // ESP_RST_JTAG is a CPU reset from a debugger and is left alone.
    if (reason != ESP_RST_USB) {
        return;
    }
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "reset over USB, but the image is not confirmed yet: no extra restart");
        return;
    }
    ESP_LOGW(TAG, "reset over USB: restarting once more by software");
    s_marker = GUARD_MAGIC;
    esp_restart();
}

extern "C" bool busware_usb_reset_guard_was_usb(void)
{
    return s_was_usb;
}

#else

extern "C" void busware_usb_reset_guard(void)
{
}

extern "C" bool busware_usb_reset_guard_was_usb(void)
{
    return false;
}

#endif
