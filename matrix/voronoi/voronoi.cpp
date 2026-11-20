// Fourteen drifting seeds and the regions they claim, shaded like stained glass.
// The layout and the motion follow Voronoi Cells at
// https://www.ascii-magic.com/ascii/voronoi-cells

#include "pattern.hpp"

#include <cmath>

namespace {

constexpr int SEEDS = 14;
constexpr float TAU = 6.2831853f;

static const uint8_t HUE[SEEDS][3] = {
    {255, 74, 106},
    {255, 154, 58},
    {255, 210, 63},
    {154, 255, 90},
    {63, 224, 160},
    {63, 208, 255},
    {74, 122, 255},
    {154, 106, 255},
    {224, 106, 255},
    {255, 106, 176},
    {255, 176, 122},
    {90, 255, 208},
    {122, 176, 255},
    {208, 255, 106},
};

struct Seed {
    float cx;
    float cy;
    float ax;
    float ay;
    float fx;
    float fy;
    float phase;
};

static Seed seeds[SEEDS];
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void shade(int index, int level, int &red, int &green, int &blue) {
    const uint8_t *colour = HUE[index];
    float mix = 0.22f + (float)level * 0.26f;
    if (level >= 3) {
        mix = 1.0f;
    }
    red = (int)(10 + colour[0] * mix);
    green = (int)(10 + colour[1] * mix);
    blue = (int)(16 + colour[2] * mix);
    if (level >= 3) {
        red = (red + 255) / 2;
        green = (green + 255) / 2;
        blue = (blue + 255) / 2;
    }
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

}  // namespace

void voronoi_start() {
    rng_state = 9;
    for (int i = 0; i < SEEDS; ++i) {
        seeds[i].cx = 10.0f + rnd() * 44.0f;
        seeds[i].cy = 10.0f + rnd() * 44.0f;
        seeds[i].ax = 6.0f + rnd() * 12.0f;
        seeds[i].ay = 6.0f + rnd() * 12.0f;
        seeds[i].fx = 0.08f + rnd() * 0.12f;
        seeds[i].fy = 0.07f + rnd() * 0.12f;
        seeds[i].phase = rnd() * TAU;
    }
}

void voronoi_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float seconds = anim_ms * 0.001f;
    float sx[SEEDS];
    float sy[SEEDS];
    for (int i = 0; i < SEEDS; ++i) {
        Seed &seed = seeds[i];
        float x = seed.cx + sinf(seconds * seed.fx * TAU * 0.5f + seed.phase) * seed.ax;
        float y = seed.cy + cosf(seconds * seed.fy * TAU * 0.5f + seed.phase * 1.7f) * seed.ay;
        if (x < 1.5f) {
            x = 1.5f;
        }
        if (y < 1.5f) {
            y = 1.5f;
        }
        if (x > 61.5f) {
            x = 61.5f;
        }
        if (y > 61.5f) {
            y = 61.5f;
        }
        sx[i] = x;
        sy[i] = y;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float nearest = 1.0e9f;
            float second = 1.0e9f;
            int owner = 0;
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            for (int i = 0; i < SEEDS; ++i) {
                float dx = px - sx[i];
                float dy = py - sy[i];
                float dist = dx * dx + dy * dy;
                if (dist < nearest) {
                    second = nearest;
                    nearest = dist;
                    owner = i;
                } else if (dist < second) {
                    second = dist;
                }
            }
            float gap = sqrtf(second) - sqrtf(nearest);
            int red;
            int green;
            int blue;
            if (gap < 1.15f) {
                red = gap < 0.55f ? 5 : 16;
                green = gap < 0.55f ? 5 : 12;
                blue = gap < 0.55f ? 10 : 22;
            } else {
                float reach = 1.0f - sqrtf(nearest) / 18.0f;
                if (reach < 0.0f) {
                    reach = 0.0f;
                }
                if (reach > 1.0f) {
                    reach = 1.0f;
                }
                float edge = gap / 8.0f;
                if (edge > 1.0f) {
                    edge = 1.0f;
                }
                int level = (int)((reach * 0.7f + edge * 0.3f) * 4.0f);
                if (level > 3) {
                    level = 3;
                }
                shade(owner, level, red, green, blue);
            }
            write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
        }
    }
    for (int i = 0; i < SEEDS; ++i) {
        int x = (int)(sx[i] + 0.5f);
        int y = (int)(sy[i] + 0.5f);
        put(frame, x, y, 255, 255, 255);
        put(frame, x - 1, y, 255, 255, 255);
        put(frame, x + 1, y, 255, 255, 255);
        put(frame, x, y - 1, 255, 255, 255);
        put(frame, x, y + 1, 255, 255, 255);
    }
}
