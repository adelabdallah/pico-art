// Two night scenes. The encoder swaps them.
// The crescent follows Eid Crescent at
// https://www.ascii-magic.com/ascii/eid-moon
// The lanterns follow Ramadan Lanterns at
// https://www.ascii-magic.com/ascii/ramadan-lanterns
//
// The crescent opens the pattern. One encoder step swaps to the lanterns,
// and the next step swaps back.
// Yellow, while held, walks the moon through its phases, one full set
// about every 3 seconds. Releasing yellow leaves the moon where it stopped.
// Orange on the crescent, while held, runs the rooftop lights at double speed.
// Orange on the lanterns, while held, walks the glass hue. Releasing orange
// leaves that hue where it stopped.
// The lantern chains hang straight down, and the lanterns sway.
// Green toggles a shower of the medium shells from fireworks, about 3 a
// second, with about 10 in the sky at once. A second press stops them.
// The shower keeps going when the scene swaps.
// Two panels show the full width. One and four panels show a square.
// On the crescent that square puts the moon in the upper right. On the
// lanterns the square is the middle of the scene.

#include "pattern.hpp"
#include "noise.hpp"

#include "pico/time.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr float TAU = 6.2831853f;
constexpr int EID_C = 100;
constexpr int EID_R = 36;
constexpr int LAN_C = 90;
constexpr int LAN_R = 36;
constexpr float MOON_CYCLE_MS = 3000.0f;
constexpr float GLASS_TURN_MS = 4000.0f;
constexpr float SHELL_EVERY_MS = 333.0f;
constexpr float RISE = 1.15f;
constexpr float FADE = 2.2f;
constexpr int SHELL_CAP = 10;
constexpr int HUE_COUNT = 7;

static const uint8_t HUE[HUE_COUNT][3] = {
    {255, 64, 64},
    {255, 150, 40},
    {255, 220, 70},
    {80, 230, 110},
    {70, 190, 255},
    {170, 90, 255},
    {255, 80, 180},
};

static const uint8_t GLASS[4][3] = {
    {255, 176, 48},
    {255, 74, 90},
    {58, 208, 138},
    {58, 200, 224},
};

static const char *SPRITE[12] = {
    "    o    ",
    "   ###   ",
    "  #####  ",
    " ####### ",
    " #GGGGG# ",
    " #GGGGG# ",
    " #GGGGG# ",
    " #GGGGG# ",
    " #GGGGG# ",
    " #e#e#e# ",
    "   #e#   ",
    "    v    ",
};

struct Lantern {
    float x;
    float len;
    float phase;
    int family;
    float flick;
};

struct Shell {
    float age;
    float bx;
    float by;
    float sx;
    int hue;
    int hue2;
    int count;
    float speed;
    int id;
    bool on;
};

struct Cam {
    float ox;
    float oy;
    float scale;
};

static uint8_t *eid_mask = nullptr;
static Lantern lanterns[8];
static Shell shells[SHELL_CAP];
static int scene = 0;
static float moon_phase = 0.16f;
static float fest_seconds = 0.0f;
static float glass_turns = 0.0f;
static float scene_seconds = 0.0f;
static bool yellow_held = false;
static bool orange_held = false;
static bool firing = false;
static float spawn_left = 0.0f;
static int launch_id = 1;
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

static void add_px(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    uint8_t *pixel = frame + (y * PANEL_WIDTH + x) * 3;
    int blue_v = pixel[0] + blue;
    int green_v = pixel[1] + green;
    int red_v = pixel[2] + red;
    if (blue_v > 255) {
        blue_v = 255;
    }
    if (green_v > 255) {
        green_v = 255;
    }
    if (red_v > 255) {
        red_v = 255;
    }
    pixel[0] = (uint8_t)blue_v;
    pixel[1] = (uint8_t)green_v;
    pixel[2] = (uint8_t)red_v;
}

static void mask_put(int cols, int rows, int x, int y, uint8_t value) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) {
        return;
    }
    eid_mask[x + y * cols] = value;
}

