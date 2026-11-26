// A terrain map solved tile by tile, then villages, paths, and travellers.
// The map follows Wave Function Collapse at
// https://www.ascii-magic.com/ascii/wave-function-collapse
// The collapse plays on the panel. A few water seeds grow into lakes and seas
// while most of the map stays land, and a river sometimes joins two lakes.
// Villages then sit on sand, grass, and forest. People bow across the land,
// sometimes sail, and wear sandy paths where they repeat a route.
// Green pressed twice, inside the double-click window, solves a new map.

#include "pattern.hpp"

#include "pico/time.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int TILES = 7;
constexpr uint8_t ALL_TILES = 127;
constexpr int SEED_LIMIT = 4;
constexpr int WATER_CAP_PERCENT = 24;
constexpr int VILLAGE_CAP = 16;
constexpr int BLOCK_CAP = 15;
constexpr int PERSON_CAP = 72;
constexpr int PATH_CAP = 160;
constexpr int SMOKE_CAP = 32;
constexpr int SPARK_CAP = 12;
constexpr float WALK_PX_PER_MS = 0.012f;
constexpr float SAIL_CHANCE = 0.2f;

constexpr int MODE_WAIT = 0;
constexpr int MODE_MOVE = 1;
constexpr int MODE_REST = 2;
constexpr int MODE_VISIT = 3;

constexpr uint8_t INK[TILES][2][3] = {
    {{10, 42, 90}, {18, 58, 122}},
    {{26, 90, 168}, {42, 116, 200}},
    {{200, 176, 112}, {224, 200, 136}},
    {{58, 138, 58}, {90, 170, 72}},
    {{26, 90, 42}, {36, 106, 52}},
    {{122, 96, 72}, {154, 122, 90}},
    {{220, 230, 240}, {255, 255, 255}},
};

// Cyan stays apart from the water. Villages take these colours in order.
constexpr int ROOF_COUNT = 6;
constexpr uint8_t ROOF[ROOF_COUNT][3] = {
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
    uint8_t mode;
    uint8_t through;
};

struct Smoke {
    float x;
    float y;
    float age;
    bool on;
};

struct Camp {
    int px;
    int py;
    float life;
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
static Camp camps[PERSON_CAP];
static Spark sparks[SPARK_CAP];
static int spark_write = 0;
static int village_count = 0;
static int person_n = 0;
static int scan_cursor = 0;
static int water_count = 0;
static int seed_n = 0;
static int seeds_closed = 0;
static int seed_x[SEED_LIMIT];
static int seed_y[SEED_LIMIT];
static uint16_t shore[512];
static int shore_n = 0;
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

static int settlement_gap() {
    if (PANEL_COUNT == 4) {
        return 32;
    }
    return 16;
}

static int house_span() {
    if (PANEL_COUNT >= 4) {
        return 3;
    }
    return 2;
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

// Mountain and snow are open. Only the edge of the panel stops a step.
static bool passable_px(int x, int y) {
    return x >= 0 && y >= 0 && x < PANEL_WIDTH && y < PANEL_HEIGHT;
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

static int lake_seed_cap() {
    if (PANEL_COUNT >= 4) {
        return 4;
    }
    if (PANEL_COUNT == 2) {
        return 3;
    }
    return 2;
}

static int lake_gap() {
    if (PANEL_COUNT >= 4) {
        return 26;
    }
    if (PANEL_COUNT == 2) {
        return 20;
    }
    return 16;
}

static bool water_under(int percent) {
    return water_count * 100 < PANEL_PIXELS * percent;
}

static int touches_water(int index) {
    int x = index % PANEL_WIDTH;
    int y = index / PANEL_WIDTH;
    int count = 0;
    const int step_x[4] = {1, -1, 0, 0};
    const int step_y[4] = {0, 0, 1, -1};
    for (int dir = 0; dir < 4; ++dir) {
        int nx = x + step_x[dir];
        int ny = y + step_y[dir];
        if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
            continue;
        }
        uint8_t mask = terrain[ny * PANEL_WIDTH + nx];
        if (bits_in(mask) == 1 && bit_index(mask) <= 1) {
            count++;
        }
    }
    return count;
}

static bool near_seed(int index, int radius) {
    int x = index % PANEL_WIDTH;
    int y = index / PANEL_WIDTH;
    for (int i = 0; i < seed_n; ++i) {
        int dx = x - seed_x[i];
        int dy = y - seed_y[i];
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (dx <= radius && dy <= radius) {
            return true;
        }
    }
    return false;
}

static int shore_owner(int index) {
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
        uint8_t mask = terrain[next];
        if (bits_in(mask) == 1 && bit_index(mask) <= 1 && wear != nullptr && wear[next] != 0) {
            return wear[next];
        }
    }
    return 0;
}

// True when this cell sits on another lake's side of the gap between seeds.
static bool beside_other_lake(int index, int own) {
    if (own <= 0 || own > seed_n || seed_n < 2) {
        return false;
    }
    int x = index % PANEL_WIDTH;
    int y = index / PANEL_WIDTH;
    int ox = x - seed_x[own - 1];
    int oy = y - seed_y[own - 1];
    if (ox < 0) {
        ox = -ox;
    }
    if (oy < 0) {
        oy = -oy;
    }
    int own_d = ox + oy;
    for (int i = 0; i < seed_n; ++i) {
        if (i + 1 == own) {
            continue;
        }
        int dx = x - seed_x[i];
        int dy = y - seed_y[i];
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (dx + dy <= own_d + 3) {
            return true;
        }
    }
    return false;
}

static void push_shore(int index) {
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
        if (bits_in(terrain[next]) <= 1) {
            continue;
        }
        if (shore_n < (int)(sizeof shore / sizeof shore[0])) {
            shore[shore_n++] = (uint16_t)next;
        } else {
            shore[rnd_int(shore_n)] = (uint16_t)next;
        }
    }
}

