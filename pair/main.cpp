// Two OLEDs on the PiCowbell STEMMA connector, GP4 SDA and GP5 SCL.
// The left glass is 0x3C and the right glass is 0x3D.
// The LSM6DSOX is whichever of 0x6A and 0x6B answers with id 0x6C.
//
// The yellow band holds a sun. Tilt slides it across both glasses.
// The corona twinkles on its own. While the sun is sliding it twinkles faster and the flares reach farther.
// The blue band is one pool. Lit pixels are the foam. Dark pixels are the water.
// Tilt scrolls that pattern toward the low side.

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

constexpr int SDA = 4;
constexpr int SCL = 5;

constexpr uint8_t LEFT = 0x3C;
constexpr uint8_t RIGHT = 0x3D;
constexpr uint8_t IMU_A = 0x6A;
constexpr uint8_t IMU_B = 0x6B;
constexpr uint8_t IMU_ID = 0x6C;
constexpr uint8_t WHO_AM_I = 0x0F;
constexpr uint8_t CTRL1_XL = 0x10;
constexpr uint8_t CTRL3_C = 0x12;
constexpr uint8_t OUTX_L_A = 0x28;
constexpr float LSB_G = 0.000061f;

// 0 is accelerometer X, 1 is Y, 2 is Z.
// Across points at the right glass. Down points at the bottom of the blue band.
// Y reads about -1 g with the yellow band up, so down is flipped.
constexpr int AXIS_ACROSS = 0;
constexpr int AXIS_DOWN = 1;
constexpr float SIGN_ACROSS = 1.f;
constexpr float SIGN_DOWN = -1.f;

constexpr int WIDTH = 256;
constexpr int HEIGHT = 64;
constexpr int YELLOW = 16;
constexpr int PAGES = HEIGHT / 8;
constexpr int SKY_Y = 7;
constexpr int SUN_R = 5;
constexpr int SUN_REACH = 8;
constexpr int SUN_MIN = SUN_REACH;
constexpr int SUN_MAX = WIDTH - 1 - SUN_REACH;

constexpr uint8_t INIT[] = {
    0x00,
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
constexpr uint8_t COL_WIN[] = {0x00, 0x21, 0x00, 0x7F};
constexpr uint8_t PAGE_WIN[] = {0x00, 0x22, 0x00, 0x07};

static uint8_t frame[WIDTH * PAGES];
static uint8_t pagebuf[129];
static uint8_t imu_raw[6];
static uint8_t imu = 0;

static float gx = 0.f;
static float gy = 0.f;
static float origin_x = 0.f;
static float origin_y = 0.f;

static int sun_x = WIDTH / 2;

// A flare is one short coronal event. When it fades, the next one picks a new angle.
constexpr int FLARE_COUNT = 3;

struct Flare {
    float angle;
    float length;
    float age;
    float life;
    float wait;
    int kind;
};

static Flare flares[FLARE_COUNT];
static float corona_time = 0.f;
static float sun_motion = 0.f;
static int sun_x_prev = WIDTH / 2;
static uint32_t flare_salt = 0x51u;

static int s16(const uint8_t *buf, int index) {
    int value = buf[index] | (buf[index + 1] << 8);
    if (value & 0x8000) {
        value -= 0x10000;
    }
    return value;
}

static void set_px(int x, int y) {
    if ((unsigned)x >= WIDTH || (unsigned)y >= HEIGHT) {
        return;
    }
    frame[(y >> 3) * WIDTH + x] |= (uint8_t)(1u << (y & 7));
}

static void sky(int x, int y) {
    if ((unsigned)x < WIDTH && (unsigned)y < YELLOW - 1) {
        set_px(x, y);
    }
}

static void draw_disc(int cx, int cy, int radius) {
    int limit = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= limit) {
                sky(cx + dx, cy + dy);
            }
        }
    }
}

static uint32_t mix(uint32_t n) {
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    return n;
}

static int iround(float value) {
    return (int)(value >= 0.f ? value + 0.5f : value - 0.5f);
}

static float flare_env(float age, float life) {
    float u = age / life;
    if (u < 0.18f) {
        return u / 0.18f;
    }
    if (u > 0.62f) {
        return (1.f - u) / 0.38f;
    }
    return 1.f;
}

static void spawn_flare(int index) {
    uint32_t n = mix(flare_salt + (uint32_t)index * 97u + (uint32_t)(corona_time * 1000.f));
    flare_salt = n;
    Flare &flare = flares[index];
    flare.angle = (n % 1000u) * 0.0062832f;
    flare.length = 1.3f + (float)(n % 3u) * 0.45f;
    flare.life = 0.75f + (float)((n >> 8) % 8u) * 0.06f;
    flare.age = 0.f;
    flare.wait = 0.f;
    flare.kind = (int)((n >> 16) & 1u);
}

static void init_flares() {
    flare_salt = 0x51u;
    corona_time = 0.f;
    for (int i = 0; i < FLARE_COUNT; ++i) {
        spawn_flare(i);
        if (i == 0) {
            flares[i].age = flares[i].life * 0.35f;
            flares[i].kind = 0;
        } else {
            flares[i].wait = 0.7f * (float)i;
        }
    }
}

