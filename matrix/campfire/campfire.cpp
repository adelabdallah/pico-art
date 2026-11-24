// A campfire under a milky way, with a tent, a pine line, and sparks rising into the sky.
// The layout and the motion follow Campfire Under Stars at
// https://www.ascii-magic.com/ascii/campfire-under-stars

// Orange throws a shower of sparks. Yellow flares the flame. Holding either keeps that going.
// Green sends one comet across the milky way. A hold keeps a meteor shower along that band.
// Letting go spends 3 seconds thinning out new meteors. Each one already in the sky finishes its crossing.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

#define HORIZON (panel_iy(40))
#define FIRE_X (panel_ix(40))
#define FIRE_Y (panel_iy(52))
constexpr float TAU = 6.2831853f;

struct Spark {
    float phase;
    float speed;
    float offset;
    float sway;
};

struct Burst {
    float x;
    float y;
    float vx;
    float life;
    bool on;
};

// The milky way ridge is y = 0.42x - 4, the same line the sky dust uses.
constexpr int METEOR_CAP = 20;
constexpr int METEOR_TAIL = 8;
constexpr int METEOR_FADE_MS = 3000;
constexpr float METEOR_EVERY_MS = 85.0f;
constexpr float BAND_SLOPE = 0.42f;
constexpr float BAND_LIFT = -4.0f;

struct Meteor {
    float x[METEOR_TAIL];
    float y[METEOR_TAIL];
    float vx;
    float vy;
    float wobble;
    float wobble_rate;
    float wobble_amp;
    bool on;
};

static int tree_top[PANEL_WIDTH_MAX];
static Spark sparks[24];
static Burst bursts[40];
static int star_x[70];
static int star_y[70];
static float star_bright[70];
static float star_phase[70];
static uint32_t rng_state = 1;
static bool shower_held = false;
static bool flare_held = false;
static int flare_phase = 0;
static int flare_ms = 0;
static Meteor meteors[METEOR_CAP];
static bool meteor_held = false;
static int meteor_fade_ms = -1;
static float meteor_wait_ms = 0.0f;

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

static void sky_at(float v, int &red, int &green, int &blue) {
    red = (int)(2 + v * 26);
    green = (int)(3 + v * 36);
    blue = (int)(12 + v * 70);
}

static void dust_at(float v, int &red, int &green, int &blue) {
    red = (int)(26 + v * 200);
    green = (int)(20 + v * 190);
    blue = (int)(64 + v * 170);
}

static void ground_at(float v, int &red, int &green, int &blue) {
    red = (int)(8 + v * 150);
    green = (int)(6 + v * 55);
    blue = (int)(8 + v * 20);
}

static void fire_at(float v, int &red, int &green, int &blue) {
    if (v < 0.35f) {
        float t = v / 0.35f;
        red = (int)(90 + t * 80);
        green = (int)(12 + t * 40);
        blue = 6;
        return;
    }
    if (v < 0.7f) {
        float t = (v - 0.35f) / 0.35f;
        red = (int)(170 + t * 85);
        green = (int)(52 + t * 110);
        blue = (int)(6 + t * 20);
        return;
    }
    float t = (v - 0.7f) / 0.3f;
    red = 255;
    green = (int)(162 + t * 80);
    blue = (int)(26 + t * 160);
}

static void paint_tent(uint8_t *frame, float flick) {
    int apex_y = panel_iy(32);
    int base_y = panel_iy(48);
    int cx = panel_ix(16);
    for (int y = apex_y; y <= base_y; ++y) {
        float down = (float)(y - apex_y) / (float)(base_y - apex_y);
        int half = (int)(down * panel_sx(12.0f) + 0.5f);
        for (int x = cx - half; x <= cx + half; ++x) {
            float side = half == 0 ? 0.0f : (float)(x - cx) / (float)half;
            float lit = clampf(0.25f + side * 0.55f * flick + down * 0.15f, 0.0f, 1.0f);
            bool door = fabsf((float)(x - cx)) < down * panel_sx(4.0f) && y > base_y - panel_iy(8);
            if (door) {
                put(frame, x, y, 18, 12, 16);
            } else {
                put(frame, x, y, (int)(20 + lit * 140), (int)(14 + lit * 50), (int)(16 + lit * 20));
            }
        }
    }
    put(frame, cx, apex_y - 1, 40, 28, 24);
}