// A decided water cell joins the lake it touches, or opens a new one while seeding.
static void write_mask(int index, uint8_t narrowed) {
    uint8_t before = terrain[index];
    if (before == narrowed) {
        return;
    }
    terrain[index] = narrowed;
    if (bits_in(before) == 1 || bits_in(narrowed) != 1) {
        return;
    }
    int tile = bit_index(narrowed);
    if (tile > 1) {
        return;
    }
    water_count++;
    int own = shore_owner(index);
    if (own == 0) {
        if (seeds_closed || seed_n >= lake_seed_cap()) {
            return;
        }
        seed_x[seed_n] = index % PANEL_WIDTH;
        seed_y[seed_n] = index / PANEL_WIDTH;
        seed_n++;
        own = seed_n;
        if (seed_n >= lake_seed_cap()) {
            seeds_closed = 1;
        }
    }
    if (wear != nullptr) {
        wear[index] = (uint8_t)own;
    }
    push_shore(index);
}

static int find_seed_cell() {
    int start = rnd_int(PANEL_PIXELS);
    for (int attempt = 0; attempt < 80 + PANEL_PIXELS; ++attempt) {
        int index = attempt < 80 ? rnd_int(PANEL_PIXELS) : start + (attempt - 80);
        if (index >= PANEL_PIXELS) {
            index -= PANEL_PIXELS;
        }
        uint8_t mask = terrain[index];
        if (bits_in(mask) <= 1 || (mask & 3) == 0) {
            continue;
        }
        if (near_seed(index, lake_gap())) {
            continue;
        }
        return index;
    }
    return -1;
}

static int take_shore() {
    if (!water_under(WATER_CAP_PERCENT) || shore_n <= 0) {
        return -1;
    }
    for (int attempt = 0; attempt < 12 && shore_n > 0; ++attempt) {
        int slot = rnd_int(shore_n);
        int index = shore[slot];
        shore[slot] = shore[--shore_n];
        if (bits_in(terrain[index]) > 1 && (terrain[index] & 3) != 0) {
            return index;
        }
    }
    return -1;
}

static int choose_tile(int index, uint8_t mask) {
    int near = touches_water(index);
    bool seeding = seeds_closed == 0 && seed_n < lake_seed_cap();
    if (near == 0 && seeding && !near_seed(index, lake_gap())) {
        if ((mask & 2) != 0) {
            return 1;
        }
        if ((mask & 1) != 0) {
            return 0;
        }
    }
    int weight[TILES];
    for (int tile = 0; tile < TILES; ++tile) {
        weight[tile] = 0;
    }
    if (near > 0) {
        int own = shore_owner(index);
        bool grow = water_under(WATER_CAP_PERCENT) && own != 0 && !beside_other_lake(index, own);
        int water_w = grow ? 8 : 0;
        int sand_w = grow ? 2 : 6;
        if ((mask & 2) != 0) {
            weight[1] = water_w;
        } else if ((mask & 1) != 0) {
            weight[0] = water_w;
        }
        if ((mask & 4) != 0) {
            weight[2] = sand_w;
        }
        for (int tile = 3; tile < TILES; ++tile) {
            if ((mask & (uint8_t)(1 << tile)) != 0) {
                weight[tile] = 1;
            }
        }
    } else {
        for (int tile = 2; tile < TILES; ++tile) {
            if ((mask & (uint8_t)(1 << tile)) != 0) {
                weight[tile] = 1;
            }
        }
    }
    int total = 0;
    for (int tile = 0; tile < TILES; ++tile) {
        total += weight[tile];
    }
    if (total <= 0) {
        for (int tile = 0; tile < TILES; ++tile) {
            if ((mask & (uint8_t)(1 << tile)) != 0) {
                weight[tile] = 1;
                total++;
            }
        }
    }
    if (total <= 0) {
        return -1;
    }
    int ticket = rnd_int(total);
    for (int tile = 0; tile < TILES; ++tile) {
        if (weight[tile] == 0) {
            continue;
        }
        if (ticket < weight[tile]) {
            return tile;
        }
        ticket -= weight[tile];
    }
    return bit_index(mask);
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
            write_mask(next, narrowed);
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
                        write_mask(next, narrowed);
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
    if (seeds_closed == 0 && seed_n < lake_seed_cap()) {
        int seed = find_seed_cell();
        if (seed >= 0) {
            return seed;
        }
        seeds_closed = 1;
    }
    int shore = take_shore();
    if (shore >= 0) {
        return shore;
    }
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
    int pick = choose_tile(index, mask);
    if (pick < 0) {
        return -1;
    }
    write_mask(index, (uint8_t)(1 << pick));
    note_spark(index);
    if (!propagate(index)) {
        return -1;
    }
    return 1;
}

static void refill_masks() {
    std::memset(terrain, ALL_TILES, (size_t)PANEL_PIXELS);
    if (wear != nullptr) {
        std::memset(wear, 0, (size_t)PANEL_PIXELS);
    }
    scan_cursor = 0;
    water_count = 0;
    seed_n = 0;
    seeds_closed = 0;
    shore_n = 0;
}

