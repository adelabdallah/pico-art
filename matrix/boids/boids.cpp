// A flock over a meadow. Birds steer by separation, alignment, and cohesion.
// A hawk dives through and the flock splits, then it regroups.
// The layout and the motion follow Boids Over Meadow at
// https://www.ascii-magic.com/ascii/boids-flock
// The meadow fills the panel. The buttons keep their shared actions.

#include "pattern.hpp"
#include "noise.hpp"

#include <cstring>

namespace {

constexpr int BIRDS = 110;
constexpr int TRAIL = 7;
constexpr int HAWK_TRAIL = 14;
constexpr int COLS = 96;
constexpr int ROWS = 34;
// The piece's world. A character cell is one unit wide and two units tall.
constexpr float WORLD_W = 96.0f;
constexpr float WORLD_H = 68.0f;
constexpr float DT = 1.0f / 30.0f;
constexpr int STEP_MS = 33;
constexpr int EPOCH_MS = 60000;
constexpr float NEIGHBOUR = 110.0f;
constexpr float SEPARATE = 6.5f;
constexpr float SCARE = 200.0f;
constexpr float TAU = 6.2831853f;

// Meadow colours from #08140e up to #18321e, set so the panel still shows the field.
static const uint8_t MEADOW[5][3] = {
    {48, 76, 59},
    {55, 89, 71},
    {68, 104, 85},
    {76, 116, 96},
    {84, 126, 104},
};

static const uint8_t TRAIL_RGB[3][3] = {
    {42, 90, 106},
    {74, 144, 168},
    {138, 208, 232},
};

static const uint8_t HAWK_TRAIL_RGB[2][3] = {
    {90, 42, 26},
    {138, 58, 30},
};

// Screen steps for → ↗ ↑ ↖ ← ↙ ↓ ↘. Y grows downward.
static const int STEP_X[8] = {1, 1, 0, -1, -1, -1, 0, 1};
static const int STEP_Y[8] = {0, -1, -1, -1, 0, 1, 1, 1};

struct Bird {
    float x;
    float y;
    float vx;
    float vy;
    float tx[TRAIL];
    float ty[TRAIL];
};

static Bird birds[BIRDS];
static float hawk[4];
static float hawk_tx[HAWK_TRAIL];
static float hawk_ty[HAWK_TRAIL];
static uint8_t *meadow = nullptr;
static uint32_t rng_state = 1;
static int step_n = 0;
static int epoch = -1;
static int pending_ms = 0;

static void rng_seed(uint32_t seed) {
    rng_state = seed;
}

// mulberry32, the same generator the piece uses to place the flock.
static float rnd() {
    rng_state += 0x6d2b79f5u;
    uint32_t t = rng_state;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return (float)((double)(t ^ (t >> 14)) * (1.0 / 4294967296.0));
}

static float wrap_unit(float value, float limit) {
    float wrapped = fmodf(value, limit);
    if (wrapped < 0.0f) {
        wrapped += limit;
    }
    return wrapped;
}

static float wrap_delta(float delta, float limit) {
    if (delta > limit * 0.5f) {
        return delta - limit;
    }
    if (delta < -limit * 0.5f) {
        return delta + limit;
    }
    return delta;
}

static int wrap_index(int value, int limit) {
    int wrapped = value % limit;
    if (wrapped < 0) {
        wrapped += limit;
    }
    return wrapped;
}

static int to_px(float x) {
    int px = (int)floorf(x * (float)PANEL_WIDTH / WORLD_W);
    return wrap_index(px, PANEL_WIDTH);
}

static int to_py(float y) {
    int py = (int)floorf(y * (float)PANEL_HEIGHT / WORLD_H);
    return wrap_index(py, PANEL_HEIGHT);
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    x = wrap_index(x, PANEL_WIDTH);
    y = wrap_index(y, PANEL_HEIGHT);
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

static void meadow_rgb(int col, int row, float shade, int &red, int &green, int &blue) {
    int band = (int)(shade * 6.0f);
    if (band < 0) {
        band = 0;
    }
    if (band > 4) {
        band = 4;
    }
    red = MEADOW[band][0];
    green = MEADOW[band][1];
    blue = MEADOW[band][2];
    // Denser glyphs in the piece are ▒, then ░, then a stipple of quotes on the open field.
    if (shade > 0.62f) {
        red += 4;
        green += 4;
        blue += 4;
    } else if (shade > 0.48f) {
        red += 2;
        green += 2;
        blue += 2;
    } else if (((col * 3 + row * 5) % 4) == 0) {
        red += 1;
        green += 2;
        blue += 1;
    }
}

static void bake_meadow() {
    meadow = pattern_alloc(PANEL_PIXELS * 3);
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        int row = y * ROWS / PANEL_HEIGHT;
        if (row >= ROWS) {
            row = ROWS - 1;
        }
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int col = x * COLS / PANEL_WIDTH;
            if (col >= COLS) {
                col = COLS - 1;
            }
            float shade = field_fbm((float)col * 0.06f, (float)row * 0.13f, 4, 2);
            int red;
            int green;
            int blue;
            meadow_rgb(col, row, shade, red, green, blue);
            write_bgr(meadow + (y * PANEL_WIDTH + x) * 3, clamp_channel((float)red), clamp_channel((float)green), clamp_channel((float)blue));
        }
    }
}