static void paint_fire(uint8_t *frame, float seconds, float flick, float flare) {
    int reach = panel_iy(16) + (int)(flare * panel_sy(8.0f));
    float wide = 1.0f + flare * 0.5f;
    float hot = flick * (1.0f + flare * 0.9f);
    for (int y = FIRE_Y - reach; y <= FIRE_Y; ++y) {
        float height = (float)(FIRE_Y - y) / (float)reach;
        float width = ((1.0f - height) * panel_sx(7.0f) * (0.65f + 0.35f * height) + panel_sx(0.6f)) * wide;
        float lean = sinf(seconds * 3.0f + y * 0.45f) * height * panel_sx(1.4f);
        for (int x = FIRE_X - panel_ix(10); x <= FIRE_X + panel_ix(10); ++x) {
            float n = field_noise(x * 0.55f, y * 0.5f + seconds * 6.0f, 3);
            n = n * 0.65f + field_noise(x * 1.1f, y * 0.9f + seconds * 9.0f, 8) * 0.35f;
            float edge = 1.0f - fabsf((float)(x - FIRE_X) - lean) / width;
            float v = edge * 1.15f - height * 0.45f + (n - 0.5f) * 0.85f;
            if (v > 0.08f) {
                int red;
                int green;
                int blue;
                fire_at(clampf(v * hot, 0.0f, 1.0f), red, green, blue);
                put(frame, x, y, red, green, blue);
            }
        }
    }
    put(frame, FIRE_X - panel_ix(5), FIRE_Y + panel_iy(1), 70, 36, 16);
    put(frame, FIRE_X - panel_ix(4), FIRE_Y + panel_iy(1), 110, 58, 24);
    put(frame, FIRE_X - panel_ix(3), FIRE_Y + panel_iy(2), 80, 42, 18);
    put(frame, FIRE_X + panel_ix(3), FIRE_Y + panel_iy(1), 120, 64, 28);
    put(frame, FIRE_X + panel_ix(4), FIRE_Y + panel_iy(2), 64, 32, 14);
    put(frame, FIRE_X + panel_ix(5), FIRE_Y + panel_iy(1), 90, 48, 20);
    for (int step = -7; step <= 7; step += 3) {
        put(frame, FIRE_X + panel_ix(step), FIRE_Y + panel_iy(3), 50, 32, 22);
        put(frame, FIRE_X + panel_ix(step + 1), FIRE_Y + panel_iy(3), 36, 24, 18);
    }
}

static void paint_sparks(uint8_t *frame, float seconds) {
    for (int i = 0; i < 24; ++i) {
        Spark &spark = sparks[i];
        float life = fmodf(seconds * 0.35f * spark.speed + spark.offset, 1.0f);
        if (life < 0.0f) {
            life += 1.0f;
        }
        if (sinf(seconds * 18.0f + spark.phase * 7.0f) < -0.25f) {
            continue;
        }
        int x = (int)(FIRE_X + sinf(spark.phase + seconds * 1.4f) * panel_sx(1.0f + life * 5.0f) + (spark.sway - 0.5f) * life * panel_sx(12.0f));
        int y = (int)(FIRE_Y - panel_sy(8.0f) - life * panel_sy(28.0f));
        int red;
        int green;
        int blue;
        fire_at(clampf(1.0f - life, 0.35f, 1.0f), red, green, blue);
        put(frame, x, y, red, green, blue);
    }
}

