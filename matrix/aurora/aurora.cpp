// Green and violet curtains over a snowy pine shore and a frozen lake.
// The picture follows Northern Lights at
// https://www.ascii-magic.com/ascii/northern-lights
// Extra width continues the forest and the lake. Extra height adds sky.
// Green steps the curtain hue by a fifth of a turn, and a hold spins it.
// The lake follows the curtains. Snow, pines, and stars stay on their own colours.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>
#include <cstdlib>

namespace {

constexpr float TAU = 6.2831853f;
constexpr int PINE_CAP = 40;

struct Pine {
    int x;
    int height;
};

static Pine pines[PINE_CAP];
static int pine_count = 0;
static float aur_turns = 0.0f;
static bool spinning = false;
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

static void curtain_rgb(float level, float height, int &red, int &green, int &blue) {
    float t = clampf(level, 0.0f, 1.0f);
    if (height > 0.55f) {
        red = (int)(28 + t * 160);
        green = (int)(30 + t * 40);
        blue = (int)(80 + t * 130);
    } else {
        red = (int)(8 + t * 40);
        green = (int)(40 + t * 200);
        blue = (int)(36 + t * 90);
    }
    hue_shift(red, green, blue, aur_turns);
}

}  // namespace

void aurora_hue_tap() {
    aur_turns += 0.2f;
    if (aur_turns >= 1.0f) {
        aur_turns -= 1.0f;
    }
}

void aurora_hue_spin(bool held) {
    spinning = held;
}

void aurora_start() {
    rng_state = 5u;
    pine_count = 0;
    int x = -2;
    while (x < PANEL_WIDTH + 2 && pine_count < PINE_CAP) {
        int height = 8 + (int)(rnd() * 10.0f);
        if (abs(x - PANEL_WIDTH / 2) < PANEL_WIDTH / 6) {
            height += 3;
        }
        pines[pine_count].x = x;
        pines[pine_count].height = height;
        pine_count += 1;
        x += 5 + (int)(rnd() * 5.0f);
    }
    aur_turns = 0.0f;
    spinning = false;
}

