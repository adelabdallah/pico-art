// Pumpkins and a hay bale in front of a river at sunset, with a tree on the right.
// The picture follows Autumn Harvest at
// https://www.ascii-magic.com/ascii/autumn-harvest
// The river keeps the harvest gold. The light web from Pool Caustics brightens it.
// https://www.ascii-magic.com/ascii/water-caustics
//
// Orange, while held, doubles how many leaves are falling.
// Yellow, while held, doubles the speed of the river.
// Green, while held, doubles how fast the tree canopy billows.
// Releasing a button returns that part to its own pace.
// Two panels show the full width. One and four panels show the square that holds the tree.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr int COLS = 96;
constexpr int ROWS = 34;
constexpr int LEAF_CAP = 92;
constexpr float TAU = 6.2831853f;

struct Cam {
    float ox;
    float oy;
    float scale;
};

static float leaf_x0[LEAF_CAP];
static float leaf_phase[LEAF_CAP];
static float leaf_speed[LEAF_CAP];
static float leaf_sway[LEAF_CAP];
static int leaf_colour[LEAF_CAP];
static float river_time = 0.0f;
static float tree_time = 0.0f;
static float leaf_time = 0.0f;
static bool leaves_fast = false;
static bool river_fast = false;
static bool tree_fast = false;
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, clamp_channel((float)red), clamp_channel((float)green), clamp_channel((float)blue));
}

static void leaf_rgb(int which, int &red, int &green, int &blue) {
    if (which == 0) {
        red = 200;
        green = 40;
        blue = 30;
    } else if (which == 1) {
        red = 232;
        green = 96;
        blue = 26;
    } else if (which == 2) {
        red = 240;
        green = 160;
        blue = 30;
    } else if (which == 3) {
        red = 138;
        green = 58;
        blue = 20;
    } else {
        red = 255;
        green = 208;
        blue = 64;
    }
}

static Cam frame_cam() {
    Cam cam;
    if (PANEL_WIDTH > PANEL_HEIGHT) {
        cam.scale = (float)PANEL_WIDTH / (float)COLS;
        float view_h = (float)PANEL_HEIGHT / cam.scale;
        cam.ox = 0.0f;
        cam.oy = (68.0f - view_h) * 0.38f;
        if (cam.oy < 0.0f) {
            cam.oy = 0.0f;
        }
        return cam;
    }
    const float view = 52.0f;
    cam.scale = (float)PANEL_WIDTH / view;
    cam.ox = 78.0f - view * 0.62f;
    cam.oy = 30.0f - view * 0.48f;
    return cam;
}

static bool ellipse(float x, float y, float cx, float cy, float rx, float ry) {
    float u = (x - cx) / rx;
    float v = (y - cy) / ry;
    return u * u + v * v <= 1.0f;
}

// Light gathers where the noise field crosses its middle. Two fields cross into a web.
static float caustic_ridge(float x, float y, int seed) {
    float n = field_noise(x, y, seed) * 0.65f + field_noise(x * 2.1f, y * 2.1f, seed + 5) * 0.35f;
    float ridge = 1.0f - fabsf(n - 0.5f) * 2.0f;
    if (ridge < 0.0f) {
        return 0.0f;
    }
    float square = ridge * ridge;
    float fourth = square * square;
    return fourth * fourth * fourth;
}

}  // namespace

void autumn_leaves(bool held) {
    leaves_fast = held;
}

void autumn_wheat(bool held) {
    river_fast = held;
}

void autumn_tree(bool held) {
    tree_fast = held;
}

void autumn_start() {
    rng_state = 31u;
    for (int i = 0; i < LEAF_CAP; ++i) {
        leaf_x0[i] = rnd() * COLS;
        leaf_phase[i] = rnd();
        leaf_speed[i] = 0.05f + rnd() * 0.06f;
        leaf_sway[i] = rnd() * TAU;
        leaf_colour[i] = (int)(rnd() * 5.0f);
        if (leaf_colour[i] > 4) {
            leaf_colour[i] = 4;
        }
    }
    river_time = 0.0f;
    tree_time = 0.0f;
    leaf_time = 0.0f;
    leaves_fast = false;
    river_fast = false;
    tree_fast = false;
}

