// Shaded hearts rising through a warm rose haze.
// The motion follows Valentine Hearts at
// https://www.ascii-magic.com/ascii/valentine-hearts
// The hearts fill whatever panel is attached. The buttons keep their shared actions.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr int HEARTS = 12;
constexpr int TINY = 28;
constexpr float TAU = 6.2831853f;

struct Heart {
    float size;
    float x;
    float phase;
    float speed;
    float sway;
    int family;
};

static Heart hearts[HEARTS];
static float tiny_x[TINY];
static float tiny_phase[TINY];
static float tiny_speed[TINY];
static float tiny_sway[TINY];
static int tiny_family[TINY];
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, clamp_channel((float)red), clamp_channel((float)green), clamp_channel((float)blue));
}

static float unit() {
    float side = (float)PANEL_WIDTH;
    if ((float)PANEL_HEIGHT < side) {
        side = (float)PANEL_HEIGHT;
    }
    return side / 64.0f;
}

static void heart_rgb(int family, float level, int &red, int &green, int &blue) {
    float t = clampf(level, 0.0f, 1.0f);
    if (family == 0) {
        red = (int)(90 + t * 165);
        green = (int)(8 + t * 80);
        blue = (int)(24 + t * 70);
    } else {
        red = (int)(120 + t * 135);
        green = (int)(30 + t * 120);
        blue = (int)(70 + t * 140);
    }
}

}  // namespace

void hearts_start() {
    rng_state = 14u;
    float scale = unit();
    if (scale < 0.5f) {
        scale = 0.5f;
    }
    for (int i = 0; i < HEARTS; ++i) {
        hearts[i].size = (1.6f + rnd() * 4.2f) * scale;
        hearts[i].x = 4.0f + rnd() * (float)(PANEL_WIDTH - 8);
        hearts[i].phase = rnd();
        hearts[i].sway = rnd() * TAU;
        hearts[i].family = rnd() < 0.55f ? 0 : 1;
        hearts[i].speed = 0.05f + 0.05f * (hearts[i].size / (6.0f * scale));
    }
    for (int i = 0; i < TINY; ++i) {
        tiny_x[i] = rnd() * (float)PANEL_WIDTH;
        tiny_phase[i] = rnd();
        tiny_speed[i] = 0.04f + rnd() * 0.05f;
        tiny_sway[i] = rnd() * TAU;
        tiny_family[i] = rnd() < 0.5f ? 0 : 1;
    }
}

void hearts_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float seconds = anim_ms * 0.001f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        float rise = 1.0f - (float)y / (float)PANEL_HEIGHT;
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float cloud = field_noise((float)x * 0.05f, (float)y * 0.08f - seconds * 0.12f, 3);
            float v = clampf(0.18f + rise * 0.28f + (cloud - 0.5f) * 0.45f, 0.0f, 1.0f);
            int red = (int)(28 + v * 140);
            int green = (int)(4 + v * 28);
            int blue = (int)(16 + v * 50);
            put(frame, x, y, red, green, blue);
        }
    }
    float scale = unit();
    for (int i = 0; i < TINY; ++i) {
        float travel = (float)PANEL_HEIGHT + 6.0f;
        float wrapped = tiny_phase[i] + seconds * tiny_speed[i];
        wrapped -= floorf(wrapped);
        float y = (float)PANEL_HEIGHT + 2.0f - wrapped * travel;
        float x = tiny_x[i] + sinf(seconds * 1.3f + tiny_sway[i]) * 2.0f * scale;
        int red;
        int green;
        int blue;
        heart_rgb(tiny_family[i], 0.72f, red, green, blue);
        put(frame, (int)x, (int)y, red, green, blue);
    }
    for (int i = 0; i < HEARTS; ++i) {
        Heart &heart = hearts[i];
        float travel = (float)PANEL_HEIGHT + heart.size * 3.0f;
        float wrapped = heart.phase + seconds * heart.speed;
        wrapped -= floorf(wrapped);
        float cy = (float)PANEL_HEIGHT + heart.size * 1.5f - wrapped * travel;
        float cx = heart.x + sinf(seconds * 0.9f + heart.sway) * 2.5f * scale;
        float sx = heart.size * 1.15f;
        float sy = heart.size;
        int y0 = (int)(cy - sy * 1.4f);
        int y1 = (int)(cy + sy * 1.4f);
        int x0 = (int)(cx - sx * 1.4f);
        int x1 = (int)(cx + sx * 1.4f);
        for (int y = y0; y <= y1; ++y) {
            if (y < 0 || y >= PANEL_HEIGHT) {
                continue;
            }
            for (int x = x0; x <= x1; ++x) {
                if (x < 0 || x >= PANEL_WIDTH) {
                    continue;
                }
                float u = ((float)x - cx) / sx;
                float w = -((float)y - cy) / sy + 0.15f;
                float a = u * u + w * w - 1.0f;
                float field = a * a * a - u * u * w * w * w;
                if (field > 0.0f) {
                    continue;
                }
                float depth = clampf(-field * 6.0f, 0.0f, 1.0f);
                float lit = clampf(0.35f + depth * 0.4f - u * 0.22f + w * 0.16f, 0.0f, 1.0f);
                if (hypotf(u + 0.45f, w - 0.45f) < 0.28f) {
                    lit = clampf(lit + 0.28f, 0.0f, 1.0f);
                }
                int red;
                int green;
                int blue;
                heart_rgb(heart.family, lit, red, green, blue);
                put(frame, x, y, red, green, blue);
            }
        }
    }
}
