// A terrain map solved tile by tile, then villages, paths, and travellers.
// The map follows Wave Function Collapse at
// https://www.ascii-magic.com/ascii/wave-function-collapse
// The collapse plays on the panel. A finished map is at least half land.
// Villages then sit on sand, grass, and forest. People walk the land, sail the water,
// and wear sandy paths where they repeat a route.
// Green pressed twice, inside the double-click window, solves a new map.

#include "pattern.hpp"

#include "pico/time.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int TILES = 7;
constexpr uint8_t ALL_TILES = 127;
constexpr int WEIGHT[TILES] = {3, 4, 2, 6, 4, 2, 1};
constexpr int VILLAGE_CAP = 16;
constexpr int BLOCK_CAP = 15;
constexpr int PERSON_CAP = 36;
constexpr int PATH_CAP = 160;
constexpr int SMOKE_CAP = 32;
constexpr int SPARK_CAP = 12;
constexpr float WALK_PX_PER_MS = 0.012f;

constexpr int MODE_WAIT = 0;
constexpr int MODE_MOVE = 1;
constexpr int MODE_REST = 2;

constexpr uint8_t INK[TILES][2][3] = {
    {{10, 42, 90}, {18, 58, 122}},
    {{26, 90, 168}, {42, 116, 200}},
    {{200, 176, 112}, {224, 200, 136}},
    {{58, 138, 58}, {90, 170, 72}},
    {{26, 90, 42}, {36, 106, 52}},
    {{122, 96, 72}, {154, 122, 90}},
    {{220, 230, 240}, {255, 255, 255}},
};

// Cyan stays apart from the water.
constexpr uint8_t ROOF[6][3] = {
    {220, 48, 40},
    {236, 132, 36},
    {240, 204, 64},
    {168, 72, 224},
    {56, 220, 236},
    {255, 48, 168},
};

struct Village {
    int sx;
    int sy;
    uint8_t roof;
    uint8_t blocks;
    uint8_t bx[BLOCK_CAP];
    uint8_t by[BLOCK_CAP];
    uint16_t smoke_ms;
};

struct Person {
    float x;
    float y;
    float face_x;
    float face_y;
    float quiet_ms;
    float rest_ms;
    float stuck_ms;
    float best_d;
    uint8_t home;
    uint8_t stops[4];
    uint8_t stop_count;
    uint8_t stop_i;
    uint8_t horse;
    uint8_t mode;
};

struct Smoke {
    float x;
    float y;
    float age;
    bool on;
};

struct Spark {
    int index;
    uint32_t at;
};

static uint32_t rng_state = 1;
static uint8_t *terrain = nullptr;
static uint8_t *wear = nullptr;
static uint16_t *parent = nullptr;
static uint16_t *bfs_queue = nullptr;
static uint8_t *path_x = nullptr;
static uint8_t *path_y = nullptr;
static uint8_t path_n[PERSON_CAP];
static uint8_t path_i[PERSON_CAP];
static Village villages[VILLAGE_CAP];
static Person people[PERSON_CAP];
static Smoke smoke[SMOKE_CAP];
static Spark sparks[SPARK_CAP];
static int spark_write = 0;
static int village_count = 0;
static int person_n = 0;
static int land_boost = 0;
static int contradictions = 0;
static int scan_cursor = 0;
static int grow_ms = 0;
static int phase_live = 0;
static uint32_t last_green = 0;
static uint16_t wave_stack[1536];

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static int rnd_int(int count) {
    if (count <= 1) {
        return 0;
    }
    int value = (int)(rnd() * (float)count);
    if (value >= count) {
        value = count - 1;
    }
    return value;
}

static int bits_in(uint8_t mask) {
    int count = 0;
    while (mask != 0) {
        count += mask & 1;
        mask = (uint8_t)(mask >> 1);
    }
    return count;
}

static int bit_index(uint8_t mask) {
    int tile = 0;
    while ((mask & 1) == 0 && tile < TILES) {
        mask = (uint8_t)(mask >> 1);
        tile++;
    }
    return tile;
}

static uint8_t adjacency(uint8_t mask) {
    return (uint8_t)((mask | (uint8_t)(mask << 1) | (uint8_t)(mask >> 1)) & ALL_TILES);
}

