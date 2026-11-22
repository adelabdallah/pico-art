// Boot intro for the xyloscope.
// A glass phrase fills with a shifting rainbow and sways, then flickers out over a starfield.
// The stars speed up, zoom into the centre, and fade. XYLO slides in from the left.
// SCOPE grows from the centre and lands below it. XYLO is blocky and SCOPE is rounded.
// A rainbow chases the border. The letters fly toward the camera, and the saved pattern fades in.
// The border is one pixel. The seven rainbow pixels are the length of the chase.

#include <cmath>
#include <cstdint>

#include "hardware/structs/rosc.h"
#include "pico/time.h"
#include "pattern.hpp"

constexpr int BOOT_FILL_MS = 3000;
constexpr int BOOT_FLICKER_MS = 1000;
constexpr int BOOT_MS = BOOT_FILL_MS + BOOT_FLICKER_MS;
constexpr int WARP_MS = 1000;
constexpr int ZOOM_MS = 1000;
constexpr int ENTER_MS = 1200;
constexpr int CHASE_MS = 2000;
constexpr int POP_MS = 280;
constexpr int SHINE_MS = 420;
constexpr int HOLD_MS = 500;
constexpr int FLY_MS = 320;
constexpr int FADE_MS = 2000;

constexpr int WARP_AT = BOOT_MS;
constexpr int ZOOM_AT = WARP_AT + WARP_MS;
constexpr int SPLASH_AT = ZOOM_AT + ZOOM_MS;
constexpr int CHASE_AT = SPLASH_AT + ENTER_MS;
constexpr int POP_AT = CHASE_AT + CHASE_MS;
constexpr int HOLD_AT = POP_AT + POP_MS;
constexpr int FLY_AT = HOLD_AT + HOLD_MS;
constexpr int FADE_AT = FLY_AT + FLY_MS;
constexpr int END_AT = FADE_AT + FADE_MS;

constexpr int FIELD_R = 4;
constexpr int FIELD_G = 10;
constexpr int FIELD_B = 18;
constexpr int CYAN_R = 77;
constexpr int CYAN_G = 208;
constexpr int CYAN_B = 225;

constexpr int BORDER_LEN = 252;
constexpr int STAR_COUNT = 150;
constexpr int BUBBLE_COUNT = 10;

// One stop for each of red, orange, yellow, green, blue, indigo, and violet.
static const int RAINBOW[7][3] = {
    {255, 24, 32},
    {255, 132, 16},
    {255, 228, 40},
    {36, 210, 72},
    {32, 88, 255},
    {72, 16, 196},
    {168, 32, 220},
};

static const int XYLO_RGB[4][3] = {
    {255, 32, 48},
    {255, 214, 32},
    {32, 208, 84},
    {36, 96, 255},
};

struct Stamp {
    const char *text;
    int count;
    int sx;
    int sy;
    int gap;
    int width;
    int height;
    // 0 is the 5 by 7 glass font. 1 is the block splash word. 2 is the round splash word.
    int kind;
};

struct Place {
    float ox;
    float oy;
    float scale;
    float shift;
    float left;
    float top;
    bool live;
};

struct Star {
    float x;
    float y;
    float z;
    float pace;
    uint8_t tint;
};

struct Bubble {
    float x;
    float phase;
    float speed;
};

static bool running = false;
static uint32_t elapsed = 0;
static int phrase_index = 0;
static uint32_t rng = 1;
static Star stars[STAR_COUNT];
static Bubble bubbles[BUBBLE_COUNT];

static uint32_t nextrand() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static int mix_channel(int from, int to, float t) {
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    return clamp_channel((float)from + ((float)to - (float)from) * t);
}

static float span_u(int start, int length) {
    if (elapsed < (uint32_t)start) {
        return 0.0f;
    }
    if (length <= 0) {
        return 1.0f;
    }
    float u = (float)(elapsed - (uint32_t)start) / (float)length;
    if (u > 1.0f) {
        return 1.0f;
    }
    return u;
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue, float gain) {
    if ((unsigned)x >= (unsigned)PANEL_WIDTH || (unsigned)y >= (unsigned)PANEL_HEIGHT) {
        return;
    }
    if (gain < 0.0f) {
        gain = 0.0f;
    }
    write_bgr(
        frame + (y * PANEL_WIDTH + x) * 3,
        clamp_channel(red * gain),
        clamp_channel(green * gain),
        clamp_channel(blue * gain));
}

static void put_brighter(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if ((unsigned)x >= (unsigned)PANEL_WIDTH || (unsigned)y >= (unsigned)PANEL_HEIGHT) {
        return;
    }
    uint8_t *pixel = frame + (y * PANEL_WIDTH + x) * 3;
    int have = (int)pixel[0] + (int)pixel[1] + (int)pixel[2];
    int want = red + green + blue;
    if (want <= have) {
        return;
    }
    write_bgr(pixel, red, green, blue);
}

static void fill_frame(uint8_t *frame, int red, int green, int blue, float gain) {
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            put(frame, x, y, red, green, blue, gain);
        }
    }
}

