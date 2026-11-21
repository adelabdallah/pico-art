// Firmware for a Waveshare P2.5 64x64 HUB75 panel on a Pico.
//
// Each pattern lives in its own directory and paints the same framebuffer.
// The first encoder click waits. When the wait ends, the current name flashes and the pattern list scrolls.
// A click while that list is open selects the name.
// A second click during the wait jumps to settings. Another double click returns to the pattern that was showing.
// That return lasts until power-off. A turn during the wait opens the list and takes the turn.
// A step shows the next name at once, even in the middle of that flash.
// The list ends on RESET?. A click there asks ARE YOU SURE?. Another click clears every pattern's mirrors, slide, turn, and the hue.
// Outside that mode, the encoder flings the picture sideways, or edits the open setting.
// Settings is the last pattern. There, blue picks a value and the encoder raises or lowers it.
// The pattern and the settings, including the double-click window, are stored in the last flash sector and restored on boot.
// Pause, speed, and the pattern a double click leaves behind last until power-off. Each pattern keeps its own turn, sideways slide, and mirrors until then.
// Each pattern starts running at full speed.
//
// Wire the panel the way the Waveshare Pico example does.
// GP0 R1, GP1 G1, GP2 B1, GP3 R2, GP4 G2, GP5 B2.
// GP6 A, GP7 B, GP8 C, GP9 D, GP10 E.
// GP11 CLK, GP12 LAT, GP13 OE.
// Share ground with the panel. Feed the panel 5V from its own supply.
//
// GP14 SDA and GP15 SCL drive the 128 by 64 OLED.
// The yellow band names the pattern, or the setting while settings is open.
// Each pattern keeps its number on the right of that band. Plasma is 1. Settings and the reset prompts have none.
// The blue band draws slow waves that follow the mirrors, the turn, and a slide.
// On liquid it shows the mic and an equalizer.
// The OLED turns off after its timer. The default is 2 minutes. A button turns it back on.
// The dimmest contrast setting is 5 percent.
// Button C goes to the GPIO and NO goes to ground.
// Any button, the encoder click, and a turn of the encoder wake the matrix and the OLED.
// Red GP19 pauses. From a full stop it runs again at full speed.
// Orange GP21 adds a diagonal mirror to the current pattern. The sixth press clears that pattern's lines.
// Holding orange turns that new line about the centre until it is released.
// Yellow GP20 adds a round mirror. The eighth press clears that pattern's circles.
// Holding yellow grows that new circle up to 40 percent of the panel, then back to empty, and repeats until it is released.
// A line runs near a corner-to-corner diagonal and misses the centre. The smaller side shows the other side reflected.
// A new circle covers 1 to 10 percent of the panel. Its inside shows the pixels outside the rim, folded in.
// At double speed the picture pulses, then falls off the bottom and fades back in at full speed.
// Green GP26 turns 5 degrees clockwise. A hold spins it.
// Blue GP22 shifts the hue by a fifth of a turn, and a hold spins it, except on settings.
// On liquid, yellow starts and stops the microphone. Listening starts on. Orange still adds a diagonal there.
// On settings, blue moves to the next value. Elsewhere blue shifts the hue. Orange and yellow stay quiet there.
// On solar, the encoder walks from the wide view through each world and back. Green flings that world's spin, or the whole orrery on the wide view. A hold keeps the spin going.
// Orange on a world returns to the orrery. Orange on the orrery pulls back over about two seconds to the galaxy, and the pattern changes when the picture arrives. A second press during the flight turns it around.
// Yellow tours the orrery and each world, holding each for 5 seconds, with the spin held and the view tumbling. Another press stops the tour. A play mark bobs on the OLED while it runs.
// On skyline, orange moves the view up and yellow moves it down. The encoder moves the view left and right, and the view stops at the edge of the harbour.
// On lighthouse, ferris wheel, and galaxy, the encoder changes how fast the beam, the wheel, and the disk move. Clockwise is faster. The sea, the rim lights, and the stars keep their own pace.
// On lighthouse, yellow strikes lightning and orange thickens the rain. The rain rises for a second, holds for a second, and falls for a second. Holding orange keeps the rain thick.
// On campfire, orange throws sparks and yellow flares the flame. Holding either keeps that going.
// Green sends one comet across the milky way. A hold keeps a meteor shower along that band, and letting go thins new meteors out over 3 seconds. The picture stays upright.
// On fireworks, orange launches a small shell and yellow a large one. Shells still launch on their own, at half the earlier pace.
// On galaxy, orange flies in to the solar orrery along the same path. The solar system sits on the outer tip of one arm.
// On galaxy, tipping the panel tilts the disk. Flat and face up is the resting oval. A steeper tip draws it thinner.
// On the wide view, tipping the panel looks around the hoops. Flat and face up is the resting view.
// The mirrors clear on reboot.
// Encoder A is GP16, B is GP17, and the click is GP18. Each goes to ground. Clockwise is A changing before B.
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
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hub75.hpp"
#include "oled.hpp"
#include "pattern.hpp"

constexpr int FRAME_MS = 40;
constexpr int FLIGHT_MS = 2000;
constexpr float CLOSE_SCALE = 18.0f;
constexpr float PANEL_CX = (PANEL_WIDTH - 1) * 0.5f;
constexpr float PANEL_CY = (PANEL_HEIGHT - 1) * 0.5f;
constexpr int DT_CAP_MS = 200;
constexpr int HOLD_MS = 320;
constexpr int MIRROR_LINE_CAP = 5;
constexpr int MIRROR_CIRCLE_CAP = 7;
constexpr float MIRROR_INSET_MIN = 4.0f;
constexpr float MIRROR_INSET_MAX = 18.0f;
constexpr float MIRROR_CENTRE_GAP = 6.0f;
constexpr float MIRROR_AREA_MIN = 0.01f;
constexpr float MIRROR_AREA_MAX = 0.10f;
constexpr int MIRROR_LINE_TRIES = 16;

constexpr int PANEL_BASIS = 8;
constexpr int BRIGHTNESS_PERCENT = 55;
constexpr int BRIGHTNESS_STEP = 5;

constexpr float HUE_TAP = 0.2f;
constexpr int HUE_TAP_MS = 300;
constexpr float HUE_SPIN = 1.0f;
constexpr int PAUSE_MS = 2000;
constexpr float TURN_STEP = 1.5707963f;
constexpr float TAP_TURN = 5.0f * 0.017453292f;
constexpr int TAP_TURN_MS = 140;
constexpr float SPIN_PER_MS = 6.2831853f / 2000.0f;
constexpr int OVERDRIVE_MS = 1000;
constexpr int BURST_FALL_MS = 900;
constexpr int BURST_FADE_MS = 700;
constexpr float SHIFT_IMPULSE = 60.0f;
constexpr float SHIFT_DECAY = 3.5f;
constexpr float SHIFT_CAP = 800.0f;