static int tile_weight(int tile) {
    int weight = WEIGHT[tile];
    if (tile >= 2 && tile <= 4) {
        weight += land_boost;
    }
    if (weight < 1) {
        weight = 1;
    }
    return weight;
}

static int settlement_gap() {
    if (PANEL_COUNT == 4) {
        return 32;
    }
    return 16;
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

static int tile_at(int x, int y) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return 5;
    }
    return terrain[y * PANEL_WIDTH + x];
}

static bool passable_px(int x, int y) {
    return tile_at(x, y) < 5;
}

static bool on_land_px(int x, int y) {
    int tile = tile_at(x, y);
    return tile >= 2 && tile <= 4;
}

static void roof_rgb(int roof, int &red, int &green, int &blue) {
    red = ROOF[roof][0];
    green = ROOF[roof][1];
    blue = ROOF[roof][2];
}

static void sail_rgb(int roof, int &red, int &green, int &blue) {
    if (roof == 4) {
        red = 96;
        green = 230;
        blue = 255;
        return;
    }
    roof_rgb(roof, red, green, blue);
}

static void note_spark(int index) {
    sparks[spark_write].index = index;
    sparks[spark_write].at = time_us_32();
    spark_write++;
    if (spark_write >= SPARK_CAP) {
        spark_write = 0;
    }
}

static bool propagate(int start) {
    int top = 0;
    wave_stack[top++] = (uint16_t)start;
    bool overflow = false;
    while (top > 0) {
        int index = wave_stack[--top];
        uint8_t allow = adjacency(terrain[index]);
        int x = index % PANEL_WIDTH;
        int y = index / PANEL_WIDTH;
        const int step_x[4] = {1, -1, 0, 0};
        const int step_y[4] = {0, 0, 1, -1};
        for (int dir = 0; dir < 4; ++dir) {
            int nx = x + step_x[dir];
            int ny = y + step_y[dir];
            if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
                continue;
            }
            int next = ny * PANEL_WIDTH + nx;
            uint8_t narrowed = (uint8_t)(terrain[next] & allow);
            if (narrowed == 0) {
                return false;
            }
            if (narrowed == terrain[next]) {
                continue;
            }
            terrain[next] = narrowed;
            if (top < (int)(sizeof wave_stack / sizeof wave_stack[0])) {
                wave_stack[top++] = (uint16_t)next;
            } else {
                overflow = true;
            }
        }
    }
    if (!overflow) {
        return true;
    }
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        for (int y = 0; y < PANEL_HEIGHT; ++y) {
            for (int x = 0; x < PANEL_WIDTH; ++x) {
                int index = y * PANEL_WIDTH + x;
                uint8_t allow = adjacency(terrain[index]);
                const int step_x[4] = {1, -1, 0, 0};
                const int step_y[4] = {0, 0, 1, -1};
                for (int dir = 0; dir < 4; ++dir) {
                    int nx = x + step_x[dir];
                    int ny = y + step_y[dir];
                    if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
                        continue;
                    }
                    int next = ny * PANEL_WIDTH + nx;
                    uint8_t narrowed = (uint8_t)(terrain[next] & allow);
                    if (narrowed == 0) {
                        return false;
                    }
                    if (narrowed != terrain[next]) {
                        terrain[next] = narrowed;
                        changed = true;
                    }
                }
            }
        }
        if (!changed) {
            break;
        }
    }
    return true;
}

static int choose_cell() {
    int best = -1;
    int best_count = 99;
    int pixels = PANEL_PIXELS;
    for (int sample = 0; sample < 48; ++sample) {
        int index = rnd_int(pixels);
        int count = bits_in(terrain[index]);
        if (count > 1 && count < best_count) {
            best = index;
            best_count = count;
            if (count == 2) {
                return best;
            }
        }
    }
    if (best >= 0) {
        return best;
    }
    for (int step = 0; step < pixels; ++step) {
        int index = scan_cursor + step;
        if (index >= pixels) {
            index -= pixels;
        }
        if (bits_in(terrain[index]) > 1) {
            scan_cursor = index + 1;
            if (scan_cursor >= pixels) {
                scan_cursor = 0;
            }
            return index;
        }
    }
    return -1;
}

