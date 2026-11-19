// Each frame samples the previous frame a little closer to the centre,
// turns it, and adds a soft moving spot. The picture smears into a tunnel.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "pattern.hpp"

constexpr float CX = (PANEL_WIDTH - 1) / 2.0f;
constexpr float CY = (PANEL_HEIGHT - 1) / 2.0f;
constexpr float PI = 3.14159265f;
constexpr float ZOOM = 1.028f;

static uint8_t history[PANEL_PIXELS * 3];

static float wrap_coord(float value) {
    float wrapped = fmodf(value, (float)PANEL_WIDTH);
    if (wrapped < 0.0f) {
        wrapped += (float)PANEL_WIDTH;
    }
    return wrapped;
}

static void sample_history(float sx, float sy, int &red, int &green, int &blue) {
    sx = wrap_coord(sx);
    sy = wrap_coord(sy);
    int x0 = (int)sx;
    int y0 = (int)sy;
    float tx = sx - x0;
    float ty = sy - y0;
    int x1 = (x0 + 1) & (PANEL_WIDTH - 1);
    int y1 = (y0 + 1) & (PANEL_HEIGHT - 1);
    x0 &= PANEL_WIDTH - 1;
    y0 &= PANEL_HEIGHT - 1;

    const uint8_t *p00 = history + (y0 * PANEL_WIDTH + x0) * 3;
    const uint8_t *p10 = history + (y0 * PANEL_WIDTH + x1) * 3;
    const uint8_t *p01 = history + (y1 * PANEL_WIDTH + x0) * 3;
    const uint8_t *p11 = history + (y1 * PANEL_WIDTH + x1) * 3;

    float blue_f = (p00[0] + (p10[0] - p00[0]) * tx) * (1.0f - ty) + (p01[0] + (p11[0] - p01[0]) * tx) * ty;
    float green_f = (p00[1] + (p10[1] - p00[1]) * tx) * (1.0f - ty) + (p01[1] + (p11[1] - p01[1]) * tx) * ty;
    float red_f = (p00[2] + (p10[2] - p00[2]) * tx) * (1.0f - ty) + (p01[2] + (p11[2] - p01[2]) * tx) * ty;
    red = (int)red_f;
    green = (int)green_f;
    blue = (int)blue_f;
}

void feedback_start() {
    memset(history, 0, sizeof history);
}

void feedback_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float time = anim_ms * 0.001f;
    float turn = time * 0.22f;
    float turn_cos = cosf(turn);
    float turn_sin = sinf(turn);
    float spot_x = CX + cosf(time * 0.83f) * 14.0f;
    float spot_y = CY + sinf(time * 0.57f) * 14.0f;
    float tint = time * 0.37f;

    uint8_t *pixel = pixels;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = x - CX;
            float dy = y - CY;
            float sx = CX + (dx * turn_cos - dy * turn_sin) / ZOOM;
            float sy = CY + (dx * turn_sin + dy * turn_cos) / ZOOM;
            sx += sinf(y * 0.18f + time * 1.4f) * 1.1f;
            sy += cosf(x * 0.18f + time * 1.1f) * 1.1f;

            int red;
            int green;
            int blue;
            sample_history(sx, sy, red, green, blue);

            float falloff_x = x - spot_x;
            float falloff_y = y - spot_y;
            float spot = 1.0f - (falloff_x * falloff_x + falloff_y * falloff_y) / 70.0f;
            if (spot < 0.0f) {
                spot = 0.0f;
            }
            float add_r = (0.55f + 0.45f * sinf(tint)) * spot * 210.0f;
            float add_g = (0.55f + 0.45f * sinf(tint + 2.1f)) * spot * 180.0f;
            float add_b = (0.55f + 0.45f * sinf(tint + 4.2f)) * spot * 220.0f;

            red = clamp_channel(red * 0.93f + add_r);
            green = clamp_channel(green * 0.93f + add_g);
            blue = clamp_channel(blue * 0.93f + add_b);
            write_bgr(pixel, red, green, blue);
            pixel += 3;
        }
    }
    memcpy(history, pixels, sizeof history);
}
