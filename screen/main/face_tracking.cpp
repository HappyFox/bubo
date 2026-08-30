#include "face_tracking.h"

#include <algorithm>
#include <cmath>
#include <list>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "human_face_detect.hpp"
#include "dl_image_define.hpp"
#include "lvgl.h"
#include "esp_lvgl_port.h"

namespace {

constexpr uint32_t kAnalysisWidth = 224;
constexpr uint32_t kAnalysisHeight = 126;
constexpr uint32_t kAnalysisBytesPerPixel = 2;
constexpr uint32_t kAnalysisBufferCount = 1;
constexpr uint32_t kAnalysisSubmitInterval = 5;
constexpr uint32_t kDisplayWidth = 800;
constexpr uint32_t kDisplayHeight = 480;
constexpr float kSourceWidth = 1280.0f;
constexpr float kSourceHeight = 720.0f;
constexpr float kSourceCropX = (kSourceWidth - kDisplayWidth) / 2.0f;
constexpr float kSourceCropY = (kSourceHeight - kDisplayHeight) / 2.0f;
constexpr float kDetectorScoreThreshold = 0.4f;
constexpr float kAnalysisToSourceX = kSourceWidth / kAnalysisWidth;
constexpr float kAnalysisToSourceY = kSourceHeight / kAnalysisHeight;

static const char *TAG = "FACE_TRACK";
static QueueHandle_t s_free_slots;
static QueueHandle_t s_ready_slots;
static uint8_t *s_analysis_buffers[kAnalysisBufferCount];
static uint8_t *s_canvas_buffer;
static lv_obj_t *s_face_canvas;
static uint32_t s_analysis_submit_counter;

struct Track {
    bool active = false;
    float x1 = 0;
    float y1 = 0;
    float x2 = 0;
    float y2 = 0;
    uint32_t missed = 0;
};

static Track s_track;

static bool select_largest_face(const std::list<dl::detect::result_t> &results, dl::detect::result_t &selected)
{
    bool found = false;
    int largest_area = 0;
    for (const auto &result : results) {
        if (result.box.size() < 4 || result.score < kDetectorScoreThreshold)
            continue;
        int area = std::max(0, result.box[2] - result.box[0]) * std::max(0, result.box[3] - result.box[1]);
        if (!found || area > largest_area) {
            selected = result;
            largest_area = area;
            found = true;
        }
    }
    return found;
}

static void process_result(const std::list<dl::detect::result_t> &results)
{
    dl::detect::result_t selected;
    if (select_largest_face(results, selected)) {
        // Map detector coordinates back through the analysis scale and
        // centered camera crop used by the display.
        float x1 = selected.box[0] * kAnalysisToSourceX - kSourceCropX;
        float y1 = selected.box[1] * kAnalysisToSourceY - kSourceCropY;
        float x2 = selected.box[2] * kAnalysisToSourceX - kSourceCropX;
        float y2 = selected.box[3] * kAnalysisToSourceY - kSourceCropY;

        constexpr float alpha = 0.55f;
        if (!s_track.active) {
            s_track.x1 = x1;
            s_track.y1 = y1;
            s_track.x2 = x2;
            s_track.y2 = y2;
        } else {
            s_track.x1 += alpha * (x1 - s_track.x1);
            s_track.y1 += alpha * (y1 - s_track.y1);
            s_track.x2 += alpha * (x2 - s_track.x2);
            s_track.y2 += alpha * (y2 - s_track.y2);
        }
        s_track.active = true;
        s_track.missed = 0;
    } else if (s_track.active) {
        s_track.missed++;
        if (s_track.missed > 5)
            s_track.active = false;
    }

}

static void face_tracking_task(void *)
{
    HumanFaceDetect detector(HumanFaceDetect::ESPDET_PICO_224_224_FACE, false);
    detector.set_score_thr(kDetectorScoreThreshold, 0);
    ESP_LOGI(TAG, "face detector initialized");

    while (true) {
        uint8_t slot;
        if (xQueueReceive(s_ready_slots, &slot, portMAX_DELAY) != pdTRUE)
            continue;

        dl::image::img_t image = {
            .data = s_analysis_buffers[slot],
            .width = kAnalysisWidth,
            .height = kAnalysisHeight,
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
        };
        auto &results = detector.run(image);
        process_result(results);
        xQueueSend(s_free_slots, &slot, portMAX_DELAY);
        vTaskDelay(1);
    }
}

} // namespace