void autumn_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    float step = dt_ms * 0.001f;
    river_time += step * (river_fast ? 2.0f : 1.0f);
    tree_time += step * (tree_fast ? 2.0f : 1.0f);
    leaf_time += step;
    if (river_time > 1000.0f) {
        river_time -= 1000.0f;
    }
    if (tree_time > 1000.0f) {
        tree_time -= 1000.0f;
    }
    if (leaf_time > 1000.0f) {
        leaf_time -= 1000.0f;
    }
    Cam cam = frame_cam();
    const float pumpkins[][4] = {{30, 30, 6, 3}, {44, 31, 9, 4}, {58, 30, 5, 2.6f}, {14, 31, 4, 2.2f}};
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float vx = cam.ox + ((float)x + 0.5f) / cam.scale;
            float vy = cam.oy + ((float)y + 0.5f) / cam.scale;
            float col = vx;
            float row = vy * 0.5f;
            int red = 20;
            int green = 8;
            int blue = 16;
            float sun = expf(-hypotf((col - 60.0f) * 0.5f, row - 12.0f) / 4.0f);
            if (row < 16.0f) {
                float v = clampf(row / 16.0f * 0.85f + sun * 0.45f, 0.0f, 1.0f);
                if (v < 0.35f) {
                    red = (int)(58 + v * 80);
                    green = (int)(26 + v * 20);
                    blue = (int)(74 - v * 40);
                } else if (v < 0.7f) {
                    float t = (v - 0.35f) / 0.35f;
                    red = (int)(140 + t * 100);
                    green = (int)(48 + t * 80);
                    blue = (int)(40 - t * 10);
                } else {
                    float t = (v - 0.7f) / 0.3f;
                    red = (int)(240);
                    green = (int)(140 + t * 90);
                    blue = (int)(40 + t * 80);
                }
                float horizon = 13.0f - 2.5f * field_noise(col * 0.15f, 1.0f, 12);
                if (row > horizon && row < 16.0f) {
                    float n = field_noise(col * 0.3f, row * 0.6f, 8);
                    if (n > 0.6f) {
                        red = 200;
                        green = 40;
                        blue = 30;
                    } else if (n > 0.45f) {
                        red = 90;
                        green = 50;
                        blue = 18;
                    } else {
                        red = 240;
                        green = 160;
                        blue = 30;
                    }
                }
            }
            if (row >= 15.0f && row < 26.0f) {
                float wx = col * 0.09f;
                float wy = row * 0.19f;
                float w1 = sinf(wy * 1.3f + river_time * 0.9f) * 0.35f;
                float w2 = cosf(wx * 1.1f - river_time * 0.7f) * 0.35f;
                float lines = caustic_ridge(wx + w1 + river_time * 0.12f, wy + w2 - river_time * 0.05f, 3);
                lines += 0.8f * caustic_ridge(wx * 1.3f - w2 - river_time * 0.09f, wy * 1.3f + w1 + river_time * 0.1f, 11);
                float depth = (row - 15.0f) / 11.0f;
                float shade = clampf(0.28f + depth * 0.35f + lines * 0.45f, 0.0f, 1.0f);
                red = (int)(90 + shade * 150);
                green = (int)(60 + shade * 120);
                blue = (int)(16 + shade * 40);
            }
            if (row >= 26.0f) {
                float n = field_noise(col * 0.2f, row * 0.4f, 5);
                red = (int)(30 + n * 70);
                green = (int)(16 + n * 28);
                blue = (int)(8 + n * 12);
            }
            if (col >= 84.0f && col < 89.0f && row >= 10.0f && row < (float)ROWS) {
                red = col > 86.0f ? 42 : 74;
                green = col > 86.0f ? 26 : 42;
                blue = 16;
            }
            if (col > 68.0f && row < 18.0f) {
            float canopy = hypotf((col - 86.0f) * 0.5f, (row - 5.0f));
            float billow = field_noise(col * 0.2f + tree_time * 0.1f, row * 0.35f, 21);
            if (canopy + (billow - 0.5f) * 6.0f < 7.5f) {
                if (billow > 0.62f) {
                    red = 255;
                    green = 208;
                    blue = 64;
                } else if (billow > 0.5f) {
                    red = 240;
                    green = 160;
                    blue = 30;
                } else if (billow > 0.4f) {
                    red = 200;
                    green = 40;
                    blue = 30;
                } else {
                    red = 138;
                    green = 58;
                    blue = 20;
                }
            }
            }
            if (row >= 25.0f && row < 32.0f && col >= 66.0f && col < 82.0f) {
                bool end = col < 69.0f;
                bool band = ((int)col + (int)row * 3) % 4 == 0;
                if (end) {
                    red = 138;
                    green = 106;
                    blue = 42;
                } else if (band) {
                    red = 232;
                    green = 200;
                    blue = 112;
                } else {
                    red = 200;
                    green = 160;
                    blue = 80;
                }
            }
            for (int p = 0; p < 4; ++p) {
                float cx = pumpkins[p][0];
                float cy = pumpkins[p][1];
                float rx = pumpkins[p][2];
                float ry = pumpkins[p][3];
                if (!ellipse(col, row, cx, cy, rx, ry)) {
                    continue;
                }
                float u = (col - cx) / rx;
                float rib = fabsf(cosf(asinf(clampf(u, -0.999f, 0.999f)) * (rx > 6.0f ? 3.5f : 2.5f)));
                float v = clampf(0.35f + rib * 0.4f + u * 0.15f, 0.0f, 1.0f);
                red = (int)(90 + v * 165);
                green = (int)(30 + v * 90);
                blue = (int)(8 + v * 20);
            }
            if (hypotf((col - 60.0f) * 0.5f, row - 12.0f) < 3.4f && row < 16.0f) {
                red = 255;
                green = 236;
                blue = 170;
            }
            put(frame, x, y, red, green, blue);
        }
    }
    int shown = leaves_fast ? LEAF_CAP : LEAF_CAP / 2;
    for (int i = 0; i < shown; ++i) {
        float progress = leaf_time * leaf_speed[i] + leaf_phase[i];
        progress -= floorf(progress);
        float row = progress * (float)(ROWS + 2) - 1.0f;
        float col = leaf_x0[i] - progress * 18.0f + sinf(leaf_time * 1.4f + leaf_sway[i]) * 2.5f;
        col = col - floorf(col / (float)COLS) * (float)COLS;
        float vx = col;
        float vy = row * 2.0f;
        int x = (int)((vx - cam.ox) * cam.scale);
        int y = (int)((vy - cam.oy) * cam.scale);
        int red;
        int green;
        int blue;
        leaf_rgb(leaf_colour[i], red, green, blue);
        put(frame, x, y, red, green, blue);
        put(frame, x + 1, y, red, green, blue);
    }
}