// 1 collapsed a cell, 0 the map is finished, -1 the rules contradicted.
static int collapse_one() {
    int index = choose_cell();
    if (index < 0) {
        return 0;
    }
    uint8_t mask = terrain[index];
    int open = bits_in(mask);
    if (open <= 1) {
        return 0;
    }
    int total = 0;
    for (int tile = 0; tile < TILES; ++tile) {
        if ((mask & (uint8_t)(1 << tile)) != 0) {
            total += tile_weight(tile);
        }
    }
    if (total <= 0) {
        return -1;
    }
    int ticket = rnd_int(total);
    int pick = 0;
    for (int tile = 0; tile < TILES; ++tile) {
        if ((mask & (uint8_t)(1 << tile)) == 0) {
            continue;
        }
        ticket -= tile_weight(tile);
        if (ticket < 0) {
            pick = tile;
            break;
        }
    }
    terrain[index] = (uint8_t)(1 << pick);
    note_spark(index);
    if (!propagate(index)) {
        return -1;
    }
    return 1;
}

static void refill_masks() {
    std::memset(terrain, ALL_TILES, (size_t)PANEL_PIXELS);
    scan_cursor = 0;
}

static bool block_room(int x, int y, int self) {
    if (x < 0 || y < 0 || x + 1 >= PANEL_WIDTH || y + 1 >= PANEL_HEIGHT) {
        return false;
    }
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int tile = tile_at(x + dx, y + dy);
            if (tile < 2 || tile > 4) {
                return false;
            }
        }
    }
    for (int v = 0; v < village_count; ++v) {
        int limit = v == self ? 3 : 4;
        for (int b = 0; b < villages[v].blocks; ++b) {
            int dx = (int)villages[v].bx[b] - x;
            int dy = (int)villages[v].by[b] - y;
            if (dx < 0) {
                dx = -dx;
            }
            if (dy < 0) {
                dy = -dy;
            }
            if (dx < limit && dy < limit) {
                return false;
            }
        }
    }
    return true;
}

static bool far_from_seeds(int x, int y) {
    int gap = settlement_gap();
    float cx = (float)x + 0.5f;
    float cy = (float)y + 0.5f;
    for (int v = 0; v < village_count; ++v) {
        float dx = cx - ((float)villages[v].sx + 0.5f);
        float dy = cy - ((float)villages[v].sy + 0.5f);
        if (dx * dx + dy * dy < (float)(gap * gap)) {
            return false;
        }
    }
    return true;
}

static bool place_seed() {
    if (village_count >= VILLAGE_CAP) {
        return false;
    }
    for (int attempt = 0; attempt < 80; ++attempt) {
        int x = rnd_int(PANEL_WIDTH - 1);
        int y = rnd_int(PANEL_HEIGHT - 1);
        if (!far_from_seeds(x, y) || !block_room(x, y, -1)) {
            continue;
        }
        Village &village = villages[village_count];
        village.sx = x;
        village.sy = y;
        village.roof = (uint8_t)rnd_int(6);
        village.blocks = 1;
        village.bx[0] = (uint8_t)x;
        village.by[0] = (uint8_t)y;
        village.smoke_ms = (uint16_t)rnd_int(2500);
        village_count++;
        return true;
    }
    return false;
}

static void grow_block(int index) {
    Village &village = villages[index];
    if (village.blocks >= BLOCK_CAP) {
        return;
    }
    int gap = settlement_gap();
    float reach = (float)gap * 0.5f - 2.0f;
    if (reach < 2.0f) {
        reach = 2.0f;
    }
    float ox = (float)village.sx + 0.5f;
    float oy = (float)village.sy + 0.5f;
    for (int attempt = 0; attempt < 24; ++attempt) {
        float angle = rnd() * 6.2831853f;
        float radius = 2.0f + rnd() * reach;
        int x = (int)(ox + cosf(angle) * radius) - 1;
        int y = (int)(oy + sinf(angle) * radius) - 1;
        float dx = (float)x + 0.5f - ox;
        float dy = (float)y + 0.5f - oy;
        if (dx * dx + dy * dy > reach * reach) {
            continue;
        }
        if (!block_room(x, y, index)) {
            continue;
        }
        village.bx[village.blocks] = (uint8_t)x;
        village.by[village.blocks] = (uint8_t)y;
        village.blocks++;
        return;
    }
}