extern "C" esp_err_t face_tracking_init(void)
{
    s_free_slots = xQueueCreate(kAnalysisBufferCount, sizeof(uint8_t));
    s_ready_slots = xQueueCreate(kAnalysisBufferCount, sizeof(uint8_t));
    if (!s_free_slots || !s_ready_slots)
        return ESP_ERR_NO_MEM;

    for (uint8_t i = 0; i < kAnalysisBufferCount; ++i) {
        s_analysis_buffers[i] = static_cast<uint8_t *>(heap_caps_malloc(
            kAnalysisWidth * kAnalysisHeight * kAnalysisBytesPerPixel,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!s_analysis_buffers[i])
            return ESP_ERR_NO_MEM;
        xQueueSend(s_free_slots, &i, 0);
    }

    s_canvas_buffer = static_cast<uint8_t *>(heap_caps_malloc(
        kDisplayWidth * kDisplayHeight * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_canvas_buffer)
        return ESP_ERR_NO_MEM;

    if (!lvgl_port_lock(0))
        return ESP_ERR_TIMEOUT;
    s_face_canvas = lv_canvas_create(lv_scr_act());
    lv_obj_set_size(s_face_canvas, kDisplayWidth, kDisplayHeight);
    lv_canvas_set_buffer(s_face_canvas, s_canvas_buffer, kDisplayWidth, kDisplayHeight, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_face_canvas);
    lv_obj_clear_flag(s_face_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                      LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_canvas_fill_bg(s_face_canvas, lv_color_black(), LV_OPA_COVER);
    lvgl_port_unlock();

    BaseType_t task_result = xTaskCreatePinnedToCore(
        face_tracking_task, "face_tracking", 8192, nullptr, 4, nullptr, 1);
    return task_result == pdPASS ? ESP_OK : ESP_FAIL;
}

extern "C" lv_obj_t *face_tracking_get_canvas(void)
{
    return s_face_canvas;
}

extern "C" void face_tracking_submit_frame(const uint8_t *camera_buf,
                                             uint32_t camera_buf_hes,
                                             uint32_t camera_buf_ves,
                                             uint32_t camera_buf_stride)
{
    if (!s_free_slots || !s_ready_slots || !camera_buf || camera_buf_stride == 0)
        return;

    if (++s_analysis_submit_counter < kAnalysisSubmitInterval)
        return;
    s_analysis_submit_counter = 0;

    uint8_t slot;
    if (xQueueReceive(s_free_slots, &slot, 0) != pdTRUE)
        return;

    uint8_t *destination = s_analysis_buffers[slot];
    for (uint32_t y = 0; y < kAnalysisHeight; ++y) {
        uint32_t source_y = (y * camera_buf_ves) / kAnalysisHeight;
        const uint8_t *source_row = camera_buf + source_y * camera_buf_stride;
        uint8_t *destination_row = destination + y * kAnalysisWidth * kAnalysisBytesPerPixel;
        for (uint32_t x = 0; x < kAnalysisWidth; ++x) {
            uint32_t source_x = (x * camera_buf_hes) / kAnalysisWidth;
            destination_row[x * 2] = source_row[source_x * 2];
            destination_row[x * 2 + 1] = source_row[source_x * 2 + 1];
        }
    }

    if (xQueueSend(s_ready_slots, &slot, 0) != pdTRUE) {
        xQueueSend(s_free_slots, &slot, 0);
        return;
    }

}
