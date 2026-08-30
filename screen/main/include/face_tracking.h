#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t face_tracking_init(void);
lv_obj_t *face_tracking_get_canvas(void);
void face_tracking_submit_frame(const uint8_t *camera_buf,
                                uint32_t camera_buf_hes,
                                uint32_t camera_buf_ves,
                                uint32_t camera_buf_stride);

#ifdef __cplusplus
}
#endif
