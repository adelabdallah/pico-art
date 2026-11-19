// SSD1306, 128 by 64, on I2C.
// The picture is stored in pages of eight rows. Bit 0 of a byte is the top row of its page.
// A command byte is sent with prefix 0x00. Picture bytes are sent with prefix 0x40.

#include "oled.hpp"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <cmath>
#include <cstring>

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_PAGES = OLED_HEIGHT / 8;
constexpr int PIN_SDA = 14;
constexpr int PIN_SCL = 15;
constexpr uint8_t OLED_ADDRESS = 0x3C;

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
static bool awake = true;
static uint32_t sleep_at = 0;
// Five quiet minutes until the settings page chooses another timeout.
static uint32_t idle_ms = 5 * 60 * 1000;

static bool write_cmd(uint8_t command) {
    uint8_t buf[2] = {0x00, command};
    int wrote = i2c_write_timeout_us(i2c1, OLED_ADDRESS, buf, 2, false, 2000);
    return wrote == 2;
}

static void set_pixel(int x, int y) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    frame[x + (y >> 3) * OLED_WIDTH] |= (uint8_t)(1u << (y & 7));
}

static void fill_rect(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            set_pixel(x + col, y + row);
        }
    }
}

static void draw_rect(int x, int y, int width, int height) {
    for (int col = 0; col < width; ++col) {
        set_pixel(x + col, y);
        set_pixel(x + col, y + height - 1);
    }
    for (int row = 0; row < height; ++row) {
        set_pixel(x, y + row);
        set_pixel(x + width - 1, y + row);
    }
}

