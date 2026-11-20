// A lighthouse on the rocks in a storm, its beam sweeping through the rain.
// The layout and the motion follow Lighthouse Storm at
// https://www.ascii-magic.com/ascii/lighthouse-storm
// The encoder speeds the beam. The sea and the spray keep their own pace.
// Yellow strikes a new bolt. Orange thickens the rain, up for a second, held for a second, then down for a second. Holding orange keeps it thick.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr int HORIZON = 36;
constexpr int LAMP_X = 44;
constexpr int LAMP_Y = 16;
constexpr float BASE_SPIN = 1.2f;
constexpr float RATE_FLOOR = 0.35f;
constexpr float RATE_CAP = 3.5f;
constexpr float TAU = 6.2831853f;

struct Drop {
    float x;
    float y;
    float speed;
    int bright;
};

struct Spray {
    float vx;
    float vy;
    float delay;
};

static float spin_rate = 1.0f;
static float spin_angle = 0.0f;
static float rock_top[PANEL_WIDTH];
static int bolt_x[40];
static int bolt_y[40];
static int bolt_count = 0;
static Drop drops[120];
static Spray spray[36];
static uint32_t rng_state = 1;
static int bolt_cycle = -1;
static int strike_ms = -1;
static bool rain_held = false;
static int rain_phase = 0;
static int rain_ms = 0;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static int clamp8(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, clamp8(red), clamp8(green), clamp8(blue));
}

static void make_bolt() {
    bolt_count = 0;
    float bx = 8.0f + rnd() * 28.0f;
    for (int y = 0; y < HORIZON && bolt_count < 40; ++y) {
        bolt_x[bolt_count] = (int)(bx + 0.5f);
        bolt_y[bolt_count] = y;
        bolt_count += 1;
        bx += (rnd() - 0.5f) * 3.2f;
        if (bx < 2.0f) {
            bx = 2.0f;
        }
        if (bx > PANEL_WIDTH - 3.0f) {
            bx = PANEL_WIDTH - 3.0f;
        }
    }
}

static float rain_boost() {
    if (rain_phase == 1) {
        return rain_ms / 1000.0f;
    }
    if (rain_phase == 2) {
        return 1.0f;
    }
    if (rain_phase == 3) {
        return 1.0f - rain_ms / 1000.0f;
    }
    return 0.0f;
}

static void advance_rain(int dt_ms) {
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    if (rain_phase == 1) {
        rain_ms += dt_ms;
        if (rain_ms >= 1000) {
            rain_phase = 2;
            rain_ms = 0;
        }
    } else if (rain_phase == 2) {
        if (rain_ms < 1000) {
            rain_ms += dt_ms;
        }
        if (rain_ms >= 1000 && !rain_held) {
            rain_phase = 3;
            rain_ms = 0;
        }
    } else if (rain_phase == 3) {
        rain_ms += dt_ms;
        if (rain_ms >= 1000) {
            rain_phase = 0;
            rain_ms = 0;
        }
    }
}

static void paint_tower(uint8_t *frame, float lamp) {
    for (int y = LAMP_Y + 4; y < 42; ++y) {
        float half = 2.2f + (float)(y - LAMP_Y) * 0.12f;
        int band = ((y - LAMP_Y - 4) / 3) % 2;
        for (int x = (int)(LAMP_X - half); x <= (int)(LAMP_X + half); ++x) {
            int side = x > LAMP_X ? 1 : 0;
            if (band == 0) {
                put(frame, x, y, 150 + side * 50, 24, 22);
            } else {
                put(frame, x, y, 170 + side * 50, 176 + side * 40, 184 + side * 40);
            }
        }
    }
    for (int x = LAMP_X - 4; x <= LAMP_X + 4; ++x) {
        put(frame, x, LAMP_Y + 3, 30, 34, 40);
    }
    for (int y = LAMP_Y; y <= LAMP_Y + 2; ++y) {
        put(frame, LAMP_X - 3, y, 36, 40, 48);
        put(frame, LAMP_X + 3, y, 36, 40, 48);
    }
    int glow = (int)(180 + lamp * 75);
    for (int y = LAMP_Y; y <= LAMP_Y + 1; ++y) {
        for (int x = LAMP_X - 2; x <= LAMP_X + 2; ++x) {
            put(frame, x, y, glow, glow - 10, (int)(120 + lamp * 80));
        }
    }
    put(frame, LAMP_X, LAMP_Y - 2, 40, 44, 52);
    put(frame, LAMP_X - 1, LAMP_Y - 1, 24, 28, 34);
    put(frame, LAMP_X, LAMP_Y - 1, 24, 28, 34);
    put(frame, LAMP_X + 1, LAMP_Y - 1, 24, 28, 34);
}

}  // namespace

