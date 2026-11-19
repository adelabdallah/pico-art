// Firmware for a Waveshare P2.5 64x64 HUB75 panel on a Pico.
//
// Each pattern lives in its own directory and paints the same framebuffer.
// Space on USB serial, or on TCP port 2323, selects the next pattern.
// Up and down change brightness by 5 percent.
// The pattern, brightness, and hue are stored in the last flash sector and restored on boot.
//
// Wire the panel the way the Waveshare Pico example does.
// GP0 R1, GP1 G1, GP2 B1, GP3 R2, GP4 G2, GP5 B2.
// GP6 A, GP7 B, GP8 C, GP9 D, GP10 E.
// GP11 CLK, GP12 LAT, GP13 OE.
// Share ground with the panel. Feed the panel 5V from its own supply.
//
// GP14 SDA and GP15 SCL drive the 128 by 64 OLED.
// The yellow rows show the last button, and on liquid the zeroed X and Y tilt.
// The blue rows show the pattern and brightness.
// Button C goes to the GPIO and NO goes to ground.
// Red GP19 steps back. Orange GP21 steps forward.
// Yellow GP20 lowers brightness. Green GP26 raises it, and a hold keeps stepping.
// Blue GP22 shifts the hue by a fifth of a turn, and a hold spins it.
// On the liquid pattern, blue toggles the microphone instead.
//
// From the matrix directory, with the Pico SDK on PICO_SDK_PATH:
// Put the network name and password in wifi_config.h first.
// cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
// cmake --build build
// Hold BOOTSEL and copy build/matrix.uf2 to the RP2350 drive.
// USB serial is /dev/cu.usbmodem*. The shelf listens at matrix.local port 2323.

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
#include "remote.hpp"

constexpr int FRAME_MS = 40;
constexpr int DT_CAP_MS = 200;
constexpr int SWITCH_GAP_MS = 200;
constexpr int HOLD_MS = 320;
constexpr int BRIGHTNESS_REPEAT_MS = 200;

constexpr int PANEL_BASIS = 8;
constexpr int BRIGHTNESS_PERCENT = 55;
constexpr int BRIGHTNESS_STEP = 5;

constexpr float HUE_TAP = 0.2f;
constexpr int HUE_TAP_MS = 300;
constexpr float HUE_SPIN = 1.0f;

constexpr int PIN_RED = 19;
constexpr int PIN_YELLOW = 20;
constexpr int PIN_ORANGE = 21;
constexpr int PIN_BLUE = 22;
constexpr int PIN_GREEN = 26;

static_assert(DISPLAY_WIDTH == PANEL_WIDTH, "panel width is 64");
static_assert(DISPLAY_HEIGHT == PANEL_HEIGHT, "panel height is 64");

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
};

constexpr int PATTERN_COUNT = (int)(sizeof patterns / sizeof patterns[0]);

// Last sector. A blank sector is erased flash, so the magic word is absent and boot stays on plasma.
constexpr uint32_t PATTERN_MAGIC = 0x6d617458;
constexpr uint32_t PATTERN_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct SavedPattern {
    uint32_t magic;
    uint32_t index;
    uint32_t brightness;
    uint32_t hue;
};

struct Settings {
    int pattern;
    int brightness;
    float hue;
};

enum class Command {
    None,
    Next,
    Brighter,
    Dimmer,
};