static void build_eid() {
    rng_state = 1447u;
    int x = 0;
    while (x < EID_C) {
        int width = 6 + (int)(rnd() * 8.0f);
        int top = 27 + (int)(rnd() * 4.0f);
        for (int i = 0; i < width && x + i < EID_C; ++i) {
            for (int y = top; y < EID_R; ++y) {
                mask_put(EID_C, EID_R, x + i, y, 1);
            }
        }
        for (int i = 2; i < width - 1; i += 3) {
            for (int y = top + 2; y < EID_R - 1; y += 3) {
                if (rnd() < 0.55f) {
                    mask_put(EID_C, EID_R, x + i, y, 2);
                    mask_put(EID_C, EID_R, x + i, y + 1, 2);
                }
            }
        }
        x += width;
    }
    const int domes[][4] = {{22, 24, 9, 7}, {50, 22, 12, 9}, {70, 25, 7, 6}, {8, 27, 5, 4}, {90, 26, 6, 5}};
    for (int d = 0; d < 5; ++d) {
        int cx = domes[d][0];
        int base = domes[d][1];
        int rx = domes[d][2];
        int ry = domes[d][3];
        for (int y = base - ry; y <= base; ++y) {
            float dy = (float)(base - y) / (float)ry;
            float half = (float)rx * sqrtf(fmaxf(0.0f, 1.0f - dy * dy));
            half *= 1.0f + 0.25f * sinf(dy * 3.14159265f) * (1.0f - dy);
            for (int i = (int)ceilf((float)cx - half); i <= (int)floorf((float)cx + half); ++i) {
                mask_put(EID_C, EID_R, i, y, i > cx + half - 1.5f ? 4 : 1);
            }
        }
        for (int y = base + 1; y < EID_R; ++y) {
            for (int i = cx - rx; i <= cx + rx; ++i) {
                mask_put(EID_C, EID_R, i, y, 1);
            }
        }
        for (int i = cx - rx + 2; i <= cx + rx - 2; i += 3) {
            mask_put(EID_C, EID_R, i, base + 3, 2);
            mask_put(EID_C, EID_R, i, base + 4, 2);
            mask_put(EID_C, EID_R, i, base + 5, 2);
        }
        mask_put(EID_C, EID_R, cx, base - ry - 1, 1);
        mask_put(EID_C, EID_R, cx, base - ry - 2, 3);
    }
    const int mins[][2] = {{34, 8}, {66, 11}, {4, 15}, {96, 14}};
    for (int m = 0; m < 4; ++m) {
        int mx = mins[m][0];
        int top = mins[m][1];
        for (int y = top; y < EID_R; ++y) {
            for (int i = -1; i <= 1; ++i) {
                mask_put(EID_C, EID_R, mx + i, y, i == 1 ? 4 : 1);
            }
        }
        for (int band = 0; band < 2; ++band) {
            int by = top + (band == 0 ? 5 : 11);
            for (int i = -2; i <= 2; ++i) {
                mask_put(EID_C, EID_R, mx + i, by, 1);
            }
        }
        mask_put(EID_C, EID_R, mx, top - 1, 1);
        mask_put(EID_C, EID_R, mx, top - 2, 3);
        mask_put(EID_C, EID_R, mx, top + 3, 2);
        mask_put(EID_C, EID_R, mx, top + 8, 2);
    }
}

static Cam frame_cam(float scene_w, float scene_h, float focus_x, float focus_y, float square_view, float anchor_x, float anchor_y) {
    Cam cam;
    if (PANEL_WIDTH > PANEL_HEIGHT) {
        cam.scale = (float)PANEL_WIDTH / scene_w;
        float view_h = (float)PANEL_HEIGHT / cam.scale;
        cam.ox = 0.0f;
        cam.oy = (scene_h - view_h) * 0.42f;
        if (cam.oy < 0.0f) {
            cam.oy = 0.0f;
        }
        return cam;
    }
    cam.scale = (float)PANEL_WIDTH / square_view;
    cam.ox = focus_x - anchor_x * square_view;
    cam.oy = focus_y - anchor_y * square_view;
    return cam;
}

static void cell_at(const Cam &cam, int x, int y, float &col, float &row) {
    float vx = cam.ox + ((float)x + 0.5f) / cam.scale;
    float vy = cam.oy + ((float)y + 0.5f) / cam.scale;
    col = vx;
    row = vy * 0.5f;
}

static void lerp3(int r0, int g0, int b0, int r1, int g1, int b1, float t, int &red, int &green, int &blue) {
    red = (int)(r0 + (r1 - r0) * t);
    green = (int)(g0 + (g1 - g0) * t);
    blue = (int)(b0 + (b1 - b0) * t);
}