static void spawn_burst() {
    int slot = -1;
    for (int i = 0; i < 40; ++i) {
        if (!bursts[i].on) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = (int)(rnd() * 40.0f);
        if (slot > 39) {
            slot = 39;
        }
    }
    Burst &burst = bursts[slot];
    burst.on = true;
    burst.life = 0.0f;
    burst.x = (float)FIRE_X + (rnd() - 0.5f) * 6.0f;
    burst.y = (float)(FIRE_Y - 8);
    burst.vx = (rnd() - 0.5f) * 10.0f;
}

static float flare_level() {
    if (flare_phase == 1) {
        return flare_ms / 400.0f;
    }
    if (flare_phase == 2) {
        return 1.0f;
    }
    if (flare_phase == 3) {
        return 1.0f - flare_ms / 600.0f;
    }
    return 0.0f;
}

static void advance_flare(int dt_ms) {
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    if (flare_phase == 1) {
        flare_ms += dt_ms;
        if (flare_ms >= 400) {
            flare_phase = 2;
            flare_ms = 0;
        }
    } else if (flare_phase == 2) {
        if (flare_ms < 500) {
            flare_ms += dt_ms;
        }
        if (flare_ms >= 500 && !flare_held) {
            flare_phase = 3;
            flare_ms = 0;
        }
    } else if (flare_phase == 3) {
        flare_ms += dt_ms;
        if (flare_ms >= 600) {
            flare_phase = 0;
            flare_ms = 0;
        }
    }
}

static void paint_bursts(uint8_t *frame, int dt_ms) {
    if (shower_held) {
        int born = dt_ms / 45;
        if (born < 1 && dt_ms > 0) {
            born = 1;
        }
        for (int i = 0; i < born; ++i) {
            spawn_burst();
        }
    }
    float step = dt_ms * 0.001f;
    for (int i = 0; i < 40; ++i) {
        Burst &burst = bursts[i];
        if (!burst.on) {
            continue;
        }
        burst.life += step * 0.75f;
        burst.x += burst.vx * step;
        burst.y -= (18.0f + (1.0f - burst.life) * 16.0f) * step;
        burst.vx *= 0.98f;
        if (burst.life >= 1.0f) {
            burst.on = false;
            continue;
        }
        int red;
        int green;
        int blue;
        fire_at(clampf(1.0f - burst.life, 0.4f, 1.0f), red, green, blue);
        put(frame, (int)burst.x, (int)burst.y, red, green, blue);
    }
}

static bool spawn_meteor() {
    int slot = -1;
    for (int i = 0; i < METEOR_CAP; ++i) {
        if (!meteors[i].on) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return false;
    }
    float slope = BAND_SLOPE + (rnd() - 0.5f) * 0.36f;
    float speed = panel_sx(32.0f + rnd() * 26.0f);
    float span = sqrtf(1.0f + slope * slope);
    Meteor &meteor = meteors[slot];
    meteor.on = true;
    meteor.vx = speed / span;
    meteor.vy = speed * slope / span;
    meteor.wobble = rnd() * TAU;
    meteor.wobble_rate = 1.6f + rnd() * 3.4f;
    meteor.wobble_amp = 5.0f + rnd() * 11.0f;
    float head_x = panel_sx(-8.0f) + rnd() * panel_sx(30.0f);
    float head_y = BAND_SLOPE * head_x + panel_sy(BAND_LIFT) + (rnd() - 0.5f) * panel_sy(14.0f);
    float ux = meteor.vx / speed;
    float uy = meteor.vy / speed;
    for (int i = 0; i < METEOR_TAIL; ++i) {
        meteor.x[i] = head_x - ux * i * 1.8f;
        meteor.y[i] = head_y - uy * i * 1.8f;
    }
    return true;
}

static void meteor_colour(float along, int &red, int &green, int &blue) {
    float glow = along * along;
    red = (int)(36.0f + glow * 219.0f);
    green = (int)(64.0f + glow * 191.0f);
    blue = (int)(120.0f + glow * 135.0f);
}