// Minutes. A zero LED entry means the matrix stays lit. The OLED list has no forever entry.
constexpr int LED_MINUTES[] = {1, 2, 5, 10, 15, 30, 60, 0};
constexpr int LED_TIMER_COUNT = 8;
constexpr int LED_TIMER_DEFAULT = 7;
constexpr int OLED_MINUTES[] = {1, 2, 5, 10, 15, 30, 60};
constexpr int OLED_TIMER_COUNT = 7;
constexpr int OLED_TIMER_DEFAULT = 1;
constexpr float LINE_TURN_PER_MS = 6.2831853f / 8000.0f;
constexpr float CIRCLE_GROW_MS = 4000.0f;
constexpr float CIRCLE_AREA_CAP = 0.40f;
constexpr int SETTING_COUNT = 8;
constexpr int OLED_BRIGHT_MIN = 5;
constexpr int ENC_CHOICES[] = {1, 2, 4, 8};
constexpr int ENC_CHOICE_COUNT = 4;
constexpr int DBL_MIN_MS = 200;
constexpr int DBL_MAX_MS = 800;
constexpr int DBL_STEP_MS = 50;
constexpr int DBL_DEFAULT_MS = 400;
constexpr int DBL_CHOICE_COUNT = (DBL_MAX_MS - DBL_MIN_MS) / DBL_STEP_MS + 1;

constexpr int PIN_RED = 19;
constexpr int PIN_YELLOW = 20;
constexpr int PIN_ORANGE = 21;
constexpr int PIN_BLUE = 22;
constexpr int PIN_GREEN = 26;
constexpr int PIN_ENC_A = 16;
constexpr int PIN_ENC_B = 17;
constexpr int PIN_ENC_CLICK = 18;
constexpr int ENC_DETENT = 4;
// Presses closer than this are the same click bouncing.
constexpr int CLICK_GAP_MS = 40;

// One mechanical detent is four quadrature edges. Positive counts are clockwise.
constexpr int8_t ENC_TABLE[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
};

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
    {"flow field", flow_start, flow_draw},
    {"voronoi cells", voronoi_start, voronoi_draw},
    {"loop", loop_start, loop_draw},
    {"liquid", environment_start, environment_draw},
    {"pipes", pipes_start, pipes_draw},
    {"reef", reef_start, reef_draw},
    {"solar", solar_start, solar_draw},
    {"campfire", campfire_start, campfire_draw},
    {"skyline", skyline_start, skyline_draw},
    {"lighthouse", lighthouse_start, lighthouse_draw},
    {"ferris wheel", ferris_start, ferris_draw},
    {"aurora", aurora_start, aurora_draw},
    {"fireworks", fireworks_start, fireworks_draw},
    {"galaxy", galaxy_start, galaxy_draw},
    {"settings", settings_start, settings_draw},
};

constexpr int PATTERN_COUNT = (int)(sizeof patterns / sizeof patterns[0]);

// Last sector. A blank sector is erased flash, so the magic word is absent and boot stays on plasma.
constexpr uint32_t PATTERN_MAGIC = 0x6d617458;
constexpr uint32_t PATTERN_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

// version 0 is a record from before the settings page. Those boots keep pattern, brightness, and hue.
// Version 5 predates the double-click window. Those boots keep the default window.
constexpr uint32_t PATTERN_VERSION = 6;

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
    uint32_t enc_sens;
    uint32_t dbl_ms;
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
    int enc;
    int dbl_ms;
    int cursor;
};

enum class ButtonRole {
    Pause,
    Line,
    Circle,
    Spin,
    Hue,
};

enum class BurstPhase {
    Idle,
    Fall,
    Fade,
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
    {PIN_RED, "red", ButtonRole::Pause, false, false, 0, 0},
    {PIN_ORANGE, "orange", ButtonRole::Line, false, false, 0, 0},
    {PIN_YELLOW, "yellow", ButtonRole::Circle, false, false, 0, 0},
    {PIN_GREEN, "green", ButtonRole::Spin, false, false, 0, 0},
    {PIN_BLUE, "blue", ButtonRole::Hue, false, false, 0, 0},
};

constexpr int BUTTON_COUNT = (int)(sizeof buttons / sizeof buttons[0]);

static SettingRow setting_rows[] = {
    {"BRIGHT:", OledIcon::Sun},
    {"OLED:", OledIcon::Sun},
    {"MIC SENS:", OledIcon::Mic},
    {"IMU SENS:", OledIcon::Axes},
    {"ENC SENS:", OledIcon::Knob},
    {"DBL CLICK:", OledIcon::Knob},
    {"LED TIMER:", OledIcon::Hourglass},
    {"OLED TIMER:", OledIcon::Hourglass},
};

static Settings settings = {0, BRIGHTNESS_PERCENT, 0.0f, 50, 50, 100, LED_TIMER_DEFAULT, OLED_TIMER_DEFAULT, 1, DBL_DEFAULT_MS, 0};
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
static bool view_spinning = false;
static float play_speed = 1.0f;
static bool overdrive = false;
static uint32_t overdrive_at = 0;
static BurstPhase burst = BurstPhase::Idle;
static uint32_t burst_at = 0;
static float shift_x = 0.0f;
static float shift_v = 0.0f;
// 0 is the solar orrery filling the view. 1 is the galaxy, with the system on an arm.
static float flight = 0.0f;
static int flight_dir = 0;
static bool selecting = false;
static bool select_moved = false;
static bool oled_now = false;
static int preview = 0;
static int nav_rest = 0;
static int enc_rest = 0;
static volatile int enc_counts = 0;
static volatile int enc_state = 0;
static bool click_down = false;
static uint32_t click_quiet = 0;
// The first click is waiting for a second click inside the double-click window.
static bool click_wait = false;
static uint32_t click_at = 0;
// Where a double click into settings returns. A later pattern change drops it. Boot starts with none.
static int return_pattern = -1;

static bool hue_tap = false;
static bool hue_spin = false;
static uint32_t hue_tap_at = 0;
static float hue_tap_from = 0.0f;
static float hue_tap_to = 0.0f;

struct MirrorLine {
    float ax;
    float ay;
    float dx;
    float dy;
    float len2;
    bool copy_positive;
};

struct MirrorCircle {
    float cx;
    float cy;
    float radius;
    float home_x;
    float home_y;
};

struct PatternMirrors {
    int lines;
    int circles;
    MirrorLine line[MIRROR_LINE_CAP];
    MirrorCircle circle[MIRROR_CIRCLE_CAP];
};

// Slide and turn belong to one pattern. A switch puts that pattern's own view back.
struct PatternView {
    float shift_x;
    float shift_v;
    float angle;
    float target;
};

static PatternMirrors mirrors[PATTERN_COUNT];
static PatternView views[PATTERN_COUNT];
static uint32_t mirror_state = 0;
static int held_line_pattern = -1;
static int held_line_index = -1;
static int held_circle_pattern = -1;
static int held_circle_index = -1;
static bool line_turning = false;
static bool circle_growing = false;
static bool reset_confirm = false;

