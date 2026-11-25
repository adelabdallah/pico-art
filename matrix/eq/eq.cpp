// Three music pictures. Selecting the pattern opens the radial bars.
// The radial bars follow Radial Sound Bars at
// https://www.ascii-magic.com/ascii/radial-sound-bars
// The encoder swaps those bars with the boombox at
// https://www.ascii-magic.com/ascii/boombox-speaker-pump
// Orange on the boombox opens the graphic equalizer at
// https://www.ascii-magic.com/ascii/graphic-eq-peak-hold
// Orange there returns to the boombox. The encoder waits on the equalizer.
// The boombox and the rack use shiny greys. Their bars run from cyan at
// the bottom to orange at the top, and the peak marks follow the bars.
// The radial picture keeps its rainbow. All three follow the microphone.

#include "pattern.hpp"

#include <cmath>

namespace {

constexpr float TAU = 6.2831853f;
constexpr int RADIAL = 0;
constexpr int BOOM = 1;
constexpr int ZOOM = 2;
constexpr int BAR_CAP = 26;

static int screen = RADIAL;
static float peaks[BAR_CAP];
static float seconds = 0.0f;

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, clamp_channel((float)red), clamp_channel((float)green), clamp_channel((float)blue));
}

static float band_at(float t) {
    float bands[8];
    environment_bands(bands, 8);
    t = clampf(t, 0.0f, 0.999f);
    float x = t * 7.0f;
    int index = (int)x;
    float frac = x - (float)index;
    int next = index + 1;
    if (next > 7) {
        next = 7;
    }
    return bands[index] * (1.0f - frac) + bands[next] * frac;
}

static void bar_colour(float t, int &red, int &green, int &blue) {
    t = clampf(t, 0.0f, 1.0f);
    red = (int)(t * 255.0f);
    green = (int)(220.0f + t * (130.0f - 220.0f));
    blue = (int)(255.0f + t * (20.0f - 255.0f));
}

static float shine_at(float x, float y) {
    float sweep = fmodf(x * 0.08f + y * 0.05f - seconds * 12.0f, 28.0f);
    if (sweep < 0.0f) {
        sweep += 28.0f;
    }
    float band = fabsf(sweep - 6.0f);
    float shine = 1.0f - band / 5.0f;
    if (shine < 0.0f) {
        return 0.0f;
    }
    return shine * shine;
}

static void chrome(int x, int y, float level, int &red, int &green, int &blue) {
    float shine = shine_at((float)x, (float)y);
    float v = clampf(level + shine * 0.55f, 0.0f, 1.0f);
    red = (int)(40 + v * 190);
    green = (int)(46 + v * 188);
    blue = (int)(58 + v * 185);
}

static void rainbow(int index, float level, int &red, int &green, int &blue) {
    const uint8_t colours[14][3] = {
        {255, 58, 106}, {255, 106, 58}, {255, 168, 58}, {255, 224, 58},
        {184, 255, 58}, {90, 255, 106}, {58, 255, 176}, {58, 232, 255},
        {58, 168, 255}, {90, 106, 255}, {154, 90, 255}, {216, 90, 255},
        {255, 90, 216}, {255, 58, 154},
    };
    int slot = index % 14;
    if (slot < 0) {
        slot += 14;
    }
    float dim = 0.35f + 0.65f * clampf(level, 0.0f, 1.0f);
    red = (int)(colours[slot][0] * dim);
    green = (int)(colours[slot][1] * dim);
    blue = (int)(colours[slot][2] * dim);
}

