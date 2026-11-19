// Langton's loop on the 64 by 64 grid.
// Eight states and a rule table grow a sheath that builds a copy of the loop.
// The rules, the colours, and the loop shape follow the public-domain
// retro_matrix animation. A generation that changes nothing clears the
// board and plants the loop at a new spot that fits on the panel.

#include <cstdint>
#include <cstring>

#include "pico/time.h"

#include "pattern.hpp"

constexpr int STEP_MS = 50;
constexpr int RULE_COUNT = 219;

struct Rule {
    uint8_t centre;
    uint8_t top;
    uint8_t right;
    uint8_t bottom;
    uint8_t left;
    uint8_t next;
};

static const Rule rules[RULE_COUNT] = {
    {0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 2},
    {0, 0, 0, 0, 2, 0},
    {0, 0, 0, 0, 3, 0},
    {0, 0, 0, 0, 5, 0},
    {0, 0, 0, 0, 6, 3},
    {0, 0, 0, 0, 7, 1},
    {0, 0, 0, 1, 1, 2},
    {0, 0, 0, 1, 2, 2},
    {0, 0, 0, 1, 3, 2},
    {0, 0, 0, 2, 1, 2},
    {0, 0, 0, 2, 2, 0},
    {0, 0, 0, 2, 3, 0},
    {0, 0, 0, 2, 6, 2},
    {0, 0, 0, 2, 7, 2},
    {0, 0, 0, 3, 2, 0},
    {0, 0, 0, 5, 2, 5},
    {0, 0, 0, 6, 2, 2},
    {0, 0, 0, 7, 2, 2},
    {0, 0, 1, 0, 2, 2},
    {0, 0, 1, 1, 2, 0},
    {0, 0, 2, 0, 2, 0},
    {0, 0, 2, 0, 3, 0},
    {0, 0, 2, 0, 5, 0},
    {0, 0, 2, 1, 2, 5},
    {0, 0, 2, 2, 2, 0},
    {0, 0, 2, 3, 2, 2},
    {0, 0, 5, 2, 2, 2},
    {0, 1, 2, 3, 2, 1},
    {0, 1, 2, 4, 2, 1},
    {0, 1, 2, 5, 2, 5},
    {0, 1, 2, 6, 2, 1},
    {0, 1, 2, 7, 2, 1},
    {0, 1, 2, 7, 5, 1},
    {0, 1, 4, 2, 2, 1},
    {0, 1, 4, 3, 2, 1},
    {0, 1, 4, 4, 2, 1},
    {0, 1, 4, 7, 2, 1},
    {0, 1, 6, 2, 5, 1},
    {0, 1, 7, 2, 2, 1},
    {0, 1, 7, 2, 5, 5},
    {0, 1, 7, 5, 2, 1},
    {0, 1, 7, 6, 2, 1},
    {0, 1, 7, 7, 2, 1},
    {0, 2, 5, 2, 7, 1},
    {1, 0, 0, 0, 1, 1},
    {1, 0, 0, 0, 6, 1},
    {1, 0, 0, 0, 7, 7},
    {1, 0, 0, 1, 1, 1},
    {1, 0, 0, 1, 2, 1},
    {1, 0, 0, 2, 1, 1},
    {1, 0, 0, 2, 4, 4},
    {1, 0, 0, 2, 7, 7},
    {1, 0, 0, 5, 1, 1},
    {1, 0, 1, 0, 1, 1},
    {1, 0, 1, 1, 1, 1},
    {1, 0, 1, 2, 4, 4},
    {1, 0, 1, 2, 7, 7},
    {1, 0, 2, 0, 2, 6},
    {1, 0, 2, 1, 2, 1},
    {1, 0, 2, 2, 1, 1},
    {1, 0, 2, 2, 4, 4},
    {1, 0, 2, 2, 6, 3},
    {1, 0, 2, 2, 7, 7},
    {1, 0, 2, 3, 2, 7},
    {1, 0, 2, 4, 2, 4},
    {1, 0, 2, 6, 2, 6},
    {1, 0, 2, 6, 4, 4},
    {1, 0, 2, 6, 7, 7},
    {1, 0, 2, 7, 1, 0},
    {1, 0, 2, 7, 2, 7},
    {1, 0, 5, 4, 2, 7},
    {1, 1, 1, 1, 2, 1},
    {1, 1, 1, 2, 2, 1},
    {1, 1, 1, 2, 4, 4},
    {1, 1, 1, 2, 5, 1},
    {1, 1, 1, 2, 6, 1},
    {1, 1, 1, 2, 7, 7},
    {1, 1, 1, 5, 2, 2},
    {1, 1, 2, 1, 2, 1},
    {1, 1, 2, 2, 2, 1},
    {1, 1, 2, 2, 4, 4},
    {1, 1, 2, 2, 5, 1},
    {1, 1, 2, 2, 7, 7},
    {1, 1, 2, 3, 2, 1},
    {1, 1, 2, 4, 2, 4},
    {1, 1, 2, 6, 2, 1},
    {1, 1, 2, 7, 2, 7},
    {1, 1, 3, 2, 2, 1},
    {1, 2, 2, 2, 4, 4},
    {1, 2, 2, 2, 7, 7},
    {1, 2, 2, 4, 3, 4},
    {1, 2, 2, 5, 4, 7},
    {1, 2, 3, 2, 4, 4},
    {1, 2, 3, 2, 7, 7},
    {1, 2, 4, 2, 5, 5},
    {1, 2, 4, 2, 6, 7},
    {1, 2, 5, 2, 7, 5},
    {2, 0, 0, 0, 1, 2},
    {2, 0, 0, 0, 2, 2},
    {2, 0, 0, 0, 4, 2},
    {2, 0, 0, 0, 7, 1},
    {2, 0, 0, 1, 2, 2},
    {2, 0, 0, 1, 5, 2},
    {2, 0, 0, 2, 1, 2},
    {2, 0, 0, 2, 2, 2},
    {2, 0, 0, 2, 3, 2},
    {2, 0, 0, 2, 4, 2},
    {2, 0, 0, 2, 5, 0},
    {2, 0, 0, 2, 6, 2},
    {2, 0, 0, 2, 7, 2},
    {2, 0, 0, 3, 2, 6},
    {2, 0, 0, 4, 2, 3},
    {2, 0, 0, 5, 1, 7},
    {2, 0, 0, 5, 2, 2},
    {2, 0, 0, 5, 7, 5},
    {2, 0, 0, 7, 2, 2},
    {2, 0, 1, 0, 2, 2},
    {2, 0, 1, 1, 2, 2},
    {2, 0, 1, 2, 2, 2},
    {2, 0, 1, 4, 2, 2},
    {2, 0, 1, 7, 2, 2},
    {2, 0, 2, 0, 2, 2},
    {2, 0, 2, 0, 3, 2},
    {2, 0, 2, 0, 5, 2},
    {2, 0, 2, 0, 7, 3},
    {2, 0, 2, 1, 2, 2},
    {2, 0, 2, 1, 5, 2},
    {2, 0, 2, 2, 1, 2},
    {2, 0, 2, 2, 2, 2},
    {2, 0, 2, 2, 7, 2},
    {2, 0, 2, 3, 2, 1},
    {2, 0, 2, 4, 2, 2},
    {2, 0, 2, 4, 5, 2},
    {2, 0, 2, 5, 2, 0},
    {2, 0, 2, 5, 5, 2},
    {2, 0, 2, 6, 2, 2},
    {2, 0, 2, 7, 2, 2},
    {2, 0, 3, 1, 2, 2},
    {2, 0, 3, 2, 1, 6},
    {2, 0, 3, 2, 2, 6},
    {2, 0, 3, 4, 2, 2},
    {2, 0, 4, 2, 2, 2},
    {2, 0, 5, 1, 2, 2},
    {2, 0, 5, 2, 1, 2},
    {2, 0, 5, 2, 2, 2},
    {2, 0, 5, 5, 2, 1},
    {2, 0, 5, 7, 2, 5},
    {2, 0, 6, 2, 2, 2},
    {2, 0, 6, 7, 2, 2},
    {2, 0, 7, 1, 2, 2},
    {2, 0, 7, 2, 2, 2},
    {2, 0, 7, 4, 2, 2},
    {2, 0, 7, 7, 2, 2},
    {2, 1, 1, 2, 2, 2},
    {2, 1, 1, 2, 6, 1},
    {2, 1, 2, 2, 2, 2},
    {2, 1, 2, 2, 4, 2},
    {2, 1, 2, 2, 6, 2},
    {2, 1, 2, 2, 7, 2},
    {2, 1, 4, 2, 2, 2},
    {2, 1, 5, 2, 2, 2},
    {2, 1, 6, 2, 2, 2},
    {2, 1, 7, 2, 2, 2},
    {2, 2, 2, 2, 7, 2},
    {2, 2, 2, 4, 4, 2},
    {2, 2, 2, 4, 6, 2},
    {2, 2, 2, 7, 6, 2},
    {2, 2, 2, 7, 7, 2},
    {3, 0, 0, 0, 1, 3},
    {3, 0, 0, 0, 2, 2},
    {3, 0, 0, 0, 4, 1},
    {3, 0, 0, 0, 7, 6},
    {3, 0, 0, 1, 2, 3},
    {3, 0, 0, 4, 2, 1},
    {3, 0, 0, 6, 2, 2},
    {3, 0, 1, 0, 2, 1},
    {3, 0, 1, 2, 2, 0},
    {3, 0, 2, 5, 1, 1},
    {4, 0, 1, 1, 2, 0},
    {4, 0, 1, 2, 2, 0},
    {4, 0, 1, 2, 5, 0},
    {4, 0, 2, 1, 2, 0},
    {4, 0, 2, 2, 2, 1},
    {4, 0, 2, 3, 2, 6},
    {4, 0, 2, 5, 2, 0},
    {4, 0, 3, 2, 2, 1},
    {5, 0, 0, 0, 2, 2},
    {5, 0, 0, 2, 1, 5},
    {5, 0, 0, 2, 2, 5},
    {5, 0, 0, 2, 3, 2},
    {5, 0, 0, 2, 7, 2},
    {5, 0, 0, 5, 2, 0},
    {5, 0, 2, 0, 2, 2},
    {5, 0, 2, 1, 2, 2},
    {5, 0, 2, 1, 5, 2},
    {5, 0, 2, 2, 2, 0},
    {5, 0, 2, 2, 4, 4},
    {5, 0, 2, 7, 2, 2},
    {5, 1, 2, 1, 2, 2},
    {5, 1, 2, 2, 2, 0},
    {5, 1, 2, 4, 2, 2},
    {5, 1, 2, 7, 2, 2},
    {6, 0, 0, 0, 1, 1},
    {6, 0, 0, 0, 2, 1},
    {6, 0, 2, 1, 2, 0},
    {6, 1, 2, 1, 2, 5},
    {6, 1, 2, 1, 3, 1},
    {6, 1, 2, 2, 2, 5},
    {7, 0, 0, 0, 7, 7},
    {7, 0, 1, 1, 2, 0},
    {7, 0, 1, 2, 2, 0},
    {7, 0, 1, 2, 5, 0},
    {7, 0, 2, 1, 2, 0},
    {7, 0, 2, 2, 2, 1},
    {7, 0, 2, 2, 5, 1},
    {7, 0, 2, 3, 2, 1},
    {7, 0, 2, 5, 2, 5},
    {7, 0, 2, 7, 2, 0}
};

