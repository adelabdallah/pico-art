// Gray-Scott reaction-diffusion on the 64 by 64 grid.
// U and V are two chemicals. V grows into coral, spots, and worms
// as the feed and kill rates drift between four stable pairs.
// A dead field is seeded again so the panel keeps a picture.

#include <cstdint>

#include "pattern.hpp"

constexpr int STEP_COUNT = 8;
constexpr int WARM_STEPS = 40;

struct Rates {
    float feed;
    float kill;
};

Rates rates[] = {
    {0.0367f, 0.0649f},
    {0.0545f, 0.0620f},
    {0.0290f, 0.0570f},
    {0.0340f, 0.0560f},
};

constexpr int RATE_COUNT = (int)(sizeof rates / sizeof rates[0]);
constexpr int RATE_SPAN_MS = 18000;

static float grid_u[2][PANEL_PIXELS];
static float grid_v[2][PANEL_PIXELS];
static int current = 0;

int wrap_index(int value, int limit) {
    value %= limit;
    if (value < 0) {
        value += limit;
    }
    return value;
}

float cell(const float *grid, int x, int y) {
    x = wrap_index(x, PANEL_WIDTH);
    y = wrap_index(y, PANEL_HEIGHT);
    return grid[y * PANEL_WIDTH + x];
}

float laplacian(const float *grid, int x, int y) {
    float centre = cell(grid, x, y);
    float edge = cell(grid, x - 1, y) + cell(grid, x + 1, y) + cell(grid, x, y - 1) + cell(grid, x, y + 1);
    float corner = cell(grid, x - 1, y - 1) + cell(grid, x + 1, y - 1) + cell(grid, x - 1, y + 1) + cell(grid, x + 1, y + 1);
    return edge * 0.2f + corner * 0.05f - centre;
}

void seed_disc(int cx, int cy, int radius) {
    float *u = grid_u[current];
    float *v = grid_v[current];
    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            int px = wrap_index(x, PANEL_WIDTH);
            int py = wrap_index(y, PANEL_HEIGHT);
            int index = py * PANEL_WIDTH + px;
            u[index] = 0.50f;
            v[index] = 0.25f;
        }
    }
}

void clear_chemicals() {
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        grid_u[0][i] = 1.0f;
        grid_v[0][i] = 0.0f;
        grid_u[1][i] = 1.0f;
        grid_v[1][i] = 0.0f;
    }
    current = 0;
    seed_disc(32, 32, 6);
    seed_disc(18, 40, 4);
    seed_disc(46, 22, 4);
}

void rates_at(uint32_t anim_ms, float &feed, float &kill) {
    uint32_t span = (uint32_t)RATE_SPAN_MS;
    uint32_t cycle = anim_ms % (span * RATE_COUNT);
    int index = (int)(cycle / span);
    float frac = (cycle % span) / (float)span;
    int next = index + 1;
    if (next >= RATE_COUNT) {
        next = 0;
    }
    feed = rates[index].feed + (rates[next].feed - rates[index].feed) * frac;
    kill = rates[index].kill + (rates[next].kill - rates[index].kill) * frac;
}

float step_chemicals(float feed, float kill) {
    const float *u = grid_u[current];
    const float *v = grid_v[current];
    int next = current ^ 1;
    float *nu = grid_u[next];
    float *nv = grid_v[next];
    float peak = 0.0f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = y * PANEL_WIDTH + x;
            float u0 = u[index];
            float v0 = v[index];
            float reaction = u0 * v0 * v0;
            float u1 = u0 + (0.16f * laplacian(u, x, y) - reaction + feed * (1.0f - u0));
            float v1 = v0 + (0.08f * laplacian(v, x, y) + reaction - (feed + kill) * v0);
            u1 = clampf(u1, 0.0f, 1.0f);
            v1 = clampf(v1, 0.0f, 1.0f);
            nu[index] = u1;
            nv[index] = v1;
            if (v1 > peak) {
                peak = v1;
            }
        }
    }
    current = next;
    return peak;
}

void paint(uint8_t *pixels) {
    const float *v = grid_v[current];
    uint8_t *pixel = pixels;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        float amount = clampf(v[i] * 3.2f, 0.0f, 1.0f);
        float hot = smooth_unit(0.45f, 1.0f, amount);
        int red = (int)(6 + amount * 40 + hot * 200);
        int green = (int)(8 + amount * 150);
        int blue = (int)(20 + amount * 90 + hot * 40);
        write_bgr(pixel, clamp_channel(red), clamp_channel(green), clamp_channel(blue));
        pixel += 3;
    }
}

void reaction_start() {
    clear_chemicals();
    for (int i = 0; i < WARM_STEPS; ++i) {
        step_chemicals(rates[0].feed, rates[0].kill);
    }
}

void reaction_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float feed;
    float kill;
    rates_at(anim_ms, feed, kill);
    float peak = 0.0f;
    for (int i = 0; i < STEP_COUNT; ++i) {
        peak = step_chemicals(feed, kill);
    }
    if (peak < 0.05f) {
        clear_chemicals();
    }
    paint(pixels);
}
