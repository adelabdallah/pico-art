// Plasma for a Waveshare P2.5 64x64 HUB75 panel on a Pico.
//
// The picture is the Unicorn plasma, the second pattern in main.py.
// Panel coordinates land in that 16 by 7 field, and the extra pixels
// sample between the original ones. The field keeps moving. Hue walks
// a full turn of the colour wheel on a timer. Every few seconds the
// view turns a quarter turn over the same half second as the Unicorn B button.
//
// Wire the panel the way the Waveshare Pico example does.
// GP0 R1, GP1 G1, GP2 B1, GP3 R2, GP4 G2, GP5 B2.
// GP6 A, GP7 B, GP8 C, GP9 D, GP10 E.
// GP11 CLK, GP12 LAT, GP13 OE.
// Share ground with the panel. Feed the panel 5V from its own supply.
//
// From this directory, with the Pico SDK on PICO_SDK_PATH:
// cmake -S . -B build -G Ninja
// cmake --build build
// Hold BOOTSEL and copy build/plasma.uf2 to the RPI-RP2 drive.
// A Pico 2 uses the same pins. Configure it with -DPICO_BOARD=pico2.

#include <cmath>
#include <cstdint>

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hub75.hpp"

constexpr int WIDTH = 64;
constexpr int HEIGHT = 64;
constexpr float CX = (WIDTH - 1) / 2.0f;
constexpr float CY = (HEIGHT - 1) / 2.0f;
// The Unicorn field is 16 by 7. Map the rotated panel pixel into it.
constexpr float PATTERN_X = 15.0f / (WIDTH - 1);
constexpr float PATTERN_Y = 6.0f / (HEIGHT - 1);

constexpr int FRAME_MS = 40;
constexpr int DT_CAP_MS = 200;
constexpr int SWEEP_MS = 2000;
constexpr int PLASMA_PHASE_MS = FRAME_MS / 2;
constexpr float PI = 3.14159265f;
constexpr float TURN_STEP = PI / 2.0f;
constexpr int TURN_MS = 500;
constexpr int TURN_PERIOD_MS = 6000;
constexpr int HUE_CYCLE_MS = 8000;
constexpr int WAVE_N = 64;

// Driver basis from the Waveshare example. Intensity stays at 55 percent
// so a desk supply can feed the panel.
constexpr int PANEL_BASIS = 8;
constexpr float PANEL_INTENSITY = 0.55f;

static_assert(DISPLAY_WIDTH == WIDTH, "panel width is 64");
static_assert(DISPLAY_HEIGHT == HEIGHT, "panel height is 64");

int16_t wave_table[WAVE_N];
uint8_t frame[WIDTH * HEIGHT * 3];

float hue = 0.0f;
float angle = 0.0f;
float angle_target = 0.0f;
float view_cos = 1.0f;
float view_sin = 0.0f;

void make_wave() {
    for (int i = 0; i < WAVE_N; ++i) {
        float sample = sinf(i * 2.0f * PI / WAVE_N) * 255.0f;
        wave_table[i] = (int16_t)sample;
    }
}

float wrap_wave(float pos) {
    float wrapped = fmodf(pos, (float)WAVE_N);
    if (wrapped < 0.0f) {
        wrapped += (float)WAVE_N;
    }
    return wrapped;
}

float wave_at(float pos) {
    pos = wrap_wave(pos);
    int base = (int)pos;
    float frac = pos - base;
    int first = wave_table[base & (WAVE_N - 1)];
    int second = wave_table[(base + 1) & (WAVE_N - 1)];
    return first + (second - first) * frac;
}

float sweep_unit(uint32_t now) {
    uint32_t phase = now % (SWEEP_MS * 2);
    if (phase <= SWEEP_MS) {
        return phase / (float)SWEEP_MS;
    }
    return (SWEEP_MS * 2 - phase) / (float)SWEEP_MS;
}

float sweep_float(uint32_t now, float low, float high) {
    return low + (high - low) * sweep_unit(now);
}

int clamp_byte(float value) {
    int rounded = (int)(value + 0.5f);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > 255) {
        return 255;
    }
    return rounded;
}

