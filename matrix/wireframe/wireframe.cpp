// A flight down an avenue of wireframe towers, cyan and magenta edges over a gridded street.
// The layout and the motion follow Wireframe City Flyby at
// https://www.ascii-magic.com/ascii/wireframe-city
// The encoder speeds the flight. The sway keeps its own pace.

#include "pattern.hpp"

#include <cmath>

namespace {

constexpr int BLOCK = 7;
constexpr int BLOCKS = 16;
constexpr int LANES = 2;
constexpr float LENGTH = (float)(BLOCKS * BLOCK);
constexpr float BASE_SPEED = 6.0f;
constexpr float RATE_FLOOR = 0.25f;
constexpr float RATE_CAP = 6.0f;
constexpr float FOCUS = 22.0f;
constexpr float HORIZON = 22.0f;
constexpr int TOWER_CAP = BLOCKS * 2 * LANES;

struct Tower {
    float x;
    float z;
    float w;
    float d;
    float h;
    int hue;
};

struct Screen {
    float x;
    float y;
    float z;
};

static Tower towers[TOWER_CAP];
static int tower_count = 0;
static float fly_rate = 1.0f;
static float fly_z = 0.0f;
static float depth[PANEL_PIXELS];
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static Screen project(float x, float y, float z) {
    if (z < 0.2f) {
        z = 0.2f;
    }
    Screen screen;
    screen.x = PANEL_WIDTH * 0.5f + (x / z) * FOCUS;
    screen.y = HORIZON - (y / z) * FOCUS;
    screen.z = 1.0f / z;
    return screen;
}

static void plot(uint8_t *frame, int x, int y, float z, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    int index = y * PANEL_WIDTH + x;
    if (z <= depth[index]) {
        return;
    }
    depth[index] = z;
    write_bgr(frame + index * 3, red, green, blue);
}

static void segment(uint8_t *frame, Screen a, Screen b, int red, int green, int blue, float bias) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    if (steps < 1) {
        steps = 1;
    }
    if (steps > 80) {
        steps = 80;
    }
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        int x = (int)(a.x + dx * t + 0.5f);
        int y = (int)(a.y + dy * t + 0.5f);
        float z = a.z + (b.z - a.z) * t + bias;
        plot(frame, x, y, z, red, green, blue);
    }
}

static void triangle(uint8_t *frame, Screen a, Screen b, Screen c, int red, int green, int blue) {
    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area > -0.5f && area < 0.5f) {
        return;
    }
    float min_x = a.x;
    if (b.x < min_x) {
        min_x = b.x;
    }
    if (c.x < min_x) {
        min_x = c.x;
    }
    float max_x = a.x;
    if (b.x > max_x) {
        max_x = b.x;
    }
    if (c.x > max_x) {
        max_x = c.x;
    }
    float min_y = a.y;
    if (b.y < min_y) {
        min_y = b.y;
    }
    if (c.y < min_y) {
        min_y = c.y;
    }
    float max_y = a.y;
    if (b.y > max_y) {
        max_y = b.y;
    }
    if (c.y > max_y) {
        max_y = c.y;
    }
    int x0 = (int)min_x;
    int x1 = (int)max_x + 1;
    int y0 = (int)min_y;
    int y1 = (int)max_y + 1;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= PANEL_WIDTH) {
        x1 = PANEL_WIDTH - 1;
    }
    if (y1 >= PANEL_HEIGHT) {
        y1 = PANEL_HEIGHT - 1;
    }
    if (max_x - min_x > 48.0f || max_y - min_y > 48.0f) {
        return;
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f) {
                continue;
            }
            plot(frame, x, y, w0 * a.z + w1 * b.z + w2 * c.z, red, green, blue);
        }
    }
}

static void edge_colour(int hue, float z, int &red, int &green, int &blue) {
    float near = clampf(1.4f - z * 0.018f, 0.15f, 1.0f);
    if (hue) {
        red = (int)(40 + near * 215);
        green = (int)(10 + near * 80);
        blue = (int)(50 + near * 205);
    } else {
        red = (int)(8 + near * 80);
        green = (int)(40 + near * 190);
        blue = (int)(50 + near * 205);
    }
}

}  // namespace

