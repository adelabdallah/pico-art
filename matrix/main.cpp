// Firmware for a Waveshare P2.5 64x64 HUB75 panel on a Pico.
//
// Each pattern lives in its own directory and paints the same framebuffer.
// Red and orange step between patterns. Yellow turns the picture. Green pauses it.
// Settings is the last pattern. There, blue picks a value, yellow lowers it, and green raises it.
// The pattern and the six settings are stored in the last flash sector and restored on boot.
// Pause and the quarter turns last until power-off. Each pattern starts running.
//
// Wire the panel the way the Waveshare Pico example does.
// GP0 R1, GP1 G1, GP2 B1, GP3 R2, GP4 G2, GP5 B2.
// GP6 A, GP7 B, GP8 C, GP9 D, GP10 E.
// GP11 CLK, GP12 LAT, GP13 OE.
// Share ground with the panel. Feed the panel 5V from its own supply.
//
// GP14 SDA and GP15 SCL drive the 128 by 64 OLED.
// The yellow band names the pattern, or the setting while settings is open.
// The blue band draws a small version of the pattern. On liquid, with the mic on, it is an equalizer.
// The OLED turns off after its timer. A button turns it back on.
// The dimmest contrast setting is 5 percent.
// Button C goes to the GPIO and NO goes to ground.
// Any button wakes the matrix and the OLED. Red GP19 steps back. Orange GP21 steps forward.
// Yellow GP20 turns the picture a quarter turn clockwise. Green GP26 pauses, and the next press runs it again.
// On settings, yellow lowers the chosen value and green raises it. A hold keeps stepping.
// Blue GP22 shifts the hue by a fifth of a turn, and a hold spins it.
// On liquid, blue toggles the microphone. On settings, blue moves to the next value.
//
// From the matrix directory, with the Pico SDK on PICO_SDK_PATH:
// cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
// cmake --build build
// Hold BOOTSEL and copy build/matrix.uf2 to the RP2350 drive.
// USB serial is /dev/cu.usbmodem*.
//
// A function or variable marked static is private to this file. main is the program entry.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hub75.hpp"
#include "oled.hpp"
#include "pattern.hpp"

constexpr int FRAME_MS = 40;
constexpr int DT_CAP_MS = 200;
constexpr int HOLD_MS = 320;
constexpr int BRIGHTNESS_REPEAT_MS = 200;

constexpr int PANEL_BASIS = 8;
constexpr int BRIGHTNESS_PERCENT = 55;
constexpr int BRIGHTNESS_STEP = 5;

constexpr float HUE_TAP = 0.2f;
constexpr int HUE_TAP_MS = 300;
constexpr float HUE_SPIN = 1.0f;
constexpr int PAUSE_MS = 2000;
constexpr int TURN_MS = 500;
constexpr float TURN_STEP = 1.5707963f;

// Minutes. A zero LED entry means the matrix stays lit. The OLED list has no forever entry.
constexpr int LED_MINUTES[] = {1, 2, 5, 10, 15, 30, 60, 0};
constexpr int LED_TIMER_COUNT = 8;
constexpr int LED_TIMER_DEFAULT = 7;
constexpr int OLED_MINUTES[] = {1, 2, 5, 10, 15, 30, 60};
constexpr int OLED_TIMER_COUNT = 7;
constexpr int OLED_TIMER_DEFAULT = 2;
constexpr int SETTING_COUNT = 6;
constexpr int OLED_BRIGHT_MIN = 5;

constexpr int PIN_RED = 19;
constexpr int PIN_YELLOW = 20;
constexpr int PIN_ORANGE = 21;
constexpr int PIN_BLUE = 22;
constexpr int PIN_GREEN = 26;

static_assert(DISPLAY_WIDTH == PANEL_WIDTH, "panel width is 64");
static_assert(DISPLAY_HEIGHT == PANEL_HEIGHT, "panel height is 64");

// start and draw hold the functions themselves, the way a Python name can refer to a function.
struct Pattern {
    const char *name;
    void (*start)();
    void (*draw)(uint8_t *frame, uint32_t anim_ms, int dt_ms);
};

