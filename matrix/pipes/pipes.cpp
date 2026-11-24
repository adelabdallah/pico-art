// Perspective pipes in a box, in the spirit of the old pipes screensaver.
// Each pipe is a shaded cylinder. A turn gets an elbow. The box clears when it fills.
// The camera stays put. Joints are elbows.

#include "pattern.hpp"

#include <cstring>

namespace {

constexpr int GRID = 6;
constexpr int CELLS = GRID * GRID * GRID;
constexpr int SEG_CAP = CELLS;
constexpr int PIPE_COUNT = 3;
constexpr int GROW_MS = 200;
constexpr int CROWD = 150;

constexpr int DIRS[6][3] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
};

constexpr uint8_t COLOURS[6][3] = {
    {220, 46, 36},
    {36, 150, 230},
    {42, 196, 96},
    {232, 176, 42},
    {176, 72, 214},
    {232, 112, 42},
};

struct Seg {
    int8_t x;
    int8_t y;
    int8_t z;
    int8_t dx;
    int8_t dy;
    int8_t dz;
    uint8_t colour;
    uint8_t elbow;
};

struct Pipe {
    int x;
    int y;
    int z;
    int dx;
    int dy;
    int dz;
    int colour;
    bool alive;
};

struct Point {
    float x;
    float y;
    float depth;
    float radius;
};

static uint32_t rng = 0xA341316Cu;
static uint8_t occupied[CELLS];
static Seg segs[SEG_CAP];
static int seg_count = 0;
static int filled = 0;
static Pipe pipes[PIPE_COUNT];
static int grow_ms = 0;
static uint16_t *depth_buf = nullptr;

static uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int cell_index(int x, int y, int z) {
    return (z * GRID + y) * GRID + x;
}

static bool inside(int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < GRID && y < GRID && z < GRID;
}

static bool open_cell(int x, int y, int z) {
    return inside(x, y, z) && occupied[cell_index(x, y, z)] == 0;
}

static Point project(float x, float y, float z) {
    float wx = x - 2.5f;
    float wy = y - 2.5f;
    float wz = z - 2.5f;
    constexpr float yaw_c = 0.7660f;
    constexpr float yaw_s = 0.6428f;
    float x1 = wx * yaw_c + wz * yaw_s;
    float z1 = -wx * yaw_s + wz * yaw_c;
    constexpr float pitch_c = 0.9063f;
    constexpr float pitch_s = 0.4226f;
    float y1 = wy * pitch_c - z1 * pitch_s;
    float z2 = wy * pitch_s + z1 * pitch_c;
    float depth = z2 + 12.0f;
    if (depth < 2.0f) {
        depth = 2.0f;
    }
    float scale = 70.0f / depth;
    Point point;
    point.x = 32.0f + x1 * scale;
    point.y = 33.0f - y1 * scale;
    point.depth = depth;
    point.radius = 0.48f * scale;
    return point;
}

static void paint(uint8_t *frame, int x, int y, float depth, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    int slot = y * PANEL_WIDTH + x;
    uint16_t key = (uint16_t)(depth * 48.0f);
    if (key >= depth_buf[slot]) {
        return;
    }
    depth_buf[slot] = key;
    write_bgr(frame + slot * 3, red, green, blue);
}

static void shade(const uint8_t *colour, float lit, float stripe, float depth, int &red, int &green, int &blue) {
    float dim = (14.0f - depth) / 8.0f;
    if (dim < 0.4f) {
        dim = 0.4f;
    } else if (dim > 1.0f) {
        dim = 1.0f;
    }
    float body = (0.16f + 0.84f * lit) * dim;
    red = clamp_channel(colour[0] * body + stripe * 190.0f);
    green = clamp_channel(colour[1] * body + stripe * 190.0f);
    blue = clamp_channel(colour[2] * body + stripe * 170.0f);
}

static void draw_sphere(uint8_t *frame, Point center, const uint8_t *colour) {
    int x0 = (int)(center.x - center.radius - 1.0f);
    int y0 = (int)(center.y - center.radius - 1.0f);
    int x1 = (int)(center.x + center.radius + 1.0f);
    int y1 = (int)(center.y + center.radius + 1.0f);
    float light_x = center.x - center.radius * 0.42f;
    float light_y = center.y - center.radius * 0.48f;
    float radius2 = center.radius * center.radius;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = x - center.x;
            float dy = y - center.y;
            float dist2 = dx * dx + dy * dy;
            if (dist2 > radius2) {
                continue;
            }
            float nd = sqrtf(dist2) / center.radius;
            float lit = sqrtf(1.0f - nd * nd);
            float lx = x - light_x;
            float ly = y - light_y;
            float stripe = 1.0f - sqrtf(lx * lx + ly * ly) / (center.radius * 0.55f);
            if (stripe < 0.0f) {
                stripe = 0.0f;
            }
            stripe *= stripe;
            int red = 0;
            int green = 0;
            int blue = 0;
            shade(colour, lit, stripe, center.depth, red, green, blue);
            paint(frame, x, y, center.depth - 0.02f, red, green, blue);
        }
    }
}

