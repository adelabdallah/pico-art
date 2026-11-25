#include "pattern.hpp"

#include "hub75.hpp"
#include "pico/stdlib.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int PANEL_WIDTH = 128;
int PANEL_HEIGHT = 64;
int PANEL_PIXELS = 128 * 64;
int PANEL_COUNT = 4;

static uint8_t *picture = nullptr;
static uint8_t *turned_buf = nullptr;
static uint8_t *arena = nullptr;
static int arena_bytes = 0;
static int arena_used = 0;

static uint8_t *need_bytes(int bytes) {
    uint8_t *block = (uint8_t *)std::malloc((size_t)bytes);
    if (block == nullptr) {
        panic("panel memory");
    }
    std::memset(block, 0, (size_t)bytes);
    return block;
}

uint8_t *panel_frame() {
    return picture;
}

uint8_t *panel_turned() {
    return turned_buf;
}

uint8_t *panel_extra() {
    return turned_buf;
}

void pattern_reset() {
    arena_used = 0;
}

uint8_t *pattern_alloc(int bytes) {
    int aligned = (bytes + 3) & ~3;
    if (arena == nullptr || arena_used > arena_bytes - aligned) {
        panic("pattern grid");
    }
    uint8_t *out = arena + arena_used;
    arena_used += aligned;
    std::memset(out, 0, (size_t)aligned);
    return out;
}

int pattern_room() {
    if (arena == nullptr || arena_used > arena_bytes) {
        return 0;
    }
    return arena_bytes - arena_used;
}

// malloc refuses a block that would pass the end of the 512 KB bank. The tail is what is still below that.
static int heap_tail() {
    extern char __StackLimit;
    char *brk = (char *)sbrk(0);
    if (brk >= &__StackLimit) {
        return 0;
    }
    return (int)(&__StackLimit - brk);
}

void panels_apply(int count) {
    if (count == 2) {
        PANEL_WIDTH = 128;
        PANEL_HEIGHT = 64;
    } else if (count == 4) {
        PANEL_WIDTH = 128;
        PANEL_HEIGHT = 128;
    } else {
        count = 1;
        PANEL_WIDTH = 64;
        PANEL_HEIGHT = 64;
    }
    PANEL_COUNT = count;
    PANEL_PIXELS = PANEL_WIDTH * PANEL_HEIGHT;

    std::free(picture);
    std::free(turned_buf);
    int frame_bytes = PANEL_PIXELS * 3;
    picture = need_bytes(frame_bytes);
    turned_buf = need_bytes(frame_bytes);

    int grid = 5 * PANEL_PIXELS;
    int plate = 192 * 108 * 3;
    int floor_bytes = grid > plate ? grid : plate;
    int flow_bytes = flow_particle_cap(PANEL_PIXELS) * FLOW_PARTICLE_BYTES;
    int ideal = flow_bytes > floor_bytes ? flow_bytes : floor_bytes;
    // Headers on the arena and the scan buffers, which the other core takes after this returns.
    constexpr int HEAP_SLACK = 8 * 1024;
    int room = heap_tail() - hub75_scan_bytes(PANEL_WIDTH, PANEL_HEIGHT) - HEAP_SLACK;
    if (room < floor_bytes) {
        panic("panel memory");
    }
    int need = ideal;
    if (need > room) {
        need = room;
    }
    if (need != arena_bytes) {
        std::free(arena);
        arena = need_bytes(need);
        arena_bytes = need;
    }
    arena_used = 0;
}
