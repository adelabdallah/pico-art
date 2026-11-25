// Particles trace a slowly turning noise field. Their colour follows the direction they travel.
// The layout and the motion follow Flow Field at
// https://www.ascii-magic.com/ascii/flow-field
// The encoder sets how much of the panel those streams cover, from a few wisps to a busy field.
// A beat bends that field, so the streams leave their usual paths and the dim field turns with them.
// The same beat adds extra streams in proportion to the encoder. At the busy end it adds about as many as are already there.
// Those extra streams finish their path and collapse back to the level the encoder set.
// A beat also brightens the streams, shifts their hue, and hurries them, then those three ease back.
// The dim field keeps its own light.
// Green starts and stops that microphone. Listening starts on.

#include "pattern.hpp"
#include "noise.hpp"

#include <cmath>

namespace {

constexpr int TRAIL = 16;
constexpr float LIFE = 4.5f;
constexpr float SPEED = 10.0f;
constexpr float TAU = 6.2831853f;
constexpr float COVER_LOW = 0.15f;
constexpr float DENSITY_STEP = 0.05f;
// Extra brightness of the streams at the hit. They ease back to their own level.
constexpr float BEAT_LIFT = 0.48f;
// About a twelfth of a turn at the hit.
constexpr float BEAT_HUE = 0.08f;
// Per second. A hit is quiet again in about a third of a second.
constexpr float BEAT_DECAY = 5.8f;
// At the hit the streams move about three times their usual pace.
constexpr float BEAT_RUSH = 2.4f;
// Radians added to the field at the hit, plus a shear of the noise sample.
constexpr float BEND_TURN = 1.7f;
constexpr float BEND_SHEAR = 0.35f;
// Per second. The field is home again in about two seconds.
constexpr float BEND_DECAY = 1.35f;

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

static_assert(sizeof(Particle) <= FLOW_PARTICLE_BYTES, "flow particle");

static Particle *particles = nullptr;
static int particle_cap = 0;
static int particle_count = 0;
static int standing_count = 0;
static float density = 0.0f;
static float beat_level = 0.0f;
static float bend = 0.0f;
static uint32_t rng_state = 1;

static float rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) * (1.0f / 16777216.0f);
}

static int count_for_cover(float cover) {
    int count = (int)((float)PANEL_PIXELS * cover / FLOW_FOOTPRINT + 0.5f);
    if (count < 8) {
        count = 8;
    }
    if (count > particle_cap) {
        count = particle_cap;
    }
    return count;
}

static int count_for_density() {
    float cover = COVER_LOW + (FLOW_COVER_HIGH - COVER_LOW) * density;
    return count_for_cover(cover);
}

// At the low end of the encoder a beat adds none. At the busy end it adds the standing field again.
static int burst_count() {
    int burst = (int)((float)count_for_density() * density + 0.5f);
    int room = particle_cap - count_for_density();
    if (room < 0) {
        room = 0;
    }
    if (burst > room) {
        burst = room;
    }
    return burst;
}

static void birth(Particle &particle, int index);

static float field_scale() {
    return 0.03f + density * 0.07f;
}

static float field_angle(float x, float y, float seconds) {
    float scale = field_scale();
    float sx = x + (y - PANEL_HEIGHT * 0.5f) * bend * BEND_SHEAR;
    float sy = y + (x - PANEL_WIDTH * 0.5f) * bend * BEND_SHEAR;
    float n = field_noise(sx * scale + 3.0f, sy * scale + seconds * 0.03f, 7);
    return n * TAU * 2.0f + seconds * 0.12f + bend * BEND_TURN;
}

