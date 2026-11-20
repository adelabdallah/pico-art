// A neon harbour wider than the panel. The view is a 64 by 64 window on a 192 by 108 picture.
// The layout and the motion follow Neon Harbour at
// https://www.ascii-magic.com/ascii/harbour-neon-skyline
// Orange moves the view up, yellow moves it down, and the encoder moves it left and right.
// The view stops at the edges. Windows, the ferry, and the bridge traffic keep moving.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>
#include <cstring>

namespace {

constexpr int WORLD_W = 192;
constexpr int WORLD_H = 108;
constexpr int HORIZON = 74;
constexpr int WINDOW_CAP = 480;

struct Win {
    int16_t x;
    int16_t y;
};

struct Car {
    float phase;
    float speed;
    int dir;
};

// The still harbour lives here for the life of the program.
// start paints it again on each visit, and the camera copies one window out of it.
static uint8_t plate[WORLD_H * WORLD_W * 3];
static Win windows[WINDOW_CAP];
static int window_count = 0;
static Car cars[9];
static int cam_x = 16;
static int cam_y = 22;
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static float unit_hash(int n, int k) {
    uint32_t h = (uint32_t)n * 374761393u + (uint32_t)k * 668265263u + 977u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)h * (1.0f / 4294967296.0f);
}

static int mixi(int a, int b, float t) {
    return a + (int)((b - a) * t);
}

static void put_plate(int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H) {
        return;
    }
    if (red < 0) {
        red = 0;
    }
    if (green < 0) {
        green = 0;
    }
    if (blue < 0) {
        blue = 0;
    }
    if (red > 255) {
        red = 255;
    }
    if (green > 255) {
        green = 255;
    }
    if (blue > 255) {
        blue = 255;
    }
    uint8_t *pixel = plate + (y * WORLD_W + x) * 3;
    pixel[0] = (uint8_t)blue;
    pixel[1] = (uint8_t)green;
    pixel[2] = (uint8_t)red;
}

static void get_plate(int x, int y, int &red, int &green, int &blue) {
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= WORLD_W) {
        x = WORLD_W - 1;
    }
    if (y >= WORLD_H) {
        y = WORLD_H - 1;
    }
    const uint8_t *pixel = plate + (y * WORLD_W + x) * 3;
    blue = pixel[0];
    green = pixel[1];
    red = pixel[2];
}

static void sky_colour(float t, int &red, int &green, int &blue) {
    if (t < 0.45f) {
        float u = t / 0.45f;
        red = mixi(28, 80, u);
        green = mixi(32, 46, u);
        blue = mixi(96, 160, u);
        return;
    }
    if (t < 0.75f) {
        float u = (t - 0.45f) / 0.3f;
        red = mixi(80, 208, u);
        green = mixi(46, 72, u);
        blue = mixi(160, 168, u);
        return;
    }
    float u = (t - 0.75f) / 0.25f;
    red = mixi(208, 255, u);
    green = mixi(72, 176, u);
    blue = mixi(168, 154, u);
}

static int deck_y(int x) {
    float t = ((float)x - 118.0f) / 74.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    return (int)(70.0f + (61.0f - 70.0f) * t + 0.5f);
}

static void cable(int x0, int y0, int x1, int y1, int sag) {
    int span = x1 - x0;
    if (span < 0) {
        span = -span;
    }
    if (span < 1) {
        span = 1;
    }
    for (int i = 0; i <= span; ++i) {
        float s = (float)i / (float)span;
        int x = (int)(x0 + (x1 - x0) * s + 0.5f);
        int y = (int)(y0 + (y1 - y0) * s + (float)sag * 4.0f * s * (1.0f - s) + 0.5f);
        put_plate(x, y, 58, 60, 90);
    }
}

