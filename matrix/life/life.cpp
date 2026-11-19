// Conway's Game of Life on the 64 by 64 grid.
// A live cell stays live with two or three live neighbours.
// A dead cell is born with three. The field is filled again
// when it stops changing, or after a few hundred generations.
// The rule and the 20 percent fill follow the public-domain
// retro_matrix animation.

#include <cstdint>

#include "pattern.hpp"

constexpr int STEP_MS = 100;
constexpr int RESEED_AT = 500;
constexpr int LIVE_RED = 255;
constexpr int LIVE_GREEN = 40;
constexpr int LIVE_BLUE = 130;

static uint8_t cells[2][PANEL_PIXELS];
static int shown = 0;
static int pending_ms = 0;
static int generation = 0;
static uint32_t rng = 1;

static uint32_t next_bits() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static int cell_at(const uint8_t *grid, int x, int y) {
    x = (x + PANEL_WIDTH) & (PANEL_WIDTH - 1);
    y = (y + PANEL_HEIGHT) & (PANEL_HEIGHT - 1);
    return grid[y * PANEL_WIDTH + x];
}

static void fill_life() {
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        cells[0][i] = (next_bits() % 1000) < 200 ? 1 : 0;
    }
    shown = 0;
    generation = 0;
}

static int step_life() {
    const uint8_t *src = cells[shown];
    int next = shown ^ 1;
    uint8_t *dst = cells[next];
    int changed = 0;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int neighbours = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    neighbours += cell_at(src, x + ox, y + oy);
                }
            }
            int index = y * PANEL_WIDTH + x;
            int alive = src[index];
            int birth = (neighbours == 3) || (alive && neighbours == 2);
            dst[index] = (uint8_t)birth;
            if (birth != alive) {
                changed += 1;
            }
        }
    }
    shown = next;
    generation += 1;
    return changed;
}

static void paint_life(uint8_t *pixels) {
    const uint8_t *src = cells[shown];
    uint8_t *pixel = pixels;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (src[i]) {
            write_bgr(pixel, LIVE_RED, LIVE_GREEN, LIVE_BLUE);
        } else {
            write_bgr(pixel, 0, 0, 0);
        }
        pixel += 3;
    }
}

void life_start() {
    rng = 0x11FEU;
    pending_ms = 0;
    fill_life();
}

void life_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    pending_ms += dt_ms;
    while (pending_ms >= STEP_MS) {
        pending_ms -= STEP_MS;
        int changed = step_life();
        if (changed == 0 || generation >= RESEED_AT) {
            fill_life();
            break;
        }
    }
    paint_life(pixels);
}