static void eid_sky(float row, float glow, float cloud, int &red, int &green, int &blue) {
    float v = clampf(0.08f + powf(clampf(row / 30.0f, 0.0f, 1.0f), 1.3f) * 0.85f + (cloud - 0.5f) * 0.18f + glow * 0.35f, 0.0f, 1.0f);
    if (v < 0.45f) {
        lerp3(4, 8, 32, 26, 46, 102, v / 0.45f, red, green, blue);
    } else if (v < 0.75f) {
        lerp3(26, 46, 102, 122, 74, 138, (v - 0.45f) / 0.3f, red, green, blue);
    } else {
        lerp3(122, 74, 138, 240, 160, 112, (v - 0.75f) / 0.25f, red, green, blue);
    }
}

static void paint_moon(uint8_t *frame, const Cam &cam, float mcol, float mrow, float radius) {
    float cx = mcol;
    float cy = mrow * 2.0f;
    float cycle = moon_phase - floorf(moon_phase);
    float wax = cycle < 0.5f ? cycle * 2.0f : (1.0f - cycle) * 2.0f;
    float side = cycle < 0.5f ? 1.0f : -1.0f;
    float cover = 1.0f - 2.0f * wax;
    int x0 = (int)((cx - radius - cam.ox) * cam.scale) - 2;
    int y0 = (int)((cy - radius - cam.oy) * cam.scale) - 2;
    int x1 = (int)((cx + radius - cam.ox) * cam.scale) + 2;
    int y1 = (int)((cy + radius - cam.oy) * cam.scale) + 2;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float vx = cam.ox + ((float)x + 0.5f) / cam.scale;
            float vy = cam.oy + ((float)y + 0.5f) / cam.scale;
            float nx = (vx - cx) / radius;
            float ny = (vy - cy) / radius;
            float dist = nx * nx + ny * ny;
            if (dist > 1.0f) {
                continue;
            }
            float limb = sqrtf(fmaxf(0.0f, 1.0f - ny * ny));
            if (side * nx <= cover * limb) {
                continue;
            }
            float shade = 0.55f + 0.45f * (1.0f - dist);
            int red = (int)(180 + shade * 75);
            int green = (int)(140 + shade * 90);
            int blue = (int)(50 + shade * 70);
            put(frame, x, y, red, green, blue);
        }
    }
    float star_x = cx + (mcol > 50.0f ? -8.0f : 7.0f);
    float star_y = cy - 2.0f;
    int sx = (int)((star_x - cam.ox) * cam.scale);
    int sy = (int)((star_y - cam.oy) * cam.scale);
    put(frame, sx, sy, 255, 244, 200);
}

static void fill_cell(uint8_t *frame, const Cam &cam, float col, float row, int red, int green, int blue) {
    int x0 = (int)((col - cam.ox) * cam.scale);
    int y0 = (int)((row * 2.0f - cam.oy) * cam.scale);
    int x1 = (int)((col + 1.0f - cam.ox) * cam.scale);
    int y1 = (int)(((row + 1.0f) * 2.0f - cam.oy) * cam.scale);
    if (x1 <= x0) {
        x1 = x0 + 1;
    }
    if (y1 <= y0) {
        y1 = y0 + 1;
    }
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            put(frame, x, y, red, green, blue);
        }
    }
}