static void paint_meteors(uint8_t *frame, int dt_ms) {
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    float rate = 0.0f;
    bool fade_done = false;
    if (meteor_held) {
        rate = 1.0f;
    } else if (meteor_fade_ms >= 0) {
        rate = 1.0f - (float)meteor_fade_ms / (float)METEOR_FADE_MS;
        if (rate < 0.0f) {
            rate = 0.0f;
        }
        meteor_fade_ms += dt_ms;
        if (meteor_fade_ms >= METEOR_FADE_MS) {
            meteor_fade_ms = -1;
            fade_done = true;
        }
    }
    if (rate > 0.0f) {
        meteor_wait_ms += (float)dt_ms * rate;
        int guard = 0;
        while (meteor_wait_ms >= METEOR_EVERY_MS && guard < METEOR_CAP) {
            if (!spawn_meteor()) {
                meteor_wait_ms = METEOR_EVERY_MS;
                break;
            }
            meteor_wait_ms -= METEOR_EVERY_MS;
            guard += 1;
        }
    }
    if (fade_done) {
        meteor_wait_ms = 0.0f;
    }
    float step = dt_ms * 0.001f;
    for (int i = 0; i < METEOR_CAP; ++i) {
        Meteor &meteor = meteors[i];
        if (!meteor.on) {
            continue;
        }
        float speed = sqrtf(meteor.vx * meteor.vx + meteor.vy * meteor.vy);
        float sway = 0.0f;
        float side_x = 0.0f;
        float side_y = 0.0f;
        if (speed > 0.001f) {
            side_x = -meteor.vy / speed;
            side_y = meteor.vx / speed;
            sway = sinf(meteor.wobble) * meteor.wobble_amp;
        }
        if (step > 0.0f) {
            float next_x = meteor.x[0] + (meteor.vx + side_x * sway) * step;
            float next_y = meteor.y[0] + (meteor.vy + side_y * sway) * step;
            meteor.wobble += meteor.wobble_rate * step;
            for (int tail = METEOR_TAIL - 1; tail > 0; --tail) {
                meteor.x[tail] = meteor.x[tail - 1];
                meteor.y[tail] = meteor.y[tail - 1];
            }
            meteor.x[0] = next_x;
            meteor.y[0] = next_y;
        }
        for (int tail = METEOR_TAIL - 1; tail >= 0; --tail) {
            int px = (int)lroundf(meteor.x[tail]);
            int py = (int)lroundf(meteor.y[tail]);
            if (py < 0 || py >= HORIZON) {
                continue;
            }
            float along = 1.0f - (float)tail / (float)(METEOR_TAIL - 1);
            int red;
            int green;
            int blue;
            meteor_colour(along, red, green, blue);
            put(frame, px, py, red, green, blue);
        }
        if (meteor.x[METEOR_TAIL - 1] > PANEL_WIDTH + 1.0f || meteor.y[METEOR_TAIL - 1] > HORIZON + 1.0f || meteor.x[0] < -24.0f) {
            meteor.on = false;
        }
    }
}

}  // namespace

void campfire_start() {
    rng_state = 17;
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        float pine = fabsf(fmodf(x * 0.37f, 1.0f) - 0.5f) * 2.0f;
        float tall = (1.0f - pine) * panel_sy(3.0f + field_fbm(x * 0.12f, 0.0f, 2, 5) * 7.0f);
        tree_top[x] = HORIZON - 1 - (int)(tall + 0.5f);
    }
    for (int i = 0; i < 70; ++i) {
        star_x[i] = (int)(rnd() * PANEL_WIDTH);
        star_y[i] = (int)(rnd() * (HORIZON - 6));
        star_bright[i] = rnd();
        star_phase[i] = rnd() * TAU;
    }
    for (int i = 0; i < 24; ++i) {
        sparks[i].phase = rnd() * TAU;
        sparks[i].speed = 1.0f + rnd() * 2.0f;
        sparks[i].offset = rnd() * 3.0f;
        sparks[i].sway = rnd();
    }
    for (int i = 0; i < 40; ++i) {
        bursts[i].on = false;
    }
    shower_held = false;
    flare_held = false;
    flare_phase = 0;
    flare_ms = 0;
    for (int i = 0; i < METEOR_CAP; ++i) {
        meteors[i].on = false;
    }
    meteor_held = false;
    meteor_fade_ms = -1;
    meteor_wait_ms = 0.0f;
}