Pattern patterns[] = {
    {"plasma", plasma_start, plasma_draw},
    {"warp", warp_start, warp_draw},
    {"feedback", feedback_start, feedback_draw},
    {"metaballs", metaballs_start, metaballs_draw},
    {"reaction", reaction_start, reaction_draw},
    {"life", life_start, life_draw},
    {"ant", ant_start, ant_draw},
    {"loop", loop_start, loop_draw},
    {"rule30", rule30_start, rule30_draw},
    {"liquid", environment_start, environment_draw},
    {"settings", settings_start, settings_draw},
};

constexpr int PATTERN_COUNT = (int)(sizeof patterns / sizeof patterns[0]);

// Last sector. A blank sector is erased flash, so the magic word is absent and boot stays on plasma.
constexpr uint32_t PATTERN_MAGIC = 0x6d617458;
constexpr uint32_t PATTERN_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

// version 0 is a record from before the settings page. Those boots keep pattern, brightness, and hue.
constexpr uint32_t PATTERN_VERSION = 1;

struct SavedPattern {
    uint32_t magic;
    uint32_t index;
    uint32_t brightness;
    uint32_t hue;
    uint32_t version;
    uint32_t mic_sens;
    uint32_t imu_sens;
    uint32_t oled_bright;
    uint32_t led_timer;
    uint32_t oled_timer;
};

struct Settings {
    int pattern;
    int brightness;
    float hue;
    int mic;
    int imu;
    int oled_bright;
    int led_timer;
    int oled_timer;
    int cursor;
};

enum class ButtonRole {
    Previous,
    Next,
    Turn,
    Pause,
    Hue,
};

struct SettingRow {
    const char *label;
    OledIcon icon;
};

struct PanelButton {
    int gpio;
    const char *name;
    ButtonRole role;
    bool down;
    bool repeated;
    uint32_t pressed_at;
    uint32_t repeat_at;
};

// Each button shorts its pin to ground. The pull-up makes a press read as 0.
static PanelButton buttons[] = {
    {PIN_RED, "red", ButtonRole::Previous, false, false, 0, 0},
    {PIN_ORANGE, "orange", ButtonRole::Next, false, false, 0, 0},
    {PIN_YELLOW, "yellow", ButtonRole::Turn, false, false, 0, 0},
    {PIN_GREEN, "green", ButtonRole::Pause, false, false, 0, 0},
    {PIN_BLUE, "blue", ButtonRole::Hue, false, false, 0, 0},
};

constexpr int BUTTON_COUNT = (int)(sizeof buttons / sizeof buttons[0]);

static SettingRow setting_rows[] = {
    {"BRIGHT:", OledIcon::Sun},
    {"OLED:", OledIcon::Sun},
    {"MIC SENS:", OledIcon::Mic},
    {"IMU SENS:", OledIcon::Axes},
    {"LED TIMER:", OledIcon::Hourglass},
    {"OLED TIMER:", OledIcon::Hourglass},
};

static Settings settings = {0, BRIGHTNESS_PERCENT, 0.0f, 50, 50, 100, LED_TIMER_DEFAULT, OLED_TIMER_DEFAULT, 0};
static uint32_t anim_ms = 0;
static float anim_frac = 0.0f;
static bool matrix_blank = false;
static uint32_t matrix_awake_until = 0;
static float motion = 1.0f;
static float motion_from = 1.0f;
static float motion_to = 1.0f;
static uint32_t motion_started = 0;
static bool motion_easing = false;
static float view_angle = 0.0f;
static float view_target = 0.0f;

static bool hue_tap = false;
static bool hue_spin = false;
static uint32_t hue_tap_at = 0;
static float hue_tap_from = 0.0f;
static float hue_tap_to = 0.0f;

static float wrap_hue(float turns) {
    turns = turns - (float)(int)turns;
    if (turns < 0.0f) {
        turns += 1.0f;
    }
    return turns;
}