static void paint_eid(uint8_t *frame, const Cam &cam) {
    const float moon_x = 76.0f;
    const float moon_y = 9.0f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float col;
            float row;
            cell_at(cam, x, y, col, row);
            int cx = (int)floorf(col);
            int ry = (int)floorf(row);
            uint8_t cell = 0;
            if (cx >= 0 && ry >= 0 && cx < EID_C && ry < EID_R) {
                cell = eid_mask[cx + ry * EID_C];
            }
            float glow = expf(-hypotf((col - moon_x) * 0.5f, row - moon_y) / 6.0f);
            float cloud = field_noise(col * 0.04f + scene_seconds * 0.02f, row * 0.13f, 4);
            if (cell == 0) {
                int red;
                int green;
                int blue;
                eid_sky(row, glow, cloud, red, green, blue);
                put(frame, x, y, red, green, blue);
            } else if (cell == 2) {
                float blink = sinf(fest_seconds * 0.6f + field_hash(cx, ry, 9) * TAU);
                if (blink > 0.5f) {
                    put(frame, x, y, 255, 207, 106);
                } else if (blink > -0.6f) {
                    put(frame, x, y, 255, 168, 58);
                } else {
                    put(frame, x, y, 90, 60, 30);
                }
            } else if (cell == 3) {
                put(frame, x, y, 216, 180, 90);
            } else if (cell == 4) {
                put(frame, x, y, 48, 56, 92);
            } else {
                int shade = ry < 26 ? 18 : 8;
                if (((cx + ry) & 7) == 0) {
                    shade += 10;
                }
                put(frame, x, y, shade, shade + 2, shade + 14);
            }
        }
    }
    for (int i = 0; i < 90; ++i) {
        int sx = (int)(field_hash(i, 1, 1447) * EID_C);
        int sy = (int)(field_hash(i, 2, 1447) * 22.0f);
        if (eid_mask[sx + sy * EID_C] != 0) {
            continue;
        }
        float phase = field_hash(i, 3, 1447) * TAU;
        float speed = 0.8f + field_hash(i, 4, 1447) * 1.6f;
        float blink = sinf(scene_seconds * speed + phase);
        if (blink <= -0.2f) {
            continue;
        }
        int red = blink > 0.4f ? 232 : 138;
        int green = blink > 0.4f ? 238 : 148;
        int blue = blink > 0.4f ? 248 : 184;
        fill_cell(frame, cam, (float)sx, (float)sy, red, green, blue);
    }
    paint_moon(frame, cam, moon_x, moon_y, 9.0f);
    float chase = fest_seconds * (orange_held && scene == 0 ? 6.0f : 3.0f);
    for (int col = 0; col < EID_C; col += 2) {
        float hang = 29.0f + 1.5f * sinf((float)(col % 25) / 25.0f * 3.14159265f);
        bool hot = ((col / 2 + (int)floorf(chase)) % 3) == 0;
        if (hot) {
            fill_cell(frame, cam, (float)col, hang, 255, 233, 168);
        } else {
            fill_cell(frame, cam, (float)col, hang, 255, 168, 58);
        }
    }
}

static void glass_rgb(int family, float level, int &red, int &green, int &blue) {
    const uint8_t *base = GLASS[family & 3];
    red = (int)(30 + base[0] * level);
    green = (int)(16 + base[1] * level);
    blue = (int)(8 + base[2] * level);
    hue_shift(red, green, blue, glass_turns);
}