// Standing streams sit in front of the extras. Thickening shifts the extras up and births the gap. Thinning drops standing streams and slides the extras down.
static void set_standing(int want) {
    int extras = particle_count - standing_count;
    if (extras < 0) {
        extras = 0;
    }
    if (want < standing_count) {
        for (int i = 0; i < extras; ++i) {
            particles[want + i] = particles[standing_count + i];
        }
        standing_count = want;
        particle_count = want + extras;
        return;
    }
    if (want > standing_count) {
        int grow = want - standing_count;
        if (standing_count + extras + grow > particle_cap) {
            int overflow = standing_count + extras + grow - particle_cap;
            if (overflow > extras) {
                overflow = extras;
            }
            extras -= overflow;
        }
        for (int i = extras - 1; i >= 0; --i) {
            particles[standing_count + grow + i] = particles[standing_count + i];
        }
        for (int i = standing_count; i < want; ++i) {
            particles[i].cycle = 0;
            birth(particles[i], i);
        }
        standing_count = want;
        particle_count = want + extras;
    }
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

static void advance(Particle &particle, float dt, float seconds, float pace) {
    float angle = field_angle(particle.x, particle.y, seconds);
    float step = SPEED * pace * dt;
    particle.x += cosf(angle) * step;
    particle.y += sinf(angle) * step;
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

static void trail_colour(const uint8_t palette[][3], int hue, float level, float lift, int &red, int &green, int &blue) {
    const uint8_t *colour = palette[hue & 7];
    red = (int)(10 + colour[0] * level);
    green = (int)(8 + colour[1] * level);
    blue = (int)(16 + colour[2] * level);
    if (level > 0.72f) {
        float white = (level - 0.72f) / 0.28f;
        red = (int)(red + (255 - red) * white);
        green = (int)(green + (255 - green) * white);
        blue = (int)(blue + (255 - blue) * white);
    }
    if (lift > 1.01f) {
        red = clamp_channel(red * lift);
        green = clamp_channel(green * lift);
        blue = clamp_channel(blue * lift);
    }
}

static void shift_palette(uint8_t palette[][3], float turns) {
    for (int i = 0; i < 8; ++i) {
        int red = HUE[i][0];
        int green = HUE[i][1];
        int blue = HUE[i][2];
        hue_shift(red, green, blue, turns);
        palette[i][0] = (uint8_t)red;
        palette[i][1] = (uint8_t)green;
        palette[i][2] = (uint8_t)blue;
    }
}

}  // namespace

void flow_start() {
    density = 0.0f;
    beat_level = 0.0f;
    bend = 0.0f;
    environment_arm_listen();
    particle_cap = flow_particle_cap(PANEL_PIXELS);
    particles = (Particle *)pattern_alloc((int)sizeof(Particle) * particle_cap);
    standing_count = count_for_density();
    particle_count = standing_count;
    rng_state = 9;
    for (int i = 0; i < particle_count; ++i) {
        particles[i].cycle = 0;
        birth(particles[i], i);
        float target = ((float)i + 0.5f) / (float)particle_count * LIFE;
        while (particles[i].age < target) {
            advance(particles[i], 0.05f, 0.0f, 1.0f);
        }
    }
}

void flow_density(int steps) {
    if (particles == nullptr || particle_cap < 1) {
        return;
    }
    density += (float)steps * DENSITY_STEP;
    if (density < 0.0f) {
        density = 0.0f;
    }
    if (density > 1.0f) {
        density = 1.0f;
    }
    set_standing(count_for_density());
}

void flow_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    float seconds = anim_ms * 0.001f;
    float dt = dt_ms * 0.001f;
    if (dt > 0.08f) {
        dt = 0.08f;
    }
    environment_hear(dt_ms);
    if (beat_level > 0.0f && dt_ms > 0) {
        float beat_dt = dt_ms * 0.001f;
        if (beat_dt > 0.2f) {
            beat_dt = 0.2f;
        }
        beat_level *= expf(-BEAT_DECAY * beat_dt);
        if (beat_level < 0.02f) {
            beat_level = 0.0f;
        }
    }
    if (bend > 0.0f && dt_ms > 0) {
        float bend_dt = dt_ms * 0.001f;
        if (bend_dt > 0.2f) {
            bend_dt = 0.2f;
        }
        bend *= expf(-BEND_DECAY * bend_dt);
        if (bend < 0.02f) {
            bend = 0.0f;
        }
    }
    if (environment_beat_hit()) {
        beat_level = 1.0f;
        bend = 1.0f;
        int want = standing_count + burst_count();
        while (particle_count < want) {
            particles[particle_count].cycle = 0;
            birth(particles[particle_count], particle_count);
            particle_count += 1;
        }
    }
    float pace = 1.0f + BEAT_RUSH * beat_level;
    const uint8_t (*palette)[3] = HUE;
    uint8_t shifted[8][3];
    float lift = 1.0f;
    if (beat_level >= 0.03f) {
        shift_palette(shifted, BEAT_HUE * beat_level);
        palette = shifted;
        lift = 1.0f + BEAT_LIFT * beat_level;
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
    for (int i = 0; i < particle_count; ++i) {
        Particle &particle = particles[i];
        if (dt > 0.0f) {
            advance(particle, dt, seconds, pace);
            if (particle.age >= LIFE) {
                if (i < standing_count) {
                    birth(particle, i);
                } else {
                    particle_count -= 1;
                    if (i < particle_count) {
                        particles[i] = particles[particle_count];
                    }
                    i -= 1;
                    continue;
                }
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
            trail_colour(palette, particle.ta[slot], level, lift, red, green, blue);
            paint(frame, particle.tx[slot], particle.ty[slot], red, green, blue);
        }
    }
}