enum class ButtonRole {
    Previous,
    Next,
    Dimmer,
    Brighter,
    Hue,
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

PanelButton buttons[] = {
    {PIN_RED, "red", ButtonRole::Previous, false, false, 0, 0},
    {PIN_ORANGE, "orange", ButtonRole::Next, false, false, 0, 0},
    {PIN_YELLOW, "yellow", ButtonRole::Dimmer, false, false, 0, 0},
    {PIN_GREEN, "green", ButtonRole::Brighter, false, false, 0, 0},
    {PIN_BLUE, "blue", ButtonRole::Hue, false, false, 0, 0},
};

constexpr int BUTTON_COUNT = (int)(sizeof buttons / sizeof buttons[0]);

Settings settings = {0, BRIGHTNESS_PERCENT, 0.0f};
uint32_t anim_ms = 0;
const char *pressed_label = "";

bool hue_tap = false;
bool hue_spin = false;
uint32_t hue_tap_at = 0;
float hue_tap_from = 0.0f;
float hue_tap_to = 0.0f;

// Escape, then '[' or 'O', then 'A' or 'B'. That is an up or down arrow.
int escape = 0;

float wrap_hue(float turns) {
    turns = turns - (float)(int)turns;
    if (turns < 0.0f) {
        turns += 1.0f;
    }
    return turns;
}

Settings load_settings() {
    Settings loaded = {0, BRIGHTNESS_PERCENT, 0.0f};
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
    return loaded;
}

void program_settings(void *param) {
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
    flash_range_erase(PATTERN_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(PATTERN_OFFSET, page, FLASH_PAGE_SIZE);
}

void save_settings(const Settings &incoming) {
    Settings copy = incoming;
    int rc = flash_safe_execute(program_settings, &copy, UINT32_MAX);
    if (rc != PICO_OK) {
        printf("pattern save failed\n");
    }
}

bool on_environment() {
    return std::strcmp(patterns[settings.pattern].name, "liquid") == 0;
}

void format_tenths(char *dst, float g) {
    if (g > 9.9f) {
        g = 9.9f;
    } else if (g < -9.9f) {
        g = -9.9f;
    }
    int tenths = (int)(g * 10.0f + (g >= 0.0f ? 0.5f : -0.5f));
    char sign = ' ';
    if (tenths < 0) {
        sign = '-';
        tenths = -tenths;
    }
    dst[0] = sign;
    dst[1] = (char)('0' + tenths / 10);
    dst[2] = '.';
    dst[3] = (char)('0' + tenths % 10);
    dst[4] = '\0';
}

void refresh_oled() {
    char pattern_line[32];
    char bright_line[24];
    char button_line[24];
    char axes[20];
    char x_text[5];
    char y_text[5];
    snprintf(pattern_line, sizeof pattern_line, "pattern: %s", patterns[settings.pattern].name);
    snprintf(bright_line, sizeof bright_line, "brightness: %d%%", settings.brightness);
    if (pressed_label != nullptr && pressed_label[0] != '\0') {
        snprintf(button_line, sizeof button_line, "%s pressed", pressed_label);
    } else {
        button_line[0] = '\0';
    }
    const char *axes_line = nullptr;
    if (on_environment()) {
        float x_g = 0.0f;
        float y_g = 0.0f;
        if (environment_axes(x_g, y_g)) {
            format_tenths(x_text, x_g);
            format_tenths(y_text, y_g);
            snprintf(axes, sizeof axes, "x:%s y:%s", x_text, y_text);
        } else {
            snprintf(axes, sizeof axes, "x:---- y:----");
        }
        axes_line = axes;
    }
    if (on_environment() && environment_listening()) {
        oled_show(pattern_line, button_line, bright_line, environment_meter(), environment_meter_peak(), axes_line);
        return;
    }
    oled_show(pattern_line, button_line, bright_line, -1.0f, false, axes_line);
}

void show_status() {
    char line[48];
    snprintf(line, sizeof line, "pattern: %s", patterns[settings.pattern].name);
    remote_status(line);
    refresh_oled();
}

void change_pattern(int delta) {
    settings.pattern += delta;
    if (settings.pattern >= PATTERN_COUNT) {
        settings.pattern = 0;
    } else if (settings.pattern < 0) {
        settings.pattern = PATTERN_COUNT - 1;
    }
    anim_ms = 0;
    patterns[settings.pattern].start();
    show_status();
    save_settings(settings);
}

bool change_brightness(int delta, bool store) {
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
    setIntensity(settings.brightness / 100.0f);
    show_status();
    if (store) {
        save_settings(settings);
    }
    return true;
}

void begin_hue_tap(uint32_t now) {
    hue_spin = false;
    hue_tap = true;
    hue_tap_at = now;
    hue_tap_from = settings.hue;
    hue_tap_to = settings.hue + HUE_TAP;
}

void begin_hue_spin() {
    if (hue_tap) {
        settings.hue = wrap_hue(hue_tap_to);
        hue_tap = false;
    }
    hue_spin = true;
}

void advance_hue(uint32_t now, int dt_ms) {
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

void apply_hue(uint8_t *pixels, float turns) {
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

Command feed_key(int ch) {
    if (escape == 1) {
        escape = (ch == '[' || ch == 'O') ? 2 : 0;
        return Command::None;
    }
    if (escape == 2) {
        escape = 0;
        if (ch == 'A') {
            return Command::Brighter;
        }
        if (ch == 'B') {
            return Command::Dimmer;
        }
        return Command::None;
    }
    if (ch == 0x1b) {
        escape = 1;
        return Command::None;
    }
    if (ch == ' ') {
        return Command::Next;
    }
    return Command::None;
}

int read_key() {
    int remote = remote_read();
    if (remote >= 0) {
        return remote;
    }
    int usb = getchar_timeout_us(0);
    if (usb == PICO_ERROR_TIMEOUT) {
        return -1;
    }
    return usb;
}

void setup_buttons() {
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        gpio_init(buttons[i].gpio);
        gpio_set_dir(buttons[i].gpio, GPIO_IN);
        gpio_pull_up(buttons[i].gpio);
    }
}

void poll_button(PanelButton &button, uint32_t now) {
    bool down = gpio_get(button.gpio) == 0;
    bool pressed = down && !button.down;
    bool released = !down && button.down;
    button.down = down;
    if (pressed) {
        pressed_label = button.name;
        button.pressed_at = now;
        button.repeated = false;
    }
    if (button.role == ButtonRole::Previous || button.role == ButtonRole::Next) {
        if (pressed) {
            change_pattern(button.role == ButtonRole::Next ? 1 : -1);
        }
        return;
    }
    if (button.role == ButtonRole::Dimmer || button.role == ButtonRole::Brighter) {
        int delta = button.role == ButtonRole::Brighter ? BRIGHTNESS_STEP : -BRIGHTNESS_STEP;
        if (pressed) {
            if (!change_brightness(delta, true)) {
                show_status();
            }
            button.repeat_at = now + HOLD_MS;
        } else if (down && (int32_t)(now - button.repeat_at) >= 0) {
            if (change_brightness(delta, false)) {
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

uint8_t frame[PANEL_PIXELS * 3];

void core1_entry() {
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
    // The radio claims a PIO state machine here, before the panel driver takes the rest.
    remote_init();

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);
    // The driver on the other core allocates its scan buffers before brightness is set.
    sleep_ms(100);
    settings = load_settings();
    setBasisBrightness(PANEL_BASIS);
    setIntensity(settings.brightness / 100.0f);
    setup_buttons();
    if (!oled_init()) {
        printf("oled not found\n");
    }

    patterns[settings.pattern].start();
    printf("patterns: plasma, warp, feedback, metaballs, reaction, life, ant, loop, rule30, liquid\n");
    remote_log("space selects the next pattern. up and down change brightness by 5%.");
    show_status();

    uint32_t switch_at = 0;
    uint32_t meter_at = 0;
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
        advance_hue(now, dt);
        remote_poll();

        Command command = Command::None;
        for (;;) {
            int ch = read_key();
            if (ch < 0) {
                break;
            }
            Command next = feed_key(ch);
            if (next != Command::None) {
                command = next;
            }
        }
        if (command != Command::None && (int32_t)(now - switch_at) >= 0) {
            if (command == Command::Next) {
                change_pattern(1);
            } else if (command == Command::Brighter) {
                change_brightness(BRIGHTNESS_STEP, true);
            } else if (command == Command::Dimmer) {
                change_brightness(-BRIGHTNESS_STEP, true);
            }
            switch_at = now + SWITCH_GAP_MS;
        }

        for (int i = 0; i < BUTTON_COUNT; ++i) {
            poll_button(buttons[i], now);
        }

        patterns[settings.pattern].draw(frame, anim_ms, dt);
        apply_hue(frame, settings.hue);
        update_bgr(frame);

        if (on_environment() && (int32_t)(now - meter_at) >= 0) {
            refresh_oled();
            meter_at = now + 150;
        }

        int spent = (int)(absolute_time_diff_us(started, get_absolute_time()) / 1000);
        if (spent < FRAME_MS) {
            sleep_ms(FRAME_MS - spent);
        }
    }
}
