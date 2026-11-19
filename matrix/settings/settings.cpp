// Settings is the last pattern, so older saved pattern numbers still land on the same picture.
// Three gears sit in the middle of the panel, about seventy percent of the square.
// Each gear is drawn here from a ring and teeth. Four tones, then one hue walk every five seconds.

#include "pattern.hpp"

#include <cmath>

namespace {

constexpr int GEAR_HUE_MS = 5000;

struct Gear {
    float x;
    float y;
    float radius;
    int teeth;
    float spin;
};

void clear_frame(uint8_t *frame) {
    for (int i = 0; i < PANEL_PIXELS * 3; ++i) {
        frame[i] = 0;
    }
}

void gear_colour(int tone, float turns, int &red, int &green, int &blue) {
    // Dark edge, hub, body, tooth highlight.
    static const int tones[4][3] = {
        {18, 36, 64},
        {46, 88, 140},
        {96, 168, 214},
        {220, 238, 255},
    };
    red = tones[tone][0];
    green = tones[tone][1];
    blue = tones[tone][2];
    hue_shift(red, green, blue, turns);
}

void stamp_gear(uint8_t *frame, const Gear &gear, float turns) {
    int x0 = (int)(gear.x - gear.radius - 2.0f);
    int y0 = (int)(gear.y - gear.radius - 2.0f);
    int x1 = (int)(gear.x + gear.radius + 2.0f);
    int y1 = (int)(gear.y + gear.radius + 2.0f);
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
    float hole = gear.radius * 0.22f;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = x - gear.x;
            float dy = y - gear.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < hole) {
                continue;
            }
            float ang = atan2f(dy, dx);
            float tooth = 0.84f + 0.16f * (0.5f + 0.5f * cosf(gear.teeth * ang + gear.spin));
            float outer = gear.radius * tooth;
            if (dist > outer) {
                continue;
            }
            float light = 0.5f + 0.5f * cosf(ang - gear.spin);
            int tone = 1;
            if (dist > outer - 1.5f) {
                tone = 0;
            } else if (dist > gear.radius * 0.72f) {
                tone = light > 0.55f ? 3 : 2;
            } else if (dist < gear.radius * 0.46f) {
                tone = 1;
            } else {
                tone = light > 0.62f ? 2 : 1;
            }
            int red = 0;
            int green = 0;
            int blue = 0;
            gear_colour(tone, turns, red, green, blue);
            write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
        }
    }
}

}  // namespace

void settings_start() {}

void settings_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    clear_frame(frame);
    float seconds = anim_ms * 0.001f;
    float turns = (anim_ms % GEAR_HUE_MS) / (float)GEAR_HUE_MS;
    float bob = sinf(seconds * 2.1f) * 2.0f;
    Gear gears[3] = {
        {32.0f, 27.0f + bob, 13.0f, 8, seconds * 0.8f},
        {20.0f, 42.0f - bob, 11.0f, 6, -seconds * 1.05f},
        {44.0f, 42.0f - bob * 0.7f, 11.0f, 6, -seconds * 0.95f},
    };
    for (int i = 0; i < 3; ++i) {
        stamp_gear(frame, gears[i], turns);
    }
}
