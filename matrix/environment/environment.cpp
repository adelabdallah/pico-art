// The 64 by 64 panel is a square cup, seen from above.
// Z is the axis out of the screen, so a level cup has no pull in the plane.
// Entering the pattern wakes the accelerometer and stores X and Y.
// The module's X lies along the panel's Y, and both axes are reversed
// from the stored pose.
// A level cup holds a round pool in the middle.
// Tilt pours that pool against the low walls, and turning the cup
// lets the pool slide along the edges.
// The pour is filtered so the shoreline moves smoothly.
// At rest the pool is 40 percent of the cup. Sound spreads it a little,
// then it gathers back to 40 percent.
// The middle of the pool is darker than the rim.
// While listening, loudness tints the pool and pitch places a glint.
// A rise in the sound sends a bright pulse from the shore into the pool.
// The microphone does not change down.
// Mic and tilt sensitivity come from the settings page.
// 50 percent is the response tuned in this file.
// Mic steps are equal ratios of loudness. Tilt steps scale how far a lean counts as poured.
// Both stay in software, so neither setting drives the sensor harder.
//
// static below means the helper exists only in this file. The environment_* functions at the bottom are the public ones.

#include "pattern.hpp"

#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <cmath>
#include <cstdint>

constexpr int REST_COUNT = (PANEL_PIXELS * 40) / 100;
constexpr int SPLASH_MAX = 820;
constexpr float PI = 3.14159265f;
constexpr int PIN_MIC = 27;
constexpr int MIC_SAMPLES = 128;
constexpr int BANDS = 8;
constexpr uint8_t MPU_ADDR = 0x68;
constexpr uint8_t REG_POWER = 0x6B;
constexpr uint8_t REG_ACCEL_CFG = 0x1C;
constexpr uint8_t REG_ACCEL = 0x3B;
constexpr float ACCEL_LSB = 16384.0f;
constexpr int ZERO_SAMPLES = 8;
// Per second. Slow on purpose, so the shoreline crawls along the walls.
constexpr float TILT_SMOOTH = 1.5f;
constexpr int SAMPLE_GAP_MS = 40;
// Full scale sits well above ordinary speech, so the meter has room.
constexpr float METER_RMS = 220.0f;
// Loudness above the room noise that fully tints the pool.
constexpr float COLOUR_RMS = 110.0f;
constexpr float ONSET_RMS = 80.0f;
constexpr int ONSET_GAP_MS = 200;
constexpr int RIPPLES = 3;

static float zero_x = 0.0f;
static float zero_y = 0.0f;
static uint32_t clock_ms = 0;
static float flow_x = 0.0f;
static float flow_y = 0.0f;
static float sensed_x = 0.0f;
static float sensed_y = 0.0f;
static float shore_x = 0.0f;
static float shore_y = 1.0f;
static float splash = 0.0f;
static float splash_goal = 0.0f;
static float glint_x = 32.0f;
static float glint = 0.0f;
static float audio_tint = 0.0f;
static float heard = 0.0f;
static float noise_rms = 40.0f;
static float meter_level = 0.0f;
static float band_level[BANDS] = {};
static int mic_percent = 50;
static int imu_percent = 50;
static bool meter_peak = false;
static bool listening = false;
static bool mpu_ready = false;
static uint32_t last_sample_ms = 0;
static uint32_t last_onset_ms = 0;

struct Ripple {
    float depth;
    float amp;
};

static Ripple ripples[RIPPLES] = {};

static bool mpu_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_write_timeout_us(i2c1, MPU_ADDR, buf, 2, false, 5000) == 2;
}

static bool mpu_read(uint8_t reg, uint8_t *dst, size_t len) {
    if (i2c_write_timeout_us(i2c1, MPU_ADDR, &reg, 1, true, 5000) != 1) {
        return false;
    }
    return i2c_read_timeout_us(i2c1, MPU_ADDR, dst, len, false, 5000) == (int)len;
}

// The module's X and Y point the other way from the panel.
static float axis_g(int sample, float zero) {
    return (zero - (float)sample) / ACCEL_LSB;
}

static bool read_tilt(float &tilt_x, float &tilt_y) {
    // Six accel bytes follow the register. Z is the last pair, perpendicular
    // to the panel, and it does not move the pool.
    uint8_t raw[4];
    if (!mpu_read(REG_ACCEL, raw, 4)) {
        return false;
    }
    int ax = (int16_t)((raw[0] << 8) | raw[1]);
    int ay = (int16_t)((raw[2] << 8) | raw[3]);
    // The module sits with its X along the panel's Y.
    tilt_x = axis_g(ay, zero_y);
    tilt_y = axis_g(ax, zero_x);
    return true;
}

