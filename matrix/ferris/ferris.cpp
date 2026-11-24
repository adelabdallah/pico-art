// A lit wheel turning at dusk, with gondolas that stay upright.
// The layout and the motion follow Ferris Wheel at
// https://www.ascii-magic.com/ascii/ferris-wheel
// The encoder speeds the wheel. The rim lights and the sway keep their own pace.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr float TAU = 6.2831853f;
constexpr float BASE_SPIN = 0.18f;
constexpr float RATE_FLOOR = 0.25f;
constexpr float RATE_CAP = 6.0f;
static float wheel_x() { return panel_sx(32.0f); }
static float wheel_y() { return panel_sy(28.0f); }
static float wheel_r() { return panel_sy(22.0f); }
constexpr int GONDOLAS = 12;

static float spin_rate = 1.0f;
static float spin_angle = 0.0f;
static int city_top[PANEL_WIDTH_MAX];
static int star_x[24];
static int star_y[24];
static float star_phase[24];
static uint32_t rng_state = 1;

static const uint8_t GONDOLA[6][3] = {
    {255, 79, 109},
    {255, 210, 63},
    {63, 208, 255},
    {125, 255, 138},
    {255, 143, 63},
    {199, 125, 255},
};

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

static void line(uint8_t *frame, int x0, int y0, int x1, int y1, int red, int green, int blue) {
    int dx = x1 - x0;
    if (dx < 0) {
        dx = -dx;
    }
    int dy = y1 - y0;
    if (dy < 0) {
        dy = -dy;
    }
    dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int n = 0; n < 96; ++n) {
        put(frame, x0, y0, red, green, blue);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void dusk(float v, int &red, int &green, int &blue) {
    if (v < 0.45f) {
        float t = v / 0.45f;
        red = (int)(20 + t * 50);
        green = (int)(12 + t * 20);
        blue = (int)(46 + t * 40);
        return;
    }
    if (v < 0.75f) {
        float t = (v - 0.45f) / 0.3f;
        red = (int)(70 + t * 160);
        green = (int)(32 + t * 50);
        blue = (int)(86 - t * 20);
        return;
    }
    float t = (v - 0.75f) / 0.25f;
    red = (int)(230 + t * 25);
    green = (int)(82 + t * 90);
    blue = (int)(66 + t * 30);
}

}  // namespace

void ferris_start() {
    spin_rate = 1.0f;
    spin_angle = 0.0f;
    rng_state = 5;
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        int extra = sinf(x * 0.35f) > 0.55f ? 3 : 0;
        city_top[x] = panel_iy(52) - (int)(rnd() * panel_sy(4.0f)) - (extra ? panel_iy(3) : 0);
    }
    for (int i = 0; i < 24; ++i) {
        star_x[i] = (int)(rnd() * PANEL_WIDTH);
        star_y[i] = (int)(rnd() * panel_sy(14.0f));
        star_phase[i] = rnd() * TAU;
    }
}

void ferris_spin(int steps) {
    spin_rate += (float)steps * 0.25f;
    if (spin_rate < RATE_FLOOR) {
        spin_rate = RATE_FLOOR;
    }
    if (spin_rate > RATE_CAP) {
        spin_rate = RATE_CAP;
    }
}

