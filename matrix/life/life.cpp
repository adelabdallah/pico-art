// Conway's Life on the whole panel. A cell's colour is how long it has lived.
// The layout and the motion follow Game of Life at
// https://www.ascii-magic.com/ascii/game-of-life
// A cell that dies leaves a short trail. A fresh board starts when the population stalls, or every 45 seconds.

#include "pattern.hpp"

#include <cstdint>

namespace {

constexpr int STEP_MS = 83;
constexpr int EPOCH_MS = 45000;
constexpr int HISTORY = 40;
constexpr int QUIET_POP = 16;

static const uint8_t AGE_RGB[7][3] = {
    {255, 246, 200},
    {255, 210, 63},
    {154, 255, 106},
    {63, 224, 192},
    {63, 160, 255},
    {90, 90, 255},
    {138, 74, 216},
};

static uint8_t *cells[2] = {nullptr, nullptr};
static uint16_t *age = nullptr;
static uint8_t *dead = nullptr;
static int shown = 0;
static int pending_ms = 0;
static int epoch = 0;
static int history[HISTORY];
static int history_n = 0;
static uint32_t rng_state = 1;
static uint32_t seed_n = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static int at(const uint8_t *grid, int x, int y) {
    x = (x + PANEL_WIDTH) & (PANEL_WIDTH - 1);
    y = (y + PANEL_HEIGHT) & (PANEL_HEIGHT - 1);
    return grid[y * PANEL_WIDTH + x];
}

static void seed_board() {
    rng_state = 0x11FEu + seed_n * 7919u;
    seed_n += 1;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        cells[0][i] = rnd() < 0.32f ? 1 : 0;
        age[i] = 0;
        dead[i] = 9;
    }
    shown = 0;
    history_n = 0;
}

static int age_band(int lived) {
    if (lived < 1) {
        return 0;
    }
    if (lived < 3) {
        return 1;
    }
    if (lived < 6) {
        return 2;
    }
    if (lived < 12) {
        return 3;
    }
    if (lived < 25) {
        return 4;
    }
    if (lived < 60) {
        return 5;
    }
    return 6;
}

static bool stalled(int pop) {
    history[history_n % HISTORY] = pop;
    history_n += 1;
    if (pop < QUIET_POP) {
        return true;
    }
    if (history_n < HISTORY) {
        return false;
    }
    int first = history[0];
    for (int i = 1; i < HISTORY; ++i) {
        if (history[i] != first) {
            return false;
        }
    }
    return true;
}

static bool step_life() {
    const uint8_t *src = cells[shown];
    int next = shown ^ 1;
    uint8_t *dst = cells[next];
    int pop = 0;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int neighbours = at(src, x - 1, y - 1) + at(src, x, y - 1) + at(src, x + 1, y - 1);
            neighbours += at(src, x - 1, y) + at(src, x + 1, y);
            neighbours += at(src, x - 1, y + 1) + at(src, x, y + 1) + at(src, x + 1, y + 1);
            int index = y * PANEL_WIDTH + x;
            int alive = (neighbours == 3) || (neighbours == 2 && src[index]);
            dst[index] = (uint8_t)alive;
            if (alive) {
                age[index] = src[index] ? (uint16_t)(age[index] < 999 ? age[index] + 1 : 999) : 0;
                dead[index] = 0;
                pop += 1;
            } else if (dead[index] < 9) {
                dead[index] = (uint8_t)(dead[index] + 1);
            }
        }
    }
    shown = next;
    return stalled(pop);
}

static void paint_life(uint8_t *frame) {
    const uint8_t *src = cells[shown];
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = y * PANEL_WIDTH + x;
            int red;
            int green;
            int blue;
            if (src[index]) {
                const uint8_t *colour = AGE_RGB[age_band(age[index])];
                red = colour[0];
                green = colour[1];
                blue = colour[2];
            } else if (dead[index] < 3) {
                red = 42;
                green = 32;
                blue = 80;
            } else if (dead[index] < 7) {
                red = 26;
                green = 20;
                blue = 64;
            } else if (((x + y) & 1) == 0) {
                red = 16;
                green = 12;
                blue = 42;
            } else {
                red = 12;
                green = 8;
                blue = 28;
            }
            write_bgr(frame + index * 3, red, green, blue);
        }
    }
}

}  // namespace

void life_start() {
    cells[0] = pattern_alloc(PANEL_PIXELS);
    cells[1] = pattern_alloc(PANEL_PIXELS);
    age = (uint16_t *)pattern_alloc(PANEL_PIXELS * (int)sizeof(uint16_t));
    dead = pattern_alloc(PANEL_PIXELS);
    seed_n = 1;
    pending_ms = 0;
    epoch = 0;
    seed_board();
}

void life_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    int now_epoch = (int)(anim_ms / EPOCH_MS);
    if (now_epoch != epoch) {
        epoch = now_epoch;
        pending_ms = 0;
        seed_board();
    }
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    pending_ms += dt_ms;
    int steps = 0;
    while (pending_ms >= STEP_MS && steps < 4) {
        pending_ms -= STEP_MS;
        steps += 1;
        if (step_life()) {
            seed_board();
            break;
        }
    }
    paint_life(frame);
}