// 5 by 7 glyphs. Bit 4 of each row is the left column.
static const uint8_t *glyph_rows(char code) {
    static const uint8_t B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t S[7] = {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E};
    static const uint8_t C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    if (code == 'B') {
        return B;
    }
    if (code == 'O') {
        return O;
    }
    if (code == 'T') {
        return T;
    }
    if (code == 'X') {
        return X;
    }
    if (code == 'Y') {
        return Y;
    }
    if (code == 'L') {
        return L;
    }
    if (code == 'S') {
        return S;
    }
    if (code == 'C') {
        return C;
    }
    if (code == 'P') {
        return P;
    }
    if (code == 'E') {
        return E;
    }
    static const uint8_t A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    static const uint8_t I[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t N[7] = {0x11, 0x19, 0x15, 0x15, 0x13, 0x11, 0x11};
    static const uint8_t W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    static const uint8_t Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const uint8_t THREE[7] = {0x1E, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1E};
    static const uint8_t BANG[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
    if (code == 'A') {
        return A;
    }
    if (code == 'D') {
        return D;
    }
    if (code == 'G') {
        return G;
    }
    if (code == 'I') {
        return I;
    }
    if (code == 'N') {
        return N;
    }
    if (code == 'W') {
        return W;
    }
    if (code == 'Z') {
        return Z;
    }
    if (code == '3') {
        return THREE;
    }
    if (code == '!') {
        return BANG;
    }
    return nullptr;
}

static Stamp make_stamp(const char *text, int sx, int sy, int gap) {
    Stamp stamp;
    stamp.text = text;
    stamp.count = 0;
    while (text[stamp.count] != '\0') {
        stamp.count += 1;
    }
    stamp.sx = sx;
    stamp.sy = sy;
    stamp.gap = gap;
    stamp.kind = 0;
    int cell = 5 * sx;
    stamp.width = stamp.count * cell + (stamp.count - 1) * gap;
    stamp.height = 7 * sy;
    return stamp;
}

static Stamp make_splash_stamp(const char *text, int kind) {
    Stamp stamp = make_stamp(text, 1, 1, 1);
    stamp.kind = kind;
    int glyph_w = kind == 1 ? 14 : 11;
    stamp.width = stamp.count * glyph_w + (stamp.count - 1) * stamp.gap;
    stamp.height = 28;
    return stamp;
}

// 7 by 14 block letters, drawn at 2x so the edges stay straight.
static bool block_cell(char code, int col, int row) {
    static const char X[14][8] = {
        "##...##", "##...##", "##...##", ".##.##.", ".##.##.", "..###..", "..###..",
        "..###..", "..###..", ".##.##.", ".##.##.", "##...##", "##...##", "##...##",
    };
    static const char Y[14][8] = {
        "##...##", "##...##", "##...##", ".##.##.", ".##.##.", "..###..", "..###..",
        "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..",
    };
    static const char L[14][8] = {
        "###....", "###....", "###....", "###....", "###....", "###....", "###....",
        "###....", "###....", "###....", "#######", "#######", "#######", "#######",
    };
    static const char O[14][8] = {
        "#######", "#######", "##...##", "##...##", "##...##", "##...##", "##...##",
        "##...##", "##...##", "##...##", "##...##", "##...##", "#######", "#######",
    };
    const char (*rows)[8] = nullptr;
    if (code == 'X') {
        rows = X;
    } else if (code == 'Y') {
        rows = Y;
    } else if (code == 'L') {
        rows = L;
    } else if (code == 'O') {
        rows = O;
    }
    if (rows == nullptr || col < 0 || col > 6 || row < 0 || row > 13) {
        return false;
    }
    return rows[row][col] == '#';
}

static bool block_word_ink(const Stamp &stamp, int x, int y, int &index) {
    int advance = 14 + stamp.gap;
    index = x / advance;
    if (index < 0 || index >= stamp.count) {
        return false;
    }
    int local = x - index * advance;
    if (local < 0 || local >= 14) {
        return false;
    }
    return block_cell(stamp.text[index], local / 2, y / 2);
}

static float ellip(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx;
    float dy = (y - cy) / ry;
    return dx * dx + dy * dy;
}

static bool ring(float x, float y, float cx, float cy, float rx, float ry, float irx, float iry) {
    return ellip(x, y, cx, cy, rx, ry) <= 1.0f && ellip(x, y, cx, cy, irx, iry) > 1.0f;
}

static bool inside_round(float x, float y, float left, float top, float right, float bottom, float rad) {
    if (x < left || x >= right || y < top || y >= bottom) {
        return false;
    }
    if (x >= left + rad && x < right - rad) {
        return true;
    }
    if (y >= top + rad && y < bottom - rad) {
        return true;
    }
    float cx = x < left + rad ? left + rad : right - rad;
    float cy = y < top + rad ? top + rad : bottom - rad;
    float dx = x - cx;
    float dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

static bool scope_o(int x, int y) {
    float px = x + 0.5f;
    float py = y + 0.5f;
    return ring(px, py, 5.5f, 14.0f, 5.3f, 13.4f, 2.5f, 10.2f);
}

static bool scope_c(int x, int y) {
    if (!scope_o(x, y)) {
        return false;
    }
    float px = x + 0.5f;
    float py = y + 0.5f;
    return ellip(px, py, 9.2f, 14.0f, 4.2f, 6.2f) > 1.0f;
}

static bool scope_s(int x, int y) {
    float px = x + 0.5f;
    float py = y + 0.5f;
    bool top = ring(px, py, 5.4f, 7.6f, 5.0f, 7.2f, 2.3f, 4.3f);
    bool bot = ring(px, py, 5.4f, 20.4f, 5.0f, 7.2f, 2.3f, 4.3f);
    if (top && px > 5.2f && py > 7.2f) {
        top = false;
    }
    if (bot && px < 5.6f && py < 20.8f) {
        bot = false;
    }
    bool join = ellip(px, py, 5.4f, 14.0f, 3.4f, 2.4f) <= 1.0f;
    return top || bot || join;
}

static bool scope_p(int x, int y) {
    float px = x + 0.5f;
    float py = y + 0.5f;
    bool stem = px >= 1.2f && px < 4.3f && py >= 1.5f && py < 26.5f;
    bool cap = ellip(px, py, 2.7f, 25.2f, 2.0f, 2.4f) <= 1.0f;
    bool bowl = ring(px, py, 5.3f, 8.2f, 4.9f, 7.6f, 2.1f, 4.4f);
    return stem || cap || bowl;
}

static bool scope_e(int x, int y) {
    float px = x + 0.5f;
    float py = y + 0.5f;
    bool spine = inside_round(px, py, 0.4f, 0.6f, 5.2f, 27.4f, 2.6f);
    bool top = inside_round(px, py, 1.0f, 0.6f, 10.6f, 6.4f, 2.4f);
    bool mid = inside_round(px, py, 1.0f, 11.0f, 9.2f, 17.0f, 2.2f);
    bool bot = inside_round(px, py, 1.0f, 21.6f, 10.6f, 27.4f, 2.4f);
    return spine || top || mid || bot;
}

static bool round_cell(char code, int col, int row) {
    if (code == 'S') {
        return scope_s(col, row);
    }
    if (code == 'C') {
        return scope_c(col, row);
    }
    if (code == 'O') {
        return scope_o(col, row);
    }
    if (code == 'P') {
        return scope_p(col, row);
    }
    if (code == 'E') {
        return scope_e(col, row);
    }
    return false;
}

static bool round_word_ink(const Stamp &stamp, int x, int y, int &index) {
    int advance = 11 + stamp.gap;
    index = x / advance;
    if (index < 0 || index >= stamp.count) {
        return false;
    }
    int local = x - index * advance;
    if (local < 0 || local >= 11) {
        return false;
    }
    return round_cell(stamp.text[index], local, y);
}

static bool stamp_ink(const Stamp &stamp, int x, int y, int &index) {
    if (x < 0 || y < 0 || x >= stamp.width || y >= stamp.height) {
        return false;
    }
    if (stamp.kind == 1) {
        return block_word_ink(stamp, x, y, index);
    }
    if (stamp.kind == 2) {
        return round_word_ink(stamp, x, y, index);
    }
    int cell = 5 * stamp.sx;
    int advance = cell + stamp.gap;
    index = x / advance;
    if (index < 0 || index >= stamp.count) {
        return false;
    }
    int local = x - index * advance;
    if (local < 0 || local >= cell) {
        return false;
    }
    int col = local / stamp.sx;
    int row = y / stamp.sy;
    const uint8_t *rows = glyph_rows(stamp.text[index]);
    if (rows == nullptr || col > 4 || row > 6) {
        return false;
    }
    return (rows[row] & (uint8_t)(0x10 >> col)) != 0;
}

static bool near_ink(const Stamp &stamp, int x, int y) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            int index = 0;
            if (stamp_ink(stamp, x + dx, y + dy, index)) {
                return true;
            }
        }
    }
    return false;
}