constexpr int RESET_SLOT = PATTERN_COUNT;
constexpr int LIST_COUNT = PATTERN_COUNT + 1;

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
    // Version 2 saved the settings pattern at index 13. That pattern is the last entry now.
    if (saved->version == 2 && saved->index == 13) {
        loaded.pattern = PATTERN_COUNT - 1;
    } else if (saved->version == 2 || saved->version == 3) {
        // Ant was index 6 and rule 30 was index 8. Loop was index 7 and is index 8 now.
        if (saved->index == 6 || saved->index == 8) {
            loaded.pattern = 5;
        } else         if (saved->index == 7) {
            loaded.pattern = 8;
        }
    }
    // Versions 3 and 4 kept wireframe at index 19, galaxy at 20, and settings at 21.
    // Wireframe opens galaxy. Galaxy and settings each move down by one.
    if ((saved->version == 3 || saved->version == 4) && saved->index > 19) {
        loaded.pattern = (int)saved->index - 1;
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
    // Version 0 predates the settings page. Later fields stay at their defaults.
    if (saved->version == 0) {
        return loaded;
    }
    if (saved->version != 1 && saved->version != 2 && saved->version != 3 && saved->version != 4 && saved->version != 5 && saved->version != PATTERN_VERSION) {
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
    // Version 1 stored the old 5 minute default at index 2. That choice becomes 2 minutes.
    if (saved->version == 1 && loaded.oled_timer == 2) {
        loaded.oled_timer = OLED_TIMER_DEFAULT;
    }
    if (saved->enc_sens == 1 || saved->enc_sens == 2 || saved->enc_sens == 4 || saved->enc_sens == 8) {
        loaded.enc = (int)saved->enc_sens;
    }
    if (saved->version >= 6) {
        int window = (int)saved->dbl_ms;
        if (window >= DBL_MIN_MS && window <= DBL_MAX_MS && (window - DBL_MIN_MS) % DBL_STEP_MS == 0) {
            loaded.dbl_ms = window;
        }
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
    saved->enc_sens = (uint32_t)incoming->enc;
    saved->dbl_ms = (uint32_t)incoming->dbl_ms;
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

static bool on_solar() {
    return std::strcmp(patterns[settings.pattern].name, "solar") == 0;
}

static bool on_skyline() {
    return std::strcmp(patterns[settings.pattern].name, "skyline") == 0;
}

static bool on_lighthouse() {
    return std::strcmp(patterns[settings.pattern].name, "lighthouse") == 0;
}

static bool on_ferris() {
    return std::strcmp(patterns[settings.pattern].name, "ferris wheel") == 0;
}

static bool on_galaxy() {
    return std::strcmp(patterns[settings.pattern].name, "galaxy") == 0;
}

static bool on_campfire() {
    return std::strcmp(patterns[settings.pattern].name, "campfire") == 0;
}

static bool on_fireworks() {
    return std::strcmp(patterns[settings.pattern].name, "fireworks") == 0;
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
    view_target += TAP_TURN;
}

static void update_turn(int dt_ms) {
    if (view_spinning) {
        view_angle += SPIN_PER_MS * dt_ms;
        view_target = view_angle;
    } else {
        float gap = view_target - view_angle;
        float step = TAP_TURN * dt_ms / (float)TAP_TURN_MS;
        if (gap > step) {
            view_angle += step;
        } else if (gap < -step) {
            view_angle -= step;
        } else {
            view_angle = view_target;
        }
    }
    constexpr float circle = 6.2831853f;
    if (view_angle > circle * 4.0f) {
        view_angle -= circle * 4.0f;
        view_target -= circle * 4.0f;
    }
}

static void update_shift(int dt_ms) {
    float seconds = dt_ms / 1000.0f;
    shift_x += shift_v * seconds;
    shift_v *= expf(-SHIFT_DECAY * seconds);
    if (shift_v < 2.0f && shift_v > -2.0f) {
        shift_v = 0.0f;
    }
    shift_x = shift_x - (float)PANEL_WIDTH * floorf(shift_x / (float)PANEL_WIDTH);
    if (shift_x < 0.0f) {
        shift_x += PANEL_WIDTH;
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
    bool browsing = selecting && (select_moved || !on_settings());
    if (on_settings() && !browsing) {
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
            snprintf(value, sizeof value, "%d", settings.enc);
            int choice = 0;
            for (int i = 0; i < ENC_CHOICE_COUNT; ++i) {
                if (ENC_CHOICES[i] == settings.enc) {
                    choice = i;
                }
            }
            bar = (choice + 1) / (float)ENC_CHOICE_COUNT;
        } else if (cursor == 5) {
            snprintf(value, sizeof value, "%dms", settings.dbl_ms);
            int choice = (settings.dbl_ms - DBL_MIN_MS) / DBL_STEP_MS;
            bar = (choice + 1) / (float)DBL_CHOICE_COUNT;
        } else if (cursor == 6) {
            format_timer(value, (int)sizeof value, LED_MINUTES[settings.led_timer]);
            bar = (settings.led_timer + 1) / (float)LED_TIMER_COUNT;
        } else {
            format_timer(value, (int)sizeof value, OLED_MINUTES[settings.oled_timer]);
            bar = (settings.oled_timer + 1) / (float)OLED_TIMER_COUNT;
        }
        oled_show_setting(setting_rows[cursor].label, setting_rows[cursor].icon, value, bar, now, selecting);
        return;
    }
    char title[16];
    int number = -1;
    if (reset_confirm) {
        format_title(title, (int)sizeof title, "ARE YOU SURE?");
    } else if (browsing && preview == RESET_SLOT) {
        format_title(title, (int)sizeof title, "RESET?");
    } else {
        int shown = browsing ? preview : settings.pattern;
        format_title(title, (int)sizeof title, patterns[shown].name);
        if (std::strcmp(patterns[shown].name, "settings") != 0) {
            number = shown + 1;
        }
    }
    if (on_environment() && !reset_confirm && (!browsing || preview == settings.pattern)) {
        char listen[8];
        float bands[8];
        snprintf(listen, sizeof listen, environment_listening() ? "MIC ON" : "MIC OFF");
        environment_bands(bands, 8);
        oled_show_liquid(title, listen, bands, 8, now, browsing, number);
        return;
    }
    float speed = shift_v;
    if (speed < 0.0f) {
        speed = -speed;
    }
    OledScene scene;
    scene.lines = mirrors[settings.pattern].lines;
    scene.circles = mirrors[settings.pattern].circles;
    scene.turn = view_angle;
    scene.slide = speed / (speed + 80.0f);
    bool play = on_solar() && !browsing && !reset_confirm && solar_touring();
    oled_show_pattern(title, now, nullptr, 0, browsing || reset_confirm, scene, number, play);
}

static void show_status() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    note_activity(now);
    present_oled(now);
}

static void reset_play() {
    play_speed = 1.0f;
    overdrive = false;
    burst = BurstPhase::Idle;
    clear_pause();
}

static void store_view(int index) {
    PatternView &view = views[index];
    view.shift_x = shift_x;
    view.shift_v = shift_v;
    view.angle = view_angle;
    view.target = view_target;
}

static void load_view(int index) {
    const PatternView &view = views[index];
    shift_x = view.shift_x;
    shift_v = view.shift_v;
    view_angle = view.angle;
    view_target = view.target;
}

static void commit_pattern(int index) {
    flight_dir = 0;
    if (index < 0) {
        index = PATTERN_COUNT - 1;
    } else if (index >= PATTERN_COUNT) {
        index = 0;
    }
    bool changed = index != settings.pattern;
    if (changed) {
        store_view(settings.pattern);
        load_view(index);
    }
    settings.pattern = index;
    anim_ms = 0;
    anim_frac = 0.0f;
    reset_play();
    patterns[settings.pattern].start();
    if (on_campfire()) {
        view_angle = 0.0f;
        view_target = 0.0f;
        view_spinning = false;
    }
    return_pattern = -1;
    show_status();
    if (changed) {
        save_settings(settings);
    }
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
        int choice = 0;
        for (int i = 0; i < ENC_CHOICE_COUNT; ++i) {
            if (ENC_CHOICES[i] == settings.enc) {
                choice = i;
            }
        }
        int next = choice + direction;
        if (next < 0 || next >= ENC_CHOICE_COUNT) {
            return false;
        }
        settings.enc = ENC_CHOICES[next];
        nav_rest = 0;
    } else if (cursor == 5) {
        int next = settings.dbl_ms + direction * DBL_STEP_MS;
        if (next < DBL_MIN_MS || next > DBL_MAX_MS) {
            return false;
        }
        settings.dbl_ms = next;
    } else if (cursor == 6) {
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

constexpr float SEAM = 8.0f;

static float wrap_coord(float value, int limit) {
    float span = (float)limit;
    value = value - span * floorf(value / span);
    if (value < 0.0f) {
        value += span;
    }
    if (value >= span) {
        value -= span;
    }
    return value;
}

static void sample_bilinear(const uint8_t *src, float x, float y, float *out) {
    x = wrap_coord(x, PANEL_WIDTH);
    y = wrap_coord(y, PANEL_HEIGHT);
    int x0 = (int)x;
    int y0 = (int)y;
    if (x0 >= PANEL_WIDTH) {
        x0 = 0;
    }
    if (y0 >= PANEL_HEIGHT) {
        y0 = 0;
    }
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    if (x1 >= PANEL_WIDTH) {
        x1 = 0;
    }
    if (y1 >= PANEL_HEIGHT) {
        y1 = 0;
    }
    float tx = x - floorf(x);
    float ty = y - floorf(y);
    const uint8_t *p00 = src + (y0 * PANEL_WIDTH + x0) * 3;
    const uint8_t *p10 = src + (y0 * PANEL_WIDTH + x1) * 3;
    const uint8_t *p01 = src + (y1 * PANEL_WIDTH + x0) * 3;
    const uint8_t *p11 = src + (y1 * PANEL_WIDTH + x1) * 3;
    for (int channel = 0; channel < 3; ++channel) {
        float value = p00[channel] * (1.0f - tx) * (1.0f - ty) + p10[channel] * tx * (1.0f - ty);
        value += p01[channel] * (1.0f - tx) * ty + p11[channel] * tx * ty;
        out[channel] = value;
    }
}

// 0 on the frame edge, 1 once SEAM pixels inward. The opposite edge fades in across that band.
static float seam_keep(float coord, int limit) {
    float span = (float)limit;
    float dist = coord;
    float far = span - coord;
    if (far < dist) {
        dist = far;
    }
    if (dist >= SEAM) {
        return 1.0f;
    }
    float t = dist / SEAM;
    return t * t * (3.0f - 2.0f * t);
}

static void sample_plane(const uint8_t *src, float x, float y, uint8_t *dst, bool vertical, bool horizontal) {
    x = wrap_coord(x, PANEL_WIDTH);
    y = wrap_coord(y, PANEL_HEIGHT);
    float base[3];
    sample_bilinear(src, x, y, base);
    if (vertical) {
        float keep = seam_keep(x, PANEL_WIDTH);
        if (keep < 0.999f) {
            float across[3];
            sample_bilinear(src, (float)PANEL_WIDTH - x, y, across);
            for (int channel = 0; channel < 3; ++channel) {
                base[channel] = base[channel] * keep + across[channel] * (1.0f - keep);
            }
        }
    }
    if (horizontal) {
        float keep = seam_keep(y, PANEL_HEIGHT);
        if (keep < 0.999f) {
            float across[3];
            sample_bilinear(src, x, (float)PANEL_HEIGHT - y, across);
            for (int channel = 0; channel < 3; ++channel) {
                base[channel] = base[channel] * keep + across[channel] * (1.0f - keep);
            }
        }
    }
    for (int channel = 0; channel < 3; ++channel) {
        int value = (int)(base[channel] + 0.5f);
        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        dst[channel] = (uint8_t)value;
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
            sample_plane(pixels, sx, sy, turned + (y * PANEL_WIDTH + x) * 3, true, true);
        }
    }
    std::memcpy(pixels, turned, sizeof turned);
}

static uint8_t burst_snap[PANEL_PIXELS * 3];

static float pulse_gain(uint32_t now) {
    float wave = 0.5f + 0.5f * sinf(now * 6.2831853f / 3000.0f);
    return 0.35f + 0.65f * wave;
}

static void apply_gain(uint8_t *pixels, float gain) {
    if (gain > 0.995f && gain < 1.005f) {
        return;
    }
    if (gain < 0.0f) {
        gain = 0.0f;
    }
    for (int i = 0; i < PANEL_PIXELS * 3; ++i) {
        int value = (int)(pixels[i] * gain + 0.5f);
        if (value > 255) {
            value = 255;
        }
        pixels[i] = (uint8_t)value;
    }
}

static void paint_fall(uint8_t *pixels, int drop) {
    std::memset(pixels, 0, PANEL_PIXELS * 3);
    if (drop < 0) {
        drop = 0;
    }
    if (drop >= PANEL_HEIGHT) {
        return;
    }
    int rows = PANEL_HEIGHT - drop;
    std::memcpy(pixels + drop * PANEL_WIDTH * 3, burst_snap, (size_t)rows * PANEL_WIDTH * 3);
}

static void begin_burst(uint8_t *pixels, uint32_t now) {
    std::memcpy(burst_snap, pixels, sizeof burst_snap);
    burst = BurstPhase::Fall;
    burst_at = now;
    play_speed = 1.0f;
    overdrive = false;
    clear_pause();
}

static void present_frame(uint8_t *pixels, uint32_t now) {
    if (overdrive && burst == BurstPhase::Idle && (int32_t)(now - overdrive_at) >= OVERDRIVE_MS) {
        begin_burst(pixels, now);
    }
    if (burst == BurstPhase::Fall) {
        float portion = (float)(now - burst_at) / (float)BURST_FALL_MS;
        if (portion >= 1.0f) {
            burst = BurstPhase::Fade;
            burst_at = now;
        } else {
            if (portion < 0.12f) {
                std::memcpy(pixels, burst_snap, sizeof burst_snap);
                apply_gain(pixels, 1.15f);
                return;
            }
            float fall = (portion - 0.12f) / 0.88f;
            paint_fall(pixels, (int)(fall * (PANEL_HEIGHT + 6)));
            return;
        }
    }
    if (burst == BurstPhase::Fade) {
        float portion = (float)(now - burst_at) / (float)BURST_FADE_MS;
        if (portion >= 1.0f) {
            burst = BurstPhase::Idle;
            return;
        }
        apply_gain(pixels, portion);
        return;
    }
    if (motion < 0.999f) {
        apply_gain(pixels, motion + (1.0f - motion) * pulse_gain(now));
        return;
    }
    if (overdrive) {
        apply_gain(pixels, pulse_gain(now));
    }
}

static void translate_view(uint8_t *pixels) {
    if (shift_x < 0.04f || shift_x > PANEL_WIDTH - 0.04f) {
        return;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            sample_plane(pixels, x - shift_x, (float)y, turned + (y * PANEL_WIDTH + x) * 3, true, false);
        }
    }
    std::memcpy(pixels, turned, sizeof turned);
}

static uint32_t mirror_next() {
    if (mirror_state == 0) {
        mirror_state = time_us_32() | 1u;
    }
    uint32_t value = mirror_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    if (value == 0) {
        value = 1;
    }
    mirror_state = value;
    return value;
}

static float mirror_unit() {
    return (mirror_next() >> 8) * (1.0f / 16777216.0f);
}

static float mirror_range(float low, float high) {
    return low + (high - low) * mirror_unit();
}

static float line_centre_distance(float ax, float ay, float dx, float dy) {
    float mid = (PANEL_WIDTH - 1) * 0.5f;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        return 0.0f;
    }
    float cross = dx * (mid - ay) - dy * (mid - ax);
    if (cross < 0.0f) {
        cross = -cross;
    }
    return cross / len;
}