static bool block_room(int x, int y, int self) {
    int span = house_span();
    if (x < 0 || y < 0 || x + span > PANEL_WIDTH || y + span > PANEL_HEIGHT) {
        return false;
    }
    for (int dy = 0; dy < span; ++dy) {
        for (int dx = 0; dx < span; ++dx) {
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
        int span = house_span();
        int x = rnd_int(PANEL_WIDTH - span + 1);
        int y = rnd_int(PANEL_HEIGHT - span + 1);
        if (!far_from_seeds(x, y) || !block_room(x, y, -1)) {
            continue;
        }
        Village &village = villages[village_count];
        village.sx = x;
        village.sy = y;
        village.roof = (uint8_t)(village_count % ROOF_COUNT);
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
    if (person.stop_count == 0 && person.stop_i == 0 && person.quiet_ms <= 0.0f) {
        person.quiet_ms = rnd() * 120000.0f;
    }
    person.rest_ms = 0.0f;
    person.stuck_ms = 0.0f;
    person.best_d = 10000.0f;
    person.through = 0;
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
        person.rest_ms = 0.0f;
    } else {
        person.mode = MODE_WAIT;
        person.rest_ms = rnd() * 5000.0f;
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
            if (passable_px(x, y)) {
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

// A coarse step counts when the walker can cross the shared edge.
// through is the fallback when the open route cannot reach the settlement.
static bool cells_touch(int ax, int ay, int bx, int by, bool through) {
    int dx = bx - ax;
    int dy = by - ay;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    if (dx + dy != 1) {
        return false;
    }
    if (through) {
        return true;
    }
    if (bx != ax) {
        int left = ax < bx ? ax : bx;
        int y = ay * 2;
        int x_left = left * 2 + 1;
        int x_right = left * 2 + 2;
        if (passable_px(x_left, y) && passable_px(x_right, y)) {
            return true;
        }
        return passable_px(x_left, y + 1) && passable_px(x_right, y + 1);
    }
    int top = ay < by ? ay : by;
    int x = ax * 2;
    int y_top = top * 2 + 1;
    int y_bottom = top * 2 + 2;
    if (passable_px(x, y_top) && passable_px(x, y_bottom)) {
        return true;
    }
    return passable_px(x + 1, y_top) && passable_px(x + 1, y_bottom);
}

// Land pixels win, so a mixed cell keeps the walker on shore. Open water is the second pass.
static bool anchor_near(int cx, int cy, int px, int py, int &ax, int &ay) {
    int x0 = cx * 2;
    int y0 = cy * 2;
    for (int pass = 0; pass < 2; ++pass) {
        int best = 1000;
        bool found = false;
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                int x = x0 + dx;
                int y = y0 + dy;
                if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
                    continue;
                }
                if (pass == 0 && terrain[y * PANEL_WIDTH + x] <= 1) {
                    continue;
                }
                int ddx = x - px;
                int ddy = y - py;
                if (ddx < 0) {
                    ddx = -ddx;
                }
                if (ddy < 0) {
                    ddy = -ddy;
                }
                int dist = ddx + ddy;
                if (!found || dist < best) {
                    best = dist;
                    ax = x;
                    ay = y;
                    found = true;
                }
            }
        }
        if (found) {
            return true;
        }
    }
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

constexpr int COARSE_MAX = (PANEL_WIDTH_MAX / 2) * (PANEL_HEIGHT_MAX / 2);

struct RouteBias {
    int sx;
    int sy;
    int gx;
    int gy;
    int dx;
    int dy;
    int span;
    int span2;
    int bend;
    int coarse_w;
};

static RouteBias route_bias;
static uint16_t route_heap[COARSE_MAX];
static uint16_t route_at[COARSE_MAX];
static int route_heap_n = 0;

static int water_cells(int cx, int cy) {
    int count = 0;
    int x0 = cx * 2;
    int y0 = cy * 2;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int x = x0 + dx;
            int y = y0 + dy;
            if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
                continue;
            }
            if (terrain[y * PANEL_WIDTH + x] <= 1) {
                count++;
            }
        }
    }
    return count;
}

// Distance in coarse cells from the bow. The hump is zero at both villages.
static int bow_error(int cx, int cy) {
    if (route_bias.span2 <= 0 || route_bias.span <= 0) {
        return 0;
    }
    int rx = cx - route_bias.sx;
    int ry = cy - route_bias.sy;
    int along = rx * route_bias.dx + ry * route_bias.dy;
    int t = along * 256 / route_bias.span2;
    if (t < 0) {
        t = 0;
    } else if (t > 256) {
        t = 256;
    }
    int hump = t * (256 - t);
    int cross = route_bias.dx * ry - route_bias.dy * rx;
    int left = cross * 16384;
    int right = route_bias.bend * hump * route_bias.span;
    int num = left - right;
    if (num < 0) {
        num = -num;
    }
    int den = route_bias.span * 16384;
    if (den <= 0) {
        return 0;
    }
    return num / den;
}

// Open water costs more than a shore, and both cost more than dry land.
static int enter_cost(int cx, int cy) {
    int cost = 2 + bow_error(cx, cy);
    int water = water_cells(cx, cy);
    if (water >= 3) {
        cost += 40;
    } else {
        cost += 8 * water;
    }
    return cost;
}

static int step_guess(int cx, int cy) {
    int dx = cx - route_bias.gx;
    int dy = cy - route_bias.gy;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return (dx + dy) * 2;
}

static int route_rank(uint16_t cell) {
    int cx = cell % route_bias.coarse_w;
    int cy = cell / route_bias.coarse_w;
    return (int)bfs_queue[cell] + step_guess(cx, cy);
}

static void heap_swap(int a, int b) {
    uint16_t node_a = route_heap[a];
    uint16_t node_b = route_heap[b];
    route_heap[a] = node_b;
    route_heap[b] = node_a;
    route_at[node_a] = (uint16_t)b;
    route_at[node_b] = (uint16_t)a;
}

static void heap_up(int index) {
    while (index > 0) {
        int parent_i = (index - 1) / 2;
        if (route_rank(route_heap[parent_i]) <= route_rank(route_heap[index])) {
            break;
        }
        heap_swap(parent_i, index);
        index = parent_i;
    }
}

static void heap_down(int index) {
    while (true) {
        int left = index * 2 + 1;
        int right = left + 1;
        int best = index;
        if (left < route_heap_n && route_rank(route_heap[left]) < route_rank(route_heap[best])) {
            best = left;
        }
        if (right < route_heap_n && route_rank(route_heap[right]) < route_rank(route_heap[best])) {
            best = right;
        }
        if (best == index) {
            break;
        }
        heap_swap(index, best);
        index = best;
    }
}

static bool heap_push(uint16_t cell) {
    int at = route_at[cell];
    if (at != 0xffff) {
        heap_up(at);
        return true;
    }
    if (route_heap_n >= COARSE_MAX) {
        return false;
    }
    at = route_heap_n;
    route_heap[at] = cell;
    route_at[cell] = (uint16_t)at;
    route_heap_n++;
    heap_up(at);
    return true;
}