static int pick_village(int skip_a, int skip_b) {
    int total = 0;
    for (int v = 0; v < village_count; ++v) {
        if (v == skip_a || v == skip_b) {
            continue;
        }
        total += villages[v].blocks;
    }
    if (total <= 0) {
        return -1;
    }
    int ticket = rnd_int(total);
    for (int v = 0; v < village_count; ++v) {
        if (v == skip_a || v == skip_b) {
            continue;
        }
        ticket -= villages[v].blocks;
        if (ticket < 0) {
            return v;
        }
    }
    return -1;
}

static void spawn_person(int index) {
    Person &person = people[index];
    int home = pick_village(-1, -1);
    if (home < 0) {
        home = 0;
    }
    person.home = (uint8_t)home;
    person.x = (float)villages[home].sx + rnd();
    person.y = (float)villages[home].sy + rnd();
    person.face_x = 1.0f;
    person.face_y = 0.0f;
    person.quiet_ms = rnd() * 60000.0f;
    person.rest_ms = 0.0f;
    person.stuck_ms = 0.0f;
    person.best_d = 10000.0f;
    person.horse = rnd() < (1.0f / 6.0f) ? 1 : 0;
    person.mode = MODE_WAIT;
    person.stop_count = 0;
    person.stop_i = 0;
    int previous = home;
    int trips = 1 + rnd_int(4);
    for (int s = 0; s < trips && person.stop_count < 4; ++s) {
        int next = pick_village(home, previous);
        if (next < 0) {
            next = pick_village(home, -1);
        }
        if (next < 0) {
            break;
        }
        person.stops[person.stop_count++] = (uint8_t)next;
        previous = next;
    }
    path_n[index] = 0;
    path_i[index] = 0;
    if (person.stop_count == 0) {
        person.mode = MODE_MOVE;
    }
}

static bool coarse_open(int cx, int cy, int coarse_w, int coarse_h) {
    if (cx < 0 || cy < 0 || cx >= coarse_w || cy >= coarse_h) {
        return false;
    }
    int x0 = cx * 2;
    int y0 = cy * 2;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int x = x0 + dx;
            int y = y0 + dy;
            if (x < PANEL_WIDTH && y < PANEL_HEIGHT && tile_at(x, y) < 5) {
                return true;
            }
        }
    }
    return false;
}

static bool seek_open(int &cx, int &cy, int coarse_w, int coarse_h) {
    if (coarse_open(cx, cy, coarse_w, coarse_h)) {
        return true;
    }
    for (int radius = 1; radius <= 8; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx != radius && dx != -radius && dy != radius && dy != -radius) {
                    continue;
                }
                int x = cx + dx;
                int y = cy + dy;
                if (coarse_open(x, y, coarse_w, coarse_h)) {
                    cx = x;
                    cy = y;
                    return true;
                }
            }
        }
    }
    return false;
}

static bool cell_anchor(int cx, int cy, int &ax, int &ay) {
    int x0 = cx * 2;
    int y0 = cy * 2;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int x = x0 + dx;
            int y = y0 + dy;
            if (x < PANEL_WIDTH && y < PANEL_HEIGHT && tile_at(x, y) < 5) {
                ax = x;
                ay = y;
                return true;
            }
        }
    }
    ax = x0;
    ay = y0;
    return false;
}

static void target_of(const Person &person, float &tx, float &ty) {
    int id = person.home;
    if (person.stop_i < person.stop_count) {
        id = person.stops[person.stop_i];
    }
    tx = (float)villages[id].sx + 0.5f;
    ty = (float)villages[id].sy + 0.5f;
}

