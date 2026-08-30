#include "usb_serial_lvgl.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

namespace {

constexpr size_t kRxBufferSize = 256;
constexpr size_t kRxQueueLength = 8;

static const char *TAG = "USB_SERIAL";
static QueueHandle_t s_rx_queue;
static usb_serial_lvgl_rx_cb_t s_rx_callback;

struct RxMessage {
    uint8_t data[kRxBufferSize];
    size_t length;
};

static uint8_t s_rx_buffer[kRxBufferSize];

static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    if (itf != TINYUSB_CDC_ACM_0 || !s_rx_queue)
        return;

    size_t rx_size = 0;
    if (tinyusb_cdcacm_read(static_cast<tinyusb_cdcacm_itf_t>(itf), s_rx_buffer,
                            sizeof(s_rx_buffer), &rx_size) != ESP_OK || rx_size == 0)
        return;

    RxMessage message = {};
    message.length = rx_size;
    memcpy(message.data, s_rx_buffer, rx_size);
    xQueueSend(s_rx_queue, &message, 0);
}

static void usb_serial_lvgl_task(void *)
{
    RxMessage message;
    while (true) {
        if (xQueueReceive(s_rx_queue, &message, portMAX_DELAY) != pdTRUE)
            continue;

        if (s_rx_callback) {
            if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
                s_rx_callback(message.data, message.length);
                lvgl_port_unlock();
            }
        } else {
            ESP_LOG_BUFFER_HEXDUMP(TAG, message.data, message.length, ESP_LOG_INFO);
        }
    }
}

} // namespace

extern "C" esp_err_t usb_serial_lvgl_init(usb_serial_lvgl_rx_cb_t callback)
{
    if (s_rx_queue)
        return ESP_ERR_INVALID_STATE;

    s_rx_callback = callback;
    s_rx_queue = xQueueCreate(kRxQueueLength, sizeof(RxMessage));
    if (!s_rx_queue)
        return ESP_ERR_NO_MEM;

    const tinyusb_config_t tinyusb_config = TINYUSB_DEFAULT_CONFIG();
    esp_err_t err = tinyusb_driver_install(&tinyusb_config);
    if (err != ESP_OK)
        return err;

    tinyusb_config_cdcacm_t cdc_config = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = tinyusb_cdc_rx_callback,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    err = tinyusb_cdcacm_init(&cdc_config);
    if (err != ESP_OK)
        return err;

    BaseType_t task_result = xTaskCreatePinnedToCore(
        usb_serial_lvgl_task, "usb_serial_lvgl", 4096, nullptr, 4, nullptr, 1);
    return task_result == pdPASS ? ESP_OK : ESP_FAIL;
}