static void reset_flock(int cycle) {
    rng_seed((uint32_t)(cycle * 37 + 3));
    for (int i = 0; i < BIRDS; ++i) {
        birds[i].x = rnd() * WORLD_W;
        birds[i].y = rnd() * WORLD_H;
        float angle = rnd() * TAU;
        birds[i].vx = cosf(angle) * 8.0f;
        birds[i].vy = sinf(angle) * 8.0f;
        for (int k = 0; k < TRAIL; ++k) {
            birds[i].tx[k] = birds[i].x;
            birds[i].ty[k] = birds[i].y;
        }
    }
    hawk[0] = WORLD_W * 0.1f;
    hawk[1] = WORLD_H * 0.5f;
    hawk[2] = 6.0f;
    hawk[3] = 0.0f;
    step_n = 0;
    for (int k = 0; k < HAWK_TRAIL; ++k) {
        hawk_tx[k] = hawk[0];
        hawk_ty[k] = hawk[1];
    }
}

static void limit_speed(float &vx, float &vy, float low, float high) {
    float speed = hypotf(vx, vy);
    float limited = clampf(speed, low, high);
    float scale = limited / (speed > 0.0f ? speed : 1.0f);
    vx *= scale;
    vy *= scale;
}

static void step_flock() {
    float hawk_x = hawk[0];
    float hawk_y = hawk[1];
    for (int i = 0; i < BIRDS; ++i) {
        float align_x = 0.0f;
        float align_y = 0.0f;
        float gather_x = 0.0f;
        float gather_y = 0.0f;
        float push_x = 0.0f;
        float push_y = 0.0f;
        int near = 0;
        for (int j = 0; j < BIRDS; ++j) {
            if (j == i) {
                continue;
            }
            float dx = wrap_delta(birds[j].x - birds[i].x, WORLD_W);
            float dy = wrap_delta(birds[j].y - birds[i].y, WORLD_H);
            float dist2 = dx * dx + dy * dy;
            if (dist2 > NEIGHBOUR) {
                continue;
            }
            near += 1;
            align_x += birds[j].vx;
            align_y += birds[j].vy;
            gather_x += dx;
            gather_y += dy;
            if (dist2 < SEPARATE) {
                push_x -= dx / (dist2 + 0.1f);
                push_y -= dy / (dist2 + 0.1f);
            }
        }
        float force_x = push_x * 14.0f;
        float force_y = push_y * 14.0f;
        if (near > 0) {
            float count = (float)near;
            force_x += (align_x / count - birds[i].vx) * 1.1f + (gather_x / count) * 1.4f;
            force_y += (align_y / count - birds[i].vy) * 1.1f + (gather_y / count) * 1.4f;
        }
        float away_x = wrap_delta(birds[i].x - hawk_x, WORLD_W);
        float away_y = wrap_delta(birds[i].y - hawk_y, WORLD_H);
        float scare2 = away_x * away_x + away_y * away_y;
        if (scare2 < SCARE) {
            float reach = sqrtf(scare2 + 1.0f);
            force_x += (away_x / reach) * 60.0f;
            force_y += (away_y / reach) * 60.0f;
        }
        birds[i].vx += force_x * DT;
        birds[i].vy += force_y * DT;
        limit_speed(birds[i].vx, birds[i].vy, 7.0f, 16.0f);
        birds[i].x = wrap_unit(birds[i].x + birds[i].vx * DT, WORLD_W);
        birds[i].y = wrap_unit(birds[i].y + birds[i].vy * DT, WORLD_H);
    }

    // The hawk hunts for 7 seconds of every 12, then breaks off and circles.
    float phase = fmodf((float)step_n / 30.0f, 12.0f);
    bool hunt = phase < 7.0f;
    float aim_x = 0.0f;
    float aim_y = 0.0f;
    float best = 1.0e9f;
    for (int i = 0; i < BIRDS; i += 3) {
        float dx = wrap_delta(birds[i].x - hawk_x, WORLD_W);
        float dy = wrap_delta(birds[i].y - hawk_y, WORLD_H);
        float dist2 = dx * dx + dy * dy;
        if (dist2 < best) {
            best = dist2;
            aim_x = dx;
            aim_y = dy;
        }
    }
    float hvx = hawk[2];
    float hvy = hawk[3];
    if (hunt) {
        float reach = hypotf(aim_x, aim_y);
        if (reach < 1.0f) {
            reach = 1.0f;
        }
        hvx += (aim_x / reach) * 40.0f * DT;
        hvy += (aim_y / reach) * 40.0f * DT;
    } else {
        hvx += -hvy * 0.6f * DT;
        hvy += hvx * 0.6f * DT;
    }
    limit_speed(hvx, hvy, 6.0f, hunt ? 18.0f : 10.0f);
    hawk[0] = wrap_unit(hawk[0] + hvx * DT, WORLD_W);
    hawk[1] = wrap_unit(hawk[1] + hvy * DT, WORLD_H);
    hawk[2] = hvx;
    hawk[3] = hvy;
    step_n += 1;
    if (step_n % 3 == 0) {
        int slot = (step_n / 3) % TRAIL;
        for (int i = 0; i < BIRDS; ++i) {
            birds[i].tx[slot] = birds[i].x;
            birds[i].ty[slot] = birds[i].y;
        }
        int hawk_slot = (step_n / 3) % HAWK_TRAIL;
        hawk_tx[hawk_slot] = hawk[0];
        hawk_ty[hawk_slot] = hawk[1];
    }
}