static bool build_route(int index) {
    Person &person = people[index];
    float tx;
    float ty;
    target_of(person, tx, ty);
    int coarse_w = PANEL_WIDTH / 2;
    int coarse_h = PANEL_HEIGHT / 2;
    int coarse_n = coarse_w * coarse_h;
    int sx = (int)person.x / 2;
    int sy = (int)person.y / 2;
    int gx = (int)tx / 2;
    int gy = (int)ty / 2;
    if (!seek_open(sx, sy, coarse_w, coarse_h) || !seek_open(gx, gy, coarse_w, coarse_h)) {
        return false;
    }
    for (int i = 0; i < coarse_n; ++i) {
        parent[i] = 0xffff;
    }
    int start = sy * coarse_w + sx;
    int goal = gy * coarse_w + gx;
    parent[start] = (uint16_t)start;
    int head = 0;
    int tail = 0;
    bfs_queue[tail++] = (uint16_t)start;
    const int step_x[4] = {1, -1, 0, 0};
    const int step_y[4] = {0, 0, 1, -1};
    bool found = start == goal;
    while (head < tail && !found) {
        int cur = bfs_queue[head++];
        int cx = cur % coarse_w;
        int cy = cur / coarse_w;
        for (int dir = 0; dir < 4; ++dir) {
            int nx = cx + step_x[dir];
            int ny = cy + step_y[dir];
            if (!coarse_open(nx, ny, coarse_w, coarse_h)) {
                continue;
            }
            int next = ny * coarse_w + nx;
            if (parent[next] != 0xffff) {
                continue;
            }
            parent[next] = (uint16_t)cur;
            bfs_queue[tail++] = (uint16_t)next;
            if (next == goal) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }
    uint8_t ring_x[PATH_CAP];
    uint8_t ring_y[PATH_CAP];
    int writes = 0;
    int cur = goal;
    int guard = 0;
    while (guard++ < coarse_n) {
        int cx = cur % coarse_w;
        int cy = cur / coarse_w;
        int ax;
        int ay;
        cell_anchor(cx, cy, ax, ay);
        ring_x[writes % PATH_CAP] = (uint8_t)ax;
        ring_y[writes % PATH_CAP] = (uint8_t)ay;
        writes++;
        if (parent[cur] == cur) {
            break;
        }
        cur = parent[cur];
    }
    int keep = writes < PATH_CAP ? writes : PATH_CAP;
    int base = index * PATH_CAP;
    for (int i = 0; i < keep; ++i) {
        int slot = writes - 1 - i;
        path_x[base + i] = ring_x[slot % PATH_CAP];
        path_y[base + i] = ring_y[slot % PATH_CAP];
    }
    path_n[index] = (uint8_t)keep;
    path_i[index] = 0;
    person.mode = MODE_MOVE;
    person.best_d = 10000.0f;
    person.stuck_ms = 0.0f;
    return true;
}

static void advance_stop(int index) {
    Person &person = people[index];
    person.stop_i++;
    person.mode = MODE_WAIT;
    path_n[index] = 0;
    path_i[index] = 0;
    person.best_d = 10000.0f;
    person.stuck_ms = 0.0f;
    if (person.stop_i > person.stop_count) {
        spawn_person(index);
    }
}

static void serve_routes() {
    int served = 0;
    for (int i = 0; i < person_n && served < 4; ++i) {
        if (people[i].mode != MODE_WAIT) {
            continue;
        }
        served++;
        if (!build_route(i)) {
            advance_stop(i);
        }
    }
}

static void separate(Person &person) {
    float push_x = 0.0f;
    float push_y = 0.0f;
    for (int i = 0; i < person_n; ++i) {
        if (&people[i] == &person) {
            continue;
        }
        float dx = person.x - people[i].x;
        float dy = person.y - people[i].y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 >= 4.0f || dist2 < 0.0001f) {
            continue;
        }
        float dist = sqrtf(dist2);
        float gap = 2.0f - dist;
        push_x += dx / dist * gap;
        push_y += dy / dist * gap;
    }
    float nx = person.x + push_x * 0.45f;
    float ny = person.y + push_y * 0.45f;
    if (passable_px((int)nx, (int)person.y)) {
        person.x = nx;
    }
    if (passable_px((int)person.x, (int)ny)) {
        person.y = ny;
    }
}