static void paint_radial(uint8_t *frame) {
    float cx = panel_cx();
    float cy = panel_cy();
    float reach = (float)PANEL_WIDTH;
    if ((float)PANEL_HEIGHT < reach) {
        reach = (float)PANEL_HEIGHT;
    }
    float bass = band_at(0.05f);
    float radius = reach * (0.16f + bass * 0.03f);
    float length = reach * 0.30f;
    constexpr int bars = 48;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dx, -dy);
            if (angle < 0.0f) {
                angle += TAU;
            }
            float along = angle / TAU * (float)bars;
            int bar = (int)along % bars;
            float frac = along - floorf(along);
            float mirror = (float)bar / (float)bars;
            if (mirror > 0.5f) {
                mirror = 1.0f - mirror;
            }
            float level = 0.08f + 0.92f * band_at(mirror * 2.0f);
            int hue = (int)(angle / TAU * 14.0f);
            if (dist < radius - 1.5f) {
                float spin = atan2f(dy, dx) - seconds * 3.2f;
                int red = 28;
                int green = 28;
                int blue = 36;
                if (dist < reach * 0.045f) {
                    red = cosf(spin) > 0.4f ? 244 : 255;
                    green = cosf(spin) > 0.4f ? 236 : 90;
                    blue = cosf(spin) > 0.4f ? 220 : 122;
                } else if (sinf(dist * 0.55f) > 0.2f) {
                    red = 70;
                    green = 72;
                    blue = 84;
                }
                float shine = shine_at((float)x, (float)y) * 0.35f;
                put(frame, x, y, red + (int)(shine * 80), green + (int)(shine * 80), blue + (int)(shine * 80));
            } else if (dist < radius) {
                put(frame, x, y, 200, 204, 214);
            } else if (dist - radius < level * length && frac < 0.72f) {
                float u = (dist - radius) / length;
                int red;
                int green;
                int blue;
                rainbow(hue, 1.0f - u * 0.35f, red, green, blue);
                put(frame, x, y, red, green, blue);
            } else {
                float v = clampf(0.22f - dist / (reach * 0.9f) + bass * 0.08f, 0.0f, 1.0f);
                put(frame, x, y, (int)(8 + v * 30), (int)(6 + v * 18), (int)(18 + v * 40));
            }
        }
    }
}

struct Cam {
    float ox;
    float oy;
    float scale;
};

static Cam boom_cam() {
    Cam cam;
    const float scene_w = 104.0f;
    const float scene_h = 68.0f;
    if (PANEL_WIDTH > PANEL_HEIGHT) {
        cam.scale = (float)PANEL_WIDTH / scene_w;
        float view_h = (float)PANEL_HEIGHT / cam.scale;
        cam.ox = 0.0f;
        cam.oy = (scene_h - view_h) * 0.5f;
        if (cam.oy < 0.0f) {
            cam.oy = 0.0f;
        }
        return cam;
    }
    const float view = 80.0f;
    cam.scale = (float)PANEL_WIDTH / view;
    cam.ox = 52.0f - view * 0.5f;
    cam.oy = 36.0f - view * 0.5f;
    return cam;
}

static void visual_at(const Cam &cam, int x, int y, float &vx, float &vy) {
    vx = cam.ox + ((float)x + 0.5f) / cam.scale;
    vy = cam.oy + ((float)y + 0.5f) / cam.scale;
}

static void paint_boom(uint8_t *frame) {
    Cam cam = boom_cam();
    float bass = band_at(0.0f);
    float pump = 1.0f + bass * 0.14f;
    const float speakers[2][2] = {{25.0f, 40.0f}, {79.0f, 40.0f}};
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float vx;
            float vy;
            visual_at(cam, x, y, vx, vy);
            float col = vx;
            float row = vy * 0.5f;
            int red = 42;
            int green = 18;
            int blue = 14;
            int brick_row = (int)floorf(row / 2.0f);
            int offset = (brick_row & 1) ? 4 : 0;
            bool mortar = ((int)row & 1) != 0 || ((int)(col + (float)offset) % 8) == 0;
            if (!mortar) {
                red = 70 + ((int)col * 3 + brick_row) % 28;
                green = 28;
                blue = 18;
            }
            bool body = col >= 8.0f && col <= 96.0f && row >= 8.0f && row <= 30.0f;
            if (body) {
                float level = clampf(0.72f - (row - 8.0f) * 0.018f, 0.15f, 0.9f);
                chrome(x, y, level, red, green, blue);
            }
            if (row >= 3.0f && row <= 4.5f && col >= 20.0f && col <= 84.0f) {
                chrome(x, y, 0.8f, red, green, blue);
            }
            for (int s = 0; s < 2; ++s) {
                float dx = (col - speakers[s][0]) / 12.5f;
                float dy = (row - 20.0f) / (6.6f * pump);
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > 1.15f) {
                    continue;
                }
                if (dist > 1.0f) {
                    red = 90;
                    green = 96;
                    blue = 108;
                    continue;
                }
                float cone = dist / pump;
                float v = clampf(0.2f + (1.0f - cone) * 0.45f + bass * 0.25f, 0.0f, 1.0f);
                red = (int)(16 + v * 90);
                green = (int)(16 + v * 90);
                blue = (int)(24 + v * 110);
                float shine = shine_at((float)x, (float)y);
                red += (int)(shine * 70);
                green += (int)(shine * 70);
                blue += (int)(shine * 80);
            }
            if (col >= 43.0f && col <= 62.0f && row >= 12.0f && row <= 17.0f) {
                int bar = (int)((col - 43.0f) / 2.0f);
                if (bar < 0) {
                    bar = 0;
                }
                if (bar > 7) {
                    bar = 7;
                }
                float level = 0.08f + 0.92f * band_at((float)bar / 7.0f);
                float u = (17.0f - row) / 5.0f;
                if (u < level) {
                    bar_colour(u / level, red, green, blue);
                } else {
                    red = 16;
                    green = 16;
                    blue = 22;
                }
            }
            put(frame, x, y, red, green, blue);
        }
    }
}