void ferris_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
    spin_angle += BASE_SPIN * spin_rate * (float)dt_ms * 0.001f;
    spin_angle = fmodf(spin_angle, TAU);
    if (spin_angle < 0.0f) {
        spin_angle += TAU;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float v = clampf((float)y / panel_sy(50.0f) + (field_fbm((float)x * 0.05f, (float)y * 0.12f, 2, 2) - 0.5f) * 0.15f, 0.0f, 1.0f);
            int red;
            int green;
            int blue;
            dusk(v, red, green, blue);
            put(frame, x, y, red, green, blue);
        }
    }
    for (int i = 0; i < 24; ++i) {
        if (sinf(seconds * 2.0f + star_phase[i]) > 0.15f) {
            put(frame, star_x[i], star_y[i], 255, 246, 200);
        }
    }
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        for (int y = city_top[x]; y < PANEL_HEIGHT; ++y) {
            if (y >= panel_iy(58)) {
                put(frame, x, y, 28, 22, 36);
            } else {
                put(frame, x, y, 16, 12, 24);
            }
        }
        if ((x * 5) % 7 == 0) {
            put(frame, x, city_top[x] + 1, 255, 210, 120);
        }
    }
    line(frame, (int)wheel_x(), (int)wheel_y(), (int)(wheel_x() - panel_sx(16.0f)), panel_iy(58), 90, 96, 112);
    line(frame, (int)wheel_x(), (int)wheel_y(), (int)(wheel_x() + panel_sx(16.0f)), panel_iy(58), 90, 96, 112);
    line(frame, (int)(wheel_x() - panel_sx(10.0f)), panel_iy(46), (int)(wheel_x() + panel_sx(10.0f)), panel_iy(46), 124, 130, 150);
    for (int k = 0; k < GONDOLAS; ++k) {
        float angle = spin_angle + (float)k * TAU / GONDOLAS;
        for (float s = 3.0f; s < wheel_r(); s += 1.0f) {
            int x = (int)(wheel_x() + cosf(angle) * s + 0.5f);
            int y = (int)(wheel_y() + sinf(angle) * s + 0.5f);
            put(frame, x, y, 160, 166, 180);
        }
    }
    for (int i = 0; i < 180; ++i) {
        float angle = (float)i / 180.0f * TAU;
        int x = (int)(wheel_x() + cosf(angle) * wheel_r() + 0.5f);
        int y = (int)(wheel_y() + sinf(angle) * wheel_r() + 0.5f);
        put(frame, x, y, 200, 204, 216);
        int x2 = (int)(wheel_x() + cosf(angle) * (wheel_r() - 3.0f) + 0.5f);
        int y2 = (int)(wheel_y() + sinf(angle) * (wheel_r() - 3.0f) + 0.5f);
        put(frame, x2, y2, 100, 106, 124);
    }
    int chase = (int)(seconds * 6.0f);
    for (int i = 0; i < 36; ++i) {
        float angle = spin_angle + (float)i / 36.0f * TAU;
        int x = (int)(wheel_x() + cosf(angle) * wheel_r() + 0.5f);
        int y = (int)(wheel_y() + sinf(angle) * wheel_r() + 0.5f);
        bool on = ((i + chase) & 3) == 0;
        if (on) {
            put(frame, x, y, 255, 246, 200);
        } else if ((i & 1) == 0) {
            put(frame, x, y, 255, 79, 109);
        } else {
            put(frame, x, y, 255, 210, 63);
        }
    }
    put(frame, (int)wheel_x() - 1, (int)wheel_y(), 200, 204, 216);
    put(frame, (int)wheel_x(), (int)wheel_y(), 255, 246, 200);
    put(frame, (int)wheel_x() + 1, (int)wheel_y(), 200, 204, 216);
    for (int k = 0; k < GONDOLAS; ++k) {
        float angle = spin_angle + ((float)k + 0.5f) * TAU / GONDOLAS;
        float swing = sinf(seconds * 1.3f + (float)k) * 0.6f;
        int x = (int)(wheel_x() + cosf(angle) * wheel_r() + swing + 0.5f);
        int y = (int)(wheel_y() + sinf(angle) * wheel_r() + 0.5f);
        const uint8_t *colour = GONDOLA[k % 6];
        put(frame, x, y, 180, 186, 200);
        for (int gx = -2; gx <= 2; ++gx) {
            put(frame, x + gx, y + 1, colour[0], colour[1], colour[2]);
            put(frame, x + gx, y + 2, colour[0] * 3 / 4, colour[1] * 3 / 4, colour[2] * 3 / 4);
        }
        put(frame, x - 1, y + 1, 255, 236, 170);
        put(frame, x + 1, y + 1, 255, 236, 170);
    }
}
