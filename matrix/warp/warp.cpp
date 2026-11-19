// Soft blobs from value noise sampled in a warped place.
// Two octaves, a fixed seed, and a palette that drifts.
// The warp follows the liquid-distortion sketches at
// https://github.com/collidingScopes/liquid-shape-distortions

#include <cstdint>

#include "pattern.hpp"

constexpr int PERM_N = 256;

static uint8_t perm[PERM_N];

struct Stop {
    int red;
    int green;
    int blue;
};

static Stop palette[] = {
    {24, 36, 110},
    {36, 196, 176},
    {236, 72, 138},
    {244, 196, 64},
};

constexpr int STOP_COUNT = (int)(sizeof palette / sizeof palette[0]);

void warp_start() {
    for (int i = 0; i < PERM_N; ++i) {
        perm[i] = (uint8_t)i;
    }
    uint32_t state = 0xA3C59AC3u;
    for (int i = PERM_N - 1; i > 0; --i) {
        state = state * 1664525u + 1013904223u;
        int pick = (int)((state >> 16) % (uint32_t)(i + 1));
        uint8_t saved = perm[i];
        perm[i] = perm[pick];
        perm[pick] = saved;
    }
}

static int hash_cell(int x, int y) {
    int wrapped_x = x & (PERM_N - 1);
    int wrapped_y = y & (PERM_N - 1);
    return perm[(perm[wrapped_x] + wrapped_y) & (PERM_N - 1)];
}

static float fade(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static float value_noise(float x, float y) {
    int x0 = (int)x;
    int y0 = (int)y;
    float tx = fade(x - x0);
    float ty = fade(y - y0);
    float a = hash_cell(x0, y0);
    float b = hash_cell(x0 + 1, y0);
    float c = hash_cell(x0, y0 + 1);
    float d = hash_cell(x0 + 1, y0 + 1);
    float ab = a + (b - a) * tx;
    float cd = c + (d - c) * tx;
    return (ab + (cd - ab) * ty) / 255.0f;
}

static float fbm(float x, float y) {
    float broad = value_noise(x, y);
    float fine = value_noise(x * 2.0f + 19.0f, y * 2.0f + 7.0f);
    return broad * 0.68f + fine * 0.32f;
}

static void colour_at(float field, float shift, int &red, int &green, int &blue) {
    float along = (field + shift) * STOP_COUNT;
    int turns = (int)(along / STOP_COUNT);
    along = along - turns * STOP_COUNT;
    int index = (int)along;
    if (index >= STOP_COUNT) {
        index = 0;
        along = 0.0f;
    }
    int next = index + 1;
    if (next >= STOP_COUNT) {
        next = 0;
    }
    float frac = along - index;
    Stop from = palette[index];
    Stop to = palette[next];
    red = (int)(from.red + (to.red - from.red) * frac);
    green = (int)(from.green + (to.green - from.green) * frac);
    blue = (int)(from.blue + (to.blue - from.blue) * frac);
}

void warp_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float time = anim_ms * 0.00022f;
    float shift = (anim_ms % 16000) / 16000.0f;
    constexpr float scale = 0.065f;
    uint8_t *pixel = pixels;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float px = x * scale + 8.0f;
            float py = y * scale + 8.0f;
            float ox = fbm(px + time, py);
            float oy = fbm(px + 4.0f, py + time * 1.17f);
            float field = fbm(px + ox * 1.7f, py + oy * 1.7f + time * 0.35f);
            int red;
            int green;
            int blue;
            colour_at(field, shift, red, green, blue);
            write_bgr(pixel, red, green, blue);
            pixel += 3;
        }
    }
}