static bool flush() {
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

void oled_set_idle_ms(uint32_t next_idle_ms) {
    if (next_idle_ms < 1000) {
        next_idle_ms = 1000;
    }
    idle_ms = next_idle_ms;
}

void oled_set_contrast(int percent) {
    if (!ready) {
        return;
    }
    if (percent < 5) {
        percent = 5;
    } else if (percent > 100) {
        percent = 100;
    }
    // 5 percent stays visible. 100 percent is the contrast the panel boots with.
    int contrast = 16 + (percent - 5) * (0xCF - 16) / 95;
    uint8_t buf[3] = {0x00, 0x81, (uint8_t)contrast};
    i2c_write_timeout_us(i2c1, OLED_ADDRESS, buf, 3, false, 2000);
}

void oled_poke(uint32_t now_ms) {
    sleep_at = now_ms + idle_ms;
    if (!ready || awake) {
        return;
    }
    if (write_cmd(0xAF)) {
        awake = true;
    }
}

void oled_idle(uint32_t now_ms) {
    if (!ready || !awake || (int32_t)(now_ms - sleep_at) < 0) {
        return;
    }
    if (write_cmd(0xAE)) {
        awake = false;
    }
}

// 8 by 8, bit 7 on the left, drawn at double height so the yellow band is filled.
// Order is space, A to Z, 0 to 9, colon, percent, infinity.
constexpr uint8_t BIG_FONT[][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00},
    {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00},
    {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
    {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
    {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00},
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x6C, 0x36, 0x00},
    {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00},
    {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00},
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
    {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    {0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00},
    {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
    {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00},
    {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
    {0x3C, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00},
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00},
    {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00},
    {0x62, 0x64, 0x08, 0x10, 0x20, 0x4C, 0x8C, 0x00},
    {0x00, 0x36, 0x49, 0x49, 0x49, 0x49, 0x36, 0x00},
};

static int big_glyph(char code) {
    if (code == ' ') {
        return 0;
    }
    if (code >= 'a' && code <= 'z') {
        code = (char)(code - 'a' + 'A');
    }
    if (code >= 'A' && code <= 'Z') {
        return 1 + (code - 'A');
    }
    if (code >= '0' && code <= '9') {
        return 27 + (code - '0');
    }
    if (code == ':') {
        return 37;
    }
    if (code == '%') {
        return 38;
    }
    if (code == '~') {
        return 39;
    }
    return 0;
}

static void blit_glyph(int x, int y, char code, int scale_x, int scale_y) {
    const uint8_t *rows = BIG_FONT[big_glyph(code)];
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        for (int sy = 0; sy < scale_y; ++sy) {
            int py = y + row * scale_y + sy;
            for (int col = 0; col < 8; ++col) {
                if ((bits & (uint8_t)(0x80 >> col)) == 0) {
                    continue;
                }
                for (int sx = 0; sx < scale_x; ++sx) {
                    set_pixel(x + col * scale_x + sx, py);
                }
            }
        }
    }
}

static void blit_text(int x, int y, const char *text, int scale_x, int scale_y, int gap) {
    if (text == nullptr) {
        return;
    }
    int advance = 8 * scale_x + gap;
    for (int i = 0; text[i] != '\0'; ++i) {
        blit_glyph(x, y, text[i], scale_x, scale_y);
        x += advance;
    }
}

static void plot_icon(uint16_t *bits, int x, int y) {
    if ((unsigned)x >= 16u || (unsigned)y >= 16u) {
        return;
    }
    bits[y] |= (uint16_t)(0x8000u >> x);
}

static void paint_icon(uint16_t *bits, OledIcon icon) {
    for (int i = 0; i < 16; ++i) {
        bits[i] = 0;
    }
    if (icon == OledIcon::Sun) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                int dx = x - 7;
                int dy = y - 7;
                int d2 = dx * dx + dy * dy;
                if (d2 >= 8 && d2 <= 18) {
                    plot_icon(bits, x, y);
                }
            }
        }
        const int rays[][2] = {
            {7, 0}, {7, 1}, {7, 14}, {7, 15}, {0, 7}, {1, 7}, {14, 7}, {15, 7},
            {2, 2}, {3, 3}, {11, 2}, {12, 3}, {2, 12}, {3, 11}, {11, 12}, {12, 11},
        };
        for (int i = 0; i < 16; ++i) {
            plot_icon(bits, rays[i][0], rays[i][1]);
        }
        return;
    }
    if (icon == OledIcon::Hourglass) {
        for (int x = 2; x <= 13; ++x) {
            plot_icon(bits, x, 1);
            plot_icon(bits, x, 14);
        }
        for (int y = 2; y <= 13; ++y) {
            int span = y < 8 ? 8 - y : y - 7;
            for (int x = 8 - span; x <= 7 + span; ++x) {
                plot_icon(bits, x, y);
            }
        }
        return;
    }
    if (icon == OledIcon::Axes) {
        for (int x = 2; x <= 14; ++x) {
            plot_icon(bits, x, 11);
        }
        for (int y = 1; y <= 12; ++y) {
            plot_icon(bits, 3, y);
        }
        plot_icon(bits, 13, 10);
        plot_icon(bits, 14, 11);
        plot_icon(bits, 13, 12);
        plot_icon(bits, 2, 2);
        plot_icon(bits, 3, 1);
        plot_icon(bits, 4, 2);
        return;
    }
    if (icon == OledIcon::Mic) {
        for (int y = 2; y <= 8; ++y) {
            for (int x = 5; x <= 10; ++x) {
                int dx = x < 7 ? 7 - x : x - 8;
                if (y == 2 && dx > 1) {
                    continue;
                }
                if (y == 8 && dx > 1) {
                    continue;
                }
                plot_icon(bits, x, y);
            }
        }
        plot_icon(bits, 4, 9);
        plot_icon(bits, 3, 10);
        plot_icon(bits, 3, 11);
        plot_icon(bits, 11, 9);
        plot_icon(bits, 12, 10);
        plot_icon(bits, 12, 11);
        plot_icon(bits, 7, 9);
        plot_icon(bits, 8, 9);
        plot_icon(bits, 7, 10);
        plot_icon(bits, 8, 10);
        plot_icon(bits, 7, 11);
        plot_icon(bits, 8, 11);
        plot_icon(bits, 7, 12);
        plot_icon(bits, 8, 12);
        for (int x = 4; x <= 11; ++x) {
            plot_icon(bits, x, 13);
        }
    }
}

