#include "oled.hpp"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <cstring>

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_PAGES = OLED_HEIGHT / 8;
constexpr int PIN_SDA = 14;
constexpr int PIN_SCL = 15;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr int BUTTON_TEXT_Y = 1;
constexpr int AXES_TEXT_Y = 8;
constexpr int BLUE_TEXT_Y = 20;
constexpr int DETAIL_TEXT_Y = 32;
constexpr int LISTEN_TEXT_Y = 44;
constexpr int METER_X = 62;
constexpr int METER_W = 40;
constexpr int METER_H = 7;

// 5 by 7 columns, bit 0 at the top. Indexed by GLYPH_CHARS.
constexpr char GLYPH_CHARS[] = " %0123456789abcdefghijklmnopqrstuvwxyz.!-:";
constexpr uint8_t GLYPHS[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x36, 0x36, 0x00, 0x00},
};

static_assert(sizeof GLYPH_CHARS - 1 == sizeof GLYPHS / sizeof GLYPHS[0], "glyph table");

constexpr uint8_t INIT[] = {
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0x8D, 0x14,
    0x20, 0x00,
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x40,
    0xA4,
    0xA6,
    0xAF,
};

static uint8_t frame[OLED_WIDTH * OLED_PAGES];
static bool ready = false;

bool write_cmd(uint8_t command) {
    uint8_t buf[2] = {0x00, command};
    int wrote = i2c_write_timeout_us(i2c1, OLED_ADDRESS, buf, 2, false, 2000);
    return wrote == 2;
}

const uint8_t *glyph(char code) {
    const char *found = std::strchr(GLYPH_CHARS, code);
    if (found == nullptr) {
        return GLYPHS[0];
    }
    return GLYPHS[found - GLYPH_CHARS];
}

void set_pixel(int x, int y) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    frame[x + (y >> 3) * OLED_WIDTH] |= (uint8_t)(1u << (y & 7));
}

void fill_rect(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            set_pixel(x + col, y + row);
        }
    }
}

void draw_rect(int x, int y, int width, int height) {
    for (int col = 0; col < width; ++col) {
        set_pixel(x + col, y);
        set_pixel(x + col, y + height - 1);
    }
    for (int row = 0; row < height; ++row) {
        set_pixel(x, y + row);
        set_pixel(x + width - 1, y + row);
    }
}

void draw_text_until(int x, int y, const char *text, int limit_x) {
    if (text == nullptr) {
        return;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (x + 6 > limit_x) {
            return;
        }
        const uint8_t *cols = glyph(*cursor);
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = cols[col];
            for (int row = 0; row < 7; ++row) {
                if ((bits & (1u << row)) != 0) {
                    set_pixel(x + col, y + row);
                }
            }
        }
        x += 6;
        if (x >= OLED_WIDTH) {
            return;
        }
    }
}

void draw_text(int x, int y, const char *text) {
    draw_text_until(x, y, text, OLED_WIDTH);
}

void draw_text_right(int y, const char *text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    int x = OLED_WIDTH - (int)std::strlen(text) * 6;
    if (x < 0) {
        x = 0;
    }
    draw_text(x, y, text);
}

bool flush() {
    if (!write_cmd(0x21) || !write_cmd(0) || !write_cmd(OLED_WIDTH - 1)) {
        return false;
    }
    if (!write_cmd(0x22) || !write_cmd(0) || !write_cmd(OLED_PAGES - 1)) {
        return false;
    }
    for (int page = 0; page < OLED_PAGES; ++page) {
        uint8_t buf[OLED_WIDTH + 1];
        buf[0] = 0x40;
        std::memcpy(buf + 1, frame + page * OLED_WIDTH, OLED_WIDTH);
        int wrote = i2c_write_timeout_us(i2c1, OLED_ADDRESS, buf, OLED_WIDTH + 1, false, 20000);
        if (wrote != OLED_WIDTH + 1) {
            return false;
        }
    }
    return true;
}

bool oled_init() {
    i2c_init(i2c1, 400000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
    ready = true;
    for (uint8_t command : INIT) {
        if (!write_cmd(command)) {
            ready = false;
            return false;
        }
    }
    return true;
}

void oled_show(const char *pattern_line, const char *button_line, const char *detail, float meter, bool peak, const char *axes) {
    if (!ready) {
        return;
    }
    std::memset(frame, 0, sizeof frame);
    draw_text(0, BUTTON_TEXT_Y, button_line);
    int axes_x = OLED_WIDTH;
    if (axes != nullptr && axes[0] != '\0') {
        axes_x = OLED_WIDTH - (int)std::strlen(axes) * 6;
        if (axes_x < 0) {
            axes_x = 0;
        }
        draw_text(axes_x, AXES_TEXT_Y, axes);
    }
    draw_text(0, BLUE_TEXT_Y, pattern_line);
    if (detail != nullptr && detail[0] != '\0') {
        draw_text(0, DETAIL_TEXT_Y, detail);
    }
    if (meter >= 0.0f) {
        if (meter > 1.0f) {
            meter = 1.0f;
        }
        draw_text(0, LISTEN_TEXT_Y, "listening");
        draw_rect(METER_X, LISTEN_TEXT_Y, METER_W, METER_H);
        int inner = METER_W - 2;
        int filled = (int)(inner * meter + 0.5f);
        if (peak) {
            filled = inner;
        }
        if (filled > 0) {
            fill_rect(METER_X + 1, LISTEN_TEXT_Y + 1, filled, METER_H - 2);
        }
        if (peak) {
            draw_text(METER_X + METER_W + 2, LISTEN_TEXT_Y, "!");
        }
    }
    if (!flush()) {
        ready = false;
    }
}
