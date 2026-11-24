// A tilted spiral galaxy turning in a dark field, warm at the core and blue along two arms.
// The layout and the motion follow Spiral Galaxy at
// https://www.ascii-magic.com/ascii/spiral-galaxy
// The solar system sits on the outer tip of one arm and turns with the disk.
// The encoder speeds the disk. The background stars keep their own pace.
// Tipping the panel tilts the disk. Flat and face up keeps the resting oval, and a steeper tip draws it thinner.
// Orange flies in from that tip to the orrery.
// Outside the disk the sky is one night colour. The arms are a smooth spiral.

#include "pattern.hpp"

#include <cmath>

namespace {

constexpr float TAU = 6.2831853f;
constexpr float BASE_SPIN = 0.12f;
constexpr float RATE_FLOOR = 0.25f;
constexpr float RATE_CAP = 8.0f;
// Outer point of one arm. The bright crest is where the angle matches the log spiral.
constexpr float HOME_RAD = 26.0f;
constexpr float ARM_K = 0.36f;

static float spin_rate = 1.0f;
static float spin_angle = 0.0f;
static float tilt_angle = 0.32f;
static float tilt_squash = 0.55f;
static bool ready = false;
static int star_x[80];
static int star_y[80];
static float star_phase[80];
static float star_bright[80];
static float disk_rad[100];
static float disk_arm[100];
static float disk_phase[100];
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static void put(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

static void build_field() {
    rng_state = 17;
    for (int i = 0; i < 80; ++i) {
        star_x[i] = (int)(rnd() * PANEL_WIDTH);
        star_y[i] = (int)(rnd() * PANEL_HEIGHT);
        star_phase[i] = rnd() * TAU;
        star_bright[i] = rnd();
    }
    for (int i = 0; i < 100; ++i) {
        float rad = 3.0f + rnd() * 26.0f;
        disk_rad[i] = panel_s(rad);
        disk_arm[i] = (rnd() < 0.5f ? 0.0f : 3.14159265f) + logf(rad) / ARM_K + (rnd() - 0.5f) * 0.7f;
        disk_phase[i] = rnd() * TAU;
    }
}

// Tilt, spin, and the disk scale are constant for a frame. The spiral fades out by radius 48.
struct DiskBasis {
    float tilt_c;
    float tilt_s;
    float spin_c;
    float spin_s;
    float cx;
    float cy;
    float unit;
    float inv_squash;
};

static void disk_basis(DiskBasis &basis) {
    basis.tilt_c = cosf(tilt_angle);
    basis.tilt_s = sinf(tilt_angle);
    basis.spin_c = cosf(-spin_angle);
    basis.spin_s = sinf(-spin_angle);
    basis.cx = panel_cx();
    basis.cy = panel_cy();
    basis.unit = panel_s(1.0f);
    if (basis.unit < 0.001f) {
        basis.unit = 0.001f;
    }
    float squash = tilt_squash;
    if (squash < 0.06f) {
        squash = 0.06f;
    }
    basis.inv_squash = 1.0f / squash;
}

static void night(int &red, int &green, int &blue) {
    red = 5;
    green = 9;
    blue = 22;
}

static bool shade_disk(const DiskBasis &basis, float px, float py, int &red, int &green, int &blue) {
    float dx = px - basis.cx;
    float dy = py - basis.cy;
    float u = dx * basis.tilt_c + dy * basis.tilt_s;
    float w = (-dx * basis.tilt_s + dy * basis.tilt_c) * basis.inv_squash;
    float rad = sqrtf(u * u + w * w) / basis.unit + 0.001f;
    if (rad > 48.0f) {
        return false;
    }
    float gu = u * basis.spin_c - w * basis.spin_s;
    float gw = u * basis.spin_s + w * basis.spin_c;
    float theta = atan2f(gw, gu);
    float arm_phase = 2.0f * (theta - logf(rad) / ARM_K);
    float arm_w = 0.5f + 0.5f * cosf(arm_phase);
    float arm = arm_w * arm_w;
    float lane_w = 0.5f + 0.5f * cosf(arm_phase + 0.9f);
    float lane2 = lane_w * lane_w;
    float lane = lane2 * lane2 * lane2;
    float bulge = expf(-rad / 5.0f) * 1.45f;
    float disk = expf(-rad / 16.0f) * (0.2f + 1.24f * arm) * (1.0f - 0.5f * lane * clampf(rad / 8.0f, 0.0f, 1.0f));
    float intensity = clampf(bulge + disk, 0.0f, 1.0f);
    if (intensity < 0.08f) {
        return false;
    }
    float warm = clampf(bulge / (bulge + disk + 0.001f) * 1.4f - 0.15f, 0.0f, 1.0f);
    if (warm > 0.5f) {
        red = (int)(50 + intensity * 205);
        green = (int)(30 + intensity * 180);
        blue = (int)(16 + intensity * 120);
    } else {
        red = (int)(12 + intensity * 120 + warm * 80);
        green = (int)(24 + intensity * 140);
        blue = (int)(50 + intensity * 190);
    }
    return true;
}

static void home_screen(float &sx, float &sy) {
    float home = panel_s(HOME_RAD);
    float theta = logf(HOME_RAD) / ARM_K;
    float gu = cosf(theta) * home;
    float gw = sinf(theta) * home;
    float u = gu * cosf(spin_angle) - gw * sinf(spin_angle);
    float w = gu * sinf(spin_angle) + gw * cosf(spin_angle);
    float flat = w * tilt_squash;
    float tilt_c = cosf(tilt_angle);
    float tilt_s = sinf(tilt_angle);
    sx = panel_cx() + u * tilt_c - flat * tilt_s;
    sy = panel_cy() + u * tilt_s + flat * tilt_c;
}

static void draw_system(uint8_t *frame, float aim_x, float aim_y) {
    int cx = (int)floorf(aim_x);
    int cy = (int)floorf(aim_y);
    put(frame, cx, cy, 255, 228, 140);
    put(frame, cx + 1, cy, 255, 186, 80);
    put(frame, cx, cy - 1, 255, 244, 210);
    put(frame, cx - 1, cy + 1, 80, 150, 255);
}

}  // namespace

void galaxy_start() {
    spin_rate = 1.0f;
    spin_angle = 0.0f;
    tilt_angle = 0.32f;
    tilt_squash = 0.55f;
    build_field();
    ready = true;
}

void galaxy_ensure() {
    if (!ready) {
        galaxy_start();
    }
}

void galaxy_arm_tip(float &x, float &y) {
    galaxy_ensure();
    home_screen(x, y);
}

void galaxy_step(int dt_ms) {
    galaxy_ensure();
    spin_angle += BASE_SPIN * spin_rate * (float)dt_ms * 0.001f;
    spin_angle = fmodf(spin_angle, TAU);
    if (spin_angle < 0.0f) {
        spin_angle += TAU;
    }
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    float goal_angle = 0.32f;
    float goal_squash = 0.55f;
    if (environment_gravity(gx, gy, gz)) {
        float horiz = sqrtf(gx * gx + gy * gy);
        float tip = atan2f(horiz, fabsf(gz)) * environment_imu_gain();
        if (tip > 1.25f) {
            tip = 1.25f;
        }
        float amount = tip / 1.25f;
        if (horiz > 0.12f) {
            float heading = atan2f(gy, gx) - 1.5707963f;
            float follow = clampf((amount - 0.04f) / 0.18f, 0.0f, 1.0f);
            float gap = heading - 0.32f;
            gap = gap - TAU * floorf(gap / TAU + 0.5f);
            goal_angle = 0.32f + gap * follow;
            goal_squash = 0.55f + (0.06f - 0.55f) * amount;
        }
    }
    float blend = 0.0f;
    if (dt_ms > 0) {
        blend = 1.0f - expf(-dt_ms * 0.008f);
    }
    float turn = goal_angle - tilt_angle;
    turn = turn - TAU * floorf(turn / TAU + 0.5f);
    tilt_angle += turn * blend;
    tilt_squash += (goal_squash - tilt_squash) * blend;
    if (tilt_squash < 0.06f) {
        tilt_squash = 0.06f;
    }
}

void galaxy_spin(int steps) {
    galaxy_ensure();
    spin_rate += (float)steps * 0.25f;
    if (spin_rate < RATE_FLOOR) {
        spin_rate = RATE_FLOOR;
    }
    if (spin_rate > RATE_CAP) {
        spin_rate = RATE_CAP;
    }
}

void galaxy_paint_at(uint8_t *frame, uint32_t anim_ms, float scale, float tip_x, float tip_y, float aim_x, float aim_y) {
    galaxy_ensure();
    float seconds = anim_ms * 0.001f;
    if (scale < 0.001f) {
        scale = 0.001f;
    }
    float inv = 1.0f / scale;
    DiskBasis basis;
    disk_basis(basis);
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float px = tip_x + ((x + 0.5f) - aim_x) * inv;
            float py = tip_y + ((y + 0.5f) - aim_y) * inv;
            int red;
            int green;
            int blue;
            if (!shade_disk(basis, px, py, red, green, blue)) {
                night(red, green, blue);
            }
            put(frame, x, y, red, green, blue);
        }
    }
    for (int i = 0; i < 80; ++i) {
        float sx = aim_x + ((star_x[i] + 0.5f) - tip_x) * scale;
        float sy = aim_y + ((star_y[i] + 0.5f) - tip_y) * scale;
        int ix = (int)floorf(sx);
        int iy = (int)floorf(sy);
        if (ix < 0 || iy < 0 || ix >= PANEL_WIDTH || iy >= PANEL_HEIGHT) {
            continue;
        }
        uint8_t *pixel = frame + (iy * PANEL_WIDTH + ix) * 3;
        if (pixel[2] > 40) {
            continue;
        }
        float twinkle = 0.5f + 0.5f * sinf(seconds * (1.0f + star_bright[i] * 2.0f) + star_phase[i]);
        if (twinkle > 0.35f) {
            int level = star_bright[i] > 0.75f ? 240 : 170;
            put(frame, ix, iy, level, level, 255);
        }
    }
    float tilt_c = cosf(tilt_angle);
    float tilt_s = sinf(tilt_angle);
    for (int i = 0; i < 100; ++i) {
        float angle = disk_arm[i] + spin_angle;
        float u = cosf(angle) * disk_rad[i];
        float w = sinf(angle) * disk_rad[i];
        float flat = w * tilt_squash;
        float base_x = panel_cx() + u * tilt_c - flat * tilt_s;
        float base_y = panel_cy() + u * tilt_s + flat * tilt_c;
        float sx = aim_x + (base_x - tip_x) * scale;
        float sy = aim_y + (base_y - tip_y) * scale;
        if (sinf(seconds * 2.0f + disk_phase[i]) > -0.2f) {
            put(frame, (int)(sx + 0.5f), (int)(sy + 0.5f), 230, 236, 255);
        }
    }
    draw_system(frame, aim_x, aim_y);
}

void galaxy_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    galaxy_step(dt_ms);
    float tip_x;
    float tip_y;
    galaxy_arm_tip(tip_x, tip_y);
    galaxy_paint_at(frame, anim_ms, 1.0f, tip_x, tip_y, tip_x, tip_y);
}