static void paint_eq(uint8_t *frame, int dt_ms) {
    int bars = PANEL_WIDTH >= 100 ? 26 : 16;
    if (bars > BAR_CAP) {
        bars = BAR_CAP;
    }
    int left = PANEL_WIDTH / 12;
    int right = PANEL_WIDTH - left;
    int top = PANEL_HEIGHT / 8;
    int bottom = PANEL_HEIGHT - PANEL_HEIGHT / 6;
    if (bottom <= top + 4) {
        bottom = top + 4;
    }
    float span = (float)(right - left);
    for (int b = 0; b < bars; ++b) {
        float level = 0.05f + 0.95f * band_at((float)b / (float)(bars - 1));
        if (level > peaks[b]) {
            peaks[b] = level;
        } else {
            peaks[b] -= (float)dt_ms * 0.00045f;
            if (peaks[b] < level) {
                peaks[b] = level;
            }
        }
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float level = 0.25f + shine_at((float)x, (float)y) * 0.2f;
            int red;
            int green;
            int blue;
            chrome(x, y, level * 0.35f, red, green, blue);
            bool rack = x > 1 && x < PANEL_WIDTH - 2 && y > 1 && y < PANEL_HEIGHT - 2;
            if (rack) {
                red = (int)(red * 0.35f);
                green = (int)(green * 0.35f);
                blue = (int)(blue * 0.4f);
            }
            if (y == top - 2 || y == bottom + 2) {
                red = 90;
                green = 96;
                blue = 110;
            }
            put(frame, x, y, red, green, blue);
        }
    }
    for (int b = 0; b < bars; ++b) {
        float x0 = (float)left + span * ((float)b + 0.15f) / (float)bars;
        float x1 = (float)left + span * ((float)b + 0.85f) / (float)bars;
        int height = bottom - top;
        float level = 0.05f + 0.95f * band_at((float)b / (float)(bars - 1));
        int live = (int)(level * (float)height);
        int mark = (int)(peaks[b] * (float)height);
        for (int s = 0; s < height; ++s) {
            int y = bottom - 1 - s;
            bool on = s < live;
            bool peak = s == mark - 1 && mark > live;
            int red = 18;
            int green = 20;
            int blue = 28;
            if (peak) {
                red = 245;
                green = 245;
                blue = 250;
            } else if (on) {
                bar_colour((float)s / (float)height, red, green, blue);
            }
            for (int x = (int)x0; x < (int)x1; ++x) {
                put(frame, x, y, red, green, blue);
            }
        }
    }
}

}  // namespace

bool eq_radial() {
    return screen == RADIAL;
}

void eq_zoom() {
    if (screen == BOOM) {
        screen = ZOOM;
    } else if (screen == ZOOM) {
        screen = BOOM;
    }
}

void eq_turn(int steps) {
    if (screen == ZOOM || steps == 0) {
        return;
    }
    int flips = steps < 0 ? -steps : steps;
    if ((flips & 1) != 0) {
        screen = screen == RADIAL ? BOOM : RADIAL;
    }
}

void eq_home() {
    screen = RADIAL;
    for (int i = 0; i < BAR_CAP; ++i) {
        peaks[i] = 0.0f;
    }
}

void eq_start() {
    eq_home();
    seconds = 0.0f;
    environment_arm_listen();
}

void eq_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    environment_hear(dt_ms);
    seconds += dt_ms * 0.001f;
    if (screen == BOOM) {
        paint_boom(frame);
    } else if (screen == ZOOM) {
        paint_eq(frame, dt_ms);
    } else {
        paint_radial(frame);
    }
}