static bool smaller_side_positive(float ax, float ay, float dx, float dy) {
    int positive = 0;
    int negative = 0;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        float y_part = dx * ((float)y - ay);
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float side = y_part - dy * ((float)x - ax);
            if (side > 0.0f) {
                positive += 1;
            } else if (side < 0.0f) {
                negative += 1;
            }
        }
    }
    return positive < negative;
}

static MirrorLine make_line(float ax, float ay, float bx, float by) {
    MirrorLine line;
    line.ax = ax;
    line.ay = ay;
    line.dx = bx - ax;
    line.dy = by - ay;
    line.len2 = line.dx * line.dx + line.dy * line.dy;
    line.copy_positive = smaller_side_positive(ax, ay, line.dx, line.dy);
    return line;
}

static MirrorLine random_line() {
    MirrorLine line = {};
    for (int attempt = 0; attempt < MIRROR_LINE_TRIES; ++attempt) {
        bool down_right = (mirror_next() & 1u) == 0;
        float ax;
        float ay;
        float bx;
        float by;
        if (down_right) {
            ax = mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
            ay = mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
            bx = (PANEL_WIDTH - 1) - mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
            by = (PANEL_HEIGHT - 1) - mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
        } else {
            ax = (PANEL_WIDTH - 1) - mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
            ay = mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
            bx = mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
            by = (PANEL_HEIGHT - 1) - mirror_range(MIRROR_INSET_MIN, MIRROR_INSET_MAX);
        }
        line = make_line(ax, ay, bx, by);
        if (line_centre_distance(line.ax, line.ay, line.dx, line.dy) >= MIRROR_CENTRE_GAP) {
            return line;
        }
    }
    float len = sqrtf(line.len2);
    if (len < 1.0f) {
        return line;
    }
    float gap = line_centre_distance(line.ax, line.ay, line.dx, line.dy);
    float push = MIRROR_CENTRE_GAP - gap + 1.0f;
    if ((mirror_next() & 1u) == 0) {
        push = -push;
    }
    line.ax += (-line.dy / len) * push;
    line.ay += (line.dx / len) * push;
    line.copy_positive = smaller_side_positive(line.ax, line.ay, line.dx, line.dy);
    return line;
}