static bool init_mpu() {
    if (!mpu_write(REG_POWER, 0)) {
        return false;
    }
    mpu_write(REG_ACCEL_CFG, 0);
    sleep_ms(20);
    return true;
}

static void capture_zero() {
    int32_t sum_x = 0;
    int32_t sum_y = 0;
    int got = 0;
    for (int i = 0; i < ZERO_SAMPLES; ++i) {
        uint8_t raw[4];
        if (!mpu_read(REG_ACCEL, raw, 4)) {
            continue;
        }
        sum_x += (int16_t)((raw[0] << 8) | raw[1]);
        sum_y += (int16_t)((raw[2] << 8) | raw[3]);
        got += 1;
        sleep_ms(2);
    }
    if (got == 0) {
        zero_x = 0.0f;
        zero_y = 0.0f;
        return;
    }
    zero_x = sum_x / (float)got;
    zero_y = sum_y / (float)got;
}

static void update_gravity() {
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    if (!read_tilt(tilt_x, tilt_y)) {
        return;
    }
    sensed_x = tilt_x;
    sensed_y = tilt_y;
}

// 50 percent leaves the thresholds as written. Each 25 points doubles or halves the loudness that counts.
static float mic_gain() {
    return exp2f((mic_percent - 50) / 25.0f);
}

// 50 percent is one. 100 percent treats half the lean as a full pour. 0 percent ignores tilt.
static float imu_gain() {
    return imu_percent / 50.0f;
}

static void launch_ripple(float amp) {
    int slot = 0;
    for (int i = 1; i < RIPPLES; ++i) {
        if (ripples[i].amp < ripples[slot].amp) {
            slot = i;
        }
    }
    ripples[slot].depth = 0.0f;
    ripples[slot].amp = amp;
}

static void listen_audio(uint32_t now) {
    if ((int32_t)(now - last_sample_ms) < SAMPLE_GAP_MS) {
        return;
    }
    last_sample_ms = now;

    uint16_t samples[MIC_SAMPLES];
    adc_select_input(1);
    uint32_t sum = 0;
    for (int i = 0; i < MIC_SAMPLES; ++i) {
        samples[i] = adc_read();
        sum += samples[i];
    }
    float mean = sum / (float)MIC_SAMPLES;
    float energy = 0.0f;
    float centered[MIC_SAMPLES];
    for (int i = 0; i < MIC_SAMPLES; ++i) {
        centered[i] = samples[i] - mean;
        energy += centered[i] * centered[i];
    }
    float rms = sqrtf(energy / MIC_SAMPLES);

    float bands[BANDS] = {};
    float band_sum = 0.0f;
    float centroid = 0.0f;
    for (int band = 0; band < BANDS; ++band) {
        float step = 2.0f * 3.14159265f * (band + 1) / MIC_SAMPLES;
        float real = 0.0f;
        float imag = 0.0f;
        for (int i = 0; i < MIC_SAMPLES; ++i) {
            float angle = step * i;
            real += centered[i] * cosf(angle);
            imag -= centered[i] * sinf(angle);
        }
        bands[band] = sqrtf(real * real + imag * imag);
        band_sum += bands[band];
        centroid += bands[band] * ((band + 0.5f) / BANDS);
    }
    if (band_sum > 0.0f) {
        centroid /= band_sum;
    } else {
        centroid = 0.5f;
    }
    glint_x = centroid * (PANEL_WIDTH - 1);

    float gain = mic_gain();
    meter_level = clampf(rms * gain / METER_RMS, 0.0f, 1.0f);
    meter_peak = meter_level >= 0.98f;
    float peak_band = 0.0f;
    for (int band = 0; band < BANDS; ++band) {
        if (bands[band] > peak_band) {
            peak_band = bands[band];
        }
    }
    for (int band = 0; band < BANDS; ++band) {
        float shaped = peak_band > 0.0f ? bands[band] / peak_band : 0.0f;
        band_level[band] = clampf(shaped * meter_level, 0.0f, 1.0f);
    }

    float above = rms - noise_rms;
    if (above < 0.0f) {
        above = 0.0f;
    }
    float loud = above * gain;
    bool onset = loud > ONSET_RMS && rms * gain > noise_rms * 1.9f && (int32_t)(now - last_onset_ms) >= ONSET_GAP_MS;
    if (onset) {
        last_onset_ms = now;
        launch_ripple(clampf(loud / COLOUR_RMS, 0.4f, 1.0f));
        if (splash_goal < 280.0f) {
            splash_goal = 280.0f;
        }
    }
    if (rms > noise_rms) {
        noise_rms += (rms - noise_rms) * 0.02f;
    } else {
        noise_rms += (rms - noise_rms) * 0.15f;
    }

    heard = clampf(loud / COLOUR_RMS, 0.0f, 1.0f);
    glint = heard;
    float held = heard * 160.0f;
    if (held > splash_goal) {
        splash_goal = held;
    }
    if (splash_goal > SPLASH_MAX) {
        splash_goal = SPLASH_MAX;
    }
}

