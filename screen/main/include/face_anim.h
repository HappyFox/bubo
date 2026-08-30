#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attach the face animation module to an existing LVGL canvas.
 *
 * The caller must hold the LVGL lock while calling this function.
 */
esp_err_t face_anim_init(lv_obj_t *canvas);

/**
 * @brief Redraw the current face state.
 *
 * This is the drawing entry point for future timers and animations. The
 * caller must hold the LVGL lock while calling it.
 */
void face_anim_draw(void);

typedef enum Glance {
    UP,    // 0
    DOWN, // 1
    NEUTRAL,    // 2
    LEFT,   // 3
    RIGHT,   // 4
    DOWN_LEFT,   // 5
    DOWN_RIGHT,   // 6
    UP_LEFT,   // 7
    UP_RIGHT   // 8
} Glance;

typedef struct Expression  {
    bool blinking;
    bool mouth_open;
    int mouth_height;
    Glance glance_direction;

} Expression;


typedef struct Point {
    int x;
    int y;
} Point;

#ifdef __cplusplus
}
#endif
