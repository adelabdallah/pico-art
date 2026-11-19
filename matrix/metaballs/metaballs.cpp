// A handful of moving balls added into one field.
// Where the field is high the colour fills in, and the balls merge when they meet.

#include <cmath>
#include <cstdint>

#include "pattern.hpp"

constexpr float CX = (PANEL_WIDTH - 1) / 2.0f;
constexpr float CY = (PANEL_HEIGHT - 1) / 2.0f;
constexpr int BALL_COUNT = 5;

struct Ball {
    float speed;
    float phase;
    float radius;
    float reach;
};

static Ball balls[BALL_COUNT] = {
    {0.71f, 0.4f, 11.0f, 20.0f},
    {1.05f, 1.7f, 9.0f, 16.0f},
    {0.58f, 3.1f, 13.0f, 22.0f},
    {1.28f, 4.6f, 8.0f, 18.0f},
    {0.86f, 5.8f, 10.0f, 24.0f},
};

void metaballs_start() {
}

void metaballs_draw(uint8_t *pixels, uint32_t anim_ms, int dt_ms) {
    (void)dt_ms;
    float time = anim_ms * 0.001f;
    float ball_x[BALL_COUNT];
    float ball_y[BALL_COUNT];
    for (int i = 0; i < BALL_COUNT; ++i) {
        Ball ball = balls[i];
        ball_x[i] = CX + sinf(time * ball.speed + ball.phase) * ball.reach;
        ball_y[i] = CY + cosf(time * ball.speed * 0.73f + ball.phase * 1.3f) * (ball.reach * 0.85f);
    }

    uint8_t *pixel = pixels;
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float field = 0.0f;
            for (int i = 0; i < BALL_COUNT; ++i) {
                float dx = x - ball_x[i];
                float dy = y - ball_y[i];
                float radius = balls[i].radius;
                field += (radius * radius) / (dx * dx + dy * dy + 8.0f);
            }
            float body = smooth_unit(0.72f, 1.25f, field);
            float core = smooth_unit(1.35f, 2.4f, field);
            int red = (int)(8 + body * 210 + core * 40);
            int green = (int)(10 + body * 48 + core * 150);
            int blue = (int)(28 + body * 96 + core * 80);
            write_bgr(pixel, clamp_channel(red), clamp_channel(green), clamp_channel(blue));
            pixel += 3;
        }
    }
}