static void aim_star(Star &star) {
    float ang = (nextrand() & 2047) / 2047.0f * 6.2831853f;
    float rad = 0.15f + (nextrand() & 2047) / 2047.0f;
    star.x = cosf(ang) * rad;
    star.y = sinf(ang) * rad;
}

static void seed_stars() {
    rng = 0xC0FFEEu;
    for (int i = 0; i < STAR_COUNT; ++i) {
        aim_star(stars[i]);
        stars[i].z = 0.08f + (nextrand() & 1023) / 1023.0f * 1.15f;
        stars[i].pace = 0.55f + (nextrand() & 255) / 255.0f * 0.75f;
        uint32_t pick = nextrand() % 10u;
        if (pick < 4u) {
            stars[i].tint = 0;
        } else if (pick < 7u) {
            stars[i].tint = 1;
        } else if (pick < 9u) {
            stars[i].tint = 2;
        } else {
            stars[i].tint = 3;
        }
    }
}

static void seed_bubbles() {
    for (int i = 0; i < BUBBLE_COUNT; ++i) {
        bubbles[i].x = 3.0f + (float)((i * 19) % 40);
        bubbles[i].phase = i * 0.37f;
        bubbles[i].speed = 6.0f + (float)(i % 4) * 2.0f;
    }
}

static float star_pace() {
    if (elapsed < (uint32_t)WARP_AT) {
        return 0.18f;
    }
    if (elapsed < (uint32_t)ZOOM_AT) {
        float u = span_u(WARP_AT, WARP_MS);
        return 0.18f + u * u * 1.7f;
    }
    float u = span_u(ZOOM_AT, ZOOM_MS);
    return 1.88f + u * u * 8.0f;
}

static float star_zoom() {
    if (elapsed < (uint32_t)ZOOM_AT) {
        return 1.0f;
    }
    float u = span_u(ZOOM_AT, ZOOM_MS);
    return 1.0f + u * u * 8.0f;
}

static float star_cover() {
    if (elapsed < (uint32_t)ZOOM_AT) {
        return 1.0f;
    }
    float u = span_u(ZOOM_AT, ZOOM_MS);
    float cover = 1.0f - u * u;
    if (cover < 0.0f) {
        return 0.0f;
    }
    return cover;
}