static int add_line_mirror() {
    PatternMirrors &set = mirrors[settings.pattern];
    if (set.lines >= MIRROR_LINE_CAP) {
        set.lines = 0;
        return -1;
    }
    int index = set.lines;
    set.line[index] = random_line();
    set.lines += 1;
    return index;
}

static void fit_circle(MirrorCircle &circle) {
    circle.cx = circle.home_x;
    circle.cy = circle.home_y;
    float lo = circle.radius;
    float hi = (float)(PANEL_WIDTH - 1) - circle.radius;
    if (hi < lo) {
        circle.cx = (PANEL_WIDTH - 1) * 0.5f;
        circle.cy = (PANEL_HEIGHT - 1) * 0.5f;
        return;
    }
    if (circle.cx < lo) {
        circle.cx = lo;
    } else if (circle.cx > hi) {
        circle.cx = hi;
    }
    if (circle.cy < lo) {
        circle.cy = lo;
    } else if (circle.cy > hi) {
        circle.cy = hi;
    }
}

static int add_circle_mirror() {
    PatternMirrors &set = mirrors[settings.pattern];
    if (set.circles >= MIRROR_CIRCLE_CAP) {
        set.circles = 0;
        return -1;
    }
    float fraction = mirror_range(MIRROR_AREA_MIN, MIRROR_AREA_MAX);
    float radius = sqrtf(fraction * (float)PANEL_PIXELS / 3.14159265f);
    float far = (PANEL_WIDTH - 1) - radius;
    MirrorCircle circle = {};
    circle.home_x = mirror_range(radius, far);
    circle.home_y = mirror_range(radius, far);
    circle.radius = radius;
    fit_circle(circle);
    int index = set.circles;
    set.circle[index] = circle;
    set.circles += 1;
    return index;
}

static void spin_line(MirrorLine &line, float angle) {
    float mid = (PANEL_WIDTH - 1) * 0.5f;
    float cosine = cosf(angle);
    float sine = sinf(angle);
    float bx = line.ax + line.dx;
    float by = line.ay + line.dy;
    float ax = mid + (line.ax - mid) * cosine - (line.ay - mid) * sine;
    float ay = mid + (line.ax - mid) * sine + (line.ay - mid) * cosine;
    float nbx = mid + (bx - mid) * cosine - (by - mid) * sine;
    float nby = mid + (bx - mid) * sine + (by - mid) * cosine;
    line = make_line(ax, ay, nbx, nby);
}

