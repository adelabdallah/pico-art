// A tilted spiral galaxy turning in a dark field, warm at the core and blue along two arms.
// The layout and the motion follow Spiral Galaxy at
// https://www.ascii-magic.com/ascii/spiral-galaxy
// The encoder speeds the disk. The background stars keep their own pace.
// Tipping the panel tilts the disk. Flat and face up keeps the resting oval, and a steeper tip draws it thinner.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr float TAU = 6.2831853f;
constexpr float BASE_SPIN = 0.12f;
constexpr float RATE_FLOOR = 0.25f;
constexpr float RATE_CAP = 8.0f;
constexpr float CX = (PANEL_WIDTH - 1) * 0.5f;
constexpr float CY = (PANEL_HEIGHT - 1) * 0.5f;

static float spin_rate = 1.0f;
static float spin_angle = 0.0f;
static float tilt_angle = 0.32f;
static float tilt_squash = 0.55f;
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

}  // namespace

void galaxy_start() {
    spin_rate = 1.0f;
    spin_angle = 0.0f;
    tilt_angle = 0.32f;
    tilt_squash = 0.55f;
    rng_state = 17;
    for (int i = 0; i < 80; ++i) {
        star_x[i] = (int)(rnd() * PANEL_WIDTH);
        star_y[i] = (int)(rnd() * PANEL_HEIGHT);
        star_phase[i] = rnd() * TAU;
        star_bright[i] = rnd();
    }
    for (int i = 0; i < 100; ++i) {
        float rad = 3.0f + rnd() * 26.0f;
        disk_rad[i] = rad;
        disk_arm[i] = (rnd() < 0.5f ? 0.0f : 3.14159265f) + logf(rad) / 0.36f + (rnd() - 0.5f) * 0.7f;
        disk_phase[i] = rnd() * TAU;
    }
}

void galaxy_spin(int steps) {
    spin_rate += (float)steps * 0.25f;
    if (spin_rate < RATE_FLOOR) {
        spin_rate = RATE_FLOOR;
    }
    if (spin_rate > RATE_CAP) {
        spin_rate = RATE_CAP;
    }
}

void galaxy_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
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
    float tilt_c = cosf(tilt_angle);
    float tilt_s = sinf(tilt_angle);
    float spin_c = cosf(-spin_angle);
    float spin_s = sinf(-spin_angle);
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = (float)x + 0.5f - CX;
            float dy = (float)y + 0.5f - CY;
            float u = dx * tilt_c + dy * tilt_s;
            float w = (-dx * tilt_s + dy * tilt_c) / tilt_squash;
            float rad = sqrtf(u * u + w * w) + 0.001f;
            float gu = u * spin_c - w * spin_s;
            float gw = u * spin_s + w * spin_c;
            float theta = atan2f(gw, gu);
            float arm_phase = 2.0f * (theta - logf(rad) / 0.36f);
            float arm = powf(0.5f + 0.5f * cosf(arm_phase), 2.2f);
            float lane = powf(0.5f + 0.5f * cosf(arm_phase + 0.9f), 6.0f);
            float clump = field_fbm(gu * 0.22f + 40.0f, gw * 0.22f, 2, 5);
            float bulge = expf(-rad / 5.0f) * 1.45f;
            float disk = expf(-rad / 16.0f) * (0.2f + 1.3f * arm * (0.45f + clump)) * (1.0f - 0.5f * lane * clampf(rad / 8.0f, 0.0f, 1.0f));
            float intensity = clampf(bulge + disk, 0.0f, 1.0f);
            int red;
            int green;
            int blue;
            if (intensity < 0.08f) {
                float haze = field_fbm(x * 0.06f, y * 0.08f, 2, 2);
                int level = (int)(clampf(haze * 0.7f, 0.0f, 1.0f) * 28);
                red = level / 2;
                green = level / 2 + 4;
                blue = 12 + level;
            } else {
                float warm = clampf(bulge / (bulge + disk + 0.001f) * 1.4f - 0.15f, 0.0f, 1.0f);
                if (warm > 0.5f) {
                    red = (int)(50 + intensity * 205);
                    green = (int)(30 + intensity * 180);
                    blue = (int)(16 + intensity * 120);
                } else if (arm > 0.6f && clump > 0.62f && rad > 8.0f) {
                    red = (int)(140 + (clump - 0.62f) * 300);
                    green = (int)(40 + (clump - 0.62f) * 120);
                    blue = (int)(90 + (clump - 0.62f) * 160);
                } else {
                    red = (int)(12 + intensity * 120 + warm * 80);
                    green = (int)(24 + intensity * 140);
                    blue = (int)(50 + intensity * 190);
                }
            }
            put(frame, x, y, red, green, blue);
        }
    }
    for (int i = 0; i < 80; ++i) {
        uint8_t *pixel = frame + (star_y[i] * PANEL_WIDTH + star_x[i]) * 3;
        if (pixel[2] > 40) {
            continue;
        }
        float twinkle = 0.5f + 0.5f * sinf(seconds * (1.0f + star_bright[i] * 2.0f) + star_phase[i]);
        if (twinkle > 0.35f) {
            int level = star_bright[i] > 0.75f ? 240 : 170;
            put(frame, star_x[i], star_y[i], level, level, 255);
        }
    }
    for (int i = 0; i < 100; ++i) {
        float angle = disk_arm[i] + spin_angle;
        float u = cosf(angle) * disk_rad[i];
        float w = sinf(angle) * disk_rad[i];
        float flat = w * tilt_squash;
        int x = (int)(CX + u * tilt_c - flat * tilt_s + 0.5f);
        int y = (int)(CY + u * tilt_s + flat * tilt_c + 0.5f);
        if (sinf(seconds * 2.0f + disk_phase[i]) > -0.2f) {
            put(frame, x, y, 230, 236, 255);
        }
    }
}