void lighthouse_start() {
    spin_rate = 1.0f;
    spin_angle = 0.0f;
    rng_state = 41;
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        float dist = fabsf((float)(x - LAMP_X - 2));
        if (dist < 16.0f) {
            rock_top[x] = 40.0f + dist * dist * 0.045f + (field_noise(x * 0.45f, 0.0f, 3) - 0.5f) * 3.0f;
        } else {
            rock_top[x] = 80.0f;
        }
    }
    bolt_cycle = -1;
    strike_ms = -1;
    rain_held = false;
    rain_phase = 0;
    rain_ms = 0;
    make_bolt();
    for (int i = 0; i < 120; ++i) {
        drops[i].x = rnd() * PANEL_WIDTH;
        drops[i].y = rnd() * PANEL_HEIGHT;
        drops[i].speed = 28.0f + rnd() * 16.0f;
        drops[i].bright = rnd() < 0.3f ? 1 : 0;
    }
    for (int i = 0; i < 36; ++i) {
        spray[i].vx = (rnd() - 0.5f) * 2.0f;
        spray[i].vy = 5.0f + rnd() * 8.0f;
        spray[i].delay = rnd() * 0.6f;
    }
}

void lighthouse_strike() {
    strike_ms = 0;
    make_bolt();
}

void lighthouse_rain_press() {
    if (rain_phase == 3) {
        rain_phase = 1;
        rain_ms = 1000 - rain_ms;
    } else if (rain_phase == 0) {
        rain_phase = 1;
        rain_ms = 0;
    }
}

void lighthouse_rain_held(bool held) {
    rain_held = held;
}

void lighthouse_spin(int steps) {
    spin_rate += (float)steps * 0.25f;
    if (spin_rate < RATE_FLOOR) {
        spin_rate = RATE_FLOOR;
    }
    if (spin_rate > RATE_CAP) {
        spin_rate = RATE_CAP;
    }
}