static void step_stars(int dt_ms) {
    float pace = star_pace();
    float dt = dt_ms * 0.001f;
    for (int i = 0; i < STAR_COUNT; ++i) {
        Star &star = stars[i];
        star.z -= pace * dt * star.pace;
        int guard = 0;
        while (star.z < 0.05f && guard < 6) {
            star.z += 1.15f;
            aim_star(star);
            guard += 1;
        }
        if (star.z < 0.05f) {
            star.z = 0.05f;
        }
    }
}

static void star_rgb(uint8_t tint, int &red, int &green, int &blue) {
    if (tint == 1) {
        red = 236;
        green = 252;
        blue = 252;
    } else if (tint == 2) {
        red = 255;
        green = 113;
        blue = 206;
    } else if (tint == 3) {
        red = 255;
        green = 179;
        blue = 71;
    } else {
        red = CYAN_R;
        green = CYAN_G;
        blue = CYAN_B;
    }
}

static void stroke_star(uint8_t *frame, float x0, float y0, float x1, float y1, int red, int green, int blue) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    int steps = (int)hypotf(dx, dy);
    if (steps < 1) {
        steps = 1;
    }
    if (steps > 24) {
        steps = 24;
    }
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        int x = (int)(x0 + dx * t);
        int y = (int)(y0 + dy * t);
        float w = 0.25f + 0.75f * t;
        put_brighter(
            frame,
            x,
            y,
            clamp_channel(red * w),
            clamp_channel(green * w),
            clamp_channel(blue * w));
    }
}

static void paint_stars(uint8_t *frame) {
    float cover = star_cover();
    float zoom_u = elapsed < (uint32_t)ZOOM_AT ? 0.0f : span_u(ZOOM_AT, ZOOM_MS);
    float lens = 16.0f * star_zoom();
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = (x + 0.5f - 31.5f) / 32.0f;
            float dy = (y + 0.5f - 31.5f) / 32.0f;
            float dist = hypotf(dx, dy);
            float core = expf(-dist * (3.4f - zoom_u * 2.2f));
            float ring = expf(-(dist - 0.62f) * (dist - 0.62f) * 14.0f);
            int red = (int)((FIELD_R + core * (28.0f + zoom_u * 190.0f) + ring * 90.0f) * cover);
            int green = (int)((FIELD_G + core * (46.0f + zoom_u * 150.0f) + ring * 24.0f) * cover);
            int blue = (int)((FIELD_B + core * (72.0f + zoom_u * 160.0f) + ring * 48.0f) * cover);
            put(frame, x, y, red, green, blue, 1.0f);
        }
    }
    float pace = star_pace();
    for (int i = 0; i < STAR_COUNT; ++i) {
        Star &star = stars[i];
        float bright = (1.2f - star.z) * (0.45f + pace * 0.18f);
        if (bright > 1.0f) {
            bright = 1.0f;
        }
        if (bright < 0.0f) {
            bright = 0.0f;
        }
        bright *= cover;
        if (bright < 0.04f) {
            continue;
        }
        float z0 = star.z + 0.03f + pace * 0.03f;
        float x1 = 31.5f + star.x * lens / star.z;
        float y1 = 31.5f + star.y * lens / star.z;
        float x0 = 31.5f + star.x * lens / z0;
        float y0 = 31.5f + star.y * lens / z0;
        int red = 0;
        int green = 0;
        int blue = 0;
        star_rgb(star.tint, red, green, blue);
        red = clamp_channel(red * bright);
        green = clamp_channel(green * bright);
        blue = clamp_channel(blue * bright);
        stroke_star(frame, x0, y0, x1, y1, red, green, blue);
    }
}

static bool boot_lit() {
    if (elapsed < 80u) {
        return true;
    }
    if (elapsed < 140u) {
        return false;
    }
    if (elapsed < 220u) {
        return true;
    }
    if (elapsed < 280u) {
        return false;
    }
    if (elapsed < (uint32_t)BOOT_FILL_MS) {
        return true;
    }
    float u = span_u(BOOT_FILL_MS, BOOT_FLICKER_MS);
    float period = 210.0f - u * u * 180.0f;
    if (period < 26.0f) {
        period = 26.0f;
    }
    float phase = fmodf((float)elapsed, period);
    return phase < period * 0.4f;
}

static float boot_gain() {
    if (elapsed < (uint32_t)BOOT_FILL_MS) {
        return 1.0f;
    }
    float u = span_u(BOOT_FILL_MS, BOOT_FLICKER_MS);
    return 1.0f - u * u;
}

static bool grid_at(int x, int y) {
    if (y < 48) {
        return false;
    }
    if (((y - 48) % 4) == 0) {
        return true;
    }
    float span = (y - 42) / 21.0f;
    if (span <= 0.0f) {
        return false;
    }
    static const int feet[] = {2, 16, 32, 48, 61};
    for (int i = 0; i < 5; ++i) {
        float line = 32.0f + (feet[i] - 32) * span;
        if (fabsf((float)x - line) < 0.6f) {
            return true;
        }
    }
    return false;
}

struct BootPhrase {
    const char *upper;
    int upper_sx;
    int upper_sy;
    int upper_gap;
    const char *lower;
    int lower_sx;
    int lower_sy;
    int lower_gap;
};