static void bird_rgb(int index, int &red, int &green, int &blue) {
    if (index % 7 == 0) {
        red = 168;
        green = 200;
        blue = 255;
        return;
    }
    if (index % 3 == 0) {
        red = 255;
        green = 255;
        blue = 255;
        return;
    }
    red = 216;
    green = 232;
    blue = 255;
}

static int bird_steps(int dir) {
    int steps = 2;
    if (STEP_Y[dir] != 0) {
        int tall = (int)((float)PANEL_HEIGHT / (float)ROWS + 0.5f);
        if (tall > steps) {
            steps = tall;
        }
    }
    if (STEP_X[dir] != 0) {
        int wide = (int)((float)PANEL_WIDTH / (float)COLS + 0.5f);
        if (wide > steps) {
            steps = wide;
        }
    }
    if (steps > 3) {
        steps = 3;
    }
    return steps;
}

static void paint_trails(uint8_t *frame) {
    int head = step_n / 3;
    for (int age = TRAIL - 1; age >= 1; --age) {
        int slot = wrap_index(head - age, TRAIL);
        int level = 0;
        if (age < 3) {
            level = 2;
        } else if (age < 5) {
            level = 1;
        }
        const uint8_t *colour = TRAIL_RGB[level];
        for (int i = 0; i < BIRDS; ++i) {
            put(frame, to_px(birds[i].tx[slot]), to_py(birds[i].ty[slot]), colour[0], colour[1], colour[2]);
        }
        int hawk_slot = wrap_index(head - age, HAWK_TRAIL);
        int hawk_level = age < 4 ? 1 : 0;
        const uint8_t *hawk_colour = HAWK_TRAIL_RGB[hawk_level];
        put(frame, to_px(hawk_tx[hawk_slot]), to_py(hawk_ty[hawk_slot]), hawk_colour[0], hawk_colour[1], hawk_colour[2]);
    }
}