static const uint8_t seed[10][14] = {
    {0, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0},
    {2, 1, 7, 0, 1, 4, 0, 1, 4, 2, 0, 0, 0, 0},
    {2, 0, 2, 2, 2, 2, 2, 2, 0, 2, 0, 0, 0, 0},
    {2, 7, 2, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0},
    {2, 1, 2, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0},
    {2, 0, 2, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0},
    {2, 7, 2, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0},
    {2, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2},
    {2, 0, 7, 1, 0, 7, 1, 0, 7, 1, 1, 1, 1, 1},
    {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
};

struct Rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

static const Rgb colours[8] = {
    {0, 0, 0},
    {0, 0, 255},
    {0, 255, 0},
    {255, 150, 0},
    {255, 225, 0},
    {0, 255, 0},
    {255, 255, 255},
    {255, 0, 0},
};

constexpr int SEED_HEIGHT = 10;
constexpr int SEED_WIDTH = 14;

static uint8_t grid[2][PANEL_PIXELS];
static int shown = 0;
static int pending_ms = 0;
static uint32_t rng = 1;

static uint32_t next_bits() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static uint8_t at(const uint8_t *cells, int x, int y) {
    x = (x + PANEL_WIDTH) & (PANEL_WIDTH - 1);
    y = (y + PANEL_HEIGHT) & (PANEL_HEIGHT - 1);
    return cells[y * PANEL_WIDTH + x];
}

static uint8_t next_state(uint8_t centre, uint8_t top, uint8_t right, uint8_t bottom, uint8_t left) {
    uint8_t turns[4][4] = {
        {top, right, bottom, left},
        {left, top, right, bottom},
        {bottom, left, top, right},
        {right, bottom, left, top},
    };
    for (int i = 0; i < RULE_COUNT; ++i) {
        Rule rule = rules[i];
        if (rule.centre != centre) {
            continue;
        }
        for (int turn = 0; turn < 4; ++turn) {
            if (rule.top == turns[turn][0] && rule.right == turns[turn][1] && rule.bottom == turns[turn][2] && rule.left == turns[turn][3]) {
                return rule.next;
            }
        }
    }
    return 0;
}

static void place_seed() {
    memset(grid, 0, sizeof grid);
    shown = 0;
    int y0 = (int)(next_bits() % (uint32_t)(PANEL_HEIGHT - SEED_HEIGHT + 1));
    int x0 = (int)(next_bits() % (uint32_t)(PANEL_WIDTH - SEED_WIDTH + 1));
    for (int y = 0; y < SEED_HEIGHT; ++y) {
        for (int x = 0; x < SEED_WIDTH; ++x) {
            grid[0][(y + y0) * PANEL_WIDTH + (x + x0)] = seed[y][x];
        }
    }
}

static bool step_loop() {
    const uint8_t *src = grid[shown];
    int next = shown ^ 1;
    uint8_t *dst = grid[next];
    bool changed = false;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = y * PANEL_WIDTH + x;
            uint8_t centre = src[index];
            uint8_t state = next_state(
                centre,
                at(src, x, y - 1),
                at(src, x + 1, y),
                at(src, x, y + 1),
                at(src, x - 1, y));
            dst[index] = state;
            if (state != centre) {
                changed = true;
            }
        }
    }
    shown = next;
    return changed;
}

static void paint_loop(uint8_t *pixels) {
    const uint8_t *src = grid[shown];
    uint8_t *pixel = pixels;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        uint8_t state = src[i];
        if (state > 7) {
            state = 0;
        }
        Rgb colour = colours[state];
        write_bgr(pixel, colour.red, colour.green, colour.blue);
        pixel += 3;
    }
}

void loop_start() {
    rng = time_us_32() | 1u;
    pending_ms = 0;
    place_seed();
}

void loop_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    pending_ms += dt_ms;
    while (pending_ms >= STEP_MS) {
        pending_ms -= STEP_MS;
        if (!step_loop()) {
            place_seed();
            break;
        }
    }
    paint_loop(pixels);
}