static const BootPhrase PHRASES[] = {
    {"BOOTING", 2, 5, 2, nullptr, 0, 0, 0},
    {"LOADING", 2, 5, 2, nullptr, 0, 0, 0},
    {"PAINTING", 2, 5, 2, nullptr, 0, 0, 0},
    {"NOW IN", 2, 3, 2, "3D!", 4, 5, 2},
    {"BEEP", 2, 4, 2, "BOOP", 2, 4, 2},
    {"WOWZA", 2, 5, 2, nullptr, 0, 0, 0},
};

constexpr int PHRASE_COUNT = 6;

struct PlacedLine {
    Stamp stamp;
    float left;
    float top;
};

// One full turn of the liquid hue about every 0.7 seconds.
static void hue_rgb(float turns, float sat, float value, int &red, int &green, int &blue) {
    float h = turns - floorf(turns);
    if (h < 0.0f) {
        h += 1.0f;
    }
    float sector = h * 6.0f;
    int index = (int)sector;
    if (index > 5) {
        index = 5;
    }
    float frac = sector - (float)index;
    float p = value * (1.0f - sat);
    float q = value * (1.0f - frac * sat);
    float t = value * (1.0f - (1.0f - frac) * sat);
    float r = value;
    float g = t;
    float b = p;
    if (index == 1) {
        r = q;
        g = value;
        b = p;
    } else if (index == 2) {
        r = p;
        g = value;
        b = t;
    } else if (index == 3) {
        r = p;
        g = q;
        b = value;
    } else if (index == 4) {
        r = t;
        g = p;
        b = value;
    } else if (index == 5) {
        r = value;
        g = p;
        b = q;
    }
    red = clamp_channel(r * 255.0f);
    green = clamp_channel(g * 255.0f);
    blue = clamp_channel(b * 255.0f);
}

static void paint_boot(uint8_t *frame, bool over_stars) {
    if (!over_stars) {
        fill_frame(frame, FIELD_R, FIELD_G, FIELD_B, 1.0f);
    }
    if (!boot_lit()) {
        return;
    }
    float gain = boot_gain();
    if (gain < 0.02f) {
        return;
    }
    const BootPhrase &phrase = PHRASES[phrase_index];
    PlacedLine lines[2];
    lines[0].stamp = make_stamp(phrase.upper, phrase.upper_sx, phrase.upper_sy, phrase.upper_gap);
    int line_count = 1;
    if (phrase.lower != nullptr) {
        lines[1].stamp = make_stamp(phrase.lower, phrase.lower_sx, phrase.lower_sy, phrase.lower_gap);
        line_count = 2;
    }
    // A few degrees, so the glass stays on the panel while it sways.
    float sec = elapsed * 0.001f;
    float ang = sinf(sec * 1.7f) * 0.09f;
    float bob = sinf(sec * 2.2f) * 1.3f;
    float fill = span_u(0, BOOT_FILL_MS);
    float cosine = cosf(ang);
    float sine = sinf(ang);
    float cy = 31.5f + bob;
    constexpr float LINE_GAP = 4.0f;
    float block_h = (float)lines[0].stamp.height;
    if (line_count == 2) {
        block_h += LINE_GAP + (float)lines[1].stamp.height;
    }
    float block_top = cy - block_h * 0.5f;
    lines[0].left = 31.5f - lines[0].stamp.width * 0.5f;
    lines[0].top = block_top;
    if (line_count == 2) {
        lines[1].left = 31.5f - lines[1].stamp.width * 0.5f;
        lines[1].top = block_top + (float)lines[0].stamp.height + LINE_GAP;
    }
    float turns = sec / 0.7f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            if (grid_at(x, y)) {
                put(frame, x, y, 14, 42, 58, gain);
            }
            float dx = (x + 0.5f) - 31.5f;
            float dy = (y + 0.5f) - cy;
            float px = 31.5f + dx * cosine + dy * sine;
            float py = cy + -dx * sine + dy * cosine;
            for (int line = 0; line < line_count; ++line) {
                float wx = px - lines[line].left;
                float wy = py - lines[line].top;
                float width = (float)lines[line].stamp.width;
                float height = (float)lines[line].stamp.height;
                if (wx < -1.0f || wy < -1.0f || wx > width + 1.0f || wy > height + 1.0f) {
                    continue;
                }
                int ix = (int)floorf(wx);
                int iy = (int)floorf(wy);
                int index = 0;
                if (stamp_ink(lines[line].stamp, ix, iy, index)) {
                    float slosh = sinf(px * 0.35f - sec * 3.2f) * 1.6f * (1.0f - fill);
                    float surf = block_top + (1.0f - fill) * block_h + slosh;
                    int red = 0;
                    int green = 0;
                    int blue = 0;
                    hue_rgb(turns, 0.45f, 0.16f, red, green, blue);
                    if (py >= surf) {
                        float depth = (py - surf) / block_h;
                        if (depth < 0.0f) {
                            depth = 0.0f;
                        }
                        if (depth > 1.0f) {
                            depth = 1.0f;
                        }
                        bool crest = (py - surf) < 1.3f;
                        float value = crest ? 1.0f : 1.0f - depth * 0.62f;
                        float sat = crest ? 0.28f : 0.88f;
                        hue_rgb(turns, sat, value, red, green, blue);
                        for (int i = 0; i < BUBBLE_COUNT; ++i) {
                            float travel = fmodf(bubbles[i].phase + sec * bubbles[i].speed, block_h);
                            float by = block_top + block_h - travel;
                            float bx = lines[line].left + bubbles[i].x * (width / 40.0f);
                            float off_x = px - bx;
                            float off_y = py - by;
                            if (off_x * off_x + off_y * off_y < 1.4f && by > surf) {
                                hue_rgb(turns, 0.08f, 1.0f, red, green, blue);
                                break;
                            }
                        }
                    }
                    put(frame, x, y, red, green, blue, gain);
                    break;
                }
                if (near_ink(lines[line].stamp, ix, iy)) {
                    int red = 0;
                    int green = 0;
                    int blue = 0;
                    hue_rgb(turns, 0.22f, 0.88f, red, green, blue);
                    put(frame, x, y, red, green, blue, gain);
                    break;
                }
            }
        }
    }
}