void lighthouse_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
    spin_angle += BASE_SPIN * spin_rate * (float)dt_ms * 0.001f;
    spin_angle = fmodf(spin_angle, TAU);
    if (spin_angle < 0.0f) {
        spin_angle += TAU;
    }
    float dir_x = cosf(spin_angle);
    float toward = sinf(spin_angle);
    float reach = 48.0f * fabsf(dir_x);
    advance_rain(dt_ms);
    int cycle = (int)(seconds / 7.0f);
    if (cycle != bolt_cycle) {
        bolt_cycle = cycle;
        make_bolt();
    }
    float flash_t = fmodf(seconds, 7.0f);
    float flash = (flash_t < 0.12f || (flash_t > 0.22f && flash_t < 0.3f)) ? 1.0f : 0.0f;
    if (strike_ms >= 0) {
        float struck = strike_ms * 0.001f;
        if (struck < 0.12f || (struck > 0.22f && struck < 0.3f)) {
            flash = 1.0f;
        }
        strike_ms += dt_ms;
        if (strike_ms > 320) {
            strike_ms = -1;
        }
    }

    for (int y = 0; y < HORIZON; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float n = field_fbm(x * 0.06f - seconds * 0.35f, y * 0.2f + seconds * 0.05f, 3, 2);
            float sky = clampf(0.08f + (n - 0.35f) * 1.3f + (float)y / HORIZON * 0.25f + flash * 0.45f, 0.0f, 1.0f);
            float dx = (float)(x - LAMP_X);
            float dy = (float)(y - LAMP_Y);
            float beam = 0.0f;
            if ((dx > 0.0f) == (dir_x > 0.0f) && fabsf(dx) < reach) {
                float spread = 0.7f + fabsf(dx) * 0.16f;
                float along = 1.0f - fabsf(dx) / (reach + 1.0f);
                beam = expf(-(dy * dy) / (spread * spread)) * along * (0.45f + 0.55f * fabsf(dir_x));
            }
            float dist = sqrtf(dx * dx * 0.25f + dy * dy);
            float glare = expf(-dist / 3.2f) * fmaxf(0.0f, toward) * 1.1f;
            if (glare > beam) {
                beam = glare;
            }
            int red;
            int green;
            int blue;
            if (beam > 0.16f) {
                float lit = clampf(beam * 0.85f + n * 0.2f, 0.0f, 1.0f);
                red = (int)(90 + lit * 165);
                green = (int)(80 + lit * 155);
                blue = (int)(50 + lit * 90);
            } else {
                red = (int)(8 + sky * 70);
                green = (int)(12 + sky * 80);
                blue = (int)(18 + sky * 100);
            }
            put(frame, x, y, red, green, blue);
        }
    }
    if (flash > 0.0f) {
        for (int i = 0; i < bolt_count; ++i) {
            put(frame, bolt_x[i], bolt_y[i], 210, 220, 255);
            put(frame, bolt_x[i] + 1, bolt_y[i], 140, 160, 220);
        }
    }
    for (int y = HORIZON; y < PANEL_HEIGHT; ++y) {
        float depth = (float)(y - HORIZON) / (float)(PANEL_HEIGHT - HORIZON);
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float w = field_fbm(x * 0.08f + seconds * 0.45f, y * 0.4f - seconds * 0.28f, 3, 7);
            float swell = sinf(x * 0.22f - seconds * 2.1f + y * 0.8f);
            if (w > 0.62f && swell > 0.15f) {
                int foam = (int)(160 + (w - 0.62f) * 250);
                put(frame, x, y, foam, foam + 10, 255);
                continue;
            }
            float lit = fmaxf(0.0f, -dir_x) * expf(-fabsf((float)(x - LAMP_X + 12)) / 18.0f) * 0.3f;
            float v = clampf(0.15f + depth * 0.35f + (w - 0.5f) * 0.7f + flash * 0.25f + lit, 0.0f, 1.0f);
            put(frame, x, y, (int)(4 + v * 20), (int)(20 + v * 70), (int)(32 + v * 90));
        }
    }
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        int top = (int)ceilf(rock_top[x]);
        for (int y = top; y < PANEL_HEIGHT; ++y) {
            float n = field_noise(x * 0.4f, y * 0.55f, 12);
            int level = (int)(18 + n * 70 + (y == top ? 30 : 0) + flash * 40);
            put(frame, x, y, level, level, level + 6);
        }
    }
    for (int k = 0; k < 2; ++k) {
        float cycle = fmodf(seconds * 0.45f + k * 0.5f, 1.0f);
        int origin = k ? LAMP_X + 14 : LAMP_X - 14;
        int side = k ? 1 : -1;
        for (int i = 0; i < 36; ++i) {
            float age = cycle * 2.0f - spray[i].delay;
            if (age < 0.0f || age > 1.5f) {
                continue;
            }
            int x = (int)(origin + spray[i].vx * age * 6.0f * side);
            int y = (int)(40.0f - spray[i].vy * age + 7.0f * age * age);
            if (y > 48) {
                continue;
            }
            int foam = (int)(200 - age * 80);
            put(frame, x, y, foam, foam + 8, 255);
        }
    }
    int drop_count = 80 + (int)(40.0f * rain_boost() + 0.5f);
    for (int i = 0; i < drop_count; ++i) {
        float y = fmodf(drops[i].y + seconds * drops[i].speed, (float)PANEL_HEIGHT + 2.0f) - 1.0f;
        float x = drops[i].x - y * 0.35f - seconds * 8.0f;
        x = fmodf(x, (float)PANEL_WIDTH);
        if (x < 0.0f) {
            x += PANEL_WIDTH;
        }
        int level = drops[i].bright ? 170 : 110;
        put(frame, (int)x, (int)y, level - 30, level - 10, level);
    }
    float lamp = 0.65f + 0.35f * fmaxf(fabsf(dir_x), toward);
    paint_tower(frame, lamp);
}