static void paint_plate() {
    std::memset(plate, 0, sizeof plate);
    window_count = 0;
    rng_state = 7;
    for (int y = 0; y < HORIZON; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            int red;
            int green;
            int blue;
            sky_colour((float)y / HORIZON, red, green, blue);
            float dx = (float)(x - 50);
            float dy = (float)(y - 24);
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < 26.0f) {
                float halo = clampf(1.0f - (dist - 8.0f) / 26.0f, 0.0f, 1.0f) * 0.45f;
                red = mixi(red, 168, halo);
                green = mixi(green, 122, halo);
                blue = mixi(blue, 154, halo);
            }
            if (dist < 6.5f) {
                red = 255;
                green = 251;
                blue = 234;
            } else if (dist < 8.0f) {
                red = 255;
                green = 240;
                blue = 196;
            }
            if (field_hash(x, y, 4) > 0.996f && dist > 10.0f) {
                red = 232;
                green = 236;
                blue = 255;
            }
            put_plate(x, y, red, green, blue);
        }
    }
    int x = 0;
    while (x < 130) {
        int width = 3 + (int)(rnd() * 7.0f);
        float wave = 0.55f + 0.45f * sinf((float)x * 0.033f);
        if (wave < 0.25f) {
            wave = 0.25f;
        }
        int height = 4 + (int)(rnd() * 10.0f * wave);
        for (int xx = x; xx < x + width && xx < WORLD_W; ++xx) {
            for (int y = HORIZON - height; y < HORIZON; ++y) {
                float t = (float)(y - (HORIZON - height)) / (float)height;
                put_plate(xx, y, mixi(192, 164, t), mixi(144, 118, t), mixi(204, 192, t));
            }
        }
        x += width;
        if (rnd() < 0.3f) {
            x += 1;
        }
    }
    x = 0;
    while (x < 128) {
        int width = 6 + (int)(rnd() * 9.0f);
        int height = 14 + (int)(rnd() * 26.0f);
        if (x > 78 && x < 92) {
            height = 58;
        }
        int top = HORIZON - height;
        for (int xx = x; xx < x + width && xx < WORLD_W; ++xx) {
            for (int y = top; y < HORIZON; ++y) {
                if (xx == x) {
                    put_plate(xx, y, 30, 33, 64);
                } else {
                    put_plate(xx, y, 12, 14, 30);
                }
            }
        }
        if (height == 58) {
            for (int y = top - 6; y < top; ++y) {
                int inset = (top - y) / 3;
                for (int xx = x + 2 + inset; xx < x + width - 2 - inset; ++xx) {
                    put_plate(xx, y, 12, 14, 30);
                }
            }
            for (int y = top - 16; y < top - 6; ++y) {
                put_plate(x + width / 2, y, 20, 23, 46);
            }
        }
        for (int y = top + 2; y < HORIZON - 2; y += 3) {
            for (int xx = x + 1; xx < x + width - 1; xx += 2) {
                if (window_count < WINDOW_CAP) {
                    windows[window_count].x = (int16_t)xx;
                    windows[window_count].y = (int16_t)y;
                    window_count += 1;
                }
            }
        }
        if (width > 7 && height > 20) {
            int sign_y = top + 3;
            int red = (x / 20) % 2 == 0 ? 255 : 63;
            int green = (x / 20) % 2 == 0 ? 63 : 230;
            int blue = (x / 20) % 2 == 0 ? 164 : 255;
            for (int xx = x + 1; xx < x + width - 1; ++xx) {
                put_plate(xx, sign_y, red, green, blue);
                put_plate(xx, sign_y + 1, red, green, blue);
            }
        }
        x += width + 1;
    }
    for (int bx = 118; bx < WORLD_W; ++bx) {
        int y = deck_y(bx);
        for (int yy = y; yy < y + 3 && yy < WORLD_H; ++yy) {
            if (yy == y) {
                put_plate(bx, yy, 58, 60, 90);
            } else {
                put_plate(bx, yy, 38, 40, 64);
            }
        }
        if (bx % 22 == 0 || bx % 22 == 1) {
            for (int yy = y + 3; yy < HORIZON; ++yy) {
                put_plate(bx, yy, 38, 40, 64);
            }
        }
        put_plate(bx, y - 1, 240, 188, 104);
    }
    int towers_x[2] = {140, 176};
    int tower_top[2];
    for (int i = 0; i < 2; ++i) {
        int tx = towers_x[i];
        int base = deck_y(tx);
        int top = base - (tx > 160 ? 34 : 26);
        tower_top[i] = top;
        for (int y = top; y < HORIZON; ++y) {
            for (int xx = tx - 2; xx <= tx + 1; ++xx) {
                put_plate(xx, y, 38, 40, 64);
            }
        }
    }
    cable(towers_x[0], tower_top[0], towers_x[1], tower_top[1], 8);
    cable(118, deck_y(118), towers_x[0], tower_top[0], 2);
    cable(towers_x[1], tower_top[1], WORLD_W - 1, deck_y(WORLD_W - 1) - 8, 4);
    for (int y = HORIZON; y < WORLD_H; ++y) {
        float depth = (float)(y - HORIZON) / (float)(WORLD_H - HORIZON);
        for (int xx = 0; xx < WORLD_W; ++xx) {
            int red;
            int green;
            int blue;
            sky_colour(0.85f - depth * 0.7f, red, green, blue);
            int src_y = HORIZON - (y - HORIZON) - 1;
            int sr;
            int sg;
            int sb;
            get_plate(xx, src_y, sr, sg, sb);
            float band = field_fbm((float)xx * 0.04f, (float)y * 0.5f, 2, 4);
            float reflect = clampf((band - 0.4f) * 2.2f, 0.0f, 1.0f) * (0.75f - depth * 0.35f);
            red = mixi(red, mixi(sr, 6, 0.35f), reflect);
            green = mixi(green, mixi(sg, 8, 0.35f), reflect);
            blue = mixi(blue, mixi(sb, 16, 0.35f), reflect);
            int moon_x = xx - 50;
            if (moon_x < 0) {
                moon_x = -moon_x;
            }
            if (moon_x < (int)(8 - depth * 4) && field_noise((float)xx * 0.3f, (float)y * 0.8f, 8) > 0.45f) {
                red = mixi(red, 255, 0.7f);
                green = mixi(green, 212, 0.7f);
                blue = mixi(blue, 168, 0.5f);
            }
            put_plate(xx, y, red, green, blue);
        }
    }
    for (int xx = 0; xx < 124; ++xx) {
        put_plate(xx, HORIZON, 6, 7, 15);
    }
    for (int i = 0; i < 9; ++i) {
        cars[i].phase = unit_hash(i, 3);
        cars[i].speed = 0.04f + unit_hash(i, 9) * 0.03f;
        cars[i].dir = i & 1;
    }
}