static void move_person(Person &person, int index, int dt_ms) {
    if (person.mode == MODE_WAIT) {
        return;
    }
    if (person.stop_count == 0) {
        person.quiet_ms -= (float)dt_ms;
        if (person.quiet_ms <= 0.0f) {
            person.mode = MODE_REST;
            person.rest_ms = 2000.0f;
            person.quiet_ms = 45000.0f + rnd() * 30000.0f;
        }
        separate(person);
        return;
    }
    int px = (int)person.x;
    int py = (int)person.y;
    if (person.mode == MODE_REST) {
        person.rest_ms -= (float)dt_ms;
        if (person.rest_ms <= 0.0f) {
            person.mode = MODE_MOVE;
            person.quiet_ms = 45000.0f + rnd() * 30000.0f;
        }
        separate(person);
        return;
    }
    float tx;
    float ty;
    target_of(person, tx, ty);
    float goal_dx = tx - person.x;
    float goal_dy = ty - person.y;
    float goal_d = sqrtf(goal_dx * goal_dx + goal_dy * goal_dy);
    if (goal_d < 1.6f) {
        advance_stop(index);
        return;
    }
    if (person.stuck_ms > 2500.0f) {
        advance_stop(index);
        return;
    }
    if (path_i[index] >= path_n[index]) {
        person.mode = MODE_WAIT;
        return;
    }
    int base = index * PATH_CAP + path_i[index];
    float wx = (float)path_x[base] + 0.5f;
    float wy = (float)path_y[base] + 0.5f;
    float dx = wx - person.x;
    float dy = wy - person.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.75f) {
        path_i[index]++;
        return;
    }
    float step = WALK_PX_PER_MS * (float)dt_ms;
    if (step > dist) {
        step = dist;
    }
    float ux = dx / dist;
    float uy = dy / dist;
    person.face_x = ux;
    person.face_y = uy;
    float nx = person.x + ux * step;
    float ny = person.y + uy * step;
    if (!passable_px((int)nx, (int)ny)) {
        if (passable_px((int)nx, (int)person.y)) {
            ny = person.y;
        } else if (passable_px((int)person.x, (int)ny)) {
            nx = person.x;
        } else {
            person.stuck_ms += (float)dt_ms;
            return;
        }
    } else {
        person.stuck_ms = 0.0f;
    }
    int ox = (int)person.x;
    int oy = (int)person.y;
    person.x = nx;
    person.y = ny;
    separate(person);
    int mx = (int)person.x;
    int my = (int)person.y;
    if ((mx != ox || my != oy) && on_land_px(mx, my)) {
        int at = my * PANEL_WIDTH + mx;
        int worn = wear[at] + 4;
        if (worn > 255) {
            worn = 255;
        }
        wear[at] = (uint8_t)worn;
    }
    if (on_land_px(px, py)) {
        person.quiet_ms -= (float)dt_ms;
        if (person.quiet_ms <= 0.0f) {
            person.mode = MODE_REST;
            person.rest_ms = 2000.0f;
        }
    } else if (person.quiet_ms <= 0.0f) {
        person.quiet_ms = 8000.0f;
    }
}

static void emit_smoke(const Village &village) {
    int slot = -1;
    for (int i = 0; i < SMOKE_CAP; ++i) {
        if (!smoke[i].on) {
            slot = i;
            break;
        }
    }
    if (slot < 0 || village.blocks == 0) {
        return;
    }
    int block = rnd_int(village.blocks);
    smoke[slot].on = true;
    smoke[slot].age = 0.0f;
    smoke[slot].x = (float)village.bx[block] + rnd() * 2.0f;
    smoke[slot].y = (float)village.by[block] - 2.4f;
}

static void simulate(int dt_ms) {
    grow_ms += dt_ms;
    while (grow_ms >= 1000) {
        grow_ms -= 1000;
        for (int v = 0; v < village_count; ++v) {
            if (villages[v].blocks < BLOCK_CAP && rnd() < (1.0f / 300.0f)) {
                grow_block(v);
            }
        }
    }
    for (int v = 0; v < village_count; ++v) {
        int due = villages[v].smoke_ms + dt_ms;
        if (due >= 2600) {
            emit_smoke(villages[v]);
            due = rnd_int(800);
        }
        villages[v].smoke_ms = (uint16_t)due;
    }
    for (int i = 0; i < SMOKE_CAP; ++i) {
        if (!smoke[i].on) {
            continue;
        }
        smoke[i].age += (float)dt_ms * 0.001f;
        smoke[i].y -= (float)dt_ms * 0.005f;
        if (smoke[i].age > 1.6f) {
            smoke[i].on = false;
        }
    }
    serve_routes();
    for (int i = 0; i < person_n; ++i) {
        move_person(people[i], i, dt_ms);
    }
}

