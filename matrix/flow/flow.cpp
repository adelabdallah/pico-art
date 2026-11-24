// Particles trace a slowly turning noise field. Their colour follows the direction they travel.
// The layout and the motion follow Flow Field at
// https://www.ascii-magic.com/ascii/flow-field

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr int COUNT = 220;
constexpr int TRAIL = 16;
constexpr float LIFE = 4.5f;
constexpr float SPEED = 10.0f;
constexpr float TAU = 6.2831853f;

static const uint8_t HUE[8][3] = {
    {255, 74, 138},
    {255, 154, 74},
    {255, 224, 90},
    {90, 255, 154},
    {58, 208, 255},
    {90, 140, 255},
    {122, 106, 255},
    {255, 74, 200},
};

struct Particle {
    float x;
    float y;
    float age;
    int16_t tx[TRAIL];
    int16_t ty[TRAIL];
    uint8_t ta[TRAIL];
    uint8_t count;
    uint8_t head;
    uint32_t cycle;
};

static Particle *particles = nullptr;
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static float field_angle(float x, float y, float seconds) {
    float n = field_noise(x * 0.05f + 3.0f, y * 0.05f + seconds * 0.03f, 7);
    return n * TAU * 2.0f + seconds * 0.12f;
}

static int hue_index(float angle) {
    float turns = angle / TAU;
    turns = turns - floorf(turns);
    int index = (int)(turns * 8.0f);
    if (index < 0) {
        index = 0;
    }
    if (index > 7) {
        index = 7;
    }
    return index;
}

static void paint(uint8_t *frame, int x, int y, int red, int green, int blue) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
}

static void birth(Particle &particle, int index) {
    particle.cycle += 1;
    rng_state = (uint32_t)index * 7919u + particle.cycle * 104729u + 1u;
    particle.x = rnd() * (PANEL_WIDTH - 1);
    particle.y = rnd() * (PANEL_HEIGHT - 1);
    particle.age = 0.0f;
    particle.count = 0;
    particle.head = 0;
}

static void advance(Particle &particle, float dt, float seconds) {
    float angle = field_angle(particle.x, particle.y, seconds);
    particle.x += cosf(angle) * SPEED * dt;
    particle.y += sinf(angle) * SPEED * dt;
    particle.age += dt;
    if (particle.x < 0.0f || particle.y < 0.0f || particle.x >= PANEL_WIDTH || particle.y >= PANEL_HEIGHT) {
        return;
    }
    particle.tx[particle.head] = (int16_t)particle.x;
    particle.ty[particle.head] = (int16_t)particle.y;
    particle.ta[particle.head] = (uint8_t)hue_index(angle);
    particle.head = (uint8_t)((particle.head + 1) % TRAIL);
    if (particle.count < TRAIL) {
        particle.count = (uint8_t)(particle.count + 1);
    }
}

static void trail_colour(int hue, float level, int &red, int &green, int &blue) {
    const uint8_t *colour = HUE[hue & 7];
    red = (int)(10 + colour[0] * level);
    green = (int)(8 + colour[1] * level);
    blue = (int)(16 + colour[2] * level);
    if (level > 0.72f) {
        float white = (level - 0.72f) / 0.28f;
        red = (int)(red + (255 - red) * white);
        green = (int)(green + (255 - green) * white);
        blue = (int)(blue + (255 - blue) * white);
    }
}

}  // namespace

void flow_start() {
    particles = (Particle *)pattern_alloc((int)sizeof(Particle) * COUNT);
    rng_state = 9;
    for (int i = 0; i < COUNT; ++i) {
        particles[i].cycle = 0;
        birth(particles[i], i);
        float target = ((float)i + 0.5f) / (float)COUNT * LIFE;
        while (particles[i].age < target) {
            advance(particles[i], 0.05f, 0.0f);
        }
    }
}

void flow_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
    float dt = dt_ms * 0.001f;
    if (dt > 0.08f) {
        dt = 0.08f;
    }
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int hue = hue_index(field_angle((float)x, (float)y, seconds));
            const uint8_t *colour = HUE[hue];
            int red = 16 + colour[0] / 12;
            int green = 18 + colour[1] / 12;
            int blue = 36 + colour[2] / 12;
            write_bgr(frame + (y * PANEL_WIDTH + x) * 3, red, green, blue);
        }
    }
    for (int i = 0; i < COUNT; ++i) {
        Particle &particle = particles[i];
        if (dt > 0.0f) {
            advance(particle, dt, seconds);
            if (particle.age >= LIFE) {
                birth(particle, i);
            }
        }
        float fade = particle.age / 0.6f;
        float remain = (LIFE - particle.age) / 0.8f;
        if (remain < fade) {
            fade = remain;
        }
        if (fade < 0.1f || particle.count == 0) {
            continue;
        }
        for (int j = particle.count - 1; j >= 0; --j) {
            int slot = (int)particle.head - 1 - j;
            while (slot < 0) {
                slot += TRAIL;
            }
            float level = (1.0f - (float)j / (float)particle.count) * fade;
            int red;
            int green;
            int blue;
            trail_colour(particle.ta[slot], level, red, green, blue);
            paint(frame, particle.tx[slot], particle.ty[slot], red, green, blue);
        }
    }
}