static Settings load_settings() {
    Settings loaded = settings;
    const auto *saved = (const SavedPattern *)(XIP_BASE + PATTERN_OFFSET);
    if (saved->magic != PATTERN_MAGIC) {
        return loaded;
    }
    if (saved->index < (uint32_t)PATTERN_COUNT) {
        loaded.pattern = (int)saved->index;
    }
    // Stored as percent + 1. Zero is a record from before brightness was saved.
    if (saved->brightness >= 1 && saved->brightness <= 101) {
        int percent = (int)saved->brightness - 1;
        if (percent % BRIGHTNESS_STEP == 0) {
            loaded.brightness = percent;
        }
    }
    // Thousandths of a turn. A record from before hue was saved reads as zero.
    if (saved->hue < 1000) {
        loaded.hue = saved->hue / 1000.0f;
    }
    if (saved->version != PATTERN_VERSION) {
        return loaded;
    }
    if (saved->mic_sens <= 100 && saved->mic_sens % BRIGHTNESS_STEP == 0) {
        loaded.mic = (int)saved->mic_sens;
    }
    if (saved->imu_sens <= 100 && saved->imu_sens % BRIGHTNESS_STEP == 0) {
        loaded.imu = (int)saved->imu_sens;
    }
    if (saved->oled_bright >= OLED_BRIGHT_MIN && saved->oled_bright <= 100 && saved->oled_bright % BRIGHTNESS_STEP == 0) {
        loaded.oled_bright = (int)saved->oled_bright;
    }
    if (saved->led_timer < LED_TIMER_COUNT) {
        loaded.led_timer = (int)saved->led_timer;
    }
    if (saved->oled_timer < OLED_TIMER_COUNT) {
        loaded.oled_timer = (int)saved->oled_timer;
    }
    return loaded;
}