static uint16_t heap_pop() {
    uint16_t cell = route_heap[0];
    route_at[cell] = 0xffff;
    route_heap_n--;
    if (route_heap_n > 0) {
        uint16_t moved = route_heap[route_heap_n];
        route_heap[0] = moved;
        route_at[moved] = 0;
        heap_down(0);
    }
    return cell;
}

// The bow is a hump off the straight line. Water adds cost, so the hump stays on land.
static bool route_land(int start, int goal, int coarse_w, int coarse_h, int coarse_n, int sx, int sy, int gx, int gy) {
    if (start == goal) {
        parent[start] = (uint16_t)start;
        return true;
    }
    int dx = gx - sx;
    int dy = gy - sy;
    int span2 = dx * dx + dy * dy;
    int span = (int)sqrtf((float)span2);
    if (span < 1) {
        span = 1;
    }
    int bend = 0;
    if (span >= 4) {
        float mag = (float)span * (0.18f + rnd() * 0.22f);
        if (rnd() < 0.5f) {
            mag = -mag;
        }
        float perp_x = -(float)dy / (float)span;
        float perp_y = (float)dx / (float)span;
        float mid_x = (float)sx + (float)dx * 0.5f;
        float mid_y = (float)sy + (float)dy * 0.5f;
        float limit = mag;
        for (int attempt = 0; attempt < 6; ++attempt) {
            float apex_x = mid_x + perp_x * limit;
            float apex_y = mid_y + perp_y * limit;
            if (apex_x >= 1.0f && apex_y >= 1.0f && apex_x <= (float)(coarse_w - 2) && apex_y <= (float)(coarse_h - 2)) {
                break;
            }
            limit *= 0.6f;
        }
        bend = (int)limit;
        if (bend == 0 && (limit >= 1.0f || limit <= -1.0f)) {
            bend = limit > 0.0f ? 1 : -1;
        }
    }
    route_bias.sx = sx;
    route_bias.sy = sy;
    route_bias.gx = gx;
    route_bias.gy = gy;
    route_bias.dx = dx;
    route_bias.dy = dy;
    route_bias.span = span;
    route_bias.span2 = span2;
    route_bias.bend = bend;
    route_bias.coarse_w = coarse_w;
    for (int i = 0; i < coarse_n; ++i) {
        parent[i] = 0xffff;
        bfs_queue[i] = 0xffff;
        route_at[i] = 0xffff;
    }
    route_heap_n = 0;
    parent[start] = (uint16_t)start;
    bfs_queue[start] = 0;
    if (!heap_push((uint16_t)start)) {
        return false;
    }
    const int step_x[4] = {1, -1, 0, 0};
    const int step_y[4] = {0, 0, 1, -1};
    while (route_heap_n > 0) {
        uint16_t cell = heap_pop();
        if ((int)cell == goal) {
            return true;
        }
        int cx = cell % coarse_w;
        int cy = cell / coarse_w;
        int g0 = bfs_queue[cell];
        for (int dir = 0; dir < 4; ++dir) {
            int nx = cx + step_x[dir];
            int ny = cy + step_y[dir];
            if (nx < 0 || ny < 0 || nx >= coarse_w || ny >= coarse_h) {
                continue;
            }
            int step = enter_cost(nx, ny);
            int g = g0 + step;
            if (g >= 0xffff) {
                continue;
            }
            int next = ny * coarse_w + nx;
            if (bfs_queue[next] != 0xffff && g >= (int)bfs_queue[next]) {
                continue;
            }
            bfs_queue[next] = (uint16_t)g;
            parent[next] = cell;
            if (!heap_push((uint16_t)next)) {
                return false;
            }
        }
    }
    return false;
}

static bool route_search(int start, int goal, int coarse_w, int coarse_h, int coarse_n, bool through) {
    for (int i = 0; i < coarse_n; ++i) {
        parent[i] = 0xffff;
    }
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
            if (nx < 0 || ny < 0 || nx >= coarse_w || ny >= coarse_h) {
                continue;
            }
            if (!cells_touch(cx, cy, nx, ny, through)) {
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
    return found;
}

static bool build_route(int index) {
    Person &person = people[index];
    float tx;
    float ty;
    target_of(person, tx, ty);
    int coarse_w = PANEL_WIDTH / 2;
    int coarse_h = PANEL_HEIGHT / 2;
    int coarse_n = coarse_w * coarse_h;
    int raw_sx = (int)person.x / 2;
    int raw_sy = (int)person.y / 2;
    int raw_gx = (int)tx / 2;
    int raw_gy = (int)ty / 2;
    if (raw_sx < 0) {
        raw_sx = 0;
    }
    if (raw_sy < 0) {
        raw_sy = 0;
    }
    if (raw_gx < 0) {
        raw_gx = 0;
    }
    if (raw_gy < 0) {
        raw_gy = 0;
    }
    if (raw_sx >= coarse_w) {
        raw_sx = coarse_w - 1;
    }
    if (raw_sy >= coarse_h) {
        raw_sy = coarse_h - 1;
    }
    if (raw_gx >= coarse_w) {
        raw_gx = coarse_w - 1;
    }
    if (raw_gy >= coarse_h) {
        raw_gy = coarse_h - 1;
    }
    int sx = raw_sx;
    int sy = raw_sy;
    int gx = raw_gx;
    int gy = raw_gy;
    // About one leg in five is a sail. That search is a straight path, water included.
    bool sailing = rnd() < SAIL_CHANCE;
    bool through = !passable_px((int)person.x, (int)person.y);
    bool found = false;
    if (!sailing && !through && seek_open(sx, sy, coarse_w, coarse_h) && seek_open(gx, gy, coarse_w, coarse_h)) {
        int start = sy * coarse_w + sx;
        int goal = gy * coarse_w + gx;
        found = route_land(start, goal, coarse_w, coarse_h, coarse_n, sx, sy, gx, gy);
    }
    if (!found) {
        through = true;
        sx = raw_sx;
        sy = raw_sy;
        gx = raw_gx;
        gy = raw_gy;
        int start = sy * coarse_w + sx;
        int goal = gy * coarse_w + gx;
        found = route_search(start, goal, coarse_w, coarse_h, coarse_n, true);
    }
    if (!found) {
        return false;
    }
    person.through = through ? 1 : 0;
    int goal = gy * coarse_w + gx;
    uint8_t ring_x[PATH_CAP];
    uint8_t ring_y[PATH_CAP];
    int writes = 0;
    int ax = 0;
    int ay = 0;
    if (!anchor_near(gx, gy, (int)tx, (int)ty, ax, ay)) {
        return false;
    }
    int cur = goal;
    int guard = 0;
    while (guard++ < coarse_n) {
        ring_x[writes % PATH_CAP] = (uint8_t)ax;
        ring_y[writes % PATH_CAP] = (uint8_t)ay;
        writes++;
        if (parent[cur] == cur) {
            break;
        }
        int prev = parent[cur];
        int px = prev % coarse_w;
        int py = prev / coarse_w;
        int next_x = ax;
        int next_y = ay;
        if (!anchor_near(px, py, ax, ay, next_x, next_y)) {
            break;
        }
        ax = next_x;
        ay = next_y;
        cur = prev;
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
    person.rest_ms = 0.0f;
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
        if (people[i].mode != MODE_WAIT || people[i].rest_ms > 0.0f) {
            continue;
        }
        served++;
        if (!build_route(i)) {
            advance_stop(i);
        }
    }
}

// A traveller who has reached a settlement waits, then sets off again.
static void begin_visit(int index) {
    Person &person = people[index];
    person.mode = MODE_VISIT;
    person.through = 0;
    person.rest_ms = rnd() * 5000.0f;
    path_n[index] = 0;
    path_i[index] = 0;
    person.stuck_ms = 0.0f;
}

static bool step_open(const Person &person, int x, int y) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return false;
    }
    if (person.through) {
        return true;
    }
    return passable_px(x, y);
}