static int pool_count() {
    int count = REST_COUNT + (int)splash;
    if (count < 1) {
        count = 1;
    }
    if (count > PANEL_PIXELS - 1) {
        count = PANEL_PIXELS - 1;
    }
    return count;
}

// s grows toward the low side. The line keeps pool_count pixels at or past it.
static float waterline(float dx, float dy, int want) {
    float lo = 0.0f;
    float hi = (PANEL_WIDTH - 1) * dx + (PANEL_HEIGHT - 1) * dy;
    if (hi < lo) {
        float swap = lo;
        lo = hi;
        hi = swap;
    }
    float side = (PANEL_WIDTH - 1) * dx;
    if (side < lo) {
        lo = side;
    } else if (side > hi) {
        hi = side;
    }
    side = (PANEL_HEIGHT - 1) * dy;
    if (side < lo) {
        lo = side;
    } else if (side > hi) {
        hi = side;
    }
    for (int step = 0; step < 12; ++step) {
        float mid = (lo + hi) * 0.5f;
        int got = 0;
        for (int y = 0; y < PANEL_HEIGHT; ++y) {
            float row = y * dy;
            for (int x = 0; x < PANEL_WIDTH; ++x) {
                if (row + x * dx >= mid) {
                    got += 1;
                }
            }
        }
        if (got > want) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (lo + hi) * 0.5f;
}

static void step_pool(float dt_s) {
    if (dt_s < 0.001f) {
        return;
    }
    float follow = 1.0f - expf(-TILT_SMOOTH * dt_s);
    flow_x += (sensed_x - flow_x) * follow;
    flow_y += (sensed_y - flow_y) * follow;
    float mag = sqrtf(flow_x * flow_x + flow_y * flow_y);
    if (mag > 0.02f) {
        shore_x = flow_x / mag;
        shore_y = flow_y / mag;
    }
    splash_goal *= expf(-1.6f * dt_s);
    float ease = 1.0f - expf(-3.0f * dt_s);
    splash += (splash_goal - splash) * ease;
    float tint_rate = heard > audio_tint ? 7.0f : 2.0f;
    audio_tint += (heard - audio_tint) * (1.0f - expf(-tint_rate * dt_s));
    if (audio_tint < 0.004f) {
        audio_tint = 0.0f;
    }
    for (int i = 0; i < RIPPLES; ++i) {
        if (ripples[i].amp <= 0.0f) {
            continue;
        }
        ripples[i].depth += 26.0f * dt_s;
        ripples[i].amp *= expf(-1.7f * dt_s);
        if (ripples[i].amp < 0.04f || ripples[i].depth > 40.0f) {
            ripples[i].amp = 0.0f;
        }
    }
    if (splash < 0.5f) {
        splash = 0.0f;
    }
    if (splash_goal < 0.5f) {
        splash_goal = 0.0f;
    }
}

static void paint(uint8_t *frame) {
    int count = pool_count();
    float mag = sqrtf(flow_x * flow_x + flow_y * flow_y);
    float pour = smooth_unit(0.04f, 0.26f, mag * imu_gain());
    float radius = sqrtf(count / PI);
    float line = waterline(shore_x, shore_y, count);
    float mid = (PANEL_WIDTH - 1) * 0.5f;
    float wave_time = clock_ms * 0.003f;
    for (int i = 0; i < PANEL_PIXELS * 3; ++i) {
        frame[i] = 0;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        float from_mid_y = y - mid;
        float row = y * shore_y;
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float from_mid_x = x - mid;
            float circle = radius - sqrtf(from_mid_x * from_mid_x + from_mid_y * from_mid_y);
            float shore = row + x * shore_x - line;
            float depth = circle + (shore - circle) * pour;
            if (depth <= -1.6f) {
                continue;
            }
            float cover = 1.0f;
            if (depth < 0.0f) {
                cover = 1.0f + depth / 1.6f;
            }
            float rim = 1.0f - depth / 3.2f;
            if (rim < 0.0f) {
                rim = 0.0f;
            }
            if (rim > 1.0f) {
                rim = 1.0f;
            }
            float deep = depth / 26.0f;
            if (deep < 0.0f) {
                deep = 0.0f;
            }
            if (deep > 1.0f) {
                deep = 1.0f;
            }
            float caustic = 0.5f + 0.5f * sinf(x * 0.65f + y * 0.35f + wave_time);
            float shine = rim * 0.65f;
            bool on_wall = x == 0 || y == 0 || x == PANEL_WIDTH - 1 || y == PANEL_HEIGHT - 1;
            if (on_wall && depth > 0.0f) {
                shine += 0.35f;
            }
            float near = 1.0f - fabsf(x - glint_x) / 8.0f;
            if (near < 0.0f) {
                near = 0.0f;
            }
            shine += near * glint * rim;
            if (shine > 1.0f) {
                shine = 1.0f;
            }
            float ripple = 0.0f;
            for (int i = 0; i < RIPPLES; ++i) {
                if (ripples[i].amp <= 0.0f) {
                    continue;
                }
                float band = 1.0f - fabsf(depth - ripples[i].depth) / 5.0f;
                if (band > 0.0f) {
                    ripple += ripples[i].amp * band;
                }
            }
            if (ripple > 1.0f) {
                ripple = 1.0f;
            }
            float red = (deep * 6.0f) + rim * (14.0f + shine * 200.0f);
            float green = 20.0f + (1.0f - deep) * 48.0f + caustic * 14.0f + rim * (80.0f + shine * 50.0f);
            float blue = 55.0f + (1.0f - deep) * 80.0f + caustic * 12.0f + rim * 50.0f;
            red += audio_tint * (28.0f + rim * 36.0f);
            green += audio_tint * (96.0f + rim * 40.0f);
            blue += audio_tint * 18.0f;
            red += ripple * 220.0f;
            green += ripple * 160.0f;
            blue += ripple * 55.0f;
            red *= cover;
            green *= cover;
            blue *= cover;
            int red_i = clamp_channel(red);
            int green_i = clamp_channel(green);
            int blue_i = clamp_channel(blue);
            if (audio_tint > 0.02f) {
                hue_shift(red_i, green_i, blue_i, audio_tint * 0.18f);
            }
            write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red_i, green_i, blue_i);
        }
    }
}

