// Rockets climb over a night skyline and burst into coloured shells.
// The layout and the motion follow Fireworks at
// https://www.ascii-magic.com/ascii/fireworks

#include "pattern.hpp"

#include <cmath>

namespace {

constexpr float EVERY = 0.55f;
constexpr float RISE = 1.15f;
constexpr float FADE = 2.2f;
constexpr float TAU = 6.2831853f;
constexpr int HUE_COUNT = 7;

static int skyline[PANEL_WIDTH];
static float window_roll[PANEL_WIDTH];
static int star_x[40];
static int star_y[40];
static float star_phase[40];
static uint32_t rng_state = 1;

static const uint8_t HUE[HUE_COUNT][3] = {
    {255, 64, 64},
    {255, 150, 40},
    {255, 220, 70},
    {80, 230, 110},
    {70, 190, 255},
    {170, 90, 255},
    {255, 80, 180},
};

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static float hash_unit(int n, int k) {
    uint32_t h = (uint32_t)n * 374761393u + (uint32_t)k * 668265263u + 977u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)h * (1.0f / 4294967296.0f);
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    if (red < 0) {
        red = 0;
    }
    if (green < 0) {
        green = 0;
    }
    if (blue < 0) {
        blue = 0;
    }
    if (red > 255) {
        red = 255;
    }
    if (green > 255) {
        green = 255;
    }
    if (blue > 255) {
        blue = 255;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

}  // namespace

void fireworks_start() {
    rng_state = 5;
    int x = 0;
    while (x < PANEL_WIDTH) {
        int width = 3 + (int)(rnd() * 6.0f);
        int height = 4 + (int)(rnd() * 8.0f);
        for (int i = 0; i < width && x < PANEL_WIDTH; ++i, ++x) {
            skyline[x] = height;
            window_roll[x] = rnd();
        }
    }
    for (int i = 0; i < 40; ++i) {
        star_x[i] = (int)(rnd() * PANEL_WIDTH);
        star_y[i] = (int)(rnd() * 36.0f);
        star_phase[i] = rnd() * TAU;
    }
}

void fireworks_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float seconds = anim_ms * 0.001f;
    int newest = (int)(seconds / EVERY);
    struct Shell {
        float age;
        float bx;
        float by;
        float sx;
        int hue;
        int hue2;
        int count;
        float speed;
        int id;
    };
    Shell shells[6];
    int shell_count = 0;
    for (int id = newest - 5; id <= newest; ++id) {
        float age = seconds - (float)id * EVERY;
        if (age < 0.0f || age > RISE + FADE || shell_count >= 6) {
            continue;
        }
        Shell &shell = shells[shell_count];
        shell.age = age;
        shell.bx = 8.0f + hash_unit(id, 1) * (PANEL_WIDTH - 16);
        shell.by = 6.0f + hash_unit(id, 2) * 18.0f;
        shell.sx = shell.bx + (hash_unit(id, 4) - 0.5f) * 10.0f;
        shell.hue = (int)(hash_unit(id, 3) * HUE_COUNT);
        shell.hue2 = (int)(hash_unit(id, 7) * HUE_COUNT);
        if (shell.hue >= HUE_COUNT) {
            shell.hue = HUE_COUNT - 1;
        }
        if (shell.hue2 >= HUE_COUNT) {
            shell.hue2 = HUE_COUNT - 1;
        }
        shell.count = 36 + (int)(hash_unit(id, 5) * 16.0f);
        shell.speed = 8.0f + hash_unit(id, 6) * 6.0f;
        shell.id = id;
        shell_count += 1;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float glow = 0.0f;
            for (int s = 0; s < shell_count; ++s) {
                float burst = shells[s].age - RISE;
                if (burst < 0.0f || burst > 1.3f) {
                    continue;
                }
                float dx = (float)x - shells[s].bx;
                float dy = (float)y - shells[s].by;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < 20.0f) {
                    float fall = 1.0f - dist / 20.0f;
                    glow += (1.0f - burst / 1.3f) * fall * fall;
                }
            }
            float v = clampf(0.15f + (float)y / PANEL_HEIGHT * 0.45f + glow * 0.7f, 0.0f, 1.0f);
            int red = (int)(4 + v * 40 + glow * 80);
            int green = (int)(8 + v * 36 + glow * 50);
            int blue = (int)(24 + v * 90 + glow * 40);
            put(frame, x, y, red, green, blue);
        }
    }
    for (int i = 0; i < 40; ++i) {
        if (sinf(seconds * 2.0f + star_phase[i]) > 0.0f) {
            put(frame, star_x[i], star_y[i], 180, 190, 230);
        }
    }
    for (int s = 0; s < shell_count; ++s) {
        Shell &shell = shells[s];
        if (shell.age < RISE) {
            float f = shell.age / RISE;
            float ease = 1.0f - (1.0f - f) * (1.0f - f);
            int x = (int)(shell.sx + (shell.bx - shell.sx) * ease + 0.5f);
            int y = (int)(PANEL_HEIGHT - 1 + (shell.by - (PANEL_HEIGHT - 1)) * ease + 0.5f);
            const uint8_t *colour = HUE[shell.hue];
            put(frame, x, y, colour[0], colour[1], colour[2]);
            put(frame, x, y + 1, colour[0] / 2, colour[1] / 2, colour[2] / 2);
            put(frame, x, y + 2, colour[0] / 4, colour[1] / 4, colour[2] / 4);
            continue;
        }
        float age = shell.age - RISE;
        float fade = age / FADE;
        float drag = (1.0f - expf(-2.2f * age)) / 2.2f;
        for (int p = 0; p < shell.count; ++p) {
            float theta = (float)p / (float)shell.count * TAU + hash_unit(shell.id, p + 20) * 0.25f;
            float speed = shell.speed * ((p & 1) ? 0.5f : 1.0f);
            float x = shell.bx + cosf(theta) * speed * drag;
            float y = shell.by + sinf(theta) * speed * drag + 3.2f * age * age;
            int hue = (p % 3 == 0) ? shell.hue2 : shell.hue;
            int dim = (int)(fade * 3.0f);
            if (dim > 3) {
                dim = 3;
            }
            const uint8_t *colour = HUE[hue];
            int scale = 4 - dim;
            if (fade > 0.65f && sinf(seconds * 30.0f + (float)p) > 0.35f) {
                continue;
            }
            put(frame, (int)x, (int)y, colour[0] * scale / 4, colour[1] * scale / 4, colour[2] * scale / 4);
        }
    }
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        int height = skyline[x];
        for (int k = 0; k < height; ++k) {
            int y = PANEL_HEIGHT - 1 - k;
            bool lit = (x % 2 == 0) && (k % 2 == 1) && window_roll[x] > 0.4f && k < height - 1;
            if (lit) {
                put(frame, x, y, 255, 200, 110);
            } else {
                int level = k == height - 1 ? 18 : 8;
                put(frame, x, y, level, level, level + 6);
            }
        }
    }
}