static void put_view(uint8_t *frame, int x, int y, int red, int green, int blue) {
    int sx = x - cam_x;
    int sy = y - cam_y;
    if (sx < 0 || sy < 0 || sx >= PANEL_WIDTH || sy >= PANEL_HEIGHT) {
        return;
    }
    if (red < 0) {
        red = 0;
    }
    if (green < 0) {
        green = 0;
    }
    if (blue < 0) {
        blue = 0;
    }
    if (red > 255) {
        red = 255;
    }
    if (green > 255) {
        green = 255;
    }
    if (blue > 255) {
        blue = 255;
    }
    write_bgr(frame + (sy * PANEL_WIDTH + sx) * 3, red, green, blue);
}

static void paint_ferry(uint8_t *frame, float seconds) {
    float span = (float)(WORLD_W + 80);
    float along = fmodf(seconds * 12.0f + 40.0f, span);
    int fx = (int)(along - 40.0f);
    int fy = 82;
    for (int k = 1; k < 28; ++k) {
        int spread = k / 7;
        int wake = 255 - k * 6;
        put_view(frame, fx - k, fy + 1 + spread, wake, wake - 40, 140);
    }
    for (int xx = 0; xx < 26; ++xx) {
        for (int yy = 0; yy < 4; ++yy) {
            if (xx > yy && xx < 25 - yy) {
                put_view(frame, fx + xx, fy - yy, 8, 10, 18);
            }
        }
    }
    for (int xx = 4; xx < 22; ++xx) {
        put_view(frame, fx + xx, fy - 4, 232, 232, 242);
        int lamp = (xx & 1) ? 240 : 232;
        put_view(frame, fx + xx, fy - 5, lamp, lamp - 20, 160);
    }
    for (int yy = 6; yy < 10; ++yy) {
        put_view(frame, fx + 12, fy - yy, 255, 58, 58);
    }
}

