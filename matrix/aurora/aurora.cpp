// Three curtains of aurora, green, teal, and rose, swaying over a dark hill.
// The layout and the motion follow Aurora Curtains at
// https://www.ascii-magic.com/ascii/aurora-curtains

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

struct Ribbon {
    float y;
    float amp;
    float freq;
    float speed;
    float amp2;
    float freq2;
    float speed2;
    float length;
    float drift;
    int seed;
};

constexpr Ribbon RIBBONS[3] = {
    {46.0f, 6.0f, 0.09f, 0.11f, 2.4f, 0.16f, 0.07f, 16.0f, 0.35f, 1},
    {32.0f, 5.0f, 0.11f, -0.08f, 2.0f, 0.2f, 0.1f, 11.0f, -0.25f, 7},
    {18.0f, 4.0f, 0.08f, 0.06f, 1.6f, 0.14f, -0.09f, 8.0f, 0.2f, 13},
};

static float hill[PANEL_WIDTH_MAX];
static uint8_t *stars = nullptr;

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

static void curtain(int which, float level, int &red, int &green, int &blue) {
    float t = clampf(level, 0.0f, 1.0f);
    if (which == 0) {
        red = (int)(6 + t * 160);
        green = (int)(40 + t * 215);
        blue = (int)(30 + t * 140);
    } else if (which == 1) {
        red = (int)(6 + t * 80);
        green = (int)(50 + t * 180);
        blue = (int)(70 + t * 185);
    } else {
        red = (int)(40 + t * 215);
        green = (int)(12 + t * 80);
        blue = (int)(60 + t * 160);
    }
}

}  // namespace

void aurora_start() {
    stars = pattern_alloc(PANEL_PIXELS);
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        hill[x] = scene_yf(58.0f) - field_fbm(x * 0.08f, 1.5f, 3, 21) * 4.5f;
    }
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        int x = i % PANEL_WIDTH;
        int y = i / PANEL_WIDTH;
        float h = field_hash(x * 3, y * 5, 3);
        stars[i] = h > 0.992f ? (h > 0.997f ? 2 : 1) : 0;
    }
}

void aurora_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float seconds = anim_ms * 0.001f;
    float base[3][PANEL_WIDTH_MAX];
    float fold[3][PANEL_WIDTH_MAX];
    float ray[3][PANEL_WIDTH_MAX];
    for (int r = 0; r < 3; ++r) {
        const Ribbon &ribbon = RIBBONS[r];
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float p = x * ribbon.freq + seconds * ribbon.speed;
            float q = x * ribbon.freq2 + seconds * ribbon.speed2;
            base[r][x] = scene_yf(ribbon.y) + ribbon.amp * sinf(p) + ribbon.amp2 * sinf(q);
            float slope = ribbon.amp * ribbon.freq * cosf(p) + ribbon.amp2 * ribbon.freq2 * cosf(q);
            fold[r][x] = 0.45f + fminf(1.1f, fabsf(slope) * 8.0f);
            float fb = field_fbm(x * 0.42f - seconds * ribbon.drift, (float)ribbon.seed, 3, ribbon.seed);
            ray[r][x] = 0.15f + 1.9f * fb * fb;
        }
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            if ((float)y >= hill[x]) {
                int shade = y - (int)hill[x];
                int level = shade < 1 ? 28 : (shade < 2 ? 10 : 2);
                put(frame, x, y, level, level + 2, level + 6);
                continue;
            }
            float best = 0.0f;
            int which = 0;
            float height = 0.0f;
            float sum = 0.0f;
            for (int r = 0; r < 3; ++r) {
                float h = base[r][x] - (float)y;
                float length = RIBBONS[r].length;
                float v = h >= 0.0f ? expf(-h / length) * (h < 1.6f ? 1.7f - h * 0.35f : 1.1f) : expf(h * 1.3f);
                float intensity = v * fold[r][x] * (h < 1.2f ? 0.7f + ray[r][x] * 0.3f : ray[r][x]);
                sum += intensity;
                if (intensity > best) {
                    best = intensity;
                    which = r;
                    height = clampf(h / (length * 1.6f), 0.0f, 1.0f);
                }
            }
            float level = clampf(sum * 0.55f, 0.0f, 1.0f);
            int index = x + y * PANEL_WIDTH;
            if (level > 0.1f) {
                int red;
                int green;
                int blue;
                if (height > 0.62f && level < 0.55f) {
                    red = (int)(30 + level * 80);
                    green = (int)(16 + level * 30);
                    blue = (int)(70 + level * 90);
                } else {
                    curtain(which, level, red, green, blue);
                }
                put(frame, x, y, red, green, blue);
            } else if (stars[index] != 0 && level < 0.08f) {
                int lit = stars[index] == 2 ? 230 : 150;
                put(frame, x, y, lit, lit, 255);
            } else {
                float sky = ((float)y - (float)scene_dy()) / (float)PANEL_DESIGN;
                if (sky < 0.0f) {
                    sky = 0.0f;
                }
                int blue = (int)(14 + sky * 40 + level * 30);
                put(frame, x, y, (int)(4 + sky * 16), (int)(8 + sky * 24), blue);
            }
        }
    }
}