static void draw_cylinder(uint8_t *frame, Point from, Point to, const uint8_t *colour) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float len2 = dx * dx + dy * dy;
    if (len2 < 0.25f) {
        draw_sphere(frame, from, colour);
        return;
    }
    float len = sqrtf(len2);
    float nx = -dy / len;
    float ny = dx / len;
    if (nx * -0.55f + ny * -0.84f < 0.0f) {
        nx = -nx;
        ny = -ny;
    }
    float margin = from.radius > to.radius ? from.radius : to.radius;
    int x0 = (int)(fminf(from.x, to.x) - margin - 1.0f);
    int y0 = (int)(fminf(from.y, to.y) - margin - 1.0f);
    int x1 = (int)(fmaxf(from.x, to.x) + margin + 1.0f);
    int y1 = (int)(fmaxf(from.y, to.y) + margin + 1.0f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float px = x - from.x;
            float py = y - from.y;
            float along = (px * dx + py * dy) / len2;
            if (along < 0.0f) {
                along = 0.0f;
            } else if (along > 1.0f) {
                along = 1.0f;
            }
            float cx = from.x + dx * along;
            float cy = from.y + dy * along;
            float radius = from.radius + (to.radius - from.radius) * along;
            float ox = x - cx;
            float oy = y - cy;
            float dist = sqrtf(ox * ox + oy * oy);
            if (dist > radius || radius <= 0.2f) {
                continue;
            }
            float nd = dist / radius;
            float lit = sqrtf(1.0f - nd * nd);
            float hx = x - (cx + nx * radius * 0.42f);
            float hy = y - (cy + ny * radius * 0.42f);
            float stripe = 1.0f - sqrtf(hx * hx + hy * hy) / (radius * 0.5f);
            if (stripe < 0.0f) {
                stripe = 0.0f;
            }
            stripe *= stripe;
            float depth = from.depth + (to.depth - from.depth) * along;
            int red = 0;
            int green = 0;
            int blue = 0;
            shade(colour, lit, stripe, depth, red, green, blue);
            paint(frame, x, y, depth, red, green, blue);
        }
    }
}

static void draw_edge(uint8_t *frame, float x0, float y0, float z0, float x1, float y1, float z1) {
    Point from = project(x0, y0, z0);
    Point to = project(x1, y1, z1);
    int steps = (int)(fabsf(to.x - from.x) + fabsf(to.y - from.y)) + 1;
    if (steps < 1) {
        steps = 1;
    }
    for (int i = 0; i <= steps; ++i) {
        float t = i / (float)steps;
        int x = (int)(from.x + (to.x - from.x) * t + 0.5f);
        int y = (int)(from.y + (to.y - from.y) * t + 0.5f);
        float depth = from.depth + (to.depth - from.depth) * t;
        paint(frame, x, y, depth + 0.4f, 18, 28, 46);
    }
}

static void draw_box(uint8_t *frame) {
    constexpr float lo = -0.35f;
    constexpr float hi = 5.35f;
    const float corner[2] = {lo, hi};
    for (int axis = 0; axis < 3; ++axis) {
        for (int a = 0; a < 2; ++a) {
            for (int b = 0; b < 2; ++b) {
                float from[3] = {corner[a], corner[b], lo};
                float to[3] = {corner[a], corner[b], lo};
                from[axis] = lo;
                to[axis] = hi;
                if (axis == 0) {
                    from[1] = corner[a];
                    from[2] = corner[b];
                    to[1] = corner[a];
                    to[2] = corner[b];
                    to[0] = hi;
                    from[0] = lo;
                } else if (axis == 1) {
                    from[0] = corner[a];
                    from[2] = corner[b];
                    to[0] = corner[a];
                    to[2] = corner[b];
                    from[1] = lo;
                    to[1] = hi;
                } else {
                    from[0] = corner[a];
                    from[1] = corner[b];
                    to[0] = corner[a];
                    to[1] = corner[b];
                    from[2] = lo;
                    to[2] = hi;
                }
                draw_edge(frame, from[0], from[1], from[2], to[0], to[1], to[2]);
            }
        }
    }
}

static void claim(int x, int y, int z) {
    int index = cell_index(x, y, z);
    if (occupied[index] == 0) {
        occupied[index] = 1;
        filled += 1;
    }
}

static void spawn(Pipe &pipe) {
    pipe.alive = false;
    for (int attempt = 0; attempt < 24; ++attempt) {
        int x = (int)(rnd() % GRID);
        int y = (int)(rnd() % GRID);
        int z = (int)(rnd() % GRID);
        if (!open_cell(x, y, z)) {
            continue;
        }
        int face = (int)(rnd() % 6);
        pipe.x = x;
        pipe.y = y;
        pipe.z = z;
        pipe.dx = DIRS[face][0];
        pipe.dy = DIRS[face][1];
        pipe.dz = DIRS[face][2];
        pipe.colour = (int)(rnd() % 6);
        pipe.alive = true;
        claim(x, y, z);
        return;
    }
}

