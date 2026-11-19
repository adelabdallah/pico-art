#pragma once

#include <cmath>
#include <cstdint>

constexpr int PANEL_WIDTH = 64;
constexpr int PANEL_HEIGHT = 64;
constexpr int PANEL_PIXELS = PANEL_WIDTH * PANEL_HEIGHT;

inline int clamp_channel(float value) {
    int rounded = (int)(value + 0.5f);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > 255) {
        return 255;
    }
    return rounded;
}

inline void write_bgr(uint8_t *pixel, int red, int green, int blue) {
    pixel[0] = (uint8_t)blue;
    pixel[1] = (uint8_t)green;
    pixel[2] = (uint8_t)red;
}

// Rotate one pixel around the colour wheel. A grey pixel stays grey.
// turns is a fraction of a full turn.
inline void hue_shift(int &red, int &green, int &blue, float turns) {
    if (red == green && green == blue) {
        return;
    }
    float peak = red;
    if (green > peak) {
        peak = green;
    }
    if (blue > peak) {
        peak = blue;
    }
    float floor = red;
    if (green < floor) {
        floor = green;
    }
    if (blue < floor) {
        floor = blue;
    }
    if (peak == 0.0f) {
        return;
    }
    float span = peak - floor;
    float hue_value;
    if (peak == red) {
        hue_value = fmodf((green - blue) / span, 6.0f);
    } else if (peak == green) {
        hue_value = (blue - red) / span + 2.0f;
    } else {
        hue_value = (red - green) / span + 4.0f;
    }
    if (hue_value < 0.0f) {
        hue_value += 6.0f;
    }
    hue_value = fmodf(hue_value / 6.0f + turns, 1.0f);
    if (hue_value < 0.0f) {
        hue_value += 1.0f;
    }
    float sat = span / peak;
    float sector = hue_value * 6.0f;
    int index = (int)sector % 6;
    float frac = sector - (int)sector;
    float low = peak * (1.0f - sat);
    float mid_down = peak * (1.0f - frac * sat);
    float mid_up = peak * (1.0f - (1.0f - frac) * sat);
    float out_r;
    float out_g;
    float out_b;
    if (index == 0) {
        out_r = peak;
        out_g = mid_up;
        out_b = low;
    } else if (index == 1) {
        out_r = mid_down;
        out_g = peak;
        out_b = low;
    } else if (index == 2) {
        out_r = low;
        out_g = peak;
        out_b = mid_up;
    } else if (index == 3) {
        out_r = low;
        out_g = mid_down;
        out_b = peak;
    } else if (index == 4) {
        out_r = mid_up;
        out_g = low;
        out_b = peak;
    } else {
        out_r = peak;
        out_g = low;
        out_b = mid_down;
    }
    red = clamp_channel(out_r);
    green = clamp_channel(out_g);
    blue = clamp_channel(out_b);
}

inline float clampf(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

inline float smooth_unit(float edge0, float edge1, float value) {
    float span = edge1 - edge0;
    float t = clampf((value - edge0) / span, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void plasma_start();
void plasma_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void warp_start();
void warp_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void feedback_start();
void feedback_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void metaballs_start();
void metaballs_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void reaction_start();
void reaction_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void life_start();
void life_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void ant_start();
void ant_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void loop_start();
void loop_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void rule30_start();
void rule30_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);

void environment_start();
void environment_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms);
bool environment_listening();
void environment_toggle_listen();
float environment_meter();
bool environment_meter_peak();
// Live accelerometer X and Y in g. False when the read fails.
bool environment_axes(float &x_g, float &y_g);