// Runs while the panel core is paused. Erasing flash stalls the chip, so the scan must not be mid-row.
static void program_settings(void *param) {
    Settings *incoming = (Settings *)param;
    uint8_t page[FLASH_PAGE_SIZE] = {};
    auto *saved = (SavedPattern *)page;
    saved->magic = PATTERN_MAGIC;
    saved->index = (uint32_t)incoming->pattern;
    saved->brightness = (uint32_t)incoming->brightness + 1;
    int milli = (int)(incoming->hue * 1000.0f + 0.5f);
    if (milli < 0) {
        milli = 0;
    }
    if (milli >= 1000) {
        milli = 0;
    }
    saved->hue = (uint32_t)milli;
    saved->version = PATTERN_VERSION;
    saved->mic_sens = (uint32_t)incoming->mic;
    saved->imu_sens = (uint32_t)incoming->imu;
    saved->oled_bright = (uint32_t)incoming->oled_bright;
    saved->led_timer = (uint32_t)incoming->led_timer;
    saved->oled_timer = (uint32_t)incoming->oled_timer;
    flash_range_erase(PATTERN_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(PATTERN_OFFSET, page, FLASH_PAGE_SIZE);
}

static void save_settings(const Settings &incoming) {
    Settings copy = incoming;
    int rc = flash_safe_execute(program_settings, &copy, UINT32_MAX);
    if (rc != PICO_OK) {
        printf("pattern save failed\n");
    }
}

static bool on_environment() {
    return std::strcmp(patterns[settings.pattern].name, "liquid") == 0;
}

static bool on_settings() {
    return std::strcmp(patterns[settings.pattern].name, "settings") == 0;
}

static bool led_forever() {
    return LED_MINUTES[settings.led_timer] <= 0;
}

static uint32_t minutes_ms(int minutes) {
    return (uint32_t)minutes * 60u * 1000u;
}

static void apply_oled_timer() {
    oled_set_idle_ms(minutes_ms(OLED_MINUTES[settings.oled_timer]));
}

static void arm_matrix_timer(uint32_t now) {
    if (led_forever()) {
        return;
    }
    matrix_awake_until = now + minutes_ms(LED_MINUTES[settings.led_timer]);
}

static void note_activity(uint32_t now) {
    oled_poke(now);
    arm_matrix_timer(now);
}

static void apply_matrix_level() {
    if (matrix_blank) {
        return;
    }
    setIntensity(settings.brightness / 100.0f);
}

static void wake_matrix() {
    if (!matrix_blank) {
        return;
    }
    matrix_blank = false;
    setIntensity(settings.brightness / 100.0f);
}

static void blank_matrix_if_due(uint32_t now) {
    if (led_forever() || matrix_blank) {
        return;
    }
    if ((int32_t)(now - matrix_awake_until) >= 0) {
        matrix_blank = true;
        setIntensity(0.0f);
    }
}

static void clear_pause() {
    motion = 1.0f;
    motion_from = 1.0f;
    motion_to = 1.0f;
    motion_easing = false;
}

static void toggle_pause(uint32_t now) {
    motion_from = motion;
    motion_to = motion_to < 0.5f ? 1.0f : 0.0f;
    motion_started = now;
    motion_easing = true;
}

static void update_motion(uint32_t now) {
    if (!motion_easing) {
        return;
    }
    float portion = (float)(now - motion_started) / (float)PAUSE_MS;
    if (portion >= 1.0f) {
        motion = motion_to;
        motion_easing = false;
        return;
    }
    float curved = portion * portion * (3.0f - 2.0f * portion);
    motion = motion_from + (motion_to - motion_from) * curved;
}

static void nudge_turn() {
    view_target += TURN_STEP;
}

static void update_turn(int dt_ms) {
    float gap = view_target - view_angle;
    if (gap <= 0.0001f) {
        view_angle = view_target;
    } else {
        float step = TURN_STEP * dt_ms / (float)TURN_MS;
        if (step > gap) {
            step = gap;
        }
        view_angle += step;
    }
    constexpr float circle = 6.2831853f;
    if (view_angle > circle * 4.0f) {
        view_angle -= circle * 4.0f;
        view_target -= circle * 4.0f;
    }
}

static void format_title(char *dst, int size, const char *name) {
    int i = 0;
    for (; name[i] != '\0' && i + 1 < size; ++i) {
        char letter = name[i];
        if (letter >= 'a' && letter <= 'z') {
            letter = (char)(letter - 'a' + 'A');
        }
        dst[i] = letter;
    }
    dst[i] = '\0';
}

static void format_timer(char *dst, int size, int minutes) {
    if (minutes <= 0) {
        snprintf(dst, size, "~");
        return;
    }
    snprintf(dst, size, "%dm", minutes);
}

static void present_oled(uint32_t now) {
    if (on_settings()) {
        char value[8];
        float bar = 0.0f;
        int cursor = settings.cursor;
        if (cursor < 0 || cursor >= SETTING_COUNT) {
            cursor = 0;
        }
        if (cursor == 0) {
            snprintf(value, sizeof value, "%d%%", settings.brightness);
            bar = settings.brightness / 100.0f;
        } else if (cursor == 1) {
            snprintf(value, sizeof value, "%d%%", settings.oled_bright);
            bar = settings.oled_bright / 100.0f;
        } else if (cursor == 2) {
            snprintf(value, sizeof value, "%d%%", settings.mic);
            bar = settings.mic / 100.0f;
        } else if (cursor == 3) {
            snprintf(value, sizeof value, "%d%%", settings.imu);
            bar = settings.imu / 100.0f;
        } else if (cursor == 4) {
            format_timer(value, (int)sizeof value, LED_MINUTES[settings.led_timer]);
            bar = (settings.led_timer + 1) / (float)LED_TIMER_COUNT;
        } else {
            format_timer(value, (int)sizeof value, OLED_MINUTES[settings.oled_timer]);
            bar = (settings.oled_timer + 1) / (float)OLED_TIMER_COUNT;
        }
        oled_show_setting(setting_rows[cursor].label, setting_rows[cursor].icon, value, bar, now);
        return;
    }
    char title[16];
    format_title(title, (int)sizeof title, patterns[settings.pattern].name);
    if (on_environment() && environment_listening()) {
        float bands[8] = {};
        environment_bands(bands, 8);
        oled_show_pattern(title, now, bands, 8);
        return;
    }
    oled_show_pattern(title, now, nullptr, 0);
}

static void show_status() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    note_activity(now);
    present_oled(now);
}