static bool finish_map() {
    int land = 0;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (bits_in(terrain[i]) != 1) {
            refill_masks();
            return false;
        }
        if (bit_index(terrain[i]) >= 2) {
            land++;
        }
    }
    if (land * 2 < PANEL_PIXELS) {
        refill_masks();
        if (land_boost < 20) {
            land_boost++;
        }
        return false;
    }
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        terrain[i] = (uint8_t)bit_index(terrain[i]);
    }
    village_count = 0;
    for (int attempt = 0; attempt < 500 && village_count < VILLAGE_CAP; ++attempt) {
        if (!place_seed()) {
            break;
        }
    }
    if (village_count < 2 && land_boost < 14) {
        refill_masks();
        land_boost++;
        return false;
    }
    wear = pattern_alloc(PANEL_PIXELS);
    int coarse_n = (PANEL_WIDTH / 2) * (PANEL_HEIGHT / 2);
    parent = (uint16_t *)pattern_alloc(coarse_n * (int)sizeof(uint16_t));
    bfs_queue = (uint16_t *)pattern_alloc(coarse_n * (int)sizeof(uint16_t));
    path_x = pattern_alloc(PERSON_CAP * PATH_CAP);
    path_y = pattern_alloc(PERSON_CAP * PATH_CAP);
    person_n = 36 * PANEL_PIXELS / (128 * 128);
    if (person_n > PERSON_CAP) {
        person_n = PERSON_CAP;
    }
    if (village_count == 0) {
        person_n = 0;
    }
    for (int i = 0; i < person_n; ++i) {
        spawn_person(i);
    }
    for (int i = 0; i < SMOKE_CAP; ++i) {
        smoke[i].on = false;
    }
    grow_ms = 0;
    phase_live = 1;
    return true;
}

static void solve_some() {
    uint32_t start = time_us_32();
    int collapses = 0;
    while (collapses < 80 && (int)(time_us_32() - start) < 5000) {
        int result = collapse_one();
        if (result == 0) {
            finish_map();
            return;
        }
        if (result < 0) {
            refill_masks();
            contradictions++;
            if (contradictions >= 8) {
                contradictions = 0;
                if (land_boost < 20) {
                    land_boost++;
                }
            }
            return;
        }
        collapses++;
    }
}

static void paint_tile(uint8_t *frame, int x, int y, int tile) {
    int shade = ((x * 13 + y * 7) & 3) == 0 ? 1 : 0;
    int red = INK[tile][shade][0];
    int green = INK[tile][shade][1];
    int blue = INK[tile][shade][2];
    if (wear != nullptr && tile >= 2 && tile <= 4) {
        float worn = (float)wear[y * PANEL_WIDTH + x] * (1.0f / 255.0f);
        red = (int)((float)red + (206.0f - (float)red) * worn);
        green = (int)((float)green + (168.0f - (float)green) * worn);
        blue = (int)((float)blue + (96.0f - (float)blue) * worn);
    }
    put(frame, x, y, red, green, blue);
}