static void reset_scene() {
    std::memset(occupied, 0, sizeof occupied);
    seg_count = 0;
    filled = 0;
    for (int i = 0; i < PIPE_COUNT; ++i) {
        spawn(pipes[i]);
    }
}

static bool same_dir(const Pipe &pipe, int dx, int dy, int dz) {
    return pipe.dx == dx && pipe.dy == dy && pipe.dz == dz;
}

static void add_segment(Pipe &pipe, int dx, int dy, int dz) {
    if (seg_count >= SEG_CAP) {
        return;
    }
    Seg &seg = segs[seg_count];
    seg.x = (int8_t)pipe.x;
    seg.y = (int8_t)pipe.y;
    seg.z = (int8_t)pipe.z;
    seg.dx = (int8_t)dx;
    seg.dy = (int8_t)dy;
    seg.dz = (int8_t)dz;
    seg.colour = (uint8_t)pipe.colour;
    bool continued = false;
    for (int i = 0; i < seg_count; ++i) {
        if (segs[i].x + segs[i].dx == pipe.x && segs[i].y + segs[i].dy == pipe.y && segs[i].z + segs[i].dz == pipe.z) {
            continued = true;
            break;
        }
    }
    seg.elbow = (!continued || !same_dir(pipe, dx, dy, dz)) ? 1 : 0;
    seg_count += 1;
    pipe.x += dx;
    pipe.y += dy;
    pipe.z += dz;
    pipe.dx = dx;
    pipe.dy = dy;
    pipe.dz = dz;
    claim(pipe.x, pipe.y, pipe.z);
}

static void grow_pipe(Pipe &pipe) {
    if (!pipe.alive) {
        spawn(pipe);
        return;
    }
    if ((rnd() % 10) < 7 && open_cell(pipe.x + pipe.dx, pipe.y + pipe.dy, pipe.z + pipe.dz)) {
        add_segment(pipe, pipe.dx, pipe.dy, pipe.dz);
        return;
    }
    int order[6] = {0, 1, 2, 3, 4, 5};
    for (int i = 5; i > 0; --i) {
        int swap = (int)(rnd() % (uint32_t)(i + 1));
        int held = order[i];
        order[i] = order[swap];
        order[swap] = held;
    }
    for (int i = 0; i < 6; ++i) {
        int dx = DIRS[order[i]][0];
        int dy = DIRS[order[i]][1];
        int dz = DIRS[order[i]][2];
        if (dx == -pipe.dx && dy == -pipe.dy && dz == -pipe.dz) {
            continue;
        }
        if (open_cell(pipe.x + dx, pipe.y + dy, pipe.z + dz)) {
            add_segment(pipe, dx, dy, dz);
            return;
        }
    }
    pipe.alive = false;
    spawn(pipe);
}

static void grow(int dt_ms) {
    grow_ms += dt_ms;
    while (grow_ms >= GROW_MS) {
        grow_ms -= GROW_MS;
        if (filled >= CROWD || seg_count >= SEG_CAP - PIPE_COUNT) {
            reset_scene();
            return;
        }
        for (int i = 0; i < PIPE_COUNT; ++i) {
            grow_pipe(pipes[i]);
        }
    }
}

static void draw_scene(uint8_t *frame) {
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        int blue = 10 + y / 10;
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            write_bgr(frame + (y * PANEL_WIDTH + x) * 3, 2, 6, blue);
        }
    }
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        depth_buf[i] = 65535;
    }
    draw_box(frame);
    for (int i = 0; i < seg_count; ++i) {
        const Seg &seg = segs[i];
        Point from = project(seg.x, seg.y, seg.z);
        Point to = project(seg.x + seg.dx, seg.y + seg.dy, seg.z + seg.dz);
        const uint8_t *colour = COLOURS[seg.colour % 6];
        draw_cylinder(frame, from, to, colour);
        if (seg.elbow) {
            draw_sphere(frame, from, colour);
        }
    }
    for (int i = 0; i < PIPE_COUNT; ++i) {
        if (!pipes[i].alive) {
            continue;
        }
        Point head = project(pipes[i].x, pipes[i].y, pipes[i].z);
        draw_sphere(frame, head, COLOURS[pipes[i].colour % 6]);
    }
}

}  // namespace

void pipes_start() {
    depth_buf = (uint16_t *)pattern_alloc(PANEL_PIXELS * (int)sizeof(uint16_t));
    rng = 0xA341316Cu;
    grow_ms = 0;
    reset_scene();
}

void pipes_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    grow(dt_ms);
    draw_scene(frame);
}
