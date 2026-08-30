#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_serial_lvgl_rx_cb_t)(const uint8_t *data, size_t length);

/**
 * @brief Start the native USB-OTG CDC reader task.
 *
 * The callback runs from the reader task while the LVGL lock is held. It must
 * not call lvgl_port_lock() itself.
 */
esp_err_t usb_serial_lvgl_init(usb_serial_lvgl_rx_cb_t callback);

#ifdef __cplusplus
}
#endif
