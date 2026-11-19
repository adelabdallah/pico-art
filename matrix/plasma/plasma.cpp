// The Unicorn plasma, the second pattern in main.py.
// Panel coordinates land in that 16 by 7 field, and the extra pixels
// sample between the original ones. Hue walks a full turn of the colour
// wheel on a timer. Every few seconds the view turns a quarter turn
// over the same half second as the Unicorn B button.

#include <cmath>
#include <cstdint>

#include "pattern.hpp"

constexpr float CX = (PANEL_WIDTH - 1) / 2.0f;
constexpr float CY = (PANEL_HEIGHT - 1) / 2.0f;
constexpr float PATTERN_X = 15.0f / (PANEL_WIDTH - 1);
constexpr float PATTERN_Y = 6.0f / (PANEL_HEIGHT - 1);

constexpr int SWEEP_MS = 2000;
constexpr int PLASMA_PHASE_MS = 20;
constexpr float PI = 3.14159265f;
constexpr float TURN_STEP = PI / 2.0f;
constexpr int TURN_MS = 500;
constexpr int TURN_PERIOD_MS = 6000;
constexpr int HUE_CYCLE_MS = 8000;
constexpr int WAVE_N = 64;

int16_t wave_table[WAVE_N];

float hue = 0.0f;
float angle = 0.0f;
float angle_target = 0.0f;
float view_cos = 1.0f;
float view_sin = 0.0f;
uint32_t next_turn = TURN_PERIOD_MS;

void make_wave() {
    for (int i = 0; i < WAVE_N; ++i) {
        float sample = sinf(i * 2.0f * PI / WAVE_N) * 255.0f;
        wave_table[i] = (int16_t)sample;
    }
}

float wrap_wave(float pos) {
    float wrapped = fmodf(pos, (float)WAVE_N);
    if (wrapped < 0.0f) {
        wrapped += (float)WAVE_N;
    }
    return wrapped;
}

float wave_at(float pos) {
    pos = wrap_wave(pos);
    int base = (int)pos;
    float frac = pos - base;
    int first = wave_table[base & (WAVE_N - 1)];
    int second = wave_table[(base + 1) & (WAVE_N - 1)];
    return first + (second - first) * frac;
}

float sweep_unit(uint32_t now) {
    uint32_t phase = now % (SWEEP_MS * 2);
    if (phase <= SWEEP_MS) {
        return phase / (float)SWEEP_MS;
    }
    return (SWEEP_MS * 2 - phase) / (float)SWEEP_MS;
}

float sweep_float(uint32_t now, float low, float high) {
    return low + (high - low) * sweep_unit(now);
}

void plasma_rgb(float x, float y, float horizontal, float phase, int &red, int &green, int &blue) {
    float sample = wave_at(x * horizontal + phase) + wave_at(y * 20.0f + phase);
    int level = (int)((sample + 510.0f) / 4.0f);
    if (level < 0) {
        level = 0;
    } else if (level > 255) {
        level = 255;
    }
    red = level;
    green = 80;
    blue = 255 - level;
}

void update_view() {
    view_cos = cosf(angle);
    view_sin = sinf(angle);
}

void update_turn(int dt) {
    float gap = angle_target - angle;
    if (gap <= 0.000001f) {
        angle = angle_target;
    } else {
        float step = TURN_STEP * dt / TURN_MS;
        if (step >= gap) {
            angle = angle_target;
        } else {
            angle += step;
        }
    }
    float cycle = PI * 2.0f;
    if (angle >= cycle) {
        angle -= cycle;
        angle_target -= cycle;
    }
}

void plasma_start() {
    make_wave();
    hue = 0.0f;
    angle = 0.0f;
    angle_target = 0.0f;
    view_cos = 1.0f;
    view_sin = 0.0f;
    next_turn = TURN_PERIOD_MS;
}

void plasma_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    hue = (anim_ms % HUE_CYCLE_MS) / (float)HUE_CYCLE_MS;
    if ((int32_t)(anim_ms - next_turn) >= 0) {
        angle_target += TURN_STEP;
        next_turn += TURN_PERIOD_MS;
    }
    update_turn(dt_ms);
    update_view();

    float horizontal = sweep_float(anim_ms, 4.0f, 14.0f);
    float phase = (anim_ms % (PLASMA_PHASE_MS * WAVE_N)) / (float)PLASMA_PHASE_MS;
    uint8_t *pixel = pixels;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = x - CX;
            float dy = y - CY;
            float sx = CX + dx * view_cos + dy * view_sin;
            float sy = CY - dx * view_sin + dy * view_cos;
            int red;
            int green;
            int blue;
            plasma_rgb(sx * PATTERN_X, sy * PATTERN_Y, horizontal, phase, red, green, blue);
            hue_shift(red, green, blue, hue);
            write_bgr(pixel, red, green, blue);
            pixel += 3;
        }
    }
}