static void border_at(int index, int &x, int &y) {
    int i = index % BORDER_LEN;
    if (i < 0) {
        i += BORDER_LEN;
    }
    if (i < 64) {
        x = i;
        y = 0;
        return;
    }
    i -= 64;
    if (i < 63) {
        x = PANEL_WIDTH - 1;
        y = 1 + i;
        return;
    }
    i -= 63;
    if (i < 63) {
        x = 62 - i;
        y = PANEL_HEIGHT - 1;
        return;
    }
    i -= 63;
    x = 0;
    y = 62 - i;
}

static void rainbow_at(float t, int &red, int &green, int &blue) {
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    float x = t * 6.0f;
    int index = (int)x;
    if (index >= 6) {
        red = RAINBOW[6][0];
        green = RAINBOW[6][1];
        blue = RAINBOW[6][2];
        return;
    }
    float frac = x - (float)index;
    red = mix_channel(RAINBOW[index][0], RAINBOW[index + 1][0], frac);
    green = mix_channel(RAINBOW[index][1], RAINBOW[index + 1][1], frac);
    blue = mix_channel(RAINBOW[index][2], RAINBOW[index + 1][2], frac);
}

static void paint_chase(uint8_t *frame, float head, float alpha, float flash) {
    int lead = (int)floorf(head);
    for (int k = 0; k < 16; ++k) {
        int x = 0;
        int y = 0;
        border_at(lead - k, x, y);
        int red = RAINBOW[6][0];
        int green = RAINBOW[6][1];
        int blue = RAINBOW[6][2];
        float gain = alpha;
        if (k < 7) {
            red = RAINBOW[k][0];
            green = RAINBOW[k][1];
            blue = RAINBOW[k][2];
            red = mix_channel(red, 255, flash);
            green = mix_channel(green, 255, flash);
            blue = mix_channel(blue, 255, flash);
        } else {
            gain *= 1.0f - (float)(k - 6) / 10.0f;
        }
        put(frame, x, y, red, green, blue, gain);
    }
}

static void paint_border(uint8_t *frame, float alpha, float flash) {
    if (alpha < 0.02f) {
        return;
    }
    // The lap ends in a flash. The full border locks after that pop.
    if (elapsed >= (uint32_t)CHASE_AT && elapsed < (uint32_t)POP_AT) {
        paint_chase(frame, span_u(CHASE_AT, CHASE_MS) * (float)BORDER_LEN, alpha, 0.0f);
        return;
    }
    if (elapsed >= (uint32_t)POP_AT && elapsed < (uint32_t)HOLD_AT) {
        paint_chase(frame, (float)BORDER_LEN, alpha, flash);
        return;
    }
    for (int i = 0; i < BORDER_LEN; ++i) {
        int x = 0;
        int y = 0;
        border_at(i, x, y);
        int red = 0;
        int green = 0;
        int blue = 0;
        rainbow_at((float)i / (float)BORDER_LEN, red, green, blue);
        red = mix_channel(red, 255, flash);
        green = mix_channel(green, 255, flash);
        blue = mix_channel(blue, 255, flash);
        put(frame, x, y, red, green, blue, alpha);
    }
}

static void rest_place(Place &place, float left, float top, float width, float height) {
    place.left = left;
    place.top = top;
    place.ox = left + width * 0.5f;
    place.oy = top + height * 0.5f;
    place.scale = 1.0f;
    place.shift = 0.0f;
    place.live = true;
}

static void place_bounds(const Place &place, const Stamp &stamp, int &x0, int &y0, int &x1, int &y1) {
    float xs[2] = {place.left, place.left + stamp.width};
    float ys[2] = {place.top, place.top + stamp.height};
    float min_x = 1000.0f;
    float max_x = -1000.0f;
    float min_y = 1000.0f;
    float max_y = -1000.0f;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            float sx = place.shift + place.ox + (xs[i] - place.ox) * place.scale;
            float sy = place.oy + (ys[j] - place.oy) * place.scale;
            if (sx < min_x) {
                min_x = sx;
            }
            if (sx > max_x) {
                max_x = sx;
            }
            if (sy < min_y) {
                min_y = sy;
            }
            if (sy > max_y) {
                max_y = sy;
            }
        }
    }
    x0 = (int)floorf(min_x) - 2;
    y0 = (int)floorf(min_y) - 2;
    x1 = (int)ceilf(max_x) + 2;
    y1 = (int)ceilf(max_y) + 2;
}