bool environment_listening() {
    return listening;
}

void environment_toggle_listen() {
    listening = !listening;
    if (!listening) {
        meter_level = 0.0f;
        meter_peak = false;
        glint = 0.0f;
        heard = 0.0f;
        for (int i = 0; i < BANDS; ++i) {
            band_level[i] = 0.0f;
        }
    }
}

static int clamp_percent(int percent) {
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

void environment_set_mic_sens(int percent) {
    mic_percent = clamp_percent(percent);
}

void environment_set_imu_sens(int percent) {
    imu_percent = clamp_percent(percent);
}

void environment_bands(float *out, int count) {
    if (out == nullptr || count <= 0) {
        return;
    }
    if (count > BANDS) {
        count = BANDS;
    }
    for (int i = 0; i < count; ++i) {
        out[i] = band_level[i];
    }
}

float environment_meter() {
    return meter_level;
}

bool environment_meter_peak() {
    return meter_peak;
}

bool environment_axes(float &x_g, float &y_g) {
    return read_tilt(x_g, y_g);
}

void environment_start() {
    listening = false;
    meter_level = 0.0f;
    meter_peak = false;
    audio_tint = 0.0f;
    heard = 0.0f;
    noise_rms = 40.0f;
    for (int i = 0; i < RIPPLES; ++i) {
        ripples[i].depth = 0.0f;
        ripples[i].amp = 0.0f;
    }
    flow_x = 0.0f;
    flow_y = 0.0f;
    sensed_x = 0.0f;
    sensed_y = 0.0f;
    shore_x = 0.0f;
    shore_y = 1.0f;
    splash = 0.0f;
    splash_goal = 0.0f;
    glint = 0.0f;
    glint_x = (PANEL_WIDTH - 1) * 0.5f;
    mpu_ready = init_mpu();
    if (mpu_ready) {
        capture_zero();
    }
    adc_init();
    adc_gpio_init(PIN_MIC);
    adc_select_input(1);
    clock_ms = 0;
    last_sample_ms = 0;
    last_onset_ms = 0;
}

void environment_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    (void)anim_ms;
    float dt_s = dt_ms / 1000.0f;
    if (dt_s > 0.05f) {
        dt_s = 0.05f;
    }
    if (mpu_ready) {
        update_gravity();
    }
    clock_ms += (uint32_t)dt_ms;
    if (listening) {
        listen_audio(clock_ms);
    } else {
        heard = 0.0f;
        for (int i = 0; i < BANDS; ++i) {
            band_level[i] = 0.0f;
        }
    }
    step_pool(dt_s);
    paint(frame);
}