static void change_pattern(int delta) {
    settings.pattern += delta;
    if (settings.pattern >= PATTERN_COUNT) {
        settings.pattern = 0;
    } else if (settings.pattern < 0) {
        settings.pattern = PATTERN_COUNT - 1;
    }
    anim_ms = 0;
    anim_frac = 0.0f;
    clear_pause();
    patterns[settings.pattern].start();
    show_status();
    save_settings(settings);
}

static bool change_brightness(int delta, bool store) {
    int brightness = settings.brightness + delta;
    if (brightness < 0) {
        brightness = 0;
    } else if (brightness > 100) {
        brightness = 100;
    }
    if (brightness == settings.brightness) {
        return false;
    }
    settings.brightness = brightness;
    apply_matrix_level();
    show_status();
    if (store) {
        save_settings(settings);
    }
    return true;
}

static int clamp_level(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static bool nudge_setting(int direction, bool store) {
    int cursor = settings.cursor;
    if (cursor == 0) {
        return change_brightness(direction * BRIGHTNESS_STEP, store);
    }
    if (cursor == 1) {
        int next = clamp_level(settings.oled_bright + direction * BRIGHTNESS_STEP, OLED_BRIGHT_MIN, 100);
        if (next == settings.oled_bright) {
            return false;
        }
        settings.oled_bright = next;
        oled_set_contrast(next);
    } else if (cursor == 2) {
        int next = clamp_level(settings.mic + direction * BRIGHTNESS_STEP, 0, 100);
        if (next == settings.mic) {
            return false;
        }
        settings.mic = next;
        environment_set_mic_sens(next);
    } else if (cursor == 3) {
        int next = clamp_level(settings.imu + direction * BRIGHTNESS_STEP, 0, 100);
        if (next == settings.imu) {
            return false;
        }
        settings.imu = next;
        environment_set_imu_sens(next);
    } else if (cursor == 4) {
        int next = settings.led_timer + direction;
        if (next < 0 || next >= LED_TIMER_COUNT) {
            return false;
        }
        settings.led_timer = next;
    } else {
        int next = settings.oled_timer + direction;
        if (next < 0 || next >= OLED_TIMER_COUNT) {
            return false;
        }
        settings.oled_timer = next;
        apply_oled_timer();
    }
    show_status();
    if (store) {
        save_settings(settings);
    }
    return true;
}

static void begin_hue_tap(uint32_t now) {
    hue_spin = false;
    hue_tap = true;
    hue_tap_at = now;
    hue_tap_from = settings.hue;
    hue_tap_to = settings.hue + HUE_TAP;
}

static void begin_hue_spin() {
    if (hue_tap) {
        settings.hue = wrap_hue(hue_tap_to);
        hue_tap = false;
    }
    hue_spin = true;
}

static void advance_hue(uint32_t now, int dt_ms) {
    if (hue_spin) {
        settings.hue = wrap_hue(settings.hue + dt_ms * HUE_SPIN / 1000.0f);
        return;
    }
    if (!hue_tap) {
        return;
    }
    float portion = (float)(now - hue_tap_at) / (float)HUE_TAP_MS;
    if (portion >= 1.0f) {
        settings.hue = wrap_hue(hue_tap_to);
        hue_tap = false;
        save_settings(settings);
        return;
    }
    float span = hue_tap_to - hue_tap_from;
    settings.hue = wrap_hue(hue_tap_from + span * portion);
}

static void apply_hue(uint8_t *pixels, float turns) {
    if (turns <= 0.0f) {
        return;
    }
    uint8_t *pixel = pixels;
    for (int i = 0; i < PANEL_PIXELS; ++i) {
        int blue = pixel[0];
        int green = pixel[1];
        int red = pixel[2];
        hue_shift(red, green, blue, turns);
        write_bgr(pixel, red, green, blue);
        pixel += 3;
    }
}

static uint8_t turned[PANEL_PIXELS * 3];

static void copy_pixel(uint8_t *dst, const uint8_t *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static const uint8_t *pixel_at(const uint8_t *src, int x, int y) {
    if (x < 0) {
        x = 0;
    } else if (x >= PANEL_WIDTH) {
        x = PANEL_WIDTH - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= PANEL_HEIGHT) {
        y = PANEL_HEIGHT - 1;
    }
    return src + (y * PANEL_WIDTH + x) * 3;
}

static void rotate_exact(uint8_t *pixels, int quarter) {
    quarter &= 3;
    if (quarter == 0) {
        return;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int sx = x;
            int sy = y;
            if (quarter == 1) {
                sx = y;
                sy = PANEL_WIDTH - 1 - x;
            } else if (quarter == 2) {
                sx = PANEL_WIDTH - 1 - x;
                sy = PANEL_HEIGHT - 1 - y;
            } else {
                sx = PANEL_HEIGHT - 1 - y;
                sy = x;
            }
            copy_pixel(turned + (y * PANEL_WIDTH + x) * 3, pixel_at(pixels, sx, sy));
        }
    }
    std::memcpy(pixels, turned, sizeof turned);
}

static void sample_pixel(const uint8_t *src, float x, float y, uint8_t *dst) {
    if (x < 0.0f) {
        x = 0.0f;
    } else if (x > PANEL_WIDTH - 1) {
        x = PANEL_WIDTH - 1;
    }
    if (y < 0.0f) {
        y = 0.0f;
    } else if (y > PANEL_HEIGHT - 1) {
        y = PANEL_HEIGHT - 1;
    }
    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    if (x1 >= PANEL_WIDTH) {
        x1 = PANEL_WIDTH - 1;
    }
    if (y1 >= PANEL_HEIGHT) {
        y1 = PANEL_HEIGHT - 1;
    }
    float tx = x - x0;
    float ty = y - y0;
    const uint8_t *p00 = pixel_at(src, x0, y0);
    const uint8_t *p10 = pixel_at(src, x1, y0);
    const uint8_t *p01 = pixel_at(src, x0, y1);
    const uint8_t *p11 = pixel_at(src, x1, y1);
    for (int channel = 0; channel < 3; ++channel) {
        float value = p00[channel] * (1.0f - tx) * (1.0f - ty) + p10[channel] * tx * (1.0f - ty);
        value += p01[channel] * (1.0f - tx) * ty + p11[channel] * tx * ty;
        dst[channel] = (uint8_t)(value + 0.5f);
    }
}

// Clockwise about the centre. A settled quarter turn stays on whole pixels.
static void rotate_view(uint8_t *pixels) {
    float gap = view_target - view_angle;
    if (gap < 0.0f) {
        gap = -gap;
    }
    float quarters = view_angle / TURN_STEP;
    int nearest = (int)(quarters + (quarters >= 0.0f ? 0.5f : -0.5f));
    float error = view_angle - nearest * TURN_STEP;
    if (error < 0.0f) {
        error = -error;
    }
    if (gap < 0.02f && error < 0.02f) {
        rotate_exact(pixels, nearest);
        return;
    }
    float cosine = cosf(view_angle);
    float sine = sinf(view_angle);
    float mid = (PANEL_WIDTH - 1) * 0.5f;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = x - mid;
            float dy = y - mid;
            float sx = mid + dx * cosine + dy * sine;
            float sy = mid - dx * sine + dy * cosine;
            sample_pixel(pixels, sx, sy, turned + (y * PANEL_WIDTH + x) * 3);
        }
    }
    std::memcpy(pixels, turned, sizeof turned);
}