// A pause comes about every two minutes, then the campfire burns for two seconds.
static float pause_gap() {
    return 90000.0f + rnd() * 60000.0f;
}

// The fire is painted beside the traveller and stays on the panel.
static void light_fire(int x, int y) {
    int slot = -1;
    int shortest = 0;
    for (int i = 0; i < PERSON_CAP; ++i) {
        if (!camps[i].on) {
            slot = i;
            break;
        }
        if (camps[i].life < camps[shortest].life) {
            shortest = i;
        }
    }
    if (slot < 0) {
        slot = shortest;
    }
    camps[slot].on = true;
    camps[slot].life = 2000.0f;
    camps[slot].px = x;
    camps[slot].py = y;
}

static void move_person(Person &person, int index, int dt_ms) {
    if (person.mode == MODE_REST) {
        person.rest_ms -= (float)dt_ms;
        if (person.rest_ms <= 0.0f) {
            person.mode = MODE_MOVE;
            person.quiet_ms = pause_gap();
        }
        return;
    }
    person.quiet_ms -= (float)dt_ms;
    if (person.quiet_ms <= 0.0f) {
        light_fire((int)person.x, (int)person.y);
        if (person.mode == MODE_VISIT || person.mode == MODE_WAIT) {
            person.quiet_ms = pause_gap();
        } else {
            person.mode = MODE_REST;
            person.rest_ms = 2000.0f;
            person.quiet_ms = 0.0f;
            return;
        }
    }
    if (person.mode == MODE_VISIT) {
        person.rest_ms -= (float)dt_ms;
        if (person.rest_ms <= 0.0f) {
            advance_stop(index);
            return;
        }
        return;
    }
    if (person.mode == MODE_WAIT) {
        if (person.rest_ms > 0.0f) {
            person.rest_ms -= (float)dt_ms;
            if (person.rest_ms < 0.0f) {
                person.rest_ms = 0.0f;
            }
        }
        return;
    }
    if (person.stop_count == 0) {
        return;
    }
    int px = (int)person.x;
    int py = (int)person.y;
    float tx;
    float ty;
    target_of(person, tx, ty);
    float goal_dx = tx - person.x;
    float goal_dy = ty - person.y;
    float goal_d = sqrtf(goal_dx * goal_dx + goal_dy * goal_dy);
    if (goal_d < 8.0f) {
        begin_visit(index);
        return;
    }
    if (path_i[index] >= path_n[index]) {
        if (goal_d < 12.0f) {
            begin_visit(index);
            return;
        }
        person.mode = MODE_WAIT;
        person.rest_ms = 0.0f;
        return;
    }
    if (person.stuck_ms > 280.0f) {
        person.stuck_ms = 0.0f;
        path_i[index]++;
        if (path_i[index] >= path_n[index]) {
            person.mode = MODE_WAIT;
            person.rest_ms = 0.0f;
        }
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
        person.stuck_ms = 0.0f;
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
    if (!step_open(person, (int)nx, (int)ny)) {
        float slide_x = person.x + ux * step;
        float slide_y = person.y;
        float slide_dx = person.x;
        float slide_dy = person.y + uy * step;
        bool slide_ok = step_open(person, (int)slide_x, (int)slide_y);
        bool other_ok = step_open(person, (int)slide_dx, (int)slide_dy);
        if (slide_ok && other_ok) {
            float left = (wx - slide_x) * (wx - slide_x) + (wy - slide_y) * (wy - slide_y);
            float right = (wx - slide_dx) * (wx - slide_dx) + (wy - slide_dy) * (wy - slide_dy);
            if (left <= right) {
                nx = slide_x;
                ny = slide_y;
            } else {
                nx = slide_dx;
                ny = slide_dy;
            }
        } else if (slide_ok) {
            nx = slide_x;
            ny = slide_y;
        } else if (other_ok) {
            nx = slide_dx;
            ny = slide_dy;
        } else {
            person.stuck_ms += (float)dt_ms;
            return;
        }
    }
    float remain_x = wx - nx;
    float remain_y = wy - ny;
    if (remain_x * remain_x + remain_y * remain_y > dist * dist - 0.02f) {
        person.stuck_ms += (float)dt_ms;
        return;
    }
    person.stuck_ms = 0.0f;
    int ox = (int)person.x;
    int oy = (int)person.y;
    person.x = nx;
    person.y = ny;
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
    smoke[slot].x = (float)village.bx[block] + rnd() * (float)house_span();
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
    for (int i = 0; i < PERSON_CAP; ++i) {
        if (!camps[i].on) {
            continue;
        }
        camps[i].life -= (float)dt_ms;
        if (camps[i].life <= 0.0f) {
            camps[i].on = false;
        }
    }
    serve_routes();
    for (int i = 0; i < person_n; ++i) {
        move_person(people[i], i, dt_ms);
    }
}

static void deepen_lakes() {
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (terrain[i] > 1) {
            continue;
        }
        int x = i % PANEL_WIDTH;
        int y = i / PANEL_WIDTH;
        bool rim = false;
        const int step_x[4] = {1, -1, 0, 0};
        const int step_y[4] = {0, 0, 1, -1};
        for (int dir = 0; dir < 4; ++dir) {
            if (tile_at(x + step_x[dir], y + step_y[dir]) > 1) {
                rim = true;
                break;
            }
        }
        wear[i] = rim ? 1 : 2;
    }
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (wear[i] == 1) {
            terrain[i] = 1;
        } else if (wear[i] == 2) {
            terrain[i] = 0;
        }
    }
    std::memset(wear, 0, (size_t)PANEL_PIXELS);
}

