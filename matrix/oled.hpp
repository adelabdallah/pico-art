#pragma once

#include <cstdint>

// 128 by 64 SSD1306 on I2C1, GP14 SDA and GP15 SCL, address 0x3C.
// The top 16 rows are the yellow band. The lower rows are blue.
// The dimmest contrast is 5 percent. The idle timeout is what turns the panel off.

enum class OledIcon : uint8_t { None, Sun, Hourglass, Axes, Mic };

bool oled_init();
// Quiet time before the panel turns off. A button calls oled_poke to light it again.
void oled_set_idle_ms(uint32_t idle_ms);
// percent is 5 to 100. 5 is dim but still drawn. 100 matches the panel's usual contrast.
void oled_set_contrast(int percent);
void oled_poke(uint32_t now_ms);
void oled_idle(uint32_t now_ms);
// Yellow band is the name, full height. Blue band is slow waves, or an equalizer when bands is set.
void oled_show_pattern(const char *title, uint32_t now_ms, const float *bands, int band_count);
// Yellow band is the setting name and a small icon. Blue band is a bar and the value.
void oled_show_setting(const char *label, OledIcon icon, const char *value, float bar, uint32_t now_ms);