static void apply_pause_breathe(uint8_t *pixels, uint32_t now) {
    if (motion > 0.999f) {
        return;
    }
    float wave = 0.5f + 0.5f * sinf(now * 6.2831853f / 3000.0f);
    float breathe = 0.35f + 0.65f * wave;
    float gain = motion + (1.0f - motion) * breathe;
    if (gain > 0.99f) {
        return;
    }
    for (int i = 0; i < PANEL_PIXELS * 3; ++i) {
        pixels[i] = (uint8_t)(pixels[i] * gain + 0.5f);
    }
}

static void setup_buttons() {
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        gpio_init(buttons[i].gpio);
        gpio_set_dir(buttons[i].gpio, GPIO_IN);
        gpio_pull_up(buttons[i].gpio);
    }
}

static void poll_button(PanelButton &button, uint32_t now) {
    bool down = gpio_get(button.gpio) == 0;
    bool pressed = down && !button.down;
    bool released = !down && button.down;
    button.down = down;
    if (down) {
        note_activity(now);
    }
    if (pressed) {
        button.pressed_at = now;
        button.repeated = false;
        wake_matrix();
    }
    if (button.role == ButtonRole::Previous || button.role == ButtonRole::Next) {
        if (pressed) {
            change_pattern(button.role == ButtonRole::Next ? 1 : -1);
        }
        return;
    }
    if (on_settings() && (button.role == ButtonRole::Turn || button.role == ButtonRole::Pause)) {
        int direction = button.role == ButtonRole::Pause ? 1 : -1;
        if (pressed) {
            if (!nudge_setting(direction, true)) {
                show_status();
            }
            button.repeat_at = now + HOLD_MS;
        } else if (down && (int32_t)(now - button.repeat_at) >= 0) {
            if (nudge_setting(direction, false)) {
                button.repeated = true;
            }
            button.repeat_at = now + BRIGHTNESS_REPEAT_MS;
        }
        if (released && button.repeated) {
            save_settings(settings);
            button.repeated = false;
        }
        return;
    }
    if (button.role == ButtonRole::Turn) {
        if (pressed) {
            nudge_turn();
            show_status();
        }
        return;
    }
    if (button.role == ButtonRole::Pause) {
        if (pressed) {
            toggle_pause(now);
            show_status();
        }
        return;
    }
    if (pressed && on_settings()) {
        settings.cursor = (settings.cursor + 1) % SETTING_COUNT;
        show_status();
        return;
    }
    if (pressed && on_environment()) {
        environment_toggle_listen();
        show_status();
        return;
    }
    if (pressed) {
        begin_hue_tap(now);
        show_status();
    }
    if (released) {
        hue_spin = false;
        if (!hue_tap) {
            save_settings(settings);
        }
    } else if (down && !hue_spin && (int32_t)(now - button.pressed_at) >= HOLD_MS) {
        begin_hue_spin();
    }
}