static int flood_body(int start, uint8_t id, int &sx, int &sy) {
    int top = 0;
    wear[start] = id;
    wave_stack[top++] = (uint16_t)start;
    sx = start % PANEL_WIDTH;
    sy = start / PANEL_WIDTH;
    int size = 0;
    bool overflow = false;
    while (top > 0) {
        int index = wave_stack[--top];
        size++;
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
            if (terrain[next] > 1 || wear[next] != 0) {
                continue;
            }
            if (top >= (int)(sizeof wave_stack / sizeof wave_stack[0])) {
                overflow = true;
                continue;
            }
            wear[next] = id;
            wave_stack[top++] = (uint16_t)next;
        }
    }
    if (!overflow) {
        return size;
    }
    bool grew = true;
    while (grew) {
        grew = false;
        for (int index = 0; index < PANEL_PIXELS; ++index) {
            if (terrain[index] > 1 || wear[index] != 0) {
                continue;
            }
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
                if (wear[ny * PANEL_WIDTH + nx] != id) {
                    continue;
                }
                wear[index] = id;
                size++;
                grew = true;
                break;
            }
        }
    }
    return size;
}

static void dissolve_pond(uint8_t id) {
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (wear[i] != id) {
            continue;
        }
        int x = i % PANEL_WIDTH;
        int y = i / PANEL_WIDTH;
        int tally[TILES];
        for (int tile = 0; tile < TILES; ++tile) {
            tally[tile] = 0;
        }
        const int step_x[4] = {1, -1, 0, 0};
        const int step_y[4] = {0, 0, 1, -1};
        for (int dir = 0; dir < 4; ++dir) {
            int nx = x + step_x[dir];
            int ny = y + step_y[dir];
            if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
                continue;
            }
            int next = ny * PANEL_WIDTH + nx;
            if (wear[next] == id) {
                continue;
            }
            int tile = terrain[next];
            if (tile >= 2 && tile < TILES) {
                tally[tile]++;
            }
        }
        int best = 3;
        int best_n = 0;
        for (int tile = 2; tile < TILES; ++tile) {
            if (tally[tile] > best_n) {
                best = tile;
                best_n = tally[tile];
            }
        }
        terrain[i] = (uint8_t)best;
    }
}

static void lay_channel(int x, int y) {
    terrain[y * PANEL_WIDTH + x] = 1;
    const int step_x[4] = {1, -1, 0, 0};
    const int step_y[4] = {0, 0, 1, -1};
    for (int dir = 0; dir < 4; ++dir) {
        int nx = x + step_x[dir];
        int ny = y + step_y[dir];
        int tile = tile_at(nx, ny);
        if (tile == 3 || tile == 4) {
            terrain[ny * PANEL_WIDTH + nx] = 2;
        }
    }
}

static bool river_step(int x, int y, int tx, int ty, int &nx, int &ny) {
    int sx = (tx > x) ? 1 : (tx < x) ? -1 : 0;
    int sy = (ty > y) ? 1 : (ty < y) ? -1 : 0;
    if (rnd() < 0.3f) {
        if (sx != 0 && sy != 0) {
            if (rnd() < 0.5f) {
                sy = 0;
            } else {
                sx = 0;
            }
        } else if (sx != 0 && rnd() < 0.5f) {
            sy = (rnd() < 0.5f) ? -1 : 1;
        } else if (sy != 0 && rnd() < 0.5f) {
            sx = (rnd() < 0.5f) ? -1 : 1;
        }
    }
    const int ox[4] = {sx, sx, 0, -sy};
    const int oy[4] = {sy, 0, sy, sx};
    for (int i = 0; i < 4; ++i) {
        if (ox[i] == 0 && oy[i] == 0) {
            continue;
        }
        int cx = x + ox[i];
        int cy = y + oy[i];
        if (tile_at(cx, cy) >= 5) {
            continue;
        }
        nx = cx;
        ny = cy;
        return true;
    }
    return false;
}

static bool step_onto(int x, int y, uint8_t id, int &nx, int &ny) {
    const int step_x[4] = {1, -1, 0, 0};
    const int step_y[4] = {0, 0, 1, -1};
    for (int dir = 0; dir < 4; ++dir) {
        int cx = x + step_x[dir];
        int cy = y + step_y[dir];
        if (cx < 0 || cy < 0 || cx >= PANEL_WIDTH || cy >= PANEL_HEIGHT) {
            continue;
        }
        int next = cy * PANEL_WIDTH + cx;
        if (wear[next] == id && terrain[next] <= 1) {
            nx = cx;
            ny = cy;
            return true;
        }
    }
    return false;
}