void campfire_spark_burst() {
    for (int i = 0; i < 16; ++i) {
        spawn_burst();
    }
}

void campfire_shower(bool held) {
    shower_held = held;
}

void campfire_flare_press() {
    if (flare_phase == 3) {
        float left = 1.0f - flare_ms / 600.0f;
        flare_phase = 1;
        flare_ms = (int)(left * 400.0f);
    } else if (flare_phase == 0) {
        flare_phase = 1;
        flare_ms = 0;
    }
}

void campfire_flare_held(bool held) {
    flare_held = held;
}

void campfire_meteor() {
    spawn_meteor();
}

void campfire_meteor_held(bool held) {
    if (held) {
        if (!meteor_held) {
            meteor_wait_ms = 0.0f;
        }
        meteor_held = true;
        meteor_fade_ms = -1;
        return;
    }
    if (meteor_held) {
        meteor_held = false;
        meteor_fade_ms = 0;
    }
}

void campfire_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
    advance_flare(dt_ms);
    float flare = flare_level();
    float flick = 0.85f + 0.15f * sinf(seconds * 13.0f) * sinf(seconds * 7.3f);
    float glow = flick * (1.0f + flare * 0.8f);
    for (int y = 0; y < HORIZON; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float ridge = panel_sy((x * 64.0f / (float)PANEL_WIDTH) * 0.42f - 4.0f);
            float band = (y - ridge) / panel_sy(5.5f);
            float dust = expf(-band * band) * (0.35f + field_fbm(x * 0.08f + seconds * 0.02f, y * 0.18f, 3, 3));
            int red;
            int green;
            int blue;
            if (dust > 0.42f) {
                dust_at(clampf((dust - 0.3f) * 0.9f, 0.0f, 1.0f), red, green, blue);
            } else {
                float n = field_fbm(x * 0.07f, y * 0.2f, 2, 9);
                sky_at(clampf((float)y / HORIZON * 0.7f + n * 0.25f, 0.0f, 1.0f), red, green, blue);
            }
            put(frame, x, y, red, green, blue);
        }
    }
    for (int i = 0; i < 70; ++i) {
        float twinkle = 0.5f + 0.5f * sinf(seconds * 2.2f + star_phase[i]);
        float v = star_bright[i] * 0.55f + twinkle * 0.45f;
        if (v > 0.4f) {
            int level = (int)(140 + v * 115);
            put(frame, star_x[i], star_y[i], level, level, (int)(level * 0.9f + 20));
        }
    }
    paint_meteors(frame, dt_ms);
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        for (int y = tree_top[x]; y < HORIZON + 1; ++y) {
            put(frame, x, y, 3, 6, 8);
        }
    }
    for (int y = HORIZON; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = (float)(x - FIRE_X);
            float dy = (float)(y - FIRE_Y) * 1.15f;
            float dist = sqrtf(dx * dx + dy * dy);
            float n = field_fbm(x * 0.18f, y * 0.4f, 2, 4);
            float v = clampf(expf(-dist / panel_s(14.0f)) * glow * 1.2f + (n - 0.5f) * 0.2f, 0.0f, 1.0f);
            int red;
            int green;
            int blue;
            ground_at(v, red, green, blue);
            put(frame, x, y, red, green, blue);
        }
    }
    paint_tent(frame, glow);
    paint_fire(frame, seconds, flick, flare);
    paint_sparks(frame, seconds);
    paint_bursts(frame, dt_ms);
}
