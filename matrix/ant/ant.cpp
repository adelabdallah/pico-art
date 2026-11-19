// Langton's ant on the 64 by 64 grid.
// On a black cell it turns left. On an orange cell it turns right.
// Then it flips that cell and steps forward. The ant itself is white.
// The turn rule follows the public-domain retro_matrix animation.

#include <cstdint>
#include <cstring>

#include "pattern.hpp"

constexpr int STEP_MS = 20;

static uint8_t cells[PANEL_PIXELS];
static int ant_row = 32;
static int ant_col = 32;
static int ant_dir = 2;
static int pending_ms = 0;

static void wrap_ant() {
    if (ant_row < 0) {
        ant_row = PANEL_HEIGHT - 1;
    } else if (ant_row >= PANEL_HEIGHT) {
        ant_row = 0;
    }
    if (ant_col < 0) {
        ant_col = PANEL_WIDTH - 1;
    } else if (ant_col >= PANEL_WIDTH) {
        ant_col = 0;
    }
}

static void step_ant() {
    int index = ant_row * PANEL_WIDTH + ant_col;
    int was_on = cells[index];
    cells[index] = was_on ? 0 : 1;
    if (was_on) {
        ant_dir += 1;
    } else {
        ant_dir -= 1;
    }
    if (ant_dir < 0) {
        ant_dir = 3;
    } else if (ant_dir > 3) {
        ant_dir = 0;
    }
    if (ant_dir == 0) {
        ant_row -= 1;
    } else if (ant_dir == 1) {
        ant_col += 1;
    } else if (ant_dir == 2) {
        ant_row += 1;
    } else {
        ant_col -= 1;
    }
    wrap_ant();
}

static void paint_ant(uint8_t *pixels) {
    uint8_t *pixel = pixels;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (cells[i]) {
            write_bgr(pixel, 255, 150, 0);
        } else {
            write_bgr(pixel, 0, 0, 0);
        }
        pixel += 3;
    }
    uint8_t *ant = pixels + (ant_row * PANEL_WIDTH + ant_col) * 3;
    write_bgr(ant, 255, 255, 255);
}

void ant_start() {
    memset(cells, 0, sizeof cells);
    ant_row = 32;
    ant_col = 32;
    ant_dir = 2;
    pending_ms = 0;
}

void ant_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    pending_ms += dt_ms;
    while (pending_ms >= STEP_MS) {
        pending_ms -= STEP_MS;
        step_ant();
    }
    paint_ant(pixels);
}