static void blit_icon(int x, int y, const uint16_t *bits, int turns) {
    for (int iy = 0; iy < 16; ++iy) {
        for (int ix = 0; ix < 16; ++ix) {
            int sx = ix;
            int sy = iy;
            if (turns == 1) {
                sx = iy;
                sy = 15 - ix;
            } else if (turns == 2) {
                sx = 15 - ix;
                sy = 15 - iy;
            } else if (turns == 3) {
                sx = 15 - iy;
                sy = ix;
            }
            if ((bits[sy] & (uint16_t)(0x8000u >> sx)) == 0) {
                continue;
            }
            set_pixel(x + ix, y + iy);
        }
    }
}

static int icon_bob(uint32_t now_ms) {
    static const int bob[] = {0, 1, 0, -1};
    return bob[(now_ms / 180) & 3];
}

static void draw_title(const char *text, OledIcon icon, uint32_t now_ms) {
    int x = 0;
    if (icon != OledIcon::None) {
        uint16_t bits[16];
        paint_icon(bits, icon);
        blit_icon(0, icon_bob(now_ms), bits, (int)((now_ms / 700) & 3));
        x = 18;
    }
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    int glyphs = (int)std::strlen(text);
    int scale_x = 1;
    int gap = 0;
    if (icon == OledIcon::None && glyphs > 0 && glyphs <= 8) {
        scale_x = 2;
    } else if (icon == OledIcon::None && glyphs > 0) {
        int leftover = OLED_WIDTH - 8 * glyphs;
        if (leftover > 0) {
            gap = leftover / glyphs;
        }
    }
    int advance = 8 * scale_x + gap;
    int width = glyphs * advance - gap;
    int start = icon == OledIcon::None ? (OLED_WIDTH - width) / 2 : x;
    if (start < x) {
        start = x;
    }
    blit_text(start, 0, text, scale_x, 2, gap);
}

static void draw_waves(uint32_t now_ms) {
    float phase = now_ms * 0.004f;
    for (int x = 0; x < OLED_WIDTH; ++x) {
        float along = x * 0.09f + phase;
        for (int band = 0; band < 3; ++band) {
            float wave = sinf(along + band * 1.4f) * (3.5f + band);
            int y = (int)(40.0f + band * 7.0f + wave);
            set_pixel(x, y);
            set_pixel(x, y + 1);
        }
    }
}

static void draw_equalizer(const float *bands, int count) {
    if (bands == nullptr || count <= 0) {
        return;
    }
    if (count > 8) {
        count = 8;
    }
    int gap = 4;
    int bar_w = (OLED_WIDTH - gap * (count + 1)) / count;
    if (bar_w < 2) {
        bar_w = 2;
    }
    for (int i = 0; i < count; ++i) {
        float level = bands[i];
        if (level < 0.0f) {
            level = 0.0f;
        } else if (level > 1.0f) {
            level = 1.0f;
        }
        int height = (int)(level * 40.0f + 0.5f);
        if (height < 2) {
            height = 2;
        }
        int x = gap + i * (bar_w + gap);
        fill_rect(x, 62 - height, bar_w, height);
    }
}

static void draw_value_bar(float bar) {
    if (bar < 0.0f) {
        bar = 0.0f;
    } else if (bar > 1.0f) {
        bar = 1.0f;
    }
    int x = 2;
    int y = 26;
    int width = 76;
    int height = 26;
    draw_rect(x, y, width, height);
    int filled = (int)((width - 6) * bar + 0.5f);
    if (filled > 0) {
        fill_rect(x + 3, y + 3, filled, height - 6);
    }
}

static bool present() {
    if (!flush()) {
        ready = false;
        return false;
    }
    return true;
}

void oled_show_pattern(const char *title, uint32_t now_ms, const float *bands, int band_count) {
    if (!ready || !awake) {
        return;
    }
    std::memset(frame, 0, sizeof frame);
    draw_title(title, OledIcon::None, now_ms);
    if (bands != nullptr && band_count > 0) {
        draw_equalizer(bands, band_count);
    } else {
        draw_waves(now_ms);
    }
    present();
}

void oled_show_setting(const char *label, OledIcon icon, const char *value, float bar, uint32_t now_ms) {
    if (!ready || !awake) {
        return;
    }
    std::memset(frame, 0, sizeof frame);
    draw_title(label, icon, now_ms);
    draw_value_bar(bar);
    blit_text(82, 24, value, 1, 3, 0);
    present();
}