static bool sample_stamp(const Place &place, const Stamp &stamp, int x, int y, int &lx, int &ly, int &index) {
    if (!place.live || place.scale < 0.12f) {
        return false;
    }
    float rest_x = place.ox + ((x + 0.5f) - place.shift - place.ox) / place.scale;
    float rest_y = place.oy + ((y + 0.5f) - place.oy) / place.scale;
    int base_x = (int)floorf(rest_x - place.left);
    int base_y = (int)floorf(rest_y - place.top);
    if (place.scale >= 0.95f) {
        lx = base_x;
        ly = base_y;
        return stamp_ink(stamp, lx, ly, index);
    }
    int reach = (int)(0.5f / place.scale);
    if (reach < 1) {
        reach = 1;
    }
    if (reach > 3) {
        reach = 3;
    }
    for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
            if (stamp_ink(stamp, base_x + ox, base_y + oy, index)) {
                lx = base_x + ox;
                ly = base_y + oy;
                return true;
            }
        }
    }
    return false;
}

static void chrome_rgb(const Stamp &stamp, int lx, int ly, float glint, int &red, int &green, int &blue) {
    int index = 0;
    bool up = !stamp_ink(stamp, lx, ly - 1, index);
    bool left = !stamp_ink(stamp, lx - 1, ly, index);
    bool down = !stamp_ink(stamp, lx, ly + 1, index);
    bool right = !stamp_ink(stamp, lx + 1, ly, index);
    red = 176;
    green = 186;
    blue = 198;
    if (up || left) {
        red = 236;
        green = 242;
        blue = 248;
    }
    if (up && left) {
        red = 255;
        green = 255;
        blue = 255;
    }
    if (down || right) {
        red = 110;
        green = 120;
        blue = 132;
    }
    if (glint > 0.72f) {
        red = 255;
        green = 255;
        blue = 255;
    }
}

static void word_colour(const Stamp &stamp, int lx, int ly, int index, bool chrome, int &red, int &green, int &blue) {
    float chase = span_u(CHASE_AT, CHASE_MS);
    bool in_chase = elapsed >= (uint32_t)CHASE_AT && elapsed < (uint32_t)POP_AT;
    bool in_pop = elapsed >= (uint32_t)POP_AT && elapsed < (uint32_t)HOLD_AT;
    float glint_phase = 0.8f;
    if (in_chase) {
        glint_phase = chase * 4.0f;
    } else if (in_pop) {
        glint_phase = 4.0f;
    }
    float glint = sinf(lx * 0.55f + ly * 0.25f + glint_phase);
    if (chrome) {
        chrome_rgb(stamp, lx, ly, glint, red, green, blue);
    } else {
        int letter = index;
        if (letter < 0) {
            letter = 0;
        }
        if (letter > 3) {
            letter = 3;
        }
        red = XYLO_RGB[letter][0];
        green = XYLO_RGB[letter][1];
        blue = XYLO_RGB[letter][2];
    }
    float wash = 0.0f;
    float surf = 0.0f;
    if (in_chase) {
        float slosh = sinf(lx * 0.5f - chase * 7.0f) * 1.5f * (1.0f - chase);
        surf = (1.0f - chase) * (float)stamp.height + slosh;
        if (chase >= 0.98f || ly + 0.5f >= surf) {
            wash = 1.0f;
        }
    } else if (in_pop) {
        wash = 1.0f - span_u(POP_AT, POP_MS);
        surf = 0.0f;
    }
    if (wash <= 0.0f) {
        return;
    }
    int liquid_r = 255;
    int liquid_g = 255;
    int liquid_b = 255;
    bool crest = (ly + 0.5f - surf) < 1.3f;
    if (chrome) {
        float depth = (ly + 0.5f - surf) / 8.0f;
        if (depth < 0.0f) {
            depth = 0.0f;
        }
        if (depth > 1.0f) {
            depth = 1.0f;
        }
        if (crest) {
            liquid_r = 255;
            liquid_g = 255;
            liquid_b = 255;
        } else {
            liquid_r = mix_channel(236, 150, depth);
            liquid_g = mix_channel(242, 162, depth);
            liquid_b = mix_channel(248, 176, depth);
        }
    } else {
        rainbow_at((float)lx / (float)stamp.width, liquid_r, liquid_g, liquid_b);
        if (crest) {
            liquid_r = mix_channel(liquid_r, 255, 0.75f);
            liquid_g = mix_channel(liquid_g, 255, 0.75f);
            liquid_b = mix_channel(liquid_b, 255, 0.75f);
        }
    }
    red = mix_channel(red, liquid_r, wash);
    green = mix_channel(green, liquid_g, wash);
    blue = mix_channel(blue, liquid_b, wash);
}

static void paint_word(uint8_t *frame, const Place &place, const Stamp &stamp, bool chrome, float alpha) {
    if (!place.live || alpha < 0.02f || place.scale < 0.12f) {
        return;
    }
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    place_bounds(place, stamp, x0, y0, x1, y1);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > PANEL_WIDTH) {
        x1 = PANEL_WIDTH;
    }
    if (y1 > PANEL_HEIGHT) {
        y1 = PANEL_HEIGHT;
    }
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int lx = 0;
            int ly = 0;
            int index = 0;
            if (!sample_stamp(place, stamp, x, y, lx, ly, index)) {
                continue;
            }
            int red = 0;
            int green = 0;
            int blue = 0;
            word_colour(stamp, lx, ly, index, chrome, red, green, blue);
            put(frame, x, y, red, green, blue, alpha);
        }
    }
}