static void paint_cars(uint8_t *frame, float seconds) {
    for (int i = 0; i < 9; ++i) {
        float u = fmodf(cars[i].phase + seconds * cars[i].speed, 1.0f);
        if (u < 0.0f) {
            u += 1.0f;
        }
        float bx = cars[i].dir ? (float)WORLD_W - u * (WORLD_W - 118) : 118.0f + u * (WORLD_W - 118);
        int x = (int)bx;
        if (x < 119 || x >= WORLD_W) {
            continue;
        }
        int y = deck_y(x) - 1;
        if (cars[i].dir) {
            put_view(frame, x, y, 255, 244, 220);
        } else {
            put_view(frame, x, y + 1, 255, 58, 58);
        }
    }
}

static void clamp_camera() {
    if (cam_x < 0) {
        cam_x = 0;
    }
    if (cam_y < 0) {
        cam_y = 0;
    }
    if (cam_x > WORLD_W - PANEL_WIDTH) {
        cam_x = WORLD_W - PANEL_WIDTH;
    }
    if (cam_y > WORLD_H - PANEL_HEIGHT) {
        cam_y = WORLD_H - PANEL_HEIGHT;
    }
}

}  // namespace

void skyline_start() {
    cam_x = 16;
    cam_y = 20;
    paint_plate();
}

void skyline_move(int dx, int dy) {
    cam_x += dx;
    cam_y += dy;
    clamp_camera();
}

void skyline_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float seconds = anim_ms * 0.001f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        const uint8_t *src = plate + ((cam_y + y) * WORLD_W + cam_x) * 3;
        std::memcpy(frame + y * PANEL_WIDTH * 3, src, PANEL_WIDTH * 3);
    }
    for (int i = 0; i < window_count; ++i) {
        int period = 4 + (i % 7);
        int tick = (int)(seconds / (float)period + unit_hash(i, 1) * 9.0f);
        bool on = unit_hash(i, tick) < 0.42f;
        if (on) {
            int warm = i % 3;
            int red = warm == 0 ? 240 : (warm == 1 ? 224 : 200);
            int green = warm == 0 ? 188 : (warm == 1 ? 154 : 126);
            put_view(frame, windows[i].x, windows[i].y, red, green, 70);
        } else {
            put_view(frame, windows[i].x, windows[i].y, 20, 23, 46);
        }
    }
    for (int r = 0; r < 10; ++r) {
        int y = HORIZON + 4 + (int)(unit_hash(r, 5) * 24.0f);
        int len = 6 + (int)(unit_hash(r, 6) * 12.0f);
        float drift = unit_hash(r, 7) * WORLD_W - seconds * (3.0f + unit_hash(r, 8) * 4.0f);
        int x0 = (int)fmodf(drift, (float)WORLD_W);
        if (x0 < 0) {
            x0 += WORLD_W;
        }
        for (int k = 0; k < len; ++k) {
            put_view(frame, x0 + k, y, 8, 10, 28);
        }
    }
    paint_ferry(frame, seconds);
    paint_cars(frame, seconds);
}