static void paint(uint8_t *frame, uint32_t anim_ms) {
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = y * PANEL_WIDTH + x;
            uint8_t mask = terrain[index];
            if (phase_live) {
                paint_tile(frame, x, y, mask);
                continue;
            }
            int count = bits_in(mask);
            if (count == 1) {
                paint_tile(frame, x, y, bit_index(mask));
                continue;
            }
            int level = count >= 6 ? 28 : count >= 4 ? 52 : count >= 3 ? 78 : 118;
            put(frame, x, y, level, level + 6, level + 24);
        }
    }
    uint32_t now = time_us_32();
    if (!phase_live) {
        for (int s = 0; s < SPARK_CAP; ++s) {
            if (sparks[s].index < 0) {
                continue;
            }
            uint32_t age = now - sparks[s].at;
            if (age < 180000) {
                int index = sparks[s].index;
                put(frame, index % PANEL_WIDTH, index / PANEL_WIDTH, 255, 236, 170);
            }
        }
        return;
    }
    for (int v = 0; v < village_count; ++v) {
        for (int b = 0; b < villages[v].blocks; ++b) {
            int x = villages[v].bx[b];
            int y = villages[v].by[b];
            put(frame, x, y, 86, 56, 34);
            put(frame, x + 1, y, 86, 56, 34);
            put(frame, x, y + 1, 62, 40, 24);
            put(frame, x + 1, y + 1, 62, 40, 24);
        }
    }
    for (int v = 0; v < village_count; ++v) {
        int red;
        int green;
        int blue;
        roof_rgb(villages[v].roof, red, green, blue);
        for (int b = 0; b < villages[v].blocks; ++b) {
            int x = villages[v].bx[b];
            int y = villages[v].by[b];
            // Eaves hang one pixel past each wall. The row above is the ridge.
            put(frame, x - 1, y - 1, red, green, blue);
            put(frame, x, y - 1, red, green, blue);
            put(frame, x + 1, y - 1, red, green, blue);
            put(frame, x + 2, y - 1, red, green, blue);
            put(frame, x, y - 2, red, green, blue);
            put(frame, x + 1, y - 2, red, green, blue);
        }
    }
    for (int i = 0; i < SMOKE_CAP; ++i) {
        if (!smoke[i].on) {
            continue;
        }
        float fade = 1.0f - smoke[i].age / 1.6f;
        if (fade < 0.0f) {
            fade = 0.0f;
        }
        int level = (int)(120.0f + 120.0f * fade);
        if ((i & 1) == 0) {
            level -= 30;
        }
        put(frame, (int)smoke[i].x, (int)smoke[i].y, level, level, level + 8);
    }
    int fire_phase = (int)((anim_ms / 90) % 3);
    const int fire_r[3] = {240, 255, 210};
    const int fire_g[3] = {70, 170, 36};
    const int fire_b[3] = {20, 30, 18};
    for (int i = 0; i < person_n; ++i) {
        Person &person = people[i];
        int x = (int)person.x;
        int y = (int)person.y;
        int roof = villages[person.home].roof;
        bool water = tile_at(x, y) <= 1;
        if (water) {
            int sail_r;
            int sail_g;
            int sail_b;
            sail_rgb(roof, sail_r, sail_g, sail_b);
            int bx = x - (int)(person.face_x * 1.2f);
            int by = y - (int)(person.face_y * 1.2f);
            put(frame, x, y, 132, 84, 42);
            put(frame, bx, by, 96, 58, 28);
            if (person.horse) {
                put(frame, bx - (int)person.face_x, by - (int)person.face_y, 78, 46, 22);
            }
            put(frame, x - (int)person.face_y, y + (int)person.face_x, sail_r, sail_g, sail_b);
        } else {
            int red;
            int green;
            int blue;
            roof_rgb(roof, red, green, blue);
            put(frame, x, y, red, green, blue);
            if (person.horse) {
                int hx = x + (int)(-person.face_y * 2.0f);
                int hy = y + (int)(person.face_x * 2.0f);
                put(frame, hx, hy, 196, 154, 96);
                put(frame, hx + (int)person.face_x, hy + (int)person.face_y, 150, 112, 64);
            }
        }
        if (person.mode == MODE_REST) {
            int phase = fire_phase;
            put(frame, x + 2, y, fire_r[phase], fire_g[phase], fire_b[phase]);
            put(frame, x + 3, y + 1, fire_r[(phase + 1) % 3], fire_g[(phase + 1) % 3], fire_b[(phase + 1) % 3]);
            put(frame, x + 2, y - 1, fire_r[(phase + 2) % 3], fire_g[(phase + 2) % 3], fire_b[(phase + 2) % 3]);
        }
    }
}

static void boot(uint32_t seed) {
    pattern_reset();
    rng_state = seed | 1u;
    phase_live = 0;
    land_boost = 0;
    contradictions = 0;
    village_count = 0;
    person_n = 0;
    scan_cursor = 0;
    grow_ms = 0;
    spark_write = 0;
    wear = nullptr;
    parent = nullptr;
    bfs_queue = nullptr;
    path_x = nullptr;
    path_y = nullptr;
    for (int i = 0; i < SPARK_CAP; ++i) {
        sparks[i].index = -1;
        sparks[i].at = 0;
    }
    terrain = pattern_alloc(PANEL_PIXELS);
    refill_masks();
}

}  // namespace

void civ_green(uint32_t now_ms, int window_ms) {
    if (last_green != 0 && (int)(now_ms - last_green) > 40 && (int)(now_ms - last_green) <= window_ms) {
        last_green = 0;
        boot(time_us_32());
        return;
    }
    last_green = now_ms;
}

void civ_start() {
    last_green = 0;
    boot(time_us_32());
}

void civ_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    if (dt_ms > 80) {
        dt_ms = 80;
    }
    if (!phase_live) {
        solve_some();
    }
    if (phase_live) {
        simulate(dt_ms);
    }
    paint(frame, anim_ms);
}