static void carve_river(int x, int y, int tx, int ty, uint8_t src, uint8_t dest) {
    int painted = 0;
    int limit = (PANEL_WIDTH + PANEL_HEIGHT) * 2;
    for (int step = 0; step < limit; ++step) {
        int nx = x;
        int ny = y;
        if (step_onto(x, y, dest, nx, ny)) {
            return;
        }
        if (!river_step(x, y, tx, ty, nx, ny)) {
            return;
        }
        x = nx;
        y = ny;
        int index = y * PANEL_WIDTH + x;
        if (terrain[index] <= 1) {
            if (painted > 0 && wear[index] != src) {
                return;
            }
            continue;
        }
        if (terrain[index] <= 4) {
            lay_channel(x, y);
            painted++;
        }
    }
}

struct LakeBody {
    int size;
    int x;
    int y;
    uint8_t id;
};

static bool pick_lakes(LakeBody *bodies, int body_n, int &a, int &b, int skip_a, int skip_b) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        int i = rnd_int(body_n);
        int j = rnd_int(body_n - 1);
        if (j >= i) {
            j++;
        }
        if ((i == skip_a && j == skip_b) || (i == skip_b && j == skip_a)) {
            continue;
        }
        int dx = bodies[i].x - bodies[j].x;
        int dy = bodies[i].y - bodies[j].y;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (dx + dy < 6) {
            continue;
        }
        a = i;
        b = j;
        return true;
    }
    return false;
}

static void remember_body(LakeBody *bodies, int &body_n, int size, int x, int y, uint8_t id) {
    if (size < 12) {
        dissolve_pond(id);
        return;
    }
    if (body_n < 8) {
        bodies[body_n].size = size;
        bodies[body_n].x = x;
        bodies[body_n].y = y;
        bodies[body_n].id = id;
        body_n++;
        return;
    }
    int small = 0;
    for (int i = 1; i < body_n; ++i) {
        if (bodies[i].size < bodies[small].size) {
            small = i;
        }
    }
    if (size <= bodies[small].size) {
        return;
    }
    bodies[small].size = size;
    bodies[small].x = x;
    bodies[small].y = y;
    bodies[small].id = id;
}

// Land of three pixels or fewer that sits in the water becomes the water around it.
static void clear_water_specks() {
    std::memset(wear, 0, (size_t)PANEL_PIXELS);
    const int step_x[4] = {1, -1, 0, 0};
    const int step_y[4] = {0, 0, 1, -1};
    int stack_cap = (int)(sizeof wave_stack / sizeof wave_stack[0]);
    for (int start = 0; start < PANEL_PIXELS; ++start) {
        if (terrain[start] <= 1 || wear[start] != 0) {
            continue;
        }
        int cells[3];
        int count = 0;
        bool big = false;
        bool overflow = false;
        int top = 0;
        wear[start] = 1;
        wave_stack[top++] = (uint16_t)start;
        while (top > 0) {
            int index = wave_stack[--top];
            if (count < 3) {
                cells[count] = index;
            } else {
                big = true;
            }
            count++;
            int x = index % PANEL_WIDTH;
            int y = index / PANEL_WIDTH;
            for (int dir = 0; dir < 4; ++dir) {
                int nx = x + step_x[dir];
                int ny = y + step_y[dir];
                if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
                    continue;
                }
                int next = ny * PANEL_WIDTH + nx;
                if (terrain[next] <= 1 || wear[next] != 0) {
                    continue;
                }
                if (top >= stack_cap) {
                    overflow = true;
                    continue;
                }
                wear[next] = 1;
                wave_stack[top++] = (uint16_t)next;
            }
        }
        if (overflow) {
            bool grew = true;
            while (grew) {
                grew = false;
                for (int index = 0; index < PANEL_PIXELS; ++index) {
                    if (terrain[index] <= 1 || wear[index] != 0) {
                        continue;
                    }
                    int x = index % PANEL_WIDTH;
                    int y = index / PANEL_WIDTH;
                    for (int dir = 0; dir < 4; ++dir) {
                        int nx = x + step_x[dir];
                        int ny = y + step_y[dir];
                        if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
                            continue;
                        }
                        if (wear[ny * PANEL_WIDTH + nx] == 0) {
                            continue;
                        }
                        wear[index] = 1;
                        grew = true;
                        break;
                    }
                }
            }
            continue;
        }
        if (big || count > 3 || count == 0) {
            continue;
        }
        bool shallow = false;
        bool deep = false;
        for (int c = 0; c < count; ++c) {
            int x = cells[c] % PANEL_WIDTH;
            int y = cells[c] / PANEL_WIDTH;
            for (int dir = 0; dir < 4; ++dir) {
                int nx = x + step_x[dir];
                int ny = y + step_y[dir];
                if (nx < 0 || ny < 0 || nx >= PANEL_WIDTH || ny >= PANEL_HEIGHT) {
                    continue;
                }
                int tile = terrain[ny * PANEL_WIDTH + nx];
                if (tile == 1) {
                    shallow = true;
                } else if (tile == 0) {
                    deep = true;
                }
            }
        }
        uint8_t fill = shallow || !deep ? 1 : 0;
        for (int c = 0; c < count; ++c) {
            terrain[cells[c]] = fill;
        }
    }
}

// Deep centres, tiny ponds fold back into the land around them, and a river
// sometimes links two lakes. Specks of land in the water are then cleared.
static void shape_water() {
    if (wear == nullptr) {
        return;
    }
    std::memset(wear, 0, (size_t)PANEL_PIXELS);
    deepen_lakes();
    LakeBody bodies[8];
    int body_n = 0;
    int next_id = 1;
    for (int i = 0; i < PANEL_PIXELS && next_id < 40; ++i) {
        if (terrain[i] > 1 || wear[i] != 0) {
            continue;
        }
        int sx = 0;
        int sy = 0;
        int size = flood_body(i, (uint8_t)next_id, sx, sy);
        remember_body(bodies, body_n, size, sx, sy, (uint8_t)next_id);
        next_id++;
    }
    if (body_n >= 2 && rnd() < 0.7f) {
        int rivers = 1;
        if (body_n >= 3 && rnd() < 0.45f) {
            rivers = 2;
        }
        int skip_a = -1;
        int skip_b = -1;
        for (int r = 0; r < rivers; ++r) {
            int a = 0;
            int b = 0;
            if (!pick_lakes(bodies, body_n, a, b, skip_a, skip_b)) {
                break;
            }
            carve_river(bodies[a].x, bodies[a].y, bodies[b].x, bodies[b].y, bodies[a].id, bodies[b].id);
            skip_a = a;
            skip_b = b;
        }
    }
    clear_water_specks();
    std::memset(wear, 0, (size_t)PANEL_PIXELS);
}

