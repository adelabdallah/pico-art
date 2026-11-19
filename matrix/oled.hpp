#pragma once

// 128 by 64 SSD1306 on I2C1, GP14 SDA and GP15 SCL, address 0x3C.
// The top 16 rows are the yellow band. The lower rows are blue.
bool oled_init();
// detail is the brightness row. meter below zero hides listening and the rectangle.
// axes is the lower yellow row, on the right.
void oled_show(const char *pattern_line, const char *button_line, const char *detail = nullptr, float meter = -1.0f, bool peak = false, const char *axes = nullptr);