static uint8_t frame[PANEL_PIXELS * 3];

// Core 1 only scans the panel. Core 0, in main, reads buttons and paints frames.
static void core1_entry() {
    // Lets the panel core pause while the other core erases the pattern sector.
    flash_safe_execute_core_init();
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

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    // The driver on the other core allocates its scan buffers before brightness is set.
    sleep_ms(100);
    settings = load_settings();
    setBasisBrightness(PANEL_BASIS);
    apply_matrix_level();
    environment_set_mic_sens(settings.mic);
    environment_set_imu_sens(settings.imu);
    setup_buttons();
    if (!oled_init()) {
        printf("oled not found\n");
    }
    apply_oled_timer();
    oled_set_contrast(settings.oled_bright);

    patterns[settings.pattern].start();
    printf("patterns: plasma, warp, feedback, metaballs, reaction, life, ant, loop, rule30, liquid, settings\n");
    show_status();

    uint32_t oled_at = 0;
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

        uint32_t now = to_ms_since_boot(started);
        advance_hue(now, dt);

        for (int i = 0; i < BUTTON_COUNT; ++i) {
            poll_button(buttons[i], now);
        }

        update_motion(now);
        update_turn(dt);
        anim_frac += dt * motion;
        int draw_dt = (int)anim_frac;
        if (draw_dt < 0) {
            draw_dt = 0;
        }
        anim_frac -= draw_dt;
        anim_ms += (uint32_t)draw_dt;

        patterns[settings.pattern].draw(frame, anim_ms, draw_dt);
        rotate_view(frame);
        if (!on_settings()) {
            apply_hue(frame, settings.hue);
        }
        apply_pause_breathe(frame, now);
        update_bgr(frame);

        oled_idle(now);
        blank_matrix_if_due(now);
        if ((int32_t)(now - oled_at) >= 0) {
            present_oled(now);
            oled_at = now + 150;
        }

        int spent = (int)(absolute_time_diff_us(started, get_absolute_time()) / 1000);
        if (spent < FRAME_MS) {
            sleep_ms(FRAME_MS - spent);
        }
    }
}