static void paint_lanterns(uint8_t *frame, const Cam &cam) {
    const float moon_x = 46.0f;
    const float moon_y = 7.0f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float col;
            float row;
            cell_at(cam, x, y, col, row);
            float cloud = field_noise(col * 0.05f + scene_seconds * 0.03f, row * 0.12f, 6);
            float v = clampf(0.12f + clampf(row / 29.0f, 0.0f, 1.0f) * 0.5f + (cloud - 0.5f) * 0.2f, 0.0f, 1.0f);
            int red = (int)(6 + v * 20);
            int green = (int)(16 + v * 50);
            int blue = (int)(36 + v * 70);
            float dm = hypotf((col - moon_x) * 0.5f, row - moon_y);
            float moon_glow = expf(-dm / 6.0f) * 0.35f;
            red += (int)(moon_glow * 80);
            green += (int)(moon_glow * 60);
            blue += (int)(moon_glow * 20);
            put(frame, x, y, red, green, blue);
        }
    }
    for (int i = 0; i < 60; ++i) {
        float sx = field_hash(i, 8, 30) * LAN_C;
        float sy = field_hash(i, 9, 30) * 26.0f;
        float phase = field_hash(i, 10, 30) * TAU;
        float blink = sinf(scene_seconds * 1.4f + phase);
        if (blink > 0.2f) {
            fill_cell(frame, cam, sx, sy, 232, 237, 248);
        }
    }
    paint_moon(frame, cam, moon_x, moon_y, 7.0f);
    for (int i = 0; i < 8; ++i) {
        Lantern &lantern = lanterns[i];
        float sway = sinf(scene_seconds * 0.8f + lantern.phase) * 1.2f;
        float hang_x = lantern.x + sway;
        float flick = 0.8f + 0.2f * sinf(scene_seconds * 9.0f + lantern.flick) * sinf(scene_seconds * 5.3f + lantern.flick * 2.0f);
        int top = (int)lantern.len;
        for (int row = 0; row < top; ++row) {
            int brass_r = (row & 1) ? 212 : 138;
            int brass_g = (row & 1) ? 164 : 100;
            int brass_b = (row & 1) ? 64 : 32;
            fill_cell(frame, cam, hang_x, (float)row, brass_r, brass_g, brass_b);
        }
        for (int row = 0; row < 12; ++row) {
            const char *line = SPRITE[row];
            for (int col = 0; line[col] != '\0'; ++col) {
                char mark = line[col];
                if (mark == ' ') {
                    continue;
                }
                int red = 138;
                int green = 100;
                int blue = 32;
                if (mark == 'G') {
                    int gi = col - 2;
                    int gj = row - 4;
                    int dist = abs(gi - 2) + abs(gj - 2);
                    float level = flick * (dist == 0 ? 1.0f : dist == 1 ? 0.82f : 0.62f);
                    glass_rgb(lantern.family, level, red, green, blue);
                } else if (mark == 'o') {
                    red = 212;
                    green = 164;
                    blue = 64;
                } else if (mark == 'v') {
                    glass_rgb(lantern.family, 0.7f, red, green, blue);
                } else if (col < 4) {
                    red = 212;
                    green = 164;
                    blue = 64;
                }
                fill_cell(frame, cam, hang_x + (float)col - 4.0f, (float)(top + row), red, green, blue);
            }
        }
        float glow_col = hang_x;
        float glow_row = lantern.len + 6.0f;
        int x0 = (int)((glow_col - 34.0f - cam.ox) * cam.scale) - 1;
        int y0 = (int)(((glow_row - 18.0f) * 2.0f - cam.oy) * cam.scale) - 1;
        int x1 = (int)((glow_col + 34.0f - cam.ox) * cam.scale) + 1;
        int y1 = (int)(((glow_row + 18.0f) * 2.0f - cam.oy) * cam.scale) + 1;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float col;
                float row;
                cell_at(cam, x, y, col, row);
                float dist = hypotf((col - glow_col) * 0.55f, row - glow_row);
                if (dist > 18.0f) {
                    continue;
                }
                float glow = expf(-dist / 3.2f) * flick * 0.55f;
                add_px(frame, x, y, (int)(glow * 90), (int)(glow * 50), (int)(glow * 12));
            }
        }
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float col;
            float row;
            cell_at(cam, x, y, col, row);
            if (row < 29.0f || col < 0.0f || col >= LAN_C) {
                continue;
            }
            float along = col - floorf(col / 12.0f) * 12.0f;
            float u = along - 5.5f;
            float spring = 33.2f - sqrtf(fmaxf(0.0f, 16.0f - u * u)) * 0.85f;
            bool open = fabsf(u) < 4.0f && row > spring;
            if ((int)floorf(row) == 29) {
                put(frame, x, y, 200, 162, 74);
            } else if (open) {
                float flicker = field_noise(col * 0.3f, row * 0.5f + scene_seconds * 0.4f, 2);
                if (flicker > 0.62f) {
                    put(frame, x, y, 255, 220, 150);
                } else {
                    put(frame, x, y, 255, 190, 90);
                }
            } else if (fabsf(fabsf(u) - 4.5f) < 0.7f) {
                put(frame, x, y, 28, 40, 52);
            } else {
                put(frame, x, y, 12, 20, 30);
            }
        }
    }
}

static bool spawn_shell() {
    int slot = -1;
    for (int i = 0; i < SHELL_CAP; ++i) {
        if (!shells[i].on) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return false;
    }
    Shell &shell = shells[slot];
    shell.on = true;
    shell.age = 0.0f;
    shell.bx = 8.0f + rnd() * (float)(PANEL_WIDTH - 16);
    float sky = (float)PANEL_HEIGHT * 0.42f;
    if (sky < 12.0f) {
        sky = 12.0f;
    }
    shell.by = 6.0f + rnd() * sky;
    shell.sx = shell.bx + (rnd() - 0.5f) * 10.0f;
    shell.hue = (int)(rnd() * HUE_COUNT);
    shell.hue2 = (int)(rnd() * HUE_COUNT);
    if (shell.hue >= HUE_COUNT) {
        shell.hue = HUE_COUNT - 1;
    }
    if (shell.hue2 >= HUE_COUNT) {
        shell.hue2 = HUE_COUNT - 1;
    }
    shell.count = 36 + (int)(rnd() * 16.0f);
    shell.speed = 8.0f + rnd() * 6.0f;
    shell.id = launch_id;
    launch_id += 1;
    return true;
}