// Rest is a slow shimmer. A quick slide drains the waits and lights the outer pixels.
static void step_sun(float dt) {
    float speed = 0.f;
    if (dt > 0.001f) {
        speed = fabsf((float)(sun_x - sun_x_prev)) / dt;
    }
    sun_x_prev = sun_x;
    float target = speed / 70.f;
    if (target > 1.f) {
        target = 1.f;
    }
    sun_motion += (target - sun_motion) * 0.4f;
    corona_time += dt;

    float hurry = 1.f + sun_motion * 6.f;
    for (int i = 0; i < FLARE_COUNT; ++i) {
        Flare &flare = flares[i];
        if (flare.wait > 0.f) {
            flare.wait -= dt * hurry;
            if (flare.wait <= 0.f) {
                spawn_flare(i);
            }
            continue;
        }
        flare.age += dt * (1.f + sun_motion * 0.8f);
        if (flare.age >= flare.life) {
            uint32_t n = mix(flare_salt + (uint32_t)i * 13u);
            flare_salt = n;
            float span = 1.5f - sun_motion * 1.25f;
            flare.wait = span * (0.45f + (float)(n % 8u) * 0.1f);
        }
    }
}

static void corona_px(float angle, int dist) {
    sky(sun_x + iround(cosf(angle) * (float)dist), SKY_Y + iround(sinf(angle) * (float)dist));
}

// Idle sparks are rare and slow. Motion lowers the gate and speeds the wave.
static bool spark(float phase) {
    float wave = sinf(corona_time * (3.5f + sun_motion * 14.f) + phase);
    return wave > 0.55f - sun_motion * 0.7f;
}

static void draw_sun() {
    draw_disc(sun_x, SKY_Y, SUN_R);

    float spin = corona_time * 0.35f;
    const float streamer[4] = {0.35f, 1.85f, 3.55f, 5.10f};
    for (int i = 0; i < 4; ++i) {
        float angle = streamer[i] + spin;
        corona_px(angle, SUN_R + 1);
        if (spark(angle * 13.f)) {
            corona_px(angle, SUN_R + 2);
        }
        if (sun_motion > 0.35f && spark(angle * 13.f + 1.7f)) {
            corona_px(angle, SUN_R + 3);
        }
    }

    for (int i = 0; i < 6; ++i) {
        float angle = (float)i * 1.0472f + spin * 0.6f;
        if (spark(angle * 5.f + 2.f)) {
            corona_px(angle, SUN_R + 1);
        }
    }

    for (int i = 0; i < FLARE_COUNT; ++i) {
        const Flare &flare = flares[i];
        if (flare.wait > 0.f || flare.life <= 0.f) {
            continue;
        }
        float env = flare_env(flare.age, flare.life);
        int extra = (int)(env * flare.length * (0.7f + sun_motion) + 0.5f);
        if (extra < 1) {
            continue;
        }
        if (flare.kind == 0) {
            for (int step = 1; step <= extra; ++step) {
                if (step == extra && sun_motion < 0.5f && !spark(flare.angle * 9.f)) {
                    continue;
                }
                corona_px(flare.angle, SUN_R + step);
            }
        } else if (env > 0.25f) {
            corona_px(flare.angle - 0.6f, SUN_R + 1);
            corona_px(flare.angle + 0.6f, SUN_R + 1);
            corona_px(flare.angle, SUN_R + extra);
        }
    }
}

static bool write_all(uint8_t addr, const uint8_t *bytes, size_t len) {
    return i2c_write_timeout_us(i2c0, addr, bytes, len, false, 20000) == (int)len;
}

static bool init_oled(uint8_t addr) {
    return write_all(addr, INIT, sizeof INIT);
}

static void flush(uint8_t addr, int x0) {
    if (!write_all(addr, COL_WIN, sizeof COL_WIN) || !write_all(addr, PAGE_WIN, sizeof PAGE_WIN)) {
        return;
    }
    for (int page = 0; page < PAGES; ++page) {
        pagebuf[0] = 0x40;
        const uint8_t *row = frame + page * WIDTH + x0;
        for (int i = 0; i < 128; ++i) {
            pagebuf[1 + i] = row[i];
        }
        write_all(addr, pagebuf, sizeof pagebuf);
    }
}

static uint8_t find_imu() {
    const uint8_t addrs[] = {IMU_A, IMU_B};
    for (uint8_t addr : addrs) {
        uint8_t id = 0;
        if (i2c_write_timeout_us(i2c0, addr, &WHO_AM_I, 1, true, 5000) != 1) {
            continue;
        }
        if (i2c_read_timeout_us(i2c0, addr, &id, 1, false, 5000) != 1 || id != IMU_ID) {
            continue;
        }
        uint8_t cfg[] = {CTRL3_C, 0x44};
        i2c_write_timeout_us(i2c0, addr, cfg, 2, false, 5000);
        cfg[0] = CTRL1_XL;
        cfg[1] = 0x50;
        i2c_write_timeout_us(i2c0, addr, cfg, 2, false, 5000);
        return addr;
    }
    return 0;
}