// Wide band, so the sweep still reads while it crosses the panel quickly.
static void paint_gold_shine(uint8_t *frame) {
    if (elapsed < (uint32_t)POP_AT || elapsed >= (uint32_t)POP_AT + (uint32_t)SHINE_MS) {
        return;
    }
    float u = span_u(POP_AT, SHINE_MS);
    float span = (float)(PANEL_WIDTH + PANEL_HEIGHT - 2);
    float half = 22.0f;
    float head = -half + u * (span + half * 2.0f);
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dist = fabsf((float)(x + y) - head);
            if (dist > half) {
                continue;
            }
            float strength = 1.0f - dist / half;
            strength = strength * strength;
            uint8_t *pixel = frame + (y * PANEL_WIDTH + x) * 3;
            int blue = pixel[0];
            int green = pixel[1];
            int red = pixel[2];
            int gold_r = 255;
            int gold_g = mix_channel(168, 246, strength);
            int gold_b = mix_channel(32, 186, strength);
            float mix = strength * 0.94f;
            write_bgr(
                pixel,
                mix_channel(red, gold_r, mix),
                mix_channel(green, gold_g, mix),
                mix_channel(blue, gold_b, mix));
        }
    }
}

static void paint_splash(uint8_t *frame) {
    float fly = span_u(FLY_AT, FLY_MS);
    float field = 1.0f;
    float letter_alpha = 1.0f;
    float border_alpha = 0.0f;
    float flash = 0.0f;
    if (elapsed >= (uint32_t)FLY_AT) {
        field = 1.0f - fly * fly;
        letter_alpha = fly < 0.4f ? 1.0f : 1.0f - (fly - 0.4f) / 0.6f;
        border_alpha = fly < 0.28f ? (1.0f - fly / 0.28f) * field : 0.0f;
    } else if (elapsed >= (uint32_t)CHASE_AT) {
        border_alpha = 1.0f;
        if (elapsed < (uint32_t)HOLD_AT) {
            flash = sinf(span_u(POP_AT, POP_MS) * 3.14159265f) * 0.9f;
        }
    }
    fill_frame(frame, FIELD_R, FIELD_G, FIELD_B, field);
    paint_border(frame, border_alpha, flash);

    Stamp xylo = make_splash_stamp("XYLO", 1);
    Stamp scope = make_splash_stamp("SCOPE", 2);
    float enter = span_u(SPLASH_AT, ENTER_MS);
    float inv = 1.0f - enter;
    float ease = 1.0f - inv * inv * inv;
    Place xylo_at;
    Place scope_at;
    // 28px tall, one pixel clear of the border, with a 2px gap between the words.
    float word_left = (float)((PANEL_WIDTH - xylo.width) / 2);
    rest_place(xylo_at, word_left, 3.0f, (float)xylo.width, (float)xylo.height);
    rest_place(scope_at, word_left, 33.0f, (float)scope.width, (float)scope.height);
    if (elapsed < (uint32_t)CHASE_AT) {
        xylo_at.shift = (1.0f - ease) * -(xylo_at.left + (float)xylo.width + 8.0f);
        scope_at.ox = 31.5f;
        scope_at.oy = 31.5f;
        scope_at.scale = ease;
        scope_at.live = ease >= 0.12f;
    }
    if (elapsed >= (uint32_t)POP_AT && elapsed < (uint32_t)HOLD_AT) {
        float bump = 1.0f + sinf(span_u(POP_AT, POP_MS) * 3.14159265f) * 0.08f;
        xylo_at.scale = bump;
        scope_at.scale = bump;
    }
    if (elapsed >= (uint32_t)FLY_AT) {
        float zoom = 1.0f + fly * fly * 10.0f;
        xylo_at.scale = zoom;
        scope_at.scale = zoom;
        xylo_at.ox = 31.5f;
        xylo_at.oy = 31.5f;
        scope_at.ox = 31.5f;
        scope_at.oy = 31.5f;
        xylo_at.shift = 0.0f;
        scope_at.shift = 0.0f;
    }
    paint_word(frame, xylo_at, xylo, false, letter_alpha);
    paint_word(frame, scope_at, scope, true, letter_alpha);
    paint_gold_shine(frame);
}

void intro_start() {
    running = true;
    elapsed = 0;
    uint32_t bits = 0;
    for (int i = 0; i < 32; ++i) {
        bits = (bits << 1) | (rosc_hw->randombit & 1u);
    }
    phrase_index = (int)((bits ^ time_us_32()) % (uint32_t)PHRASE_COUNT);
    seed_stars();
    seed_bubbles();
}

void intro_skip() {
    running = false;
}

bool intro_active() {
    return running;
}

bool intro_fading() {
    return running && elapsed >= (uint32_t)FADE_AT && elapsed < (uint32_t)END_AT;
}

float intro_gain() {
    if (elapsed <= (uint32_t)FADE_AT) {
        return 0.0f;
    }
    float u = (float)(elapsed - (uint32_t)FADE_AT) / (float)FADE_MS;
    if (u > 1.0f) {
        u = 1.0f;
    }
    return u * u * (3.0f - 2.0f * u);
}

void intro_advance(int dt_ms) {
    if (!running) {
        return;
    }
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    if (elapsed < (uint32_t)SPLASH_AT) {
        step_stars(dt_ms);
    }
    elapsed += (uint32_t)dt_ms;
    if (elapsed >= (uint32_t)END_AT) {
        running = false;
    }
}

void intro_paint(uint8_t *frame) {
    if (elapsed < (uint32_t)BOOT_FILL_MS) {
        paint_boot(frame, false);
        return;
    }
    if (elapsed < (uint32_t)SPLASH_AT) {
        paint_stars(frame);
        if (elapsed < (uint32_t)BOOT_MS) {
            paint_boot(frame, true);
        }
        return;
    }
    paint_splash(frame);
}