static void paint_birds(uint8_t *frame) {
    float x_scale = (float)PANEL_WIDTH / WORLD_W;
    float y_scale = (float)PANEL_HEIGHT / WORLD_H;
    for (int i = 0; i < BIRDS; ++i) {
        int red;
        int green;
        int blue;
        bird_rgb(i, red, green, blue);
        float angle = atan2f(-(birds[i].vy * y_scale), birds[i].vx * x_scale);
        int dir = (int)roundf(angle / (3.14159265f * 0.25f));
        dir = wrap_index(dir, 8);
        int steps = bird_steps(dir);
        int head_x = to_px(birds[i].x);
        int head_y = to_py(birds[i].y);
        if (steps >= 3) {
            int back_x = head_x - STEP_X[dir];
            int back_y = head_y - STEP_Y[dir];
            int side_x = STEP_Y[dir];
            int side_y = -STEP_X[dir];
            put(frame, back_x + side_x, back_y + side_y, (red * 3) / 4, (green * 3) / 4, (blue * 3) / 4);
            put(frame, back_x - side_x, back_y - side_y, (red * 3) / 4, (green * 3) / 4, (blue * 3) / 4);
        }
        for (int s = steps - 1; s >= 0; --s) {
            float gain = 0.45f + 0.55f * (float)(steps - s) / (float)steps;
            put(frame, head_x - STEP_X[dir] * s, head_y - STEP_Y[dir] * s, (int)((float)red * gain), (int)((float)green * gain), (int)((float)blue * gain));
        }
    }
}

static void paint_hawk(uint8_t *frame) {
    int x = to_px(hawk[0]);
    int y = to_py(hawk[1]);
    int tall = (int)((float)PANEL_HEIGHT / (float)ROWS + 0.5f);
    if (tall < 1) {
        tall = 1;
    }
    if (tall > 4) {
        tall = 4;
    }
    int wide = (int)((float)PANEL_WIDTH / (float)COLS + 0.5f);
    if (wide < 1) {
        wide = 1;
    }
    int half = tall / 2;
    for (int dy = -half; dy < tall - half; ++dy) {
        put(frame, x, y + dy, 255, 90, 58);
    }
    put(frame, x - wide, y, 255, 176, 74);
    put(frame, x + wide, y, 255, 176, 74);
}

}  // namespace

void boids_start() {
    bake_meadow();
    epoch = -1;
    pending_ms = 0;
}

void boids_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    int cycle = (int)(anim_ms / EPOCH_MS);
    if (cycle != epoch) {
        epoch = cycle;
        reset_flock(cycle);
        pending_ms = 0;
    }
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    pending_ms += dt_ms;
    int steps = 0;
    while (pending_ms >= STEP_MS && steps < 4) {
        pending_ms -= STEP_MS;
        step_flock();
        steps += 1;
    }
    if (meadow != nullptr) {
        std::memcpy(frame, meadow, (size_t)PANEL_PIXELS * 3);
    }
    paint_trails(frame);
    paint_birds(frame);
    paint_hawk(frame);
}