static void read_gravity() {
    float across = 0.f;
    float down = 0.f;
    if (imu) {
        uint8_t reg = OUTX_L_A;
        if (i2c_write_timeout_us(i2c0, imu, &reg, 1, true, 5000) == 1 &&
            i2c_read_timeout_us(i2c0, imu, imu_raw, 6, false, 5000) == 6) {
            float sample[3] = {
                s16(imu_raw, 0) * LSB_G,
                s16(imu_raw, 2) * LSB_G,
                s16(imu_raw, 4) * LSB_G,
            };
            across = SIGN_ACROSS * sample[AXIS_ACROSS];
            down = SIGN_DOWN * sample[AXIS_DOWN];
        }
    }
    gx += (across - gx) * 0.45f;
    gy += (down - gy) * 0.45f;
}

// Bright foam sits on the ridges of two warped noise fields. The same
// recipe as the pool caustics picture, kept to one bit.
static float hash2(int x, int y, int seed) {
    uint32_t n = (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u ^ (uint32_t)seed * 1442695041u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)(n & 1023) * (1.f / 1023.f);
}

static float fade(float t) {
    return t * t * (3.f - 2.f * t);
}

static float noise2(float x, float y, int seed) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float fx = x - (float)x0;
    float fy = y - (float)y0;
    float ux = fade(fx);
    float uy = fade(fy);
    float a = hash2(x0, y0, seed);
    float b = hash2(x0 + 1, y0, seed);
    float c = hash2(x0, y0 + 1, seed);
    float d = hash2(x0 + 1, y0 + 1, seed);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

static float ridge(float x, float y, int seed) {
    float n = noise2(x, y, seed) * 0.65f + noise2(x * 2.1f, y * 2.1f, seed + 5) * 0.35f;
    float r = 1.f - fabsf(n - 0.5f) * 2.f;
    if (r < 0.f) {
        r = 0.f;
    }
    float r2 = r * r;
    float r4 = r2 * r2;
    return r4 * r4;
}

static void draw_pool(float seconds) {
    for (int y = YELLOW; y < HEIGHT; ++y) {
        int page = y >> 3;
        uint8_t bit = (uint8_t)(1u << (y & 7));
        uint8_t *row = frame + page * WIDTH;
        float wy = (float)(y - YELLOW) * 0.11f + origin_y;
        float w1 = sinf(wy * 1.3f + seconds * 0.9f) * 0.35f;
        for (int x = 0; x < WIDTH; ++x) {
            float wx = (float)x * 0.11f + origin_x;
            float w2 = cosf(wx * 1.1f - seconds * 0.7f) * 0.35f;
            float a = ridge(wx + w1 + seconds * 0.12f, wy + w2 - seconds * 0.05f, 3);
            float b = ridge(wx * 1.3f - w2 - seconds * 0.09f, wy * 1.3f + w1 + seconds * 0.1f, 11);
            if (a > 0.22f || b > 0.28f) {
                row[x] |= bit;
            }
        }
    }
}

// Level holds the sun on the join. One g of sideways tilt parks it at either end.
static void place_sun() {
    float mid = (SUN_MIN + SUN_MAX) * 0.5f;
    float reach = (SUN_MAX - SUN_MIN) * 0.5f;
    float x = mid + gx * reach;
    if (x < SUN_MIN) {
        x = SUN_MIN;
    } else if (x > SUN_MAX) {
        x = SUN_MAX;
    }
    sun_x = (int)(x + 0.5f);
}

int main() {
    stdio_init_all();
    i2c_init(i2c0, 400000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA);
    gpio_pull_up(SCL);

    bool left_ok = init_oled(LEFT);
    bool right_ok = init_oled(RIGHT);
    imu = find_imu();
    printf("left %s\n", left_ok ? "0x3c" : "missing");
    printf("right %s\n", right_ok ? "0x3d" : "missing");
    printf("imu %s\n", imu == IMU_A ? "0x6a" : imu == IMU_B ? "0x6b" : "missing");
    init_flares();

    uint32_t last = to_ms_since_boot(get_absolute_time());

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        float dt = (float)(int32_t)(now - last) / 1000.f;
        last = now;
        if (dt < 0.f) {
            dt = 0.f;
        } else if (dt > 0.05f) {
            dt = 0.05f;
        }

        read_gravity();
        place_sun();
        step_sun(dt);
        // One g slides the foam about a screen-width every few seconds.
        origin_x += gx * 18.f * dt;
        origin_y += gy * 12.f * dt;

        for (int i = 0; i < WIDTH * PAGES; ++i) {
            frame[i] = 0;
        }
        draw_pool(now / 1000.f);
        draw_sun();
        if (left_ok) {
            flush(LEFT, 0);
        }
        if (right_ok) {
            flush(RIGHT, 128);
        }
    }
}