static void paint_shells(uint8_t *frame, float step) {
    if (firing) {
        spawn_left += step * 1000.0f;
        int guard = 0;
        while (spawn_left >= SHELL_EVERY_MS && guard < 4) {
            if (!spawn_shell()) {
                break;
            }
            spawn_left -= SHELL_EVERY_MS;
            guard += 1;
        }
    }
    for (int i = 0; i < SHELL_CAP; ++i) {
        Shell &shell = shells[i];
        if (!shell.on) {
            continue;
        }
        shell.age += step;
        if (shell.age > RISE + FADE) {
            shell.on = false;
            continue;
        }
        if (shell.age < RISE) {
            float portion = shell.age / RISE;
            float ease = 1.0f - (1.0f - portion) * (1.0f - portion);
            int x = (int)(shell.sx + (shell.bx - shell.sx) * ease + 0.5f);
            int y = (int)((float)(PANEL_HEIGHT - 1) + (shell.by - (float)(PANEL_HEIGHT - 1)) * ease + 0.5f);
            const uint8_t *colour = HUE[shell.hue];
            add_px(frame, x, y, colour[0], colour[1], colour[2]);
            add_px(frame, x, y + 1, colour[0] / 2, colour[1] / 2, colour[2] / 2);
            continue;
        }
        float age = shell.age - RISE;
        float fade = age / FADE;
        float drag = (1.0f - expf(-2.2f * age)) / 2.2f;
        for (int p = 0; p < shell.count; ++p) {
            float theta = (float)p / (float)shell.count * TAU + field_hash(shell.id, p + 20, 3) * 0.25f;
            float speed = shell.speed * ((p & 1) ? 0.55f : 1.0f);
            float x = shell.bx + cosf(theta) * speed * drag;
            float y = shell.by + sinf(theta) * speed * drag + 3.2f * age * age;
            int hue = (p % 3 == 0) ? shell.hue2 : shell.hue;
            int dim = (int)(fade * 3.0f);
            if (dim > 3) {
                dim = 3;
            }
            if (fade > 0.65f && sinf(scene_seconds * 30.0f + (float)p) > 0.35f) {
                continue;
            }
            const uint8_t *colour = HUE[hue];
            int scale = 4 - dim;
            add_px(frame, (int)x, (int)y, colour[0] * scale / 4, colour[1] * scale / 4, colour[2] * scale / 4);
        }
    }
}

static void place_lanterns() {
    const float spots[][2] = {{5, 9}, {15, 15}, {25, 6}, {33, 17}, {59, 16}, {67, 7}, {76, 14}, {85, 9}};
    rng_state = 30u;
    for (int i = 0; i < 8; ++i) {
        lanterns[i].x = spots[i][0];
        lanterns[i].len = spots[i][1];
        lanterns[i].phase = rnd() * TAU;
        lanterns[i].family = i & 3;
        lanterns[i].flick = rnd() * 9.0f;
    }
}

}  // namespace

void ramadan_turn(int steps) {
    int flips = steps < 0 ? -steps : steps;
    if ((flips & 1) != 0) {
        scene ^= 1;
    }
}

void ramadan_yellow(bool held) {
    yellow_held = held;
}

void ramadan_orange(bool held) {
    orange_held = held;
}

void ramadan_green() {
    firing = !firing;
    if (firing) {
        spawn_left = SHELL_EVERY_MS;
    }
}

void ramadan_start() {
    eid_mask = pattern_alloc(EID_C * EID_R);
    build_eid();
    place_lanterns();
    scene = 0;
    moon_phase = 0.16f;
    fest_seconds = 0.0f;
    glass_turns = 0.0f;
    scene_seconds = 0.0f;
    yellow_held = false;
    orange_held = false;
    firing = false;
    spawn_left = 0.0f;
    rng_state = time_us_32() | 1u;
    for (int i = 0; i < SHELL_CAP; ++i) {
        shells[i].on = false;
    }
}

void ramadan_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    float step = dt_ms * 0.001f;
    scene_seconds += step;
    fest_seconds += step;
    if (fest_seconds > 1000.0f) {
        fest_seconds -= 1000.0f;
    }
    if (yellow_held) {
        moon_phase += (float)dt_ms / MOON_CYCLE_MS;
        moon_phase -= floorf(moon_phase);
    }
    if (scene == 1 && orange_held) {
        glass_turns += (float)dt_ms / GLASS_TURN_MS;
        glass_turns -= floorf(glass_turns);
    }
    if (scene == 0) {
        Cam cam = frame_cam(100.0f, 72.0f, 76.0f, 18.0f, 52.0f, 0.75f, 0.25f);
        paint_eid(frame, cam);
    } else {
        Cam cam = frame_cam(90.0f, 72.0f, 45.0f, 36.0f, 52.0f, 0.5f, 0.5f);
        paint_lanterns(frame, cam);
    }
    paint_shells(frame, step);
}
