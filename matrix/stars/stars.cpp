// A band of twinkling stars, tiled until the panel is full.
// The band follows Twinkle Star Band at
// https://www.ascii-magic.com/ascii/stars-divider
// Louder music twinkles the stars faster and lifts their brightness a little.
// The twinkle clock speeds up with the meter. The stars stay where they are.
// The buttons keep their shared actions.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr int COLS = 96;
constexpr int ROWS = 5;
constexpr float TAU = 6.2831853f;
constexpr int BIG[6] = {8, 24, 39, 57, 72, 88};

static float twinkle = 0.0f;
static float loud = 0.0f;

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, clamp_channel((float)red), clamp_channel((float)green), clamp_channel((float)blue));
}

static void family_rgb(int family, float level, int &red, int &green, int &blue) {
    float t = clampf(level, 0.0f, 1.0f);
    if (family == 1) {
        red = (int)(58 + t * 197);
        green = (int)(52 + t * 196);
        blue = (int)(36 + t * 184);
    } else if (family == 2) {
        red = (int)(46 + t * 164);
        green = (int)(38 + t * 130);
        blue = (int)(64 + t * 191);
    } else {
        red = (int)(42 + t * 213);
        green = (int)(47 + t * 165);
        blue = (int)(58 + t * 180);
    }
}

}  // namespace

void stars_start() {
    twinkle = 0.0f;
    loud = 0.0f;
    environment_arm_listen();
}

void stars_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    environment_hear(dt_ms);
    float meter = environment_meter();
    // A beat rises quickly. The level settles more slowly, so the brightness holds.
    float ease = meter > loud ? 70.0f : 260.0f;
    float step = (float)dt_ms / ease;
    if (step > 1.0f) {
        step = 1.0f;
    }
    loud += (meter - loud) * step;
    float rate = 1.0f + loud;
    twinkle += dt_ms * 0.001f * rate;
    float lift = 1.0f + loud * 0.18f;
    float cell = 2.0f;
    if (PANEL_WIDTH < 80) {
        cell = (float)PANEL_WIDTH / 48.0f;
    }
    float tile_w = (float)COLS * cell;
    float tile_h = (float)ROWS * cell;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            put(frame, x, y, 0, 0, 0);
        }
    }
    int tiles_x = (int)((float)PANEL_WIDTH / tile_w) + 2;
    int tiles_y = (int)((float)PANEL_HEIGHT / tile_h) + 2;
    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            float origin_x = (float)tx * tile_w;
            float origin_y = (float)ty * tile_h;
            for (int i = 0; i < 28; ++i) {
                int col = (int)(field_hash(i + tx * 3, ty + 1, 42) * COLS);
                int row = (int)(field_hash(i, ty + 4, 42) * ROWS);
                float phase = field_hash(i, 7, 42 + tx) * TAU;
                float speed = 1.0f + field_hash(i, 8, 11) * 2.5f;
                int family = (int)(field_hash(i, 9, 5) * 3.0f);
                if (family > 2) {
                    family = 2;
                }
                float weight = row == 2 ? 1.0f : (row == 0 || row == 4 ? 0.6f : 0.85f);
                float blink = (0.5f + 0.5f * sinf(twinkle * speed + phase)) * weight;
                if (blink < 0.18f) {
                    continue;
                }
                int red;
                int green;
                int blue;
                family_rgb(family, blink * lift, red, green, blue);
                int px = (int)(origin_x + (float)col * cell);
                int py = (int)(origin_y + (float)row * cell);
                put(frame, px, py, red, green, blue);
            }
            for (int b = 0; b < 6; ++b) {
                float phase = (float)b * 1.3f + (float)(tx + ty);
                float speed = 1.1f + (float)(b % 3) * 0.25f;
                int family = b % 3;
                float blink = fmaxf(0.0f, sinf(twinkle * speed + phase));
                int reach = (int)(blink * 3.0f);
                int cx = (int)(origin_x + (float)BIG[b] * cell);
                int cy = (int)(origin_y + 2.0f * cell);
                int red;
                int green;
                int blue;
                family_rgb(family, (0.35f + blink * 0.65f) * lift, red, green, blue);
                for (int arm = 1; arm <= reach; ++arm) {
                    float fade = 1.0f - (float)arm / 5.0f;
                    int ar;
                    int ag;
                    int ab;
                    family_rgb(family, blink * fade * lift, ar, ag, ab);
                    put(frame, cx - (int)((float)arm * cell * 0.8f), cy, ar, ag, ab);
                    put(frame, cx + (int)((float)arm * cell * 0.8f), cy, ar, ag, ab);
                }
                if (blink > 0.45f) {
                    put(frame, cx, cy - (int)cell, red, green, blue);
                    put(frame, cx, cy + (int)cell, red, green, blue);
                }
                put(frame, cx, cy, red, green, blue);
            }
        }
    }
}