void aurora_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    if (spinning && dt_ms > 0) {
        aur_turns += dt_ms * 0.001f;
        aur_turns -= floorf(aur_turns);
    }
    float seconds = anim_ms * 0.001f;
    int shore = scene_y(40);
    if (shore < 8) {
        shore = 8;
    }
    if (shore > PANEL_HEIGHT - 10) {
        shore = PANEL_HEIGHT - 10;
    }
    int lake = shore + 5;
    if (lake > PANEL_HEIGHT - 4) {
        lake = PANEL_HEIGHT - 4;
    }
    float base[3][PANEL_WIDTH_MAX];
    float ray[3][PANEL_WIDTH_MAX];
    for (int c = 0; c < 3; ++c) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float fold = field_fbm((float)x * 0.02f + seconds * 0.05f + (float)c * 9.0f, (float)c * 3.0f + seconds * 0.03f, 3, c + 1);
            base[c][x] = (float)shore * (0.28f + (float)c * 0.12f) + (fold - 0.5f) * 18.0f;
            base[c][x] += sinf((float)x * 0.05f + seconds * 0.4f + (float)c * 2.0f) * 6.0f;
            float rays = field_fbm((float)x * 0.08f + seconds * 0.35f * (c == 1 ? -1.0f : 1.0f), seconds * 0.2f + (float)c, 2, 11 + c);
            ray[c][x] = powf(fmaxf(0.05f, rays), 1.4f);
        }
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            if (y >= lake) {
                float depth = (float)(y - lake) / (float)(PANEL_HEIGHT - lake + 1);
                int red = (int)(8 + depth * 20);
                int green = (int)(18 + depth * 28);
                int blue = (int)(40 + depth * 50);
                float crack = fabsf(field_noise((float)x * 0.08f, (float)y * 0.2f, 31) - 0.5f);
                if (crack < 0.03f) {
                    put(frame, x, y, 180, 200, 220);
                    continue;
                }
                int sample = lake - (y - lake) - 6;
                if (sample < 0) {
                    sample = 0;
                }
                float best = 0.0f;
                float height = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    float up = base[c][x] - (float)sample;
                    float body = up >= 0.0f ? expf(-up / (10.0f + (float)c * 2.0f)) : expf(up * 0.4f);
                    float amount = body * ray[c][x];
                    if (amount > best) {
                        best = amount;
                        height = clampf(up / 16.0f, 0.0f, 1.0f);
                    }
                }
                if (best > 0.2f) {
                    int ar;
                    int ag;
                    int ab;
                    curtain_rgb(best * 0.7f, height, ar, ag, ab);
                    red = (red + ar) / 2;
                    green = (green + ag) / 2;
                    blue = (blue + ab) / 2;
                }
                put(frame, x, y, red, green, blue);
                continue;
            }
            float best = 0.0f;
            float height = 0.0f;
            for (int c = 0; c < 3; ++c) {
                float up = base[c][x] - (float)y;
                float body = up >= 0.0f ? expf(-up / (12.0f + (float)c * 3.0f)) : expf(up * 0.45f);
                float amount = body * ray[c][x] * (c == 1 ? 1.0f : 0.8f);
                if (amount > best) {
                    best = amount;
                    height = clampf(up / 18.0f, 0.0f, 1.0f);
                }
            }
            if (y < shore && best > 0.16f) {
                int red;
                int green;
                int blue;
                curtain_rgb(best, height, red, green, blue);
                put(frame, x, y, red, green, blue);
            } else if (y >= shore - 3 && y < lake) {
                float snow = field_noise((float)x * 0.15f, (float)y * 0.2f, 6);
                int level = (int)(150 + snow * 80);
                put(frame, x, y, level, level + 6, level + 16);
            } else {
                float v = clampf(powf((float)y / (float)shore, 2.0f), 0.0f, 1.0f);
                put(frame, x, y, (int)(4 + v * 16), (int)(10 + v * 28), (int)(28 + v * 50));
            }
        }
    }
    for (int i = 0; i < pine_count; ++i) {
        int px = pines[i].x;
        int height = pines[i].height;
        int foot = shore + 1;
        for (int j = 0; j < height; ++j) {
            int y = foot - j;
            if (y < 0 || y >= lake) {
                continue;
            }
            int half = (int)((float)(height - j) * 0.42f);
            if (half < 1) {
                half = 1;
            }
            for (int dx = -half; dx <= half; ++dx) {
                bool cap = abs(dx) == half || j < 2;
                if (cap) {
                    put(frame, px + dx, y, 170, 190, 200);
                } else {
                    int dark = ((px + dx + y) & 3) == 0 ? 6 : 14;
                    put(frame, px + dx, y, dark, dark + 10, dark + 8);
                }
            }
        }
    }
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        float drift = (float)PANEL_HEIGHT - 2.0f;
        drift -= fmaxf(0.0f, 6.0f - (float)x * 0.2f);
        drift -= fmaxf(0.0f, ((float)x - (float)PANEL_WIDTH + 24.0f) * 0.2f);
        for (int y = (int)drift; y < PANEL_HEIGHT; ++y) {
            put(frame, x, y, 220, 230, 240);
        }
    }
    int star_rows = shore - 2;
    if (star_rows < 1) {
        star_rows = 1;
    }
    int stars = PANEL_WIDTH * star_rows / 48;
    if (stars > 80) {
        stars = 80;
    }
    for (int i = 0; i < stars; ++i) {
        int x = (int)(field_hash(i, 2, 5) * (float)PANEL_WIDTH);
        int y = (int)(field_hash(i, 3, 5) * (float)star_rows);
        float phase = field_hash(i, 4, 5) * TAU;
        float twinkle = sinf(seconds * 2.0f + phase);
        if (twinkle < -0.15f) {
            continue;
        }
        int level = twinkle > 0.55f ? 242 : 150;
        put(frame, x, y, level - 10, level, 255);
    }
}