static void grow_circle(MirrorCircle &circle, int dt_ms) {
    float area = circle.radius * circle.radius * 3.14159265f / (float)PANEL_PIXELS;
    area += CIRCLE_AREA_CAP * (float)dt_ms / CIRCLE_GROW_MS;
    if (area >= CIRCLE_AREA_CAP) {
        area = 0.0f;
    }
    circle.radius = sqrtf(area * (float)PANEL_PIXELS / 3.14159265f);
    fit_circle(circle);
}

static void update_mirror_motion(int dt_ms) {
    if (line_turning && held_line_pattern >= 0 && held_line_pattern < PATTERN_COUNT) {
        PatternMirrors &set = mirrors[held_line_pattern];
        if (held_line_index >= 0 && held_line_index < set.lines) {
            spin_line(set.line[held_line_index], LINE_TURN_PER_MS * dt_ms);
        } else {
            line_turning = false;
        }
    }
    if (circle_growing && held_circle_pattern >= 0 && held_circle_pattern < PATTERN_COUNT) {
        PatternMirrors &set = mirrors[held_circle_pattern];
        if (held_circle_index >= 0 && held_circle_index < set.circles) {
            grow_circle(set.circle[held_circle_index], dt_ms);
        } else {
            circle_growing = false;
        }
    }
}

static void clear_view() {
    for (int i = 0; i < PATTERN_COUNT; ++i) {
        mirrors[i].lines = 0;
        mirrors[i].circles = 0;
    }
    line_turning = false;
    circle_growing = false;
    held_line_index = -1;
    held_circle_index = -1;
    for (int i = 0; i < PATTERN_COUNT; ++i) {
        views[i] = {};
    }
    shift_x = 0.0f;
    shift_v = 0.0f;
    view_angle = 0.0f;
    view_target = 0.0f;
    view_spinning = false;
    hue_spin = false;
    hue_tap = false;
    settings.hue = 0.0f;
    save_settings(settings);
}

static void reflect_line(const MirrorLine &line, float &x, float &y) {
    if (line.len2 < 1.0f) {
        return;
    }
    float side = line.dx * (y - line.ay) - line.dy * (x - line.ax);
    bool copy = line.copy_positive ? side > 0.0f : side < 0.0f;
    if (!copy) {
        return;
    }
    float vx = x - line.ax;
    float vy = y - line.ay;
    float t = (vx * line.dx + vy * line.dy) / line.len2;
    float near_x = line.ax + t * line.dx;
    float near_y = line.ay + t * line.dy;
    x = near_x + near_x - x;
    y = near_y + near_y - y;
}

static void reflect_circle(const MirrorCircle &circle, float &x, float &y) {
    float dx = x - circle.cx;
    float dy = y - circle.cy;
    float dist2 = dx * dx + dy * dy;
    float radius2 = circle.radius * circle.radius;
    if (dist2 >= radius2 || dist2 < 0.01f) {
        return;
    }
    float dist = sqrtf(dist2);
    float scale = (circle.radius + circle.radius - dist) / dist;
    x = circle.cx + dx * scale;
    y = circle.cy + dy * scale;
}

// The turn and the slide run first, so the pattern moves through mirrors that stay put.
static void apply_mirrors(uint8_t *pixels) {
    const PatternMirrors &set = mirrors[settings.pattern];
    if (set.lines == 0 && set.circles == 0) {
        return;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float sx = (float)x;
            float sy = (float)y;
            for (int i = set.circles - 1; i >= 0; --i) {
                reflect_circle(set.circle[i], sx, sy);
            }
            for (int i = set.lines - 1; i >= 0; --i) {
                reflect_line(set.line[i], sx, sy);
            }
            sample_plane(pixels, sx, sy, turned + (y * PANEL_WIDTH + x) * 3, false, false);
        }
    }
    std::memcpy(pixels, turned, sizeof turned);
}

static void enc_isr(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
    int state = (gpio_get(PIN_ENC_A) << 1) | gpio_get(PIN_ENC_B);
    int previous = enc_state;
    enc_counts += ENC_TABLE[(previous << 2) | state];
    enc_state = state;
}

static int take_detents() {
    uint32_t irq = save_and_disable_interrupts();
    int counts = enc_counts;
    enc_counts = 0;
    restore_interrupts(irq);
    enc_rest += counts;
    int detents;
    if (enc_rest >= 0) {
        detents = enc_rest / ENC_DETENT;
    } else {
        detents = -((-enc_rest) / ENC_DETENT);
    }
    enc_rest -= detents * ENC_DETENT;
    // This encoder's A and B run opposite the table, so clockwise arrives negative.
    return -detents;
}

static int consume_nav(int detents) {
    nav_rest += detents;
    int sens = settings.enc;
    if (sens < 1) {
        sens = 1;
    }
    int steps;
    if (nav_rest >= 0) {
        steps = nav_rest / sens;
    } else {
        steps = -((-nav_rest) / sens);
    }
    nav_rest -= steps * sens;
    return steps;
}