static bool finish_map() {
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        if (bits_in(terrain[i]) != 1) {
            refill_masks();
            return false;
        }
    }
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        terrain[i] = (uint8_t)bit_index(terrain[i]);
    }
    shape_water();
    village_count = 0;
    for (int attempt = 0; attempt < 500 && village_count < VILLAGE_CAP; ++attempt) {
        if (!place_seed()) {
            break;
        }
    }
    int coarse_n = (PANEL_WIDTH / 2) * (PANEL_HEIGHT / 2);
    parent = (uint16_t *)pattern_alloc(coarse_n * (int)sizeof(uint16_t));
    bfs_queue = (uint16_t *)pattern_alloc(coarse_n * (int)sizeof(uint16_t));
    path_x = pattern_alloc(PERSON_CAP * PATH_CAP);
    path_y = pattern_alloc(PERSON_CAP * PATH_CAP);
    person_n = PERSON_CAP * PANEL_PIXELS / (128 * 128);
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

// The hull is a row of three and a row of two. The sail is a four pixel triangle.
static void paint_boat(uint8_t *frame, int x, int y, float face_x, float face_y, int sail_r, int sail_g, int sail_b) {
    int fx = 0;
    int fy = 0;
    if (std::fabs(face_x) >= std::fabs(face_y)) {
        fx = face_x >= 0.0f ? 1 : -1;
    } else {
        fy = face_y >= 0.0f ? 1 : -1;
    }
    int rx = -fy;
    int ry = fx;
    const int along[9] = {-1, 0, 1, 0, 1, 0, -1, 0, 1};
    const int side[9] = {0, 0, 0, 1, 1, -2, -1, -1, -1};
    for (int i = 0; i < 9; ++i) {
        int px = x + along[i] * fx + side[i] * rx;
        int py = y + along[i] * fy + side[i] * ry;
        if (i < 3) {
            put(frame, px, py, 132, 84, 42);
        } else if (i < 5) {
            put(frame, px, py, 96, 58, 28);
        } else {
            put(frame, px, py, sail_r, sail_g, sail_b);
        }
    }
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
            int span = house_span();
            for (int dy = 0; dy < span; ++dy) {
                int red = 86;
                int green = 56;
                int blue = 34;
                if (dy == span - 1) {
                    red = 62;
                    green = 40;
                    blue = 24;
                } else if (dy > 0) {
                    red = 74;
                    green = 48;
                    blue = 29;
                }
                for (int dx = 0; dx < span; ++dx) {
                    put(frame, x + dx, y + dy, red, green, blue);
                }
            }
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
            // The eave is four pixels wide, and the row above is the ridge.
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
            paint_boat(frame, x, y, person.face_x, person.face_y, sail_r, sail_g, sail_b);
        } else {
            int red;
            int green;
            int blue;
            roof_rgb(roof, red, green, blue);
            put(frame, x, y, red, green, blue);
            if (PANEL_COUNT >= 4) {
                put(frame, x, y - 1, red, green, blue);
            }
        }
    }
    for (int i = 0; i < PERSON_CAP; ++i) {
        if (!camps[i].on) {
            continue;
        }
        int x = camps[i].px;
        int y = camps[i].py;
        int phase = fire_phase;
        if (PANEL_COUNT >= 4) {
            int dir = 1;
            if (x + 3 >= PANEL_WIDTH) {
                dir = -1;
            }
            int cx = x + 2 * dir;
            if (cx < 1) {
                cx = 1;
            }
            if (cx > PANEL_WIDTH - 2) {
                cx = PANEL_WIDTH - 2;
            }
            int by = y;
            if (by < 1) {
                by = 1;
            }
            if (by >= PANEL_HEIGHT) {
                by = PANEL_HEIGHT - 1;
            }
            put(frame, cx, by - 1, fire_r[phase], fire_g[phase], fire_b[phase]);
            put(frame, cx - 1, by, fire_r[(phase + 1) % 3], fire_g[(phase + 1) % 3], fire_b[(phase + 1) % 3]);
            put(frame, cx, by, fire_r[(phase + 2) % 3], fire_g[(phase + 2) % 3], fire_b[(phase + 2) % 3]);
            put(frame, cx + 1, by, fire_r[phase], fire_g[phase], fire_b[phase]);
        } else {
            int ox = x + 2;
            int oy = y;
            if (ox < 0) {
                ox = 0;
            }
            if (ox + 1 >= PANEL_WIDTH) {
                ox = PANEL_WIDTH - 2;
            }
            if (oy - 1 < 0) {
                oy = 1;
            }
            if (oy + 1 >= PANEL_HEIGHT) {
                oy = PANEL_HEIGHT - 2;
            }
            put(frame, ox, oy, fire_r[phase], fire_g[phase], fire_b[phase]);
            put(frame, ox + 1, oy + 1, fire_r[(phase + 1) % 3], fire_g[(phase + 1) % 3], fire_b[(phase + 1) % 3]);
            put(frame, ox, oy - 1, fire_r[(phase + 2) % 3], fire_g[(phase + 2) % 3], fire_b[(phase + 2) % 3]);
        }
    }
}

static void boot(uint32_t seed) {
    pattern_reset();
    rng_state = seed | 1u;
    phase_live = 0;
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
    for (int i = 0; i < PERSON_CAP; ++i) {
        camps[i].on = false;
    }
    terrain = pattern_alloc(PANEL_PIXELS);
    wear = pattern_alloc(PANEL_PIXELS);
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