void hue_shift(int &red, int &green, int &blue, float turns) {
    if (red == green && green == blue) {
        return;
    }
    float peak = red;
    if (green > peak) {
        peak = green;
    }
    if (blue > peak) {
        peak = blue;
    }
    float floor = red;
    if (green < floor) {
        floor = green;
    }
    if (blue < floor) {
        floor = blue;
    }
    if (peak == 0.0f) {
        return;
    }
    float span = peak - floor;
    float hue_value;
    if (peak == red) {
        hue_value = fmodf((green - blue) / span, 6.0f);
    } else if (peak == green) {
        hue_value = (blue - red) / span + 2.0f;
    } else {
        hue_value = (red - green) / span + 4.0f;
    }
    if (hue_value < 0.0f) {
        hue_value += 6.0f;
    }
    hue_value = fmodf(hue_value / 6.0f + turns, 1.0f);
    if (hue_value < 0.0f) {
        hue_value += 1.0f;
    }
    float sat = span / peak;
    float sector = hue_value * 6.0f;
    int index = (int)sector % 6;
    float frac = sector - (int)sector;
    float low = peak * (1.0f - sat);
    float mid_down = peak * (1.0f - frac * sat);
    float mid_up = peak * (1.0f - (1.0f - frac) * sat);
    float out_r;
    float out_g;
    float out_b;
    if (index == 0) {
        out_r = peak;
        out_g = mid_up;
        out_b = low;
    } else if (index == 1) {
        out_r = mid_down;
        out_g = peak;
        out_b = low;
    } else if (index == 2) {
        out_r = low;
        out_g = peak;
        out_b = mid_up;
    } else if (index == 3) {
        out_r = low;
        out_g = mid_down;
        out_b = peak;
    } else if (index == 4) {
        out_r = mid_up;
        out_g = low;
        out_b = peak;
    } else {
        out_r = peak;
        out_g = low;
        out_b = mid_down;
    }
    red = clamp_byte(out_r);
    green = clamp_byte(out_g);
    blue = clamp_byte(out_b);
}

void plasma_rgb(float x, float y, float horizontal, float phase, int &red, int &green, int &blue) {
    float sample = wave_at(x * horizontal + phase) + wave_at(y * 20.0f + phase);
    int level = (int)((sample + 510.0f) / 4.0f);
    if (level < 0) {
        level = 0;
    } else if (level > 255) {
        level = 255;
    }
    red = level;
    green = 80;
    blue = 255 - level;
}

void update_view() {
    view_cos = cosf(angle);
    view_sin = sinf(angle);
}

void update_turn(int dt) {
    float gap = angle_target - angle;
    if (gap <= 0.000001f) {
        angle = angle_target;
    } else {
        float step = TURN_STEP * dt / TURN_MS;
        if (step >= gap) {
            angle = angle_target;
        } else {
            angle += step;
        }
    }
    float cycle = PI * 2.0f;
    if (angle >= cycle) {
        angle -= cycle;
        angle_target -= cycle;
    }
}

void draw_plasma(uint32_t anim_ms) {
    float horizontal = sweep_float(anim_ms, 4.0f, 14.0f);
    float phase = (anim_ms % (PLASMA_PHASE_MS * WAVE_N)) / (float)PLASMA_PHASE_MS;
    uint8_t *pixel = frame;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            float dx = x - CX;
            float dy = y - CY;
            float sx = CX + dx * view_cos + dy * view_sin;
            float sy = CY - dx * view_sin + dy * view_cos;
            int red;
            int green;
            int blue;
            plasma_rgb(sx * PATTERN_X, sy * PATTERN_Y, horizontal, phase, red, green, blue);
            hue_shift(red, green, blue, hue);
            pixel[0] = (uint8_t)blue;
            pixel[1] = (uint8_t)green;
            pixel[2] = (uint8_t)red;
            pixel += 3;
        }
    }
}

void core1_entry() {
    create_hub75_driver(DISPLAY_WIDTH, DISPLAY_HEIGHT, PANEL_TYPE, INVERTED_STB);
    start_hub75_driver();
    while (true) {
        tight_loop_contents();
    }
}

int main() {
    // 266 MHz matches the Waveshare HUB75 example. The panel timing is built for that clock.
    set_sys_clock_khz(266000, true);
    stdio_init_all();
    make_wave();

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    // The driver on the other core allocates its scan buffers before brightness is set.
    sleep_ms(100);
    setBasisBrightness(PANEL_BASIS);
    setIntensity(PANEL_INTENSITY);

    uint32_t anim_ms = 0;
    uint32_t next_turn = TURN_PERIOD_MS;
    absolute_time_t last = get_absolute_time();

    while (true) {
        absolute_time_t started = get_absolute_time();
        int dt = (int)(absolute_time_diff_us(last, started) / 1000);
        last = started;
        if (dt < 0) {
            dt = 0;
        } else if (dt > DT_CAP_MS) {
            dt = DT_CAP_MS;
        }
        anim_ms += (uint32_t)dt;

        uint32_t now = to_ms_since_boot(started);
        hue = (now % HUE_CYCLE_MS) / (float)HUE_CYCLE_MS;
        if ((int32_t)(now - next_turn) >= 0) {
            angle_target += TURN_STEP;
            next_turn += TURN_PERIOD_MS;
        }

        update_turn(dt);
        update_view();
        draw_plasma(anim_ms);
        update_bgr(frame);

        int spent = (int)(absolute_time_diff_us(started, get_absolute_time()) / 1000);
        if (spent < FRAME_MS) {
            sleep_ms(FRAME_MS - spent);
        }
    }
}
