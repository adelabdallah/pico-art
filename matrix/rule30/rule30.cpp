// Rule 30 on the 64 by 64 grid.
// The top row is the elementary rule, left XOR (centre OR right).
// Older rows scroll down, so the triangle grows from one live cell.

#include <cstdint>
#include <cstring>

#include "pattern.hpp"

constexpr int STEP_MS = 50;
constexpr int SEED_COL = 33;

static uint8_t rows[PANEL_HEIGHT][PANEL_WIDTH];
static int pending_ms = 0;

static int rule_bit(int left, int mid, int right) {
    return left ^ (mid | right);
}

static void step_rule() {
    uint8_t next[PANEL_WIDTH];
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        int left = rows[0][(x + PANEL_WIDTH - 1) & (PANEL_WIDTH - 1)];
        int mid = rows[0][x];
        int right = rows[0][(x + 1) & (PANEL_WIDTH - 1)];
        next[x] = (uint8_t)rule_bit(left, mid, right);
    }
    for (int y = PANEL_HEIGHT - 1; y > 0; --y) {
        memcpy(rows[y], rows[y - 1], PANEL_WIDTH);
    }
    memcpy(rows[0], next, PANEL_WIDTH);
}

static void paint_rule(uint8_t *pixels) {
    uint8_t *pixel = pixels;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            if (rows[y][x]) {
                write_bgr(pixel, 0, 255, 50);
            } else {
                write_bgr(pixel, 0, 0, 0);
            }
            pixel += 3;
        }
    }
}

void rule30_start() {
    memset(rows, 0, sizeof rows);
    rows[0][SEED_COL] = 1;
    pending_ms = 0;
}

void rule30_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    pending_ms += dt_ms;
    while (pending_ms >= STEP_MS) {
        pending_ms -= STEP_MS;
        step_rule();
    }
    paint_rule(pixels);
}