void wireframe_start() {
    fly_rate = 1.0f;
    fly_z = 0.0f;
    rng_state = 12;
    tower_count = 0;
    for (int block = 0; block < BLOCKS; ++block) {
        for (int side = -1; side <= 1; side += 2) {
            for (int lane = 0; lane < LANES; ++lane) {
                if (tower_count >= TOWER_CAP) {
                    return;
                }
                Tower &tower = towers[tower_count];
                tower.w = 1.8f + rnd() * 1.8f;
                tower.d = 2.2f + rnd() * 2.2f;
                tower.h = lane == 0 ? 4.0f + rnd() * 8.0f : 7.0f + rnd() * 12.0f;
                tower.x = (float)side * (4.5f + (float)lane * 5.5f + rnd());
                tower.z = (float)block * BLOCK + rnd() * 2.0f;
                tower.hue = rnd() < 0.35f ? 1 : 0;
                tower_count += 1;
            }
        }
    }
}

void wireframe_speed(int steps) {
    fly_rate += (float)steps * 0.25f;
    if (fly_rate < RATE_FLOOR) {
        fly_rate = RATE_FLOOR;
    }
    if (fly_rate > RATE_CAP) {
        fly_rate = RATE_CAP;
    }
}

void wireframe_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
    fly_z += BASE_SPEED * fly_rate * (float)dt_ms * 0.001f;
    fly_z = fmodf(fly_z, LENGTH);
    if (fly_z < 0.0f) {
        fly_z += LENGTH;
    }
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        depth[i] = 0.0f;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int red;
            int green;
            int blue;
            if (y < (int)HORIZON) {
                float v = (float)y / HORIZON;
                red = (int)(2 + v * 28);
                green = (int)(4 + v * 16);
                blue = (int)(12 + v * 40);
            } else {
                red = 4;
                green = 6;
                blue = 14;
            }
            write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
        }
    }
    float cam_z = fly_z;
    float cam_y = 3.0f + 0.45f * sinf(seconds * 0.4f);
    float cam_x = 0.7f * sinf(seconds * 0.3f);
    float tilt = 0.04f * sinf(seconds * 0.3f);
    for (int k = 0; k < 28; ++k) {
        float zw = ceilf(cam_z / 2.0f) * 2.0f + (float)k * 2.0f;
        float z = zw - cam_z;
        if (z < 0.8f) {
            continue;
        }
        Screen a = project(-40.0f - cam_x, -cam_y, z);
        Screen b = project(40.0f - cam_x, -cam_y, z);
        int level = 40 + (int)(80.0f / (1.0f + z * 0.15f));
        segment(frame, a, b, level / 3, level / 2, level, 0.0f);
    }
    for (int lane = -4; lane <= 4; ++lane) {
        float lx = (float)lane * 4.0f;
        Screen a = project(lx - cam_x, -cam_y, 0.8f);
        Screen b = project(lx - cam_x, -cam_y, 70.0f);
        int red = lane == 0 ? 40 : 10;
        int green = lane == 0 ? 20 : 30;
        int blue = lane == 0 ? 70 : 50;
        segment(frame, a, b, red, green, blue, 0.0f);
    }
    for (int i = 0; i < tower_count; ++i) {
        Tower &tower = towers[i];
        float z = fmodf(tower.z - cam_z, LENGTH);
        if (z < 0.0f) {
            z += LENGTH;
        }
        if (z < 1.4f || z > 70.0f) {
            continue;
        }
        float x0 = tower.x - cam_x - tower.w * 0.5f;
        float x1 = x0 + tower.w;
        float y0 = -cam_y;
        float y1 = tower.h - cam_y;
        float z0 = z;
        float z1 = z + tower.d;
        float verts[8][3] = {
            {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
        };
        Screen screen[8];
        for (int v = 0; v < 8; ++v) {
            float vx = verts[v][0] + verts[v][1] * tilt;
            float vy = verts[v][1] - verts[v][0] * tilt;
            screen[v] = project(vx, vy, verts[v][2]);
        }
        static const int FACES[6][4] = {
            {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
            {3, 2, 6, 7}, {1, 2, 6, 5}, {0, 3, 7, 4},
        };
        for (int f = 0; f < 6; ++f) {
            triangle(frame, screen[FACES[f][0]], screen[FACES[f][1]], screen[FACES[f][2]], 4, 6, 16);
            triangle(frame, screen[FACES[f][0]], screen[FACES[f][2]], screen[FACES[f][3]], 4, 6, 16);
        }
        int red;
        int green;
        int blue;
        edge_colour(tower.hue, z, red, green, blue);
        static const int EDGES[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (int e = 0; e < 12; ++e) {
            segment(frame, screen[EDGES[e][0]], screen[EDGES[e][1]], red, green, blue, 0.01f);
        }
    }
}