static void setup_encoder() {
    gpio_init(PIN_ENC_A);
    gpio_init(PIN_ENC_B);
    gpio_init(PIN_ENC_CLICK);
    gpio_set_dir(PIN_ENC_A, GPIO_IN);
    gpio_set_dir(PIN_ENC_B, GPIO_IN);
    gpio_set_dir(PIN_ENC_CLICK, GPIO_IN);
    gpio_pull_up(PIN_ENC_A);
    gpio_pull_up(PIN_ENC_B);
    gpio_pull_up(PIN_ENC_CLICK);
    enc_state = (gpio_get(PIN_ENC_A) << 1) | gpio_get(PIN_ENC_B);
    gpio_set_irq_enabled_with_callback(PIN_ENC_A, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, enc_isr);
    gpio_set_irq_enabled(PIN_ENC_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

static void cosmos_orange() {
    if (flight_dir != 0) {
        flight_dir = -flight_dir;
        return;
    }
    bool tour = solar_touring();
    if (on_solar()) {
        if (!solar_orrery_ready()) {
            solar_show_orrery();
            solar_tour_stop();
            if (tour) {
                show_status();
            }
            return;
        }
        solar_tour_stop();
        galaxy_ensure();
        flight = 0.0f;
        flight_dir = 1;
        if (tour) {
            show_status();
        }
        return;
    }
    solar_ensure();
    galaxy_ensure();
    flight = 1.0f;
    flight_dir = -1;
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
    if (button.role == ButtonRole::Pause) {
        if (pressed) {
            if (play_speed <= 0.001f) {
                play_speed = 1.0f;
                clear_pause();
            } else {
                toggle_pause(now);
            }
            show_status();
        }
        return;
    }
    if (on_environment() && button.role == ButtonRole::Circle) {
        if (pressed) {
            environment_toggle_listen();
            show_status();
        }
        return;
    }
    if (on_skyline() && (button.role == ButtonRole::Line || button.role == ButtonRole::Circle)) {
        int dir = button.role == ButtonRole::Line ? -1 : 1;
        if (pressed) {
            skyline_move(0, dir * 4);
        } else if (down && (int32_t)(now - button.pressed_at) >= HOLD_MS) {
            skyline_move(0, dir);
        }
        return;
    }
    if (on_lighthouse() && button.role == ButtonRole::Circle) {
        if (pressed) {
            lighthouse_strike();
        }
        return;
    }
    if (on_lighthouse() && button.role == ButtonRole::Line) {
        if (pressed) {
            lighthouse_rain_press();
        }
        lighthouse_rain_held(down);
        return;
    }
    if (on_campfire() && button.role == ButtonRole::Line) {
        if (pressed) {
            campfire_spark_burst();
        }
        campfire_shower(down);
        return;
    }
    if (on_campfire() && button.role == ButtonRole::Circle) {
        if (pressed) {
            campfire_flare_press();
        }
        campfire_flare_held(down);
        return;
    }
    if (on_campfire() && button.role == ButtonRole::Spin) {
        if (pressed) {
            campfire_meteor();
        }
        if (released) {
            campfire_meteor_held(false);
        } else if (down && (int32_t)(now - button.pressed_at) >= HOLD_MS) {
            campfire_meteor_held(true);
        }
        return;
    }
    if (on_fireworks() && button.role == ButtonRole::Line) {
        if (pressed) {
            fireworks_launch(false);
        }
        return;
    }
    if (on_fireworks() && button.role == ButtonRole::Circle) {
        if (pressed) {
            fireworks_launch(true);
        }
        return;
    }
    if (!selecting && button.role == ButtonRole::Line && (on_solar() || on_galaxy())) {
        if (pressed) {
            cosmos_orange();
        }
        return;
    }
    if (!selecting && on_solar() && button.role == ButtonRole::Circle) {
        if (pressed && flight_dir == 0) {
            solar_tour_toggle();
            show_status();
        }
        return;
    }
    if (button.role == ButtonRole::Line || button.role == ButtonRole::Circle) {
        if (pressed && on_settings()) {
            if (button.role == ButtonRole::Line) {
                line_turning = false;
                held_line_index = -1;
            } else {
                circle_growing = false;
                held_circle_index = -1;
            }
        } else if (pressed) {
            if (button.role == ButtonRole::Line) {
                line_turning = false;
                held_line_pattern = settings.pattern;
                held_line_index = add_line_mirror();
            } else {
                circle_growing = false;
                held_circle_pattern = settings.pattern;
                held_circle_index = add_circle_mirror();
            }
            show_status();
        }
        if (released) {
            if (button.role == ButtonRole::Line) {
                line_turning = false;
            } else {
                circle_growing = false;
            }
        } else if (down && (int32_t)(now - button.pressed_at) >= HOLD_MS) {
            if (button.role == ButtonRole::Line && held_line_index >= 0) {
                line_turning = true;
            } else if (button.role == ButtonRole::Circle && held_circle_index >= 0) {
                circle_growing = true;
            }
        }
        return;
    }
    if (button.role == ButtonRole::Spin) {
        if (on_solar() && !selecting) {
            if (pressed) {
                solar_spin_tap();
            }
            if (released) {
                solar_spin_release();
            } else if (down && (int32_t)(now - button.pressed_at) >= HOLD_MS) {
                solar_spin_hold();
            }
            return;
        }
        if (pressed) {
            nudge_turn();
        }
        if (released) {
            view_spinning = false;
        } else if (down && (int32_t)(now - button.pressed_at) >= HOLD_MS) {
            view_spinning = true;
        }
        return;
    }
    if (pressed && on_settings()) {
        settings.cursor = (settings.cursor + 1) % SETTING_COUNT;
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

static int find_pattern(const char *name) {
    for (int i = 0; i < PATTERN_COUNT; ++i) {
        if (std::strcmp(patterns[i].name, name) == 0) {
            return i;
        }
    }
    return 0;
}

static void handoff_pattern(int index) {
    if (index < 0 || index >= PATTERN_COUNT || index == settings.pattern) {
        show_status();
        return;
    }
    store_view(settings.pattern);
    load_view(index);
    settings.pattern = index;
    return_pattern = -1;
    show_status();
    save_settings(settings);
}

static void open_pattern_list() {
    flight_dir = 0;
    selecting = true;
    reset_confirm = false;
    preview = settings.pattern;
    select_moved = false;
    nav_rest = 0;
    show_status();
}

static void jump_settings() {
    if (on_settings()) {
        if (return_pattern < 0 || return_pattern >= PATTERN_COUNT - 1) {
            return;
        }
        commit_pattern(return_pattern);
        return;
    }
    int from = settings.pattern;
    commit_pattern(PATTERN_COUNT - 1);
    return_pattern = from;
}

static void poll_encoder(uint32_t now) {
    bool down = gpio_get(PIN_ENC_CLICK) == 0;
    bool pressed = down && !click_down;
    click_down = down;
    if (down) {
        note_activity(now);
    }
    bool accept = pressed && (int32_t)(now - click_quiet) >= 0;
    bool in_window = click_wait && (int32_t)(now - click_at) <= settings.dbl_ms;
    if (accept && !selecting && in_window) {
        click_quiet = now + CLICK_GAP_MS;
        wake_matrix();
        click_wait = false;
        jump_settings();
    } else {
        if (click_wait && (int32_t)(now - click_at) >= settings.dbl_ms) {
            click_wait = false;
            open_pattern_list();
        }
        if (accept) {
            click_quiet = now + CLICK_GAP_MS;
            wake_matrix();
            if (!selecting) {
                click_wait = true;
                click_at = now;
            } else if (reset_confirm) {
                selecting = false;
                reset_confirm = false;
                nav_rest = 0;
                clear_view();
                show_status();
            } else if (preview == RESET_SLOT) {
                reset_confirm = true;
                show_status();
            } else {
                selecting = false;
                nav_rest = 0;
                if (preview != settings.pattern) {
                    commit_pattern(preview);
                } else {
                    show_status();
                }
            }
        }
    }
    int detents = take_detents();
    if (detents == 0) {
        return;
    }
    note_activity(now);
    wake_matrix();
    if (click_wait) {
        click_wait = false;
        open_pattern_list();
    }
    if (selecting) {
        int steps = consume_nav(detents);
        if (steps != 0) {
            reset_confirm = false;
            preview += steps;
            preview %= LIST_COUNT;
            if (preview < 0) {
                preview += LIST_COUNT;
            }
            select_moved = true;
            oled_hold_title(now);
            oled_now = true;
        }
        return;
    }
    if (on_settings()) {
        int steps = consume_nav(detents);
        if (steps == 0) {
            return;
        }
        int direction = steps > 0 ? 1 : -1;
        int count = steps > 0 ? steps : -steps;
        bool changed = false;
        for (int i = 0; i < count; ++i) {
            if (nudge_setting(direction, false)) {
                changed = true;
            }
        }
        show_status();
        if (changed) {
            save_settings(settings);
        }
        return;
    }
    if (flight_dir != 0 && (on_solar() || on_galaxy())) {
        return;
    }
    if (on_solar()) {
        int steps = consume_nav(detents);
        if (steps != 0) {
            bool tour = solar_touring();
            solar_turn(steps);
            if (tour) {
                show_status();
            }
        }
        return;
    }
    if (on_skyline()) {
        int steps = consume_nav(detents);
        if (steps != 0) {
            skyline_move(steps * 8, 0);
        }
        return;
    }
    if (on_lighthouse()) {
        int steps = consume_nav(detents);
        if (steps != 0) {
            lighthouse_spin(steps);
        }
        return;
    }
    if (on_ferris()) {
        int steps = consume_nav(detents);
        if (steps != 0) {
            ferris_spin(steps);
        }
        return;
    }
    if (on_galaxy()) {
        int steps = consume_nav(detents);
        if (steps != 0) {
            galaxy_spin(steps);
        }
        return;
    }
    shift_v += detents * SHIFT_IMPULSE;
    if (shift_v > SHIFT_CAP) {
        shift_v = SHIFT_CAP;
    } else if (shift_v < -SHIFT_CAP) {
        shift_v = -SHIFT_CAP;
    }
}

static float flight_ease(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static void blend_channel(uint8_t &pixel, int sample, float alpha) {
    int value = (int)(pixel * (1.0f - alpha) + sample * alpha + 0.5f);
    if (value < 0) {
        value = 0;
    } else if (value > 255) {
        value = 255;
    }
    pixel = (uint8_t)value;
}

// One pullback. Ease 0 is the orrery filling the panel. Ease 1 is the galaxy, and the system sits on the arm.
static void paint_cosmos(uint8_t *pixels, uint32_t now_ms) {
    static uint8_t orrery[PANEL_PIXELS * 3];
    float tip_x;
    float tip_y;
    galaxy_arm_tip(tip_x, tip_y);
    float ease = flight_ease(flight);
    float scale = expf(logf(CLOSE_SCALE) * (1.0f - ease));
    float aim_x = PANEL_CX + (tip_x - PANEL_CX) * ease;
    float aim_y = PANEL_CY + (tip_y - PANEL_CY) * ease;
    float orrery_scale = scale / CLOSE_SCALE;
    if (orrery_scale < 0.001f) {
        orrery_scale = 0.001f;
    }
    float cover = 1.0f;
    if (ease > 0.90f) {
        cover = (1.0f - ease) / 0.10f;
    }
    solar_paint_wide(orrery, now_ms);
    galaxy_paint_at(pixels, now_ms, scale, tip_x, tip_y, aim_x, aim_y);
    if (cover <= 0.0f) {
        return;
    }
    float window = 46.0f * orrery_scale;
    float inv_orr = 1.0f / orrery_scale;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float dx = (x + 0.5f) - aim_x;
            float dy = (y + 0.5f) - aim_y;
            float dist = sqrtf(dx * dx + dy * dy);
            float edge = (window - dist) / 1.25f;
            if (edge < 0.0f) {
                edge = 0.0f;
            } else if (edge > 1.0f) {
                edge = 1.0f;
            }
            float alpha = edge * cover;
            if (alpha <= 0.0f) {
                continue;
            }
            float ox = PANEL_CX + dx * inv_orr;
            float oy = PANEL_CY + dy * inv_orr;
            int ix = (int)floorf(ox - 0.5f);
            int iy = (int)floorf(oy - 0.5f);
            int blue = 0;
            int green = 0;
            int red = 0;
            if (ix >= 0 && iy >= 0 && ix < PANEL_WIDTH && iy < PANEL_HEIGHT) {
                uint8_t *src = orrery + (iy * PANEL_WIDTH + ix) * 3;
                blue = src[0];
                green = src[1];
                red = src[2];
            }
            uint8_t *dst = pixels + (y * PANEL_WIDTH + x) * 3;
            blend_channel(dst[0], blue, alpha);
            blend_channel(dst[1], green, alpha);
            blend_channel(dst[2], red, alpha);
        }
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
    setup_encoder();
    if (!oled_init()) {
        printf("oled not found\n");
    }
    apply_oled_timer();
    oled_set_contrast(settings.oled_bright);

    patterns[settings.pattern].start();
    printf("patterns: plasma, warp, feedback, metaballs, reaction, life, flow field, voronoi cells, loop, liquid, pipes, reef, solar, campfire, skyline, lighthouse, ferris wheel, aurora, fireworks, galaxy, settings\n");
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
        poll_encoder(now);

        update_motion(now);
        if (on_campfire()) {
            view_angle = 0.0f;
            view_target = 0.0f;
            view_spinning = false;
        }
        update_turn(dt);
        update_shift(dt);
        update_mirror_motion(dt);
        float rate = motion * play_speed;
        if (burst != BurstPhase::Idle) {
            rate = 1.0f;
        }
        anim_frac += dt * rate;
        int draw_dt = (int)anim_frac;
        if (draw_dt < 0) {
            draw_dt = 0;
        }
        anim_frac -= draw_dt;
        anim_ms += (uint32_t)draw_dt;

        if (flight_dir != 0) {
            float span = (float)draw_dt / (float)FLIGHT_MS;
            flight += (float)flight_dir * span;
            if (flight > 1.0f) {
                flight = 1.0f;
            } else if (flight < 0.0f) {
                flight = 0.0f;
            }
            if ((flight_dir > 0 && flight >= 1.0f) || (flight_dir < 0 && flight <= 0.0f)) {
                int dir = flight_dir;
                flight_dir = 0;
                if (dir > 0 && !on_galaxy()) {
                    handoff_pattern(find_pattern("galaxy"));
                } else if (dir < 0 && !on_solar()) {
                    handoff_pattern(find_pattern("solar"));
                }
            }
        }
        if (flight_dir != 0) {
            solar_step(draw_dt);
            galaxy_step(draw_dt);
            paint_cosmos(frame, anim_ms);
        } else {
            patterns[settings.pattern].draw(frame, anim_ms, draw_dt);
        }
        rotate_view(frame);
        translate_view(frame);
        if (!on_settings()) {
            apply_mirrors(frame);
            apply_hue(frame, settings.hue);
        }
        present_frame(frame, now);
        update_bgr(frame);

        oled_idle(now);
        blank_matrix_if_due(now);
        if (oled_now || (int32_t)(now - oled_at) >= 0) {
            present_oled(now);
            oled_at = now + 150;
            oled_now = false;
        }

        int spent = (int)(absolute_time_diff_us(started, get_absolute_time()) / 1000);
        if (spent < FRAME_MS) {
            sleep_ms(FRAME_MS - spent);
        }
    }
}
