#include "face_anim.h"
#include "esp_log.h"

namespace {

static lv_obj_t *s_canvas = nullptr;
static const char *TAG = "FACE_ANI"; 

static struct Point left_eye_pos = {200, 140};  // Initial position for left eye
static struct Point right_eye_pos = {500, 140}; // Initial position for right eye

static struct Point GlanceOffsets[] = {
    {0, -20},  // UP
    {0, 20},   // DOWN
    {0, 0},    // NEUTRAL
    {-20, 0},  // LEFT
    {20, 0},   // RIGHT
    {-20, 20}, // DOWN_LEFT
    {20, 20},  // DOWN_RIGHT
    {-20, -20},// UP_LEFT
    {20, -20}  // UP_RIGHT
};



// Only visible inside this source file
static struct Expression face_expression = {
    .blinking = false,
    .mouth_open = false,
    .mouth_height = 20,
    .glance_direction = NEUTRAL
};

#define CANVAS_WIDTH 800
#define CANVAS_HEIGHT 480

// Global canvas objects
//lv_obj_t * face_canvas;
//lv_color_t * canvas_buffer = NULL;

//lv_timer_t * blink_close_timer;
//lv_timer_t * blink_open_timer;
//lv_timer_t * speaking_timer;

//void update_face_expression(void * arg);

static void draw_eyes()
{
    lv_draw_rect_dsc_t eye_dsc;
    lv_draw_rect_dsc_init(&eye_dsc);
    eye_dsc.bg_color = lv_color_hex(0x00FFFF); 
    eye_dsc.radius = 15; // Rounded corners
    if (face_expression.blinking) {
        // Draw cl//osed eyes as thin slits
        printf("blinking\n");
        lv_canvas_draw_rect(s_canvas, 200, 180, 100, 10, &eye_dsc); // Left Eye
        lv_canvas_draw_rect(s_canvas, 500, 180, 100, 10, &eye_dsc); // Right Eye
    } else {
        // Draw open eyes
        lv_canvas_draw_rect(s_canvas, 200, 140, 100, 90, &eye_dsc); // Left Eye
        lv_canvas_draw_rect(s_canvas, 500, 140, 100, 90, &eye_dsc); // Right Eye
    }
}

static void draw_face()
{

    ESP_LOGI(TAG,"start drawing");
    if (!s_canvas)
        return;

    ESP_LOGI(TAG,"Have canvas");
    // TODO: Draw the face layers and the current animation frame here.
    // Keep all canvas changes inside this function so timers and serial
    // commands can share the same drawing path later.
    //
    lv_canvas_fill_bg(s_canvas, lv_color_hex(0x000000), LV_OPA_COVER); 
    draw_eyes();

    lv_obj_invalidate(s_canvas);
}



} // namespace

extern "C" esp_err_t face_anim_init(lv_obj_t *canvas)
{
    if (!canvas)
        return ESP_ERR_INVALID_ARG;

    s_canvas = canvas;
    draw_face();
    return ESP_OK;
}

extern "C" void face_anim_draw(void)
{
    draw_face();
}
