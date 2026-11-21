// An orrery on the wide view, and a close portrait of each world.
// Each orbit is its own hoop, leaned so the paths cross in front of and behind each other.
// Tipping the panel looks around that model. Flat, face up, is the resting view.
// The encoder walks the stops. Green flings the portrait's spin, or the whole orrery on the wide view.
// Orange returns to the orrery, and from there the view pulls back into the galaxy.
// Yellow tours the stops. Each one holds for 5 seconds. The view tumbles slowly and each world stays centred while its surface turns.
// Orbit spacing is even so the rings stay apart. Periods follow the real order, with the outer worlds compressed into a few minutes.
// Coasts, bands, and crater rims fade softly. The gas giants' bands wave and slowly roil. Earth carries the finest surface.
// The Moon and Charon are large enough to read. They spin and orbit faster while green is held, and while the tour runs.
// The sun's corona follows uneven curving streamers. Flares rise in several shapes and shift between yellow and red.

#include "pattern.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int STOP_WIDE = 0;
constexpr int STOP_SUN = 1;
constexpr int STOP_MERCURY = 2;
constexpr int STOP_VENUS = 3;
constexpr int STOP_EARTH = 4;
constexpr int STOP_MARS = 5;
constexpr int STOP_BELT = 6;
constexpr int STOP_JUPITER = 7;
constexpr int STOP_SATURN = 8;
constexpr int STOP_URANUS = 9;
constexpr int STOP_NEPTUNE = 10;
constexpr int STOP_PLUTO = 11;
constexpr int STOP_COUNT = 12;

constexpr float PI = 3.14159265f;
constexpr float TWO_PI = 6.2831853f;
constexpr float HALF_PI = 1.5707963f;
constexpr float CX = (PANEL_WIDTH - 1) * 0.5f;
constexpr float CY = (PANEL_HEIGHT - 1) * 0.5f;
constexpr float CAM_PITCH = 0.95f;
constexpr float PORTRAIT_R = 13.0f;
constexpr float SPRING_K = 80.0f;
constexpr float SPRING_D = 11.0f;
constexpr float SPIN_HOLD = 3.14159265f;
constexpr float ORRERY_HOLD = 0.78539816f;
constexpr float SPIN_KICK = 2.2f;
constexpr float ORRERY_KICK = 0.85f;
constexpr float TOUR_DWELL_S = 5.0f;
constexpr int WIDE_STARS = 36;
constexpr int PORTRAIT_STARS = 24;
constexpr int BELT_DOTS = 32;
constexpr int ROCKS = 12;

// Semi-major axis in pixels. The sun sits at the centre. Pluto's ring clips the side edges.
constexpr float ORBIT_R[STOP_COUNT] = {
    0.0f, 0.0f, 6.0f, 9.2f, 12.4f, 15.6f, 18.8f, 22.0f, 25.2f, 28.4f, 31.6f, 34.8f,
};

// Leans are wide enough that each hoop clears its neighbours on this panel. The inner ones stay shallow.
struct Hoop {
    float incl;
    float node;
};

constexpr Hoop HOOP[STOP_COUNT] = {
    {0.0f, 0.0f},
    {0.0f, 0.0f},
    {0.22f, 0.50f},
    {0.16f, 2.10f},
    {0.10f, 3.40f},
    {0.26f, 1.10f},
    {0.30f, 0.20f},
    {0.34f, 2.40f},
    {0.40f, 4.30f},
    {0.46f, 0.80f},
    {0.50f, 3.60f},
    {0.55f, 1.50f},
};

// Inner worlds keep their real ratios with Earth at 60 seconds. Outer worlds are squeezed onto 5 to 10 minutes.
constexpr float PERIOD_S[STOP_COUNT] = {
    0.0f, 0.0f, 14.5f, 37.0f, 60.0f, 113.0f, 0.0f, 300.0f, 390.0f, 493.0f, 560.0f, 600.0f,
};

constexpr float PHASE0[STOP_COUNT] = {
    0.0f, 0.0f, 0.5f, 1.7f, 3.1f, 4.4f, 0.8f, 2.0f, 5.2f, 0.9f, 3.6f, 1.3f,
};

// Idle axial turn, radians per second. Venus goes backwards.
constexpr float IDLE_SPIN[STOP_COUNT] = {
    0.0f,
    TWO_PI / 6.0f,
    TWO_PI / 20.0f,
    -TWO_PI / 25.0f,
    TWO_PI / 12.0f,
    TWO_PI / 13.0f,
    0.0f,
    TWO_PI / 8.0f,
    TWO_PI / 9.0f,
    TWO_PI / 14.0f,
    TWO_PI / 11.0f,
    TWO_PI / 18.0f,
};

struct Ink {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr Ink INK[STOP_COUNT] = {
    {0, 0, 0},
    {255, 210, 90},
    {168, 162, 156},
    {232, 198, 120},
    {48, 108, 214},
    {204, 72, 38},
    {150, 124, 82},
    {220, 156, 86},
    {232, 198, 140},
    {150, 214, 206},
    {46, 82, 214},
    {196, 156, 142},
};

struct Moon {
    int stop;
    float orbit;
    float period;
    float phase;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    float radius;
    float spin_period;
};

// The Moon and Charon are 40 percent of their planet and sit just clear of the limb.
constexpr Moon MOONS[] = {
    {STOP_EARTH, 26.5f, 20.0f, 0.2f, 186, 182, 172, PORTRAIT_R * 0.40f, 24.0f},
    {STOP_MARS, 17.5f, 9.0f, 1.0f, 150, 130, 110, 0.0f, 0.0f},
    {STOP_MARS, 22.0f, 16.0f, 2.4f, 110, 100, 90, 0.0f, 0.0f},
    {STOP_JUPITER, 17.0f, 7.0f, 0.3f, 210, 196, 160, 0.0f, 0.0f},
    {STOP_JUPITER, 19.5f, 12.0f, 1.5f, 180, 196, 210, 0.0f, 0.0f},
    {STOP_JUPITER, 22.0f, 18.0f, 3.0f, 220, 200, 170, 0.0f, 0.0f},
    {STOP_JUPITER, 25.0f, 30.0f, 4.2f, 160, 150, 140, 0.0f, 0.0f},
    {STOP_SATURN, 18.0f, 14.0f, 0.6f, 210, 190, 150, 0.0f, 0.0f},
    {STOP_SATURN, 21.0f, 22.0f, 2.1f, 180, 170, 145, 0.0f, 0.0f},
    {STOP_SATURN, 24.0f, 34.0f, 4.0f, 150, 165, 175, 0.0f, 0.0f},
    {STOP_URANUS, 18.5f, 16.0f, 0.8f, 180, 220, 220, 0.0f, 0.0f},
    {STOP_URANUS, 22.5f, 26.0f, 2.6f, 160, 200, 210, 0.0f, 0.0f},
    {STOP_NEPTUNE, 20.0f, 18.0f, 1.2f, 170, 190, 210, 0.0f, 0.0f},
    {STOP_PLUTO, 26.5f, 14.0f, 0.5f, 196, 176, 166, PORTRAIT_R * 0.40f, 28.0f},
};

constexpr int MOON_COUNT = (int)(sizeof MOONS / sizeof MOONS[0]);

// North to south, 15 degrees a row. Columns run east from Greenwich, 15 degrees each.
// '#' is land. The poles are painted white on top of this, so the top and bottom rows are ice.
constexpr const char *EARTH_ROWS[12] = {
    ".....................#..",
    "#########.......######..",
    "###########.....#####...",
    "#########.......#####...",
    "###..###.........##....#",
    "###.....##........###...",
    ".##.....#.........####..",
    ".#......###.......###...",
    "........##.........#....",
    "...................#....",
    "########################",
    "########################",
};

constexpr int earth_row_len(const char *row) {
    int n = 0;
    while (row[n] != '\0') {
        ++n;
    }
    return n;
}

static_assert(earth_row_len(EARTH_ROWS[0]) == 24, "earth row 0");
static_assert(earth_row_len(EARTH_ROWS[1]) == 24, "earth row 1");
static_assert(earth_row_len(EARTH_ROWS[2]) == 24, "earth row 2");
static_assert(earth_row_len(EARTH_ROWS[3]) == 24, "earth row 3");
static_assert(earth_row_len(EARTH_ROWS[4]) == 24, "earth row 4");
static_assert(earth_row_len(EARTH_ROWS[5]) == 24, "earth row 5");
static_assert(earth_row_len(EARTH_ROWS[6]) == 24, "earth row 6");
static_assert(earth_row_len(EARTH_ROWS[7]) == 24, "earth row 7");
static_assert(earth_row_len(EARTH_ROWS[8]) == 24, "earth row 8");
static_assert(earth_row_len(EARTH_ROWS[9]) == 24, "earth row 9");
static_assert(earth_row_len(EARTH_ROWS[10]) == 24, "earth row 10");
static_assert(earth_row_len(EARTH_ROWS[11]) == 24, "earth row 11");

struct Oval {
    float lon;
    float lat;
    float rx;
    float ry;
};

constexpr Oval CRATERS[] = {
    {0.20f, 0.32f, 0.26f, 0.24f},
    {-0.48f, -0.18f, 0.22f, 0.20f},
    {0.62f, -0.42f, 0.20f, 0.18f},
    {-0.15f, 0.62f, 0.16f, 0.16f},
    {1.70f, 0.10f, 0.28f, 0.26f},
    {0.05f, -0.08f, 0.13f, 0.12f},
    {-0.72f, 0.28f, 0.15f, 0.14f},
    {0.95f, 0.12f, 0.12f, 0.11f},
    {-0.28f, -0.55f, 0.14f, 0.13f},
};

constexpr int CRATER_COUNT = (int)(sizeof CRATERS / sizeof CRATERS[0]);

// A flare is one coronal event. When it fades, the next one chooses a new angle on the limb.
constexpr int FLARE_COUNT = 5;

struct Flare {
    float angle;
    float length;
    float age;
    float life;
    float wait;
    int kind;
};

constexpr Oval SUNSPOTS[] = {
    {0.10f, 0.48f, 0.20f, 0.16f},
    {0.40f, 0.58f, 0.11f, 0.09f},
    {-1.75f, -0.20f, 0.22f, 0.17f},
    {-1.45f, -0.40f, 0.10f, 0.09f},
};

static int target = STOP_WIDE;
static int depart = STOP_SUN;
static float shown = 1.0f;
static float shown_v = 0.0f;
static float zoom = 0.0f;
static float zoom_v = 0.0f;
static bool spin_held = false;
static bool touring = false;
static bool tour_wait = true;
static float tour_left = TOUR_DWELL_S;
static float tour_time = 0.0f;
static float tour_blend = 0.0f;
static float cam_roll = 0.0f;
static bool ready = false;
static float orrery_vel = 0.0f;
static float spin_angle[STOP_COUNT];
static float spin_vel[STOP_COUNT];
static float orbit_angle[STOP_COUNT];
static float belt_anchor = 0.8f;
static float belt_dot[BELT_DOTS];
static float belt_period[BELT_DOTS];
static float belt_jitter[BELT_DOTS];
static float belt_lift[BELT_DOTS];
static float cam_pitch = CAM_PITCH;
static float cam_yaw = 0.0f;
static float rock_x[ROCKS];
static float rock_y[ROCKS];
static float rock_speed[ROCKS];
static float rock_depth[ROCKS];
static float rock_rad[ROCKS];
static int rock_crater[ROCKS];
static float moon_angle[MOON_COUNT];
static float moon_spin[MOON_COUNT];
static float moon_orbit_vel[MOON_COUNT];
static float moon_spin_vel[MOON_COUNT];
static Flare flares[FLARE_COUNT];
static float corona_time = 0.0f;
static float cloud_time = 0.0f;
static uint32_t flare_salt = 0x51u;

static uint32_t mix(uint32_t n) {
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    return n;
}

static float wrap_tau(float angle) {
    angle = fmodf(angle, TWO_PI);
    if (angle < 0.0f) {
        angle += TWO_PI;
    }
    return angle;
}

static float wrap_pi(float angle) {
    angle = wrap_tau(angle);
    if (angle > PI) {
        angle -= TWO_PI;
    }
    return angle;
}

static void paint(uint8_t *frame, int x, int y, int red, int green, int blue, float alpha) {
    if (x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT || alpha <= 0.0f) {
        return;
    }
    uint8_t *pixel = frame + (y * PANEL_WIDTH + x) * 3;
    if (alpha >= 0.999f) {
        write_bgr(pixel, red, green, blue);
        return;
    }
    float keep = 1.0f - alpha;
    int b = (int)(pixel[0] * keep + blue * alpha + 0.5f);
    int g = (int)(pixel[1] * keep + green * alpha + 0.5f);
    int r = (int)(pixel[2] * keep + red * alpha + 0.5f);
    write_bgr(pixel, r, g, b);
}

static void paint_f(uint8_t *frame, float x, float y, int red, int green, int blue, float alpha) {
    paint(frame, (int)lroundf(x), (int)lroundf(y), red, green, blue, alpha);
}

static int star_level(uint32_t n, uint32_t anim_ms) {
    float phase = (n & 255) * 0.025f;
    float rate = 0.7f + (n % 5) * 0.17f;
    float wobble = 0.5f + 0.5f * sinf(anim_ms * 0.0035f * rate + phase);
    int level = (int)(8.0f + wobble * 12.0f);
    float blink = sinf(anim_ms * 0.0013f * (0.55f + (n % 7) * 0.11f) + phase * 2.0f);
    if (blink > 0.94f) {
        level = 20 + (int)((blink - 0.94f) * 160.0f);
    }
    if (level > 36) {
        level = 36;
    }
    return level;
}

static float oval_cover(float lon, float lat, float lon0, float lat0, float rx, float ry) {
    float dx = wrap_pi(lon - lon0) / rx;
    float dy = (lat - lat0) / ry;
    float dist = sqrtf(dx * dx + dy * dy);
    return 1.0f - smooth_unit(0.55f, 1.12f, dist);
}

static void mix_ink(int &red, int &green, int &blue, int toward_r, int toward_g, int toward_b, float cover) {
    if (cover <= 0.0f) {
        return;
    }
    if (cover > 1.0f) {
        cover = 1.0f;
    }
    red = (int)(red + (toward_r - red) * cover);
    green = (int)(green + (toward_g - green) * cover);
    blue = (int)(blue + (toward_b - blue) * cover);
}

static float land_bit(int col, int row) {
    col %= 24;
    if (col < 0) {
        col += 24;
    }
    if (row < 0) {
        row = 0;
    }
    if (row > 11) {
        row = 11;
    }
    return EARTH_ROWS[row][col] == '#' ? 1.0f : 0.0f;
}

static float land_cover(float lon, float lat, int &col) {
    float turns = wrap_tau(lon) / TWO_PI;
    float col_f = turns * 24.0f;
    float south = (lat + HALF_PI) / PI;
    float row_f = 11.0f - south * 12.0f;
    int c0 = (int)floorf(col_f);
    int r0 = (int)floorf(row_f);
    float tx = col_f - floorf(col_f);
    float ty = row_f - floorf(row_f);
    float top = land_bit(c0, r0) * (1.0f - tx) + land_bit(c0 + 1, r0) * tx;
    float low = land_bit(c0, r0 + 1) * (1.0f - tx) + land_bit(c0 + 1, r0 + 1) * tx;
    float cover = top * (1.0f - ty) + low * ty;
    cover += sinf(lon * 13.0f + lat * 9.0f) * 0.07f;
    col = c0 % 24;
    if (col < 0) {
        col += 24;
    }
    return clampf(cover, 0.0f, 1.0f);
}

static void gas_flow(float &lon, float &lat) {
    float t = cloud_time;
    lon += sinf(lat * 7.0f + t * 0.28f) * 0.22f + sinf(lat * 3.0f - t * 0.16f) * 0.08f;
    lat += sinf(lon * 3.2f + t * 0.21f) * 0.05f;
}

// The masses drift across the surface and slowly change shape.
static float earth_clouds(float lon, float lat) {
    float t = cloud_time;
    float x = lon + t * 0.28f;
    float y = lat + sinf(t * 0.05f) * 0.18f;
    float a = sinf(x * 1.7f + 0.6f) * sinf(y * 2.4f + t * 0.07f);
    float b = sinf(x * 3.3f - y * 1.8f + t * 0.15f);
    float c = sinf(x * 5.1f + y * 3.7f - t * 0.11f);
    float d = sinf(x * 9.0f + y * 6.5f + t * 0.22f);
    float field = a * 0.40f + b * 0.30f + c * 0.22f + d * 0.16f;
    return smooth_unit(0.18f, 0.72f, field);
}

static void colour_earth(float lon, float lat, int &red, int &green, int &blue) {
    int col = 0;
    float cover = land_cover(lon, lat, col);
    float gyre = 0.5f + 0.5f * sinf(lon * 2.0f + lat * 3.0f);
    float shelf = 0.5f + 0.5f * sinf(lon * 6.5f - lat * 4.2f);
    int sea_r = 14;
    int sea_g = clamp_channel(32 + gyre * 26 + shelf * 10);
    int sea_b = clamp_channel(132 + gyre * 50 + shelf * 22);
    bool africa = col <= 2 || col == 23;
    int land_r = 42;
    int land_g = 132;
    int land_b = 52;
    float grain = 0.5f + 0.5f * sinf(lon * 8.0f + 0.4f) * sinf(lat * 7.0f);
    float fine = 0.5f + 0.5f * sinf(lon * 15.0f + lat * 11.0f);
    if (lat > 0.15f && lat < 0.55f && africa) {
        land_r = clamp_channel(150 + grain * 40);
        land_g = clamp_channel(118 + fine * 28);
        land_b = clamp_channel(58 + grain * 18);
    } else if (lat > 0.55f) {
        land_r = clamp_channel(110 + grain * 36);
        land_g = clamp_channel(92 + fine * 24);
        land_b = clamp_channel(48 + grain * 16);
    } else {
        land_r = clamp_channel(28 + grain * 28 + fine * 12);
        land_g = clamp_channel(108 + grain * 40);
        land_b = clamp_channel(40 + fine * 22);
    }
    red = (int)(sea_r + (land_r - sea_r) * cover);
    green = (int)(sea_g + (land_g - sea_g) * cover);
    blue = (int)(sea_b + (land_b - sea_b) * cover);
    float clouds = earth_clouds(lon, lat);
    if (clouds > 0.0f) {
        red = (int)(red + (232 - red) * clouds);
        green = (int)(green + (238 - green) * clouds);
        blue = (int)(blue + (244 - blue) * clouds);
    }
}

static void colour_sun(float lon, float lat, float nz, int &red, int &green, int &blue) {
    float grain = sinf(lon * 5.0f) * sinf(lat * 4.0f);
    float glow = 0.75f + 0.25f * nz;
    float warm = 0.70f + 0.30f * grain;
    red = clamp_channel((220.0f + warm * 35.0f) * glow);
    green = clamp_channel((120.0f + warm * 110.0f) * glow);
    blue = clamp_channel((24.0f + warm * 50.0f) * glow);
    mix_ink(red, green, blue, clamp_channel(255 * glow), clamp_channel(236 * glow), clamp_channel(150 * glow), oval_cover(lon, lat, 0.62f, 0.32f, 0.22f, 0.16f));
    mix_ink(red, green, blue, clamp_channel(255 * glow), clamp_channel(236 * glow), clamp_channel(150 * glow), oval_cover(lon, lat, -1.15f, -0.02f, 0.20f, 0.15f));
    for (int i = 0; i < 4; ++i) {
        const Oval &spot = SUNSPOTS[i];
        float spot_cover = oval_cover(lon, lat, spot.lon, spot.lat, spot.rx, spot.ry);
        mix_ink(red, green, blue, clamp_channel(92 * glow), clamp_channel(42 * glow), clamp_channel(16 * glow), spot_cover);
    }
}

static void colour_mercury(float lon, float lat, int &red, int &green, int &blue) {
    float grain = 0.5f + 0.5f * sinf(lon * 6.0f) * sinf(lat * 5.0f);
    red = clamp_channel(148 + grain * 16);
    green = clamp_channel(142 + grain * 14);
    blue = clamp_channel(136 + grain * 12);
    mix_ink(red, green, blue, 120, 114, 108, oval_cover(lon, lat, 0.05f, -0.02f, 0.90f, 0.48f));
    for (int i = 0; i < CRATER_COUNT; ++i) {
        const Oval &crater = CRATERS[i];
        float dx = wrap_pi(lon - crater.lon) / crater.rx;
        float dy = (lat - crater.lat) / crater.ry;
        float dist = sqrtf(dx * dx + dy * dy);
        float bowl = 1.0f - smooth_unit(0.15f, 0.85f, dist);
        float rim = smooth_unit(0.72f, 0.92f, dist) * (1.0f - smooth_unit(0.92f, 1.18f, dist));
        mix_ink(red, green, blue, 78, 74, 70, bowl);
        mix_ink(red, green, blue, 188, 182, 174, rim);
    }
}

static void colour_venus(float lon, float lat, int &red, int &green, int &blue) {
    float band = sinf(lat * 6.0f + sinf(lon * 2.2f) * 0.55f);
    float lift = 0.5f + 0.5f * band;
    red = clamp_channel(160 + lift * 86);
    green = clamp_channel(96 + lift * 118);
    blue = clamp_channel(28 + lift * 52);
    float dx = wrap_pi(lon - 0.15f);
    float stem = 0.0f;
    if (dx > -0.20f && dx < 1.15f) {
        stem = (1.0f - smooth_unit(0.08f, 0.22f, fabsf(lat))) * (1.0f - smooth_unit(1.05f, 1.25f, dx)) * smooth_unit(-0.20f, -0.02f, dx);
    }
    float arm = 0.0f;
    if (dx > -1.25f && dx < 0.25f) {
        float fork = 0.75f * fmaxf(0.0f, -dx);
        arm = 1.0f - smooth_unit(0.08f, 0.22f, fabsf(fabsf(lat) - fork));
        arm *= smooth_unit(-1.25f, -1.05f, dx) * (1.0f - smooth_unit(0.05f, 0.25f, dx));
    }
    float mark = fmaxf(stem, arm);
    mix_ink(red, green, blue, 248, 216, 126, mark);
    float hood = smooth_unit(1.02f, 1.38f, fabsf(lat));
    red = (int)(red + (246 - red) * hood);
    green = (int)(green + (230 - green) * hood);
    blue = (int)(blue + (176 - blue) * hood);
}

static void colour_mars(float lon, float lat, int &red, int &green, int &blue) {
    float dust = 0.5f + 0.5f * sinf(lon * 3.0f) * sinf(lat * 2.0f);
    red = clamp_channel(176 + dust * 50);
    green = clamp_channel(62 + dust * 30);
    blue = clamp_channel(28 + dust * 16);
    mix_ink(red, green, blue, 102, 38, 26, oval_cover(lon, lat, 0.35f, 0.18f, 0.48f, 0.28f));
    mix_ink(red, green, blue, 102, 38, 26, oval_cover(lon, lat, -0.95f, -0.22f, 0.34f, 0.22f));
    float canyon = wrap_pi(lon);
    if (canyon > -0.45f && canyon < 1.30f) {
        float cut = (1.0f - smooth_unit(0.06f, 0.16f, fabsf(lat + 0.06f))) * smooth_unit(-0.45f, -0.25f, canyon) * (1.0f - smooth_unit(1.10f, 1.30f, canyon));
        mix_ink(red, green, blue, 78, 30, 20, cut);
    }
    mix_ink(red, green, blue, 232, 178, 132, oval_cover(lon, lat, 1.35f, -0.40f, 0.30f, 0.20f));
}

static void colour_bands(float lon, float lat, const int bands[][3], int count, int &red, int &green, int &blue) {
    float t = cloud_time;
    float wave = sinf(lon * 2.4f + t * 0.33f) * 0.10f + sinf(lon * 5.6f - t * 0.19f) * 0.045f;
    float u = (lat + wave + HALF_PI) / PI;
    float pos = clampf(u * (float)count, 0.0f, (float)count - 0.001f);
    int band = (int)pos;
    int next = band + 1;
    if (next >= count) {
        next = band;
    }
    float frac = pos - (float)band;
    float blend = frac * frac * (3.0f - 2.0f * frac);
    float roil = 0.90f + 0.10f * sinf(lon * 4.0f + (float)band + t * 0.42f);
    roil += 0.06f * sinf(lon * 9.0f - lat * 6.0f + t * 0.57f);
    red = clamp_channel((bands[band][0] + (bands[next][0] - bands[band][0]) * blend) * roil);
    green = clamp_channel((bands[band][1] + (bands[next][1] - bands[band][1]) * blend) * roil);
    blue = clamp_channel((bands[band][2] + (bands[next][2] - bands[band][2]) * blend) * roil);
}

static void colour_jupiter(float lon, float lat, int &red, int &green, int &blue) {
    constexpr int bands[9][3] = {
        {150, 78, 32},
        {232, 206, 156},
        {158, 70, 28},
        {246, 220, 176},
        {140, 62, 24},
        {238, 214, 168},
        {172, 88, 36},
        {226, 198, 148},
        {154, 80, 34},
    };
    gas_flow(lon, lat);
    colour_bands(lon, lat, bands, 9, red, green, blue);
    float spot = oval_cover(lon, lat, 0.20f + sinf(cloud_time * 0.12f) * 0.04f, -0.35f, 0.40f, 0.13f);
    mix_ink(red, green, blue, 214, 62, 40, spot);
    float eye = oval_cover(lon, lat, 0.20f + sinf(cloud_time * 0.12f) * 0.04f, -0.35f, 0.16f, 0.06f);
    mix_ink(red, green, blue, 168, 36, 24, eye);
    mix_ink(red, green, blue, 252, 248, 236, oval_cover(lon, lat, -0.85f, 0.35f, 0.16f, 0.10f));
}

static void colour_saturn(float lon, float lat, int &red, int &green, int &blue) {
    constexpr int bands[7][3] = {
        {188, 156, 100},
        {230, 206, 152},
        {176, 146, 92},
        {236, 214, 164},
        {168, 138, 86},
        {224, 200, 146},
        {198, 168, 112},
    };
    gas_flow(lon, lat);
    colour_bands(lon, lat, bands, 7, red, green, blue);
    mix_ink(red, green, blue, 248, 232, 190, oval_cover(lon, lat, 0.40f, 0.28f, 0.24f, 0.11f));
    mix_ink(red, green, blue, 150, 118, 72, oval_cover(lon, lat, -1.10f, -0.22f, 0.18f, 0.09f));
}

static void colour_uranus(float nx, float ny, float nz, float spin, int &red, int &green, int &blue, float &light) {
    constexpr float ax = 0.863f;
    constexpr float ay = -0.421f;
    constexpr float az = 0.281f;
    float pole = nx * ax + ny * ay + nz * az;
    float bx = -ay;
    float by = ax;
    float bz = 0.0f;
    float bl = sqrtf(bx * bx + by * by + bz * bz);
    bx /= bl;
    by /= bl;
    bz /= bl;
    float cx = ay * bz - az * by;
    float cy = az * bx - ax * bz;
    float cz = ax * by - ay * bx;
    float lon = atan2f(nx * cx + ny * cy + nz * cz, nx * bx + ny * by + nz * bz) + spin;
    float lat = asinf(clampf(pole, -1.0f, 1.0f));
    float hood = smooth_unit(0.78f, 0.96f, pole);
    gas_flow(lon, lat);
    float band = 0.78f + 0.18f * sinf(pole * 7.0f + sinf(lon * 2.0f) * 0.4f);
    band += 0.06f * sinf(lon * 5.0f + cloud_time * 0.35f);
    red = clamp_channel(148 * band);
    green = clamp_channel(214 * band);
    blue = clamp_channel(206 * band);
    float streak = 1.0f - smooth_unit(0.08f, 0.28f, fabsf(wrap_pi(lon)));
    mix_ink(red, green, blue, clamp_channel(red + 28), clamp_channel(green + 24), clamp_channel(blue + 22), streak);
    float storm = (1.0f - smooth_unit(0.12f, 0.40f, fabsf(wrap_pi(lon - 1.15f)))) * (1.0f - smooth_unit(0.08f, 0.24f, fabsf(pole - 0.02f)));
    mix_ink(red, green, blue, clamp_channel(red + 40), clamp_channel(green + 32), clamp_channel(blue + 28), storm);
    red = (int)(red + (232 - red) * hood);
    green = (int)(green + (248 - green) * hood);
    blue = (int)(blue + (244 - blue) * hood);
    light = light + hood * (1.05f - light);
}

static void colour_neptune(float lon, float lat, int &red, int &green, int &blue) {
    gas_flow(lon, lat);
    float zone = 1.0f - smooth_unit(0.10f, 0.70f, fabsf(lat));
    float roil = 0.94f + 0.06f * sinf(lon * 4.0f + cloud_time * 0.3f) * sinf(lat * 5.0f - cloud_time * 0.2f);
    red = clamp_channel((18 + zone * 16) * roil);
    green = clamp_channel((48 + zone * 40) * roil);
    blue = clamp_channel((168 + zone * 48) * roil);
    mix_ink(red, green, blue, 10, 22, 78, oval_cover(lon, lat, 0.25f, 0.12f, 0.30f, 0.16f));
    mix_ink(red, green, blue, 10, 22, 78, oval_cover(lon, lat, -1.15f, -0.28f, 0.16f, 0.11f));
    float dx = wrap_pi(lon - 1.1f);
    float cloud = (1.0f - smooth_unit(0.28f, 0.62f, fabsf(dx))) * (1.0f - smooth_unit(0.04f, 0.14f, fabsf(lat - 0.48f)));
    mix_ink(red, green, blue, 226, 236, 248, cloud);
    float dx2 = wrap_pi(lon + 0.4f);
    float cloud2 = (1.0f - smooth_unit(0.18f, 0.48f, fabsf(dx2))) * (1.0f - smooth_unit(0.04f, 0.12f, fabsf(lat + 0.15f)));
    mix_ink(red, green, blue, 210, 226, 244, cloud2);
}

static float pluto_heart(float lon, float lat) {
    constexpr char rows[8][10] = {
        "..#...#..",
        ".#######.",
        "#########",
        "#########",
        ".#######.",
        "..#####..",
        "....#....",
        "....#....",
    };
    float col_f = wrap_pi(lon) / 0.20f + 4.0f;
    float row_f = (0.62f - lat) / 0.16f;
    int c0 = (int)floorf(col_f);
    int r0 = (int)floorf(row_f);
    float tx = col_f - floorf(col_f);
    float ty = row_f - floorf(row_f);
    auto bit = [&](int c, int r) -> float {
        if (c < 0 || c > 8 || r < 0 || r > 7) {
            return 0.0f;
        }
        return rows[r][c] == '#' ? 1.0f : 0.0f;
    };
    float top = bit(c0, r0) * (1.0f - tx) + bit(c0 + 1, r0) * tx;
    float low = bit(c0, r0 + 1) * (1.0f - tx) + bit(c0 + 1, r0 + 1) * tx;
    return top * (1.0f - ty) + low * ty;
}

static void colour_pluto(float lon, float lat, int &red, int &green, int &blue) {
    float mottled = sinf(lon * 5.0f) * sinf(lat * 4.0f);
    red = clamp_channel(118 + mottled * 18);
    green = clamp_channel(86 + mottled * 12);
    blue = clamp_channel(68 + mottled * 8);
    float heart = pluto_heart(lon, lat);
    float dark = (1.0f - smooth_unit(0.22f, 0.38f, fabsf(lat))) * smooth_unit(0.85f, 1.15f, fabsf(wrap_pi(lon)));
    mix_ink(red, green, blue, 46, 34, 30, dark * (1.0f - heart));
    mix_ink(red, green, blue, 236, 204, 190, heart);
    mix_ink(red, green, blue, 232, 220, 206, oval_cover(lon, lat, 2.15f, 0.42f, 0.18f, 0.14f) * (1.0f - heart));
    mix_ink(red, green, blue, 232, 220, 206, oval_cover(lon, lat, -1.35f, -0.48f, 0.16f, 0.12f) * (1.0f - heart));
}

static void gyro_aim(float time, float &yaw, float &pitch, float &roll);

static void face_tour(float &nx, float &ny, float &nz) {
    if (tour_blend <= 0.001f) {
        return;
    }
    float yaw;
    float pitch;
    float roll;
    gyro_aim(tour_time, yaw, pitch, roll);
    float yaw_now = yaw * tour_blend;
    float pitch_now = (pitch - CAM_PITCH) * tour_blend;
    (void)roll;
    float cy = cosf(yaw_now);
    float sy = sinf(yaw_now);
    float x1 = nx * cy + nz * sy;
    float z1 = -nx * sy + nz * cy;
    float cp = cosf(pitch_now);
    float sp = sinf(pitch_now);
    float y2 = ny * cp - z1 * sp;
    float z2 = ny * sp + z1 * cp;
    nx = x1;
    ny = y2;
    nz = z2;
}

static void shade_body(int stop, float nx, float ny, float nz, int &red, int &green, int &blue) {
    face_tour(nx, ny, nz);
    float spin = spin_angle[stop];
    float lon = atan2f(nx, nz) + spin;
    float lat = asinf(clampf(-ny, -1.0f, 1.0f));
    constexpr float lx = -0.42f;
    constexpr float ly = -0.58f;
    constexpr float lz = 0.70f;
    float light = 0.30f + 0.82f * fmaxf(0.0f, nx * lx + ny * ly + nz * lz);
    if (stop == STOP_SUN) {
        colour_sun(lon, lat, nz, red, green, blue);
        return;
    }
    if (stop == STOP_URANUS) {
        colour_uranus(nx, ny, nz, spin, red, green, blue, light);
    } else if (stop == STOP_MERCURY) {
        colour_mercury(lon, lat, red, green, blue);
    } else if (stop == STOP_VENUS) {
        colour_venus(lon, lat, red, green, blue);
    } else if (stop == STOP_EARTH) {
        colour_earth(lon, lat, red, green, blue);
    } else if (stop == STOP_MARS) {
        colour_mars(lon, lat, red, green, blue);
    } else if (stop == STOP_JUPITER) {
        colour_jupiter(lon, lat, red, green, blue);
    } else if (stop == STOP_SATURN) {
        colour_saturn(lon, lat, red, green, blue);
    } else if (stop == STOP_NEPTUNE) {
        colour_neptune(lon, lat, red, green, blue);
    } else {
        colour_pluto(lon, lat, red, green, blue);
    }
    red = clamp_channel(red * light);
    green = clamp_channel(green * light);
    blue = clamp_channel(blue * light);
    float ice = 0.0f;
    if (stop == STOP_EARTH) {
        ice = smooth_unit(1.00f, 1.22f, fabsf(lat));
    } else if (stop == STOP_MARS) {
        ice = smooth_unit(0.75f, 1.05f, fabsf(lat));
    }
    if (ice > 0.0f) {
        red = (int)(red + (244 - red) * ice);
        green = (int)(green + (248 - green) * ice);
        blue = (int)(blue + (252 - blue) * ice);
    }
    if (stop == STOP_EARTH && ice < 0.15f && light < 0.48f) {
        int col = 0;
        if (earth_clouds(lon, lat) < 0.45f && land_cover(lon, lat, col) > 0.55f) {
            int row_key = (int)((lat + HALF_PI) * 8.0f);
            uint32_t h = mix((uint32_t)col * 19u + (uint32_t)row_key * 131u + 7u);
            if ((h % 4u) == 0u) {
                float night = (0.48f - light) / 0.48f;
                red = (int)(red + (255 - red) * night);
                green = (int)(green + (186 - green) * night);
                blue = (int)(blue + (70 - blue) * night);
            }
        }
    }
}

static void orbit_xyz(float angle, float radius, float incl, float node, float &x, float &y, float &z) {
    float ci = cosf(incl);
    float si = sinf(incl);
    float cn = cosf(node);
    float sn = sinf(node);
    float ca = cosf(angle);
    float sa = sinf(angle);
    x = radius * (cn * ca - sn * sa * ci);
    y = radius * (sn * ca + cn * sa * ci);
    z = radius * sa * si;
}

static void view_of(float x, float y, float z, float &sx, float &sy, float &depth) {
    float c = cosf(cam_yaw);
    float s = sinf(cam_yaw);
    float x1 = x * c - y * s;
    float y1 = x * s + y * c;
    float sp = sinf(cam_pitch);
    float cp = cosf(cam_pitch);
    sx = CX + x1;
    sy = CY + y1 * sp - z * cp;
    depth = y1 * cp + z * sp;
}

// zoom 0 leaves every world where it is. zoom 1 parks the focus body on the centre and spreads the rings.
static void orient_screen(float &sx, float &sy, float fx, float fy, float zoom_now, float roll) {
    float scale = 1.0f + zoom_now * 2.4f;
    sx = fx + (sx - fx) * scale + (CX - fx) * zoom_now;
    sy = fy + (sy - fy) * scale + (CY - fy) * zoom_now;
    float dx = sx - CX;
    float dy = sy - CY;
    float c = cosf(roll);
    float s = sinf(roll);
    sx = CX + dx * c - dy * s;
    sy = CY + dx * s + dy * c;
}

static void to_screen(float lx, float ly, float body_cx, float body_cy, float roll, float &sx, float &sy) {
    float x = body_cx + lx;
    float y = body_cy + ly;
    float dx = x - CX;
    float dy = y - CY;
    float c = cosf(roll);
    float s = sinf(roll);
    sx = CX + dx * c - dy * s;
    sy = CY + dx * s + dy * c;
}

static void to_local(float sx, float sy, float body_cx, float body_cy, float roll, float &lx, float &ly) {
    float dx = sx - CX;
    float dy = sy - CY;
    float c = cosf(roll);
    float s = sinf(roll);
    float x = CX + dx * c + dy * s;
    float y = CY - dx * s + dy * c;
    lx = x - body_cx;
    ly = y - body_cy;
}

static void draw_wide_stars(uint8_t *frame, uint32_t anim_ms, float zoom_now, float roll, float alpha) {
    for (int i = 0; i < WIDE_STARS; ++i) {
        uint32_t n = mix(0x5100u + (uint32_t)i * 97u);
        float az = (n % 1000u) * 0.0062832f;
        float el = ((n >> 10) % 1000u) * 0.0016f - 0.80f;
        float reach = 46.0f;
        float x = cosf(el) * cosf(az) * reach;
        float y = cosf(el) * sinf(az) * reach;
        float z = sinf(el) * reach;
        float sx;
        float sy;
        float depth;
        view_of(x, y, z, sx, sy, depth);
        orient_screen(sx, sy, CX, CY, zoom_now * 0.35f, roll);
        int level = star_level(n, anim_ms);
        int blue = level + (int)(n % 3u);
        paint_f(frame, sx, sy, level, level, blue, alpha);
        (void)depth;
    }
}

struct Mark {
    int16_t x;
    int16_t y;
    int16_t depth;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t alpha;
    uint8_t size;
};

constexpr int MARK_CAP = 1600;
static Mark marks[MARK_CAP];
static int mark_count = 0;

static void draw_blob(uint8_t *frame, float x, float y, int radius2, int red, int green, int blue, float alpha);

static int mark_farther(const void *left, const void *right) {
    int a = ((const Mark *)left)->depth;
    int b = ((const Mark *)right)->depth;
    return (a > b) - (a < b);
}

static void push_mark(float sx, float sy, float depth, int red, int green, int blue, int size, float alpha, float fx, float fy, float zoom_now, float roll) {
    if (mark_count >= MARK_CAP) {
        return;
    }
    orient_screen(sx, sy, fx, fy, zoom_now, roll);
    int x = (int)lroundf(sx);
    int y = (int)lroundf(sy);
    if (x < -3 || y < -3 || x >= PANEL_WIDTH + 3 || y >= PANEL_HEIGHT + 3) {
        return;
    }
    Mark &mark = marks[mark_count];
    mark.x = (int16_t)x;
    mark.y = (int16_t)y;
    mark.depth = (int16_t)(depth * 32.0f);
    mark.r = (uint8_t)red;
    mark.g = (uint8_t)green;
    mark.b = (uint8_t)blue;
    int cover = (int)(alpha * 255.0f + 0.5f);
    if (cover > 255) {
        cover = 255;
    }
    mark.alpha = (uint8_t)cover;
    mark.size = (uint8_t)size;
    mark_count += 1;
}

static void paint_marks(uint8_t *frame) {
    std::qsort(marks, (size_t)mark_count, sizeof(Mark), mark_farther);
    for (int i = 0; i < mark_count; ++i) {
        const Mark &mark = marks[i];
        float alpha = mark.alpha / 255.0f;
        if (mark.size == 0) {
            paint(frame, mark.x, mark.y, mark.r, mark.g, mark.b, alpha);
        } else if (mark.size == 1) {
            draw_blob(frame, mark.x, mark.y, 1, mark.r, mark.g, mark.b, alpha);
        } else {
            draw_blob(frame, mark.x, mark.y, 2, mark.r, mark.g, mark.b, alpha);
        }
    }
}

static void draw_blob(uint8_t *frame, float x, float y, int radius2, int red, int green, int blue, float alpha) {
    int reach = 3;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (dx * dx + dy * dy > radius2) {
                continue;
            }
            paint(frame, (int)lroundf(x) + dx, (int)lroundf(y) + dy, red, green, blue, alpha);
        }
    }
}

static void body_xyz(int stop, float &x, float &y, float &z) {
    if (stop <= STOP_SUN) {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        return;
    }
    float angle = stop == STOP_BELT ? belt_anchor : orbit_angle[stop];
    orbit_xyz(angle, ORBIT_R[stop], HOOP[stop].incl, HOOP[stop].node, x, y, z);
}

static float line_shade(float depth) {
    float near = clampf(depth / 36.0f, -1.0f, 1.0f);
    return 0.50f + 0.50f * (near * 0.5f + 0.5f);
}

static void draw_orrery(uint8_t *frame, uint32_t anim_ms, float zoom_now, float roll, float alpha) {
    draw_wide_stars(frame, anim_ms, zoom_now, roll, alpha);
    mark_count = 0;
    int focus = target == STOP_WIDE ? depart : target;
    float focus_x;
    float focus_y;
    float focus_z;
    body_xyz(focus, focus_x, focus_y, focus_z);
    float fx;
    float fy;
    float focus_depth;
    view_of(focus_x, focus_y, focus_z, fx, fy, focus_depth);
    (void)focus_depth;
    for (int stop = STOP_MERCURY; stop <= STOP_PLUTO; ++stop) {
        const Ink &ink = INK[stop];
        int steps = (int)(ORBIT_R[stop] * 6.0f);
        if (steps < 48) {
            steps = 48;
        }
        for (int i = 0; i < steps; ++i) {
            float angle = TWO_PI * (float)i / (float)steps;
            float x;
            float y;
            float z;
            orbit_xyz(angle, ORBIT_R[stop], HOOP[stop].incl, HOOP[stop].node, x, y, z);
            float sx;
            float sy;
            float depth;
            view_of(x, y, z, sx, sy, depth);
            float shade = line_shade(depth);
            push_mark(sx, sy, depth, (int)(ink.r * 0.4f * shade), (int)(ink.g * 0.4f * shade), (int)(ink.b * 0.4f * shade), 0, alpha, fx, fy, zoom_now, roll);
        }
    }
    for (int i = 0; i < BELT_DOTS; ++i) {
        float x;
        float y;
        float z;
        orbit_xyz(belt_dot[i], ORBIT_R[STOP_BELT] + belt_jitter[i], HOOP[STOP_BELT].incl, HOOP[STOP_BELT].node, x, y, z);
        z += belt_lift[i];
        float sx;
        float sy;
        float depth;
        view_of(x, y, z, sx, sy, depth);
        push_mark(sx, sy, depth, 132, 112, 74, 0, alpha, fx, fy, zoom_now, roll);
    }
    for (int stop = STOP_MERCURY; stop <= STOP_PLUTO; ++stop) {
        if (stop == STOP_BELT) {
            continue;
        }
        float x;
        float y;
        float z;
        body_xyz(stop, x, y, z);
        float sx;
        float sy;
        float depth;
        view_of(x, y, z, sx, sy, depth);
        const Ink &ink = INK[stop];
        int size = (stop == STOP_JUPITER || stop == STOP_SATURN) ? 1 : 0;
        push_mark(sx, sy, depth + 0.4f, ink.r, ink.g, ink.b, size, alpha, fx, fy, zoom_now, roll);
        if (stop == STOP_SATURN) {
            float sun_x;
            float sun_y;
            float sun_depth;
            view_of(0.0f, 0.0f, 0.0f, sun_x, sun_y, sun_depth);
            (void)sun_depth;
            float dx = sx - sun_x;
            float dy = sy - sun_y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 1.0f) {
                float tx = -dy / len;
                float ty = dx / len;
                for (int k = -2; k <= 2; ++k) {
                    if (k == 0) {
                        continue;
                    }
                    push_mark(sx + tx * k * 1.15f, sy + ty * k * 1.15f, depth + 0.3f, 210, 180, 120, 0, alpha, fx, fy, zoom_now, roll);
                }
            }
        }
    }
    float sun_x;
    float sun_y;
    float sun_depth;
    view_of(0.0f, 0.0f, 0.0f, sun_x, sun_y, sun_depth);
    push_mark(sun_x, sun_y, sun_depth + 0.2f, 255, 210, 80, 2, alpha, fx, fy, zoom_now, roll);
    paint_marks(frame);
}

// Three rates that do not share a period, so the tumble keeps changing.
static void gyro_aim(float time, float &yaw, float &pitch, float &roll) {
    yaw = 0.70f * sinf(time * 0.35f);
    pitch = CAM_PITCH + 0.45f * sinf(time * 0.22f + 1.1f);
    roll = 0.55f * sinf(time * 0.17f + 0.7f);
}

static void follow_hand(float dt) {
    float tour_goal = touring ? 1.0f : 0.0f;
    tour_blend += (tour_goal - tour_blend) * (1.0f - expf(-dt * 3.0f));
    float blend = 1.0f - expf(-dt * 7.0f);
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    if (environment_gravity(gx, gy, gz)) {
        float gain = environment_imu_gain();
        float horiz = sqrtf(gx * gx + gy * gy);
        float tip = atan2f(horiz, fabsf(gz)) * gain;
        if (tip > 1.25f) {
            tip = 1.25f;
        }
        float goal_pitch = CAM_PITCH - tip * 0.80f;
        cam_pitch += (goal_pitch - cam_pitch) * blend;
        if (gain <= 0.0f) {
            cam_yaw = wrap_pi(cam_yaw * (1.0f - blend));
        } else if (horiz > 0.18f) {
            float heading = atan2f(gy, gx);
            float turn = wrap_pi(heading - cam_yaw);
            float follow = clampf((horiz - 0.18f) / 0.35f, 0.0f, 1.0f) * clampf(gain, 0.0f, 1.0f);
            cam_yaw = wrap_pi(cam_yaw + turn * blend * follow);
        }
    }
    float gyro_yaw;
    float gyro_pitch;
    float gyro_roll;
    gyro_aim(tour_time, gyro_yaw, gyro_pitch, gyro_roll);
    cam_pitch += (gyro_pitch - cam_pitch) * tour_blend;
    cam_yaw = wrap_pi(cam_yaw + wrap_pi(gyro_yaw - cam_yaw) * tour_blend);
    cam_roll += (gyro_roll * tour_blend - cam_roll) * blend;
}

static bool saturn_ring(float x, float y, float radius, bool &gap, float &shine) {
    constexpr float tilt = 0.38f;
    float c = cosf(tilt);
    float s = sinf(tilt);
    float lx = x * c + y * s;
    float ly = -x * s + y * c;
    float outer_x = radius * 1.82f;
    float outer_y = outer_x * 0.40f;
    float nx = lx / outer_x;
    float ny = ly / outer_y;
    float dist = sqrtf(nx * nx + ny * ny);
    constexpr float inner = 0.62f;
    if (dist > 1.0f || dist < inner) {
        return false;
    }
    float t = (dist - inner) / (1.0f - inner);
    gap = t > 0.46f && t < 0.58f;
    shine = t < 0.46f ? 1.0f : 0.70f;
    return true;
}

static bool uranus_ring(float x, float y, float radius) {
    constexpr float ax = 0.863f;
    constexpr float ay = -0.421f;
    float along = x * ax + y * ay;
    float across = -x * ay + y * ax;
    float thin = 1.5f;
    float reach = radius * 1.75f;
    float inner = radius * 1.12f;
    float dist = sqrtf((along / thin) * (along / thin) + (across / reach) * (across / reach));
    float span = fabsf(across);
    return dist <= 1.0f && span >= inner && span <= reach;
}

static float flare_env(float age, float life) {
    float u = age / life;
    if (u < 0.18f) {
        return u / 0.18f;
    }
    if (u > 0.62f) {
        return (1.0f - u) / 0.38f;
    }
    return 1.0f;
}

static void spawn_flare(int index) {
    uint32_t n = mix(flare_salt + (uint32_t)index * 97u + (uint32_t)(corona_time * 1000.0f));
    flare_salt = n;
    Flare &flare = flares[index];
    flare.angle = (n % 1000u) * 0.0062832f;
    flare.length = 5.2f + (float)(n % 32u) * 0.1f;
    flare.life = 0.90f + (float)((n >> 8) % 18u) * 0.06f;
    flare.age = 0.0f;
    flare.wait = 0.0f;
    flare.kind = (int)((n >> 16) % 5u);
}

static void init_flares() {
    flare_salt = 0x51u;
    corona_time = 0.0f;
    for (int i = 0; i < FLARE_COUNT; ++i) {
        spawn_flare(i);
        if (i < 2) {
            flares[i].age = flares[i].life * (i == 0 ? 0.40f : 0.12f);
            flares[i].kind = i % 5;
        } else {
            flares[i].wait = 0.55f * (float)(i - 1);
        }
    }
}

static void step_flares(float dt) {
    corona_time += dt;
    for (int i = 0; i < FLARE_COUNT; ++i) {
        Flare &flare = flares[i];
        if (flare.wait > 0.0f) {
            flare.wait -= dt;
            if (flare.wait <= 0.0f) {
                spawn_flare(i);
            }
            continue;
        }
        flare.age += dt;
        if (flare.age >= flare.life) {
            uint32_t n = mix(flare_salt + (uint32_t)i * 13u);
            flare_salt = n;
            flare.wait = 0.9f + (float)(n % 40u) * 0.09f;
        }
    }
}

static void flare_rgb(float cycle, float amount, int &red, int &green, int &blue) {
    float yellow = 1.0f - cycle;
    red = clamp_channel(255.0f * amount);
    green = clamp_channel((36.0f + yellow * 176.0f) * amount);
    blue = clamp_channel((16.0f + yellow * 40.0f) * amount);
}

static float flare_weight(const Flare &flare, float lx, float ly, float radius, float ang, float spin) {
    float env = flare_env(flare.age, flare.life);
    if (env <= 0.0f) {
        return 0.0f;
    }
    float aim = flare.angle + spin;
    float dist = sqrtf(lx * lx + ly * ly);
    float out = dist - radius;
    if (flare.kind == 0) {
        float gap = wrap_pi(ang - aim);
        if (out < -1.0f) {
            return 0.0f;
        }
        return env * expf(-gap * gap * 16.0f);
    }
    if (flare.kind == 1) {
        float arch = 2.6f + flare.length * 0.22f * env;
        float cx = cosf(aim) * radius;
        float cy = sinf(aim) * radius;
        float ring = fabsf(sqrtf((lx - cx) * (lx - cx) + (ly - cy) * (ly - cy)) - arch);
        float from_limb = (lx - cx) * cosf(aim) + (ly - cy) * sinf(aim);
        if (from_limb > -1.2f && ring < 2.0f) {
            return env * (1.0f - ring / 2.0f);
        }
        return 0.0f;
    }
    if (flare.kind == 2) {
        float lift = radius + 4.2f + flare.length * 0.12f * env;
        float cx = cosf(aim) * lift;
        float cy = sinf(aim) * lift;
        float arch = 5.2f + flare.length * 0.20f * env;
        float ring = fabsf(sqrtf((lx - cx) * (lx - cx) + (ly - cy) * (ly - cy)) - arch);
        float facing = wrap_pi(atan2f(ly - cy, lx - cx) - aim);
        if (fabsf(facing) < 1.15f && ring < 1.7f && out > -0.5f) {
            return env * (1.0f - ring / 1.7f);
        }
        return 0.0f;
    }
    if (flare.kind == 3) {
        float side = sinf(flare.angle) >= 0.0f ? 1.0f : -1.0f;
        float span = flare.length * env + 1.0f;
        float curve = wrap_pi(ang - aim) - side * (out * out) / (flare.length * flare.length + 4.0f) * 1.6f;
        if (out > -0.4f && out < span && fabsf(curve) < 0.16f + 0.12f * (out / span)) {
            return env * (1.0f - fabsf(curve) / 0.28f);
        }
        return 0.0f;
    }
    float span = flare.length * env + 1.2f;
    if (out < -0.4f || out > span) {
        return 0.0f;
    }
    float t = out / span;
    float strand_a = wrap_pi(ang - aim - 0.16f) - sinf(t * 2.2f) * 0.45f;
    float strand_b = wrap_pi(ang - aim + 0.20f) + sinf(t * 1.8f) * 0.38f;
    float strands = expf(-strand_a * strand_a * 22.0f) + expf(-strand_b * strand_b * 22.0f);
    return env * clampf(strands, 0.0f, 1.0f);
}

static bool corona_colour(float lx, float ly, float radius, int &red, int &green, int &blue, float &cover, float &prom_out) {
    float dist = sqrtf(lx * lx + ly * ly);
    if (dist < radius - 2.2f || dist > radius + 16.0f) {
        return false;
    }
    float ang = atan2f(ly, lx);
    float spin = spin_angle[STOP_SUN];
    float body = ang - spin;
    float along = dist - radius;
    float bend = sinf(body * 2.0f + corona_time * 0.55f) * 0.42f + sinf(body * 5.0f - corona_time * 0.33f) * 0.16f;
    float curved = body + bend * clampf(along / 9.0f, 0.0f, 1.4f);
    float asym = 0.62f + 0.38f * (0.5f + 0.5f * sinf(body + 0.8f));
    constexpr float streamer[7] = {0.15f, 0.85f, 1.55f, 2.40f, 3.35f, 4.25f, 5.35f};
    float reach = (3.4f + 2.8f * (0.5f + 0.5f * cosf(curved * 2.0f))) * (0.7f + asym);
    float spike = 0.0f;
    for (int i = 0; i < 7; ++i) {
        float d = wrap_pi(curved - streamer[i]);
        float w = expf(-d * d * 22.0f);
        reach += 6.2f * w * asym;
        spike += 0.7f * w;
    }
    float grain = 0.78f + 0.22f * sinf(curved * 11.0f + corona_time * 2.4f);
    float prom = 0.0f;
    float flare_r = 0.0f;
    float flare_g = 0.0f;
    float flare_b = 0.0f;
    for (int i = 0; i < FLARE_COUNT; ++i) {
        const Flare &flare = flares[i];
        if (flare.wait > 0.0f || flare.life <= 0.0f) {
            continue;
        }
        float weight = flare_weight(flare, lx, ly, radius, ang, spin);
        if (weight <= 0.0f) {
            continue;
        }
        if (flare.kind == 0) {
            reach += flare.length * weight;
            spike += 1.6f * weight;
        }
        float cycle = 0.5f + 0.5f * sinf(flare.age * 4.0f + flare.angle * 1.7f);
        int fr = 0;
        int fg = 0;
        int fb = 0;
        flare_rgb(cycle, weight, fr, fg, fb);
        flare_r += (float)fr;
        flare_g += (float)fg;
        flare_b += (float)fb;
        prom += weight;
    }
    prom_out = clampf(prom, 0.0f, 1.0f);
    if (dist <= radius && prom_out < 0.25f) {
        return false;
    }
    float fall = 1.0f - (dist - radius) / reach;
    if (fall <= 0.0f && prom <= 0.05f) {
        return false;
    }
    if (fall < 0.0f) {
        fall = 0.0f;
    }
    fall *= grain * (0.75f + 0.25f * asym);
    float glow = fall * (88.0f + spike * 120.0f);
    if (prom_out > 0.2f) {
        red = clamp_channel(flare_r + glow * 0.15f);
        green = clamp_channel(flare_g + glow * 0.05f);
        blue = clamp_channel(flare_b);
        cover = clampf(prom_out, 0.0f, 1.0f);
        return true;
    }
    red = clamp_channel(glow);
    green = clamp_channel(glow * (0.42f + spike * 0.35f));
    blue = clamp_channel(glow * (0.08f + spike * 0.28f));
    cover = clampf(fall * 0.9f + spike * fall * 0.4f, 0.0f, 1.0f);
    return cover > 0.04f;
}

static void draw_disc(uint8_t *frame, int stop, float body_cx, float body_cy, float radius, float roll, float alpha) {
    float scx;
    float scy;
    to_screen(0.0f, 0.0f, body_cx, body_cy, roll, scx, scy);
    float pad = radius + 6.0f;
    if (stop == STOP_SATURN) {
        pad = radius * 2.15f;
    } else if (stop == STOP_SUN) {
        pad = radius + 16.0f;
    }
    int x0 = (int)floorf(scx - pad);
    int y0 = (int)floorf(scy - pad);
    int x1 = (int)ceilf(scx + pad);
    int y1 = (int)ceilf(scy + pad);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= PANEL_WIDTH) {
        x1 = PANEL_WIDTH - 1;
    }
    if (y1 >= PANEL_HEIGHT) {
        y1 = PANEL_HEIGHT - 1;
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float lx;
            float ly;
            to_local((float)x, (float)y, body_cx, body_cy, roll, lx, ly);
            float dist = sqrtf(lx * lx + ly * ly);
            bool gap = false;
            float shine = 1.0f;
            bool ring = false;
            if (stop == STOP_SATURN) {
                ring = saturn_ring(lx, ly, radius, gap, shine);
            }
            bool far_ring = ring && ly <= 0.0f && dist > radius;
            if (far_ring && !gap) {
                paint(frame, x, y, (int)(206 * shine), (int)(176 * shine), (int)(118 * shine), alpha * 0.92f);
            }
            if (stop == STOP_URANUS && dist > radius && uranus_ring(lx, ly, radius)) {
                paint(frame, x, y, 170, 230, 226, alpha);
            }
            float cover = 1.0f - smooth_unit(radius - 0.95f, radius + 0.70f, dist);
            if (cover > 0.0f) {
                float nx = lx / radius;
                float ny = ly / radius;
                float nz = sqrtf(fmaxf(0.0f, 1.0f - nx * nx - ny * ny));
                int red = 0;
                int green = 0;
                int blue = 0;
                shade_body(stop, nx, ny, nz, red, green, blue);
                paint(frame, x, y, red, green, blue, alpha * cover);
                if (stop == STOP_SUN && dist > radius - 2.2f) {
                    int ray_r = 0;
                    int ray_g = 0;
                    int ray_b = 0;
                    float ray = 0.0f;
                    float prom = 0.0f;
                    if (corona_colour(lx, ly, radius, ray_r, ray_g, ray_b, ray, prom) && prom > 0.35f) {
                        paint(frame, x, y, ray_r, ray_g, ray_b, alpha * prom);
                    }
                }
            } else if (stop == STOP_EARTH && dist < radius + 1.8f) {
                float fall = 1.0f - (dist - radius) / 1.8f;
                paint(frame, x, y, 86, 156, 226, alpha * fall * 0.9f);
            } else if (stop == STOP_SUN) {
                int red = 0;
                int green = 0;
                int blue = 0;
                float cover_ray = 0.0f;
                float prom = 0.0f;
                if (corona_colour(lx, ly, radius, red, green, blue, cover_ray, prom)) {
                    paint(frame, x, y, red, green, blue, alpha * cover_ray);
                }
            }
            bool near_ring = ring && ly > 0.0f;
            if (near_ring && !gap) {
                paint(frame, x, y, (int)(214 * shine), (int)(184 * shine), (int)(124 * shine), alpha * 0.95f);
            }
        }
    }
}

static void draw_portrait_stars(uint8_t *frame, int stop, float body_cx, float body_cy, float roll, float alpha, uint32_t anim_ms) {
    for (int i = 0; i < PORTRAIT_STARS; ++i) {
        uint32_t n = mix((uint32_t)stop * 1000u + (uint32_t)i * 49u);
        float ang = (n % 1000u) / 1000.0f * TWO_PI;
        float reach = PORTRAIT_R + 5.0f + (float)(n % 11u);
        float sx;
        float sy;
        to_screen(cosf(ang) * reach, sinf(ang) * reach, body_cx, body_cy, roll, sx, sy);
        int level = star_level(n, anim_ms);
        paint_f(frame, sx, sy, level, level, level + (int)(n % 3u), alpha);
    }
}

static void shade_moon(bool luna, float nx, float ny, float nz, float spin, int &red, int &green, int &blue) {
    float lon = atan2f(nx, nz) + spin;
    float lat = asinf(clampf(-ny, -1.0f, 1.0f));
    constexpr float lx = -0.42f;
    constexpr float ly = -0.58f;
    constexpr float lz = 0.70f;
    float light = 0.32f + 0.80f * fmaxf(0.0f, nx * lx + ny * ly + nz * lz);
    float grain = 0.5f + 0.5f * sinf(lon * 7.0f) * sinf(lat * 6.0f);
    if (luna) {
        red = clamp_channel(168.0f + grain * 22.0f);
        green = clamp_channel(164.0f + grain * 20.0f);
        blue = clamp_channel(156.0f + grain * 16.0f);
        mix_ink(red, green, blue, 92, 90, 86, oval_cover(lon, lat, -0.55f, 0.18f, 0.55f, 0.32f));
        mix_ink(red, green, blue, 78, 76, 74, oval_cover(lon, lat, 0.85f, -0.22f, 0.38f, 0.24f));
        mix_ink(red, green, blue, 104, 100, 96, oval_cover(lon, lat, 0.15f, 0.55f, 0.28f, 0.18f));
    } else {
        red = clamp_channel(188.0f + grain * 16.0f);
        green = clamp_channel(166.0f + grain * 14.0f);
        blue = clamp_channel(154.0f + grain * 12.0f);
        mix_ink(red, green, blue, 132, 108, 98, oval_cover(lon, lat, 0.20f, 0.05f, 0.62f, 0.36f));
        mix_ink(red, green, blue, 150, 124, 112, oval_cover(lon, lat, -1.00f, -0.35f, 0.34f, 0.22f));
    }
    constexpr float craters[5][4] = {
        {0.40f, -0.15f, 0.22f, 0.22f},
        {-0.70f, 0.35f, 0.16f, 0.16f},
        {1.40f, 0.10f, 0.18f, 0.14f},
        {-0.20f, -0.55f, 0.14f, 0.14f},
        {0.90f, 0.55f, 0.12f, 0.12f},
    };
    float shift = luna ? 0.0f : 0.70f;
    for (int i = 0; i < 5; ++i) {
        float dx = wrap_pi(lon - (craters[i][0] + shift)) / craters[i][2];
        float dy = (lat - (craters[i][1] - shift * 0.30f)) / craters[i][3];
        float dist = sqrtf(dx * dx + dy * dy);
        float bowl = 1.0f - smooth_unit(0.15f, 0.85f, dist);
        float rim = smooth_unit(0.72f, 0.92f, dist) * (1.0f - smooth_unit(0.92f, 1.18f, dist));
        if (luna) {
            mix_ink(red, green, blue, 96, 94, 90, bowl);
            mix_ink(red, green, blue, 214, 210, 202, rim);
        } else {
            mix_ink(red, green, blue, 118, 96, 86, bowl);
            mix_ink(red, green, blue, 226, 206, 194, rim);
        }
    }
    red = clamp_channel((float)red * light);
    green = clamp_channel((float)green * light);
    blue = clamp_channel((float)blue * light);
}

static void draw_moon_disc(uint8_t *frame, int index, float body_cx, float body_cy, float roll, float alpha, bool behind) {
    float ang = moon_angle[index];
    float lx = cosf(ang) * MOONS[index].orbit;
    float ly = sinf(ang) * MOONS[index].orbit * 0.72f;
    float radius = MOONS[index].radius;
    float scx;
    float scy;
    to_screen(lx, ly, body_cx, body_cy, roll, scx, scy);
    int x0 = (int)floorf(scx - radius - 1.0f);
    int y0 = (int)floorf(scy - radius - 1.0f);
    int x1 = (int)ceilf(scx + radius + 1.0f);
    int y1 = (int)ceilf(scy + radius + 1.0f);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= PANEL_WIDTH) {
        x1 = PANEL_WIDTH - 1;
    }
    if (y1 >= PANEL_HEIGHT) {
        y1 = PANEL_HEIGHT - 1;
    }
    bool luna = MOONS[index].stop == STOP_EARTH;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float px;
            float py;
            to_local((float)x, (float)y, body_cx, body_cy, roll, px, py);
            if (behind && px * px + py * py < PORTRAIT_R * PORTRAIT_R) {
                continue;
            }
            float mx = px - lx;
            float my = py - ly;
            float dist = sqrtf(mx * mx + my * my);
            float cover = 1.0f - smooth_unit(radius - 0.85f, radius + 0.55f, dist);
            if (cover <= 0.0f) {
                continue;
            }
            float nx = mx / radius;
            float ny = my / radius;
            float nz = sqrtf(fmaxf(0.0f, 1.0f - nx * nx - ny * ny));
            int red = 0;
            int green = 0;
            int blue = 0;
            shade_moon(luna, nx, ny, nz, moon_spin[index], red, green, blue);
            paint(frame, x, y, red, green, blue, alpha * cover);
        }
    }
}

static void draw_moons(uint8_t *frame, int stop, float body_cx, float body_cy, float roll, float alpha, bool front) {
    for (int i = 0; i < MOON_COUNT; ++i) {
        if (MOONS[i].stop != stop) {
            continue;
        }
        float ang = moon_angle[i];
        if (MOONS[i].radius > 1.0f) {
            bool ahead = sinf(ang) > 0.0f;
            if (ahead != front) {
                continue;
            }
            draw_moon_disc(frame, i, body_cx, body_cy, roll, alpha, !front);
            continue;
        }
        if (front) {
            continue;
        }
        float lx = cosf(ang) * MOONS[i].orbit;
        float ly = sinf(ang) * MOONS[i].orbit * 0.72f;
        if (lx * lx + ly * ly < (PORTRAIT_R + 2.0f) * (PORTRAIT_R + 2.0f)) {
            continue;
        }
        float sx;
        float sy;
        to_screen(lx, ly, body_cx, body_cy, roll, sx, sy);
        paint_f(frame, sx, sy, MOONS[i].r, MOONS[i].g, MOONS[i].b, alpha);
    }
}

static void draw_rock(uint8_t *frame, float cx, float cy, float radius, float depth, bool crater, float alpha) {
    int reach = (int)ceilf(radius);
    int base_r = (int)(150 * depth);
    int base_g = (int)(118 * depth);
    int base_b = (int)(78 * depth);
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            float d2 = (float)(x * x + y * y);
            if (d2 > radius * radius) {
                continue;
            }
            float nz = sqrtf(fmaxf(0.0f, 1.0f - d2 / (radius * radius)));
            float light = 0.35f + 0.75f * fmaxf(0.0f, -x / radius * 0.45f - y / radius * 0.35f + nz * 0.75f);
            int red = clamp_channel(base_r * light);
            int green = clamp_channel(base_g * light);
            int blue = clamp_channel(base_b * light);
            if (crater && x >= -2 && x <= -1 && y >= -2 && y <= -1) {
                red = clamp_channel(40 * light);
                green = clamp_channel(28 * light);
                blue = clamp_channel(18 * light);
            }
            paint(frame, (int)lroundf(cx) + x, (int)lroundf(cy) + y, red, green, blue, alpha);
        }
    }
}

static void draw_belt(uint8_t *frame, float body_cx, float body_cy, float roll, float alpha, uint32_t anim_ms) {
    draw_portrait_stars(frame, STOP_BELT, body_cx, body_cy, roll, alpha, anim_ms);
    int order[ROCKS];
    for (int i = 0; i < ROCKS; ++i) {
        order[i] = i;
    }
    for (int i = 1; i < ROCKS; ++i) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && rock_depth[order[j]] > rock_depth[key]) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
    for (int n = 0; n < ROCKS; ++n) {
        int i = order[n];
        float sx;
        float sy;
        to_screen(rock_x[i] - CX, rock_y[i] - CY, body_cx, body_cy, roll, sx, sy);
        draw_rock(frame, sx, sy, rock_rad[i] * (0.65f + 0.35f * rock_depth[i]), rock_depth[i], rock_crater[i] != 0, alpha);
    }
}

static void draw_portrait(uint8_t *frame, int stop, float offset, float alpha, float roll, uint32_t anim_ms) {
    float body_cx = CX + offset;
    float body_cy = CY;
    if (stop == STOP_BELT) {
        draw_belt(frame, body_cx, body_cy, roll, alpha, anim_ms);
        return;
    }
    draw_portrait_stars(frame, stop, body_cx, body_cy, roll, alpha, anim_ms);
    bool has_moons = stop != STOP_SUN && stop != STOP_MERCURY && stop != STOP_VENUS;
    if (has_moons) {
        draw_moons(frame, stop, body_cx, body_cy, roll, alpha, false);
    }
    float radius = stop == STOP_SUN ? 14.0f : PORTRAIT_R;
    draw_disc(frame, stop, body_cx, body_cy, radius, roll, alpha);
    if (has_moons) {
        draw_moons(frame, stop, body_cx, body_cy, roll, alpha, true);
    }
}

static void draw_portraits(uint8_t *frame, float alpha, float roll, uint32_t anim_ms) {
    int i0 = (int)floorf(shown);
    for (int step = 0; step < 2; ++step) {
        int stop = i0 + step;
        if (stop < STOP_SUN || stop > STOP_PLUTO) {
            continue;
        }
        float offset = ((float)stop - shown) * (float)PANEL_WIDTH;
        if (offset > 78.0f || offset < -78.0f) {
            continue;
        }
        draw_portrait(frame, stop, offset, alpha, roll, anim_ms);
    }
}

static bool passes_wide(int from, int steps) {
    int dir = steps > 0 ? 1 : -1;
    int count = steps > 0 ? steps : -steps;
    int index = from;
    for (int i = 0; i < count; ++i) {
        index += dir;
        if (index >= STOP_COUNT) {
            index = 0;
        } else if (index < 0) {
            index = STOP_COUNT - 1;
        }
        if (index == STOP_WIDE) {
            return true;
        }
    }
    return false;
}

static void turn_stops(int steps);

static void step_tour(float dt) {
    if (!touring) {
        return;
    }
    tour_time += dt;
    float shown_goal = target == STOP_WIDE ? (float)depart : (float)target;
    float zoom_goal = target == STOP_WIDE ? 0.0f : 1.0f;
    float shown_gap = shown - shown_goal;
    if (shown_gap < 0.0f) {
        shown_gap = -shown_gap;
    }
    float zoom_gap = zoom - zoom_goal;
    if (zoom_gap < 0.0f) {
        zoom_gap = -zoom_gap;
    }
    float shown_speed = shown_v < 0.0f ? -shown_v : shown_v;
    float zoom_speed = zoom_v < 0.0f ? -zoom_v : zoom_v;
    if (shown_gap > 0.05f || zoom_gap > 0.05f || shown_speed > 0.2f || zoom_speed > 0.2f) {
        tour_wait = true;
        return;
    }
    if (tour_wait) {
        tour_wait = false;
        tour_left = TOUR_DWELL_S;
    }
    tour_left -= dt;
    if (tour_left > 0.0f) {
        return;
    }
    tour_wait = true;
    turn_stops(1);
}

static void step_motion(float dt) {
    if (spin_held && target == STOP_WIDE) {
        if (orrery_vel < ORRERY_HOLD) {
            orrery_vel = ORRERY_HOLD;
        }
    }
    if (spin_held && target > STOP_WIDE && target < STOP_COUNT) {
        if (spin_vel[target] < SPIN_HOLD) {
            spin_vel[target] = SPIN_HOLD;
        }
    }
    float shown_goal = target == STOP_WIDE ? (float)depart : (float)target;
    float zoom_goal = target == STOP_WIDE ? 0.0f : 1.0f;
    shown_v += ((shown_goal - shown) * SPRING_K - shown_v * SPRING_D) * dt;
    shown += shown_v * dt;
    zoom_v += ((zoom_goal - zoom) * SPRING_K - zoom_v * SPRING_D) * dt;
    zoom += zoom_v * dt;

    for (int stop = STOP_MERCURY; stop <= STOP_PLUTO; ++stop) {
        if (stop == STOP_BELT) {
            continue;
        }
        orbit_angle[stop] = wrap_tau(orbit_angle[stop] + (TWO_PI / PERIOD_S[stop] + orrery_vel) * dt);
    }
    belt_anchor = wrap_tau(belt_anchor + (TWO_PI / 180.0f + orrery_vel) * dt);
    for (int i = 0; i < BELT_DOTS; ++i) {
        belt_dot[i] = wrap_tau(belt_dot[i] + (TWO_PI / belt_period[i] + orrery_vel) * dt);
    }
    float push = spin_vel[STOP_BELT] * 6.0f;
    for (int i = 0; i < ROCKS; ++i) {
        rock_x[i] += (rock_speed[i] + push) * dt;
        rock_x[i] = rock_x[i] - (float)PANEL_WIDTH * floorf(rock_x[i] / (float)PANEL_WIDTH);
        if (rock_x[i] < 0.0f) {
            rock_x[i] += PANEL_WIDTH;
        }
    }
    for (int stop = STOP_SUN; stop <= STOP_PLUTO; ++stop) {
        spin_angle[stop] = wrap_tau(spin_angle[stop] + (IDLE_SPIN[stop] + spin_vel[stop]) * dt);
    }
    cloud_time += dt;
    for (int i = 0; i < MOON_COUNT; ++i) {
        bool detailed = MOONS[i].radius > 1.0f;
        bool boost = detailed && (touring || (spin_held && target == MOONS[i].stop));
        if (boost) {
            if (moon_orbit_vel[i] < 0.9f) {
                moon_orbit_vel[i] = 0.9f;
            }
            if (moon_spin_vel[i] < 1.6f) {
                moon_spin_vel[i] = 1.6f;
            }
        }
        moon_angle[i] = wrap_tau(moon_angle[i] + (TWO_PI / MOONS[i].period + moon_orbit_vel[i]) * dt);
        if (MOONS[i].spin_period > 0.0f) {
            moon_spin[i] = wrap_tau(moon_spin[i] + (TWO_PI / MOONS[i].spin_period + moon_spin_vel[i]) * dt);
        }
        if (!boost) {
            moon_orbit_vel[i] *= expf(-dt / 0.45f);
            moon_spin_vel[i] *= expf(-dt / 0.45f);
        }
    }
    if (!(spin_held && target == STOP_WIDE)) {
        orrery_vel *= expf(-dt / 2.0f);
    }
    for (int stop = STOP_SUN; stop <= STOP_PLUTO; ++stop) {
        if (spin_held && target == stop) {
            continue;
        }
        spin_vel[stop] *= expf(-dt / 0.45f);
    }
    step_tour(dt);
}

static void advance(int dt_ms) {
    float left = dt_ms * 0.001f;
    if (left < 0.0f) {
        left = 0.0f;
    }
    if (left > 0.2f) {
        left = 0.2f;
    }
    while (left > 0.0001f) {
        float dt = left > 0.02f ? 0.02f : left;
        left -= dt;
        step_motion(dt);
    }
}

static void turn_stops(int steps) {
    if (steps == 0) {
        return;
    }
    int from = target;
    int next = from + steps;
    next %= STOP_COUNT;
    if (next < 0) {
        next += STOP_COUNT;
    }
    bool zoom_move = from == STOP_WIDE || next == STOP_WIDE || passes_wide(from, steps);
    if (zoom_move) {
        if (from != STOP_WIDE) {
            depart = from;
        }
        if (next != STOP_WIDE) {
            shown = (float)next;
            shown_v = 0.0f;
            depart = next;
            if (from != STOP_WIDE) {
                zoom = 0.12f;
                zoom_v = 0.0f;
            }
        } else {
            shown = (float)depart;
            shown_v = 0.0f;
        }
    }
    target = next;
}

static void init_field() {
    for (int i = 0; i < BELT_DOTS; ++i) {
        uint32_t n = mix(1000u + (uint32_t)i * 17u);
        belt_dot[i] = (n % 6283u) * 0.001f;
        belt_period[i] = 140.0f + (float)(n % 100u);
        belt_jitter[i] = ((int)(n % 5u) - 2) * 0.55f;
        belt_lift[i] = ((int)((n >> 12) % 9u) - 4) * 0.55f;
    }
    for (int i = 0; i < ROCKS; ++i) {
        uint32_t n = mix(400u + (uint32_t)i * 17u);
        rock_y[i] = 6.0f + (float)(n % 52u);
        rock_x[i] = (float)(n % 64u);
        rock_speed[i] = 3.0f + (float)(n % 13u);
        if ((n & 1u) != 0u) {
            rock_speed[i] = -rock_speed[i];
        }
        rock_depth[i] = 0.45f + (float)((n >> 8) % 10u) * 0.05f;
        rock_rad[i] = 1.15f + (float)((n >> 12) % 8u) * 0.14f;
        rock_crater[i] = 0;
    }
    rock_rad[2] = 3.4f;
    rock_rad[7] = 3.1f;
    rock_crater[2] = 1;
    rock_crater[7] = 1;
    rock_depth[2] = 1.0f;
    rock_depth[7] = 0.92f;
}

static void reset_motion() {
    target = STOP_WIDE;
    depart = STOP_SUN;
    shown = 1.0f;
    shown_v = 0.0f;
    zoom = 0.0f;
    zoom_v = 0.0f;
    spin_held = false;
    touring = false;
    tour_wait = true;
    tour_left = TOUR_DWELL_S;
    tour_time = 0.0f;
    tour_blend = 0.0f;
    cam_roll = 0.0f;
    orrery_vel = 0.0f;
    cam_pitch = CAM_PITCH;
    cam_yaw = 0.0f;
    cloud_time = 0.0f;
    belt_anchor = PHASE0[STOP_BELT];
    for (int stop = 0; stop < STOP_COUNT; ++stop) {
        spin_angle[stop] = 0.0f;
        spin_vel[stop] = 0.0f;
        orbit_angle[stop] = PHASE0[stop];
    }
    for (int i = 0; i < MOON_COUNT; ++i) {
        moon_angle[i] = MOONS[i].phase;
        moon_spin[i] = 0.0f;
        moon_orbit_vel[i] = 0.0f;
        moon_spin_vel[i] = 0.0f;
    }
    init_field();
    init_flares();
}

}  // namespace

void solar_start() {
    reset_motion();
    ready = true;
}

void solar_ensure() {
    if (!ready) {
        solar_start();
    }
}

void solar_step(int dt_ms) {
    advance(dt_ms);
    step_flares(dt_ms * 0.001f);
    follow_hand(dt_ms * 0.001f);
}

void solar_paint_wide(uint8_t *frame, uint32_t anim_ms) {
    std::memset(frame, 0, PANEL_PIXELS * 3);
    draw_orrery(frame, anim_ms, 0.0f, cam_roll, 1.0f);
}

void solar_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    solar_step(dt_ms);
    std::memset(frame, 0, PANEL_PIXELS * 3);
    float draw_zoom = clampf(zoom, 0.0f, 1.0f);
    float roll = sinf(draw_zoom * PI) * 0.21f + cam_roll;
    float orrery_alpha = 1.0f - smooth_unit(0.28f, 0.88f, draw_zoom);
    float portrait_alpha = smooth_unit(0.42f, 0.95f, draw_zoom);
    if (orrery_alpha > 0.02f) {
        draw_orrery(frame, anim_ms, draw_zoom, roll, orrery_alpha);
    }
    if (portrait_alpha > 0.02f) {
        draw_portraits(frame, portrait_alpha, roll, anim_ms);
    }
}

bool solar_orrery_ready() {
    float depth = zoom < 0.0f ? -zoom : zoom;
    float speed = zoom_v < 0.0f ? -zoom_v : zoom_v;
    return target == STOP_WIDE && depth < 0.06f && speed < 0.35f;
}

void solar_show_orrery() {
    if (target == STOP_WIDE) {
        return;
    }
    turn_stops(STOP_COUNT - target);
}

bool solar_touring() {
    return touring;
}

void solar_tour_stop() {
    touring = false;
}

void solar_tour_toggle() {
    if (touring) {
        touring = false;
        return;
    }
    touring = true;
    tour_wait = true;
    if (target != STOP_WIDE) {
        turn_stops(STOP_COUNT - target);
    }
}

void solar_turn(int steps) {
    touring = false;
    turn_stops(steps);
}

void solar_spin_tap() {
    if (target == STOP_WIDE) {
        orrery_vel += ORRERY_KICK;
        return;
    }
    if (target <= STOP_WIDE || target >= STOP_COUNT) {
        return;
    }
    spin_vel[target] += SPIN_KICK;
    if (target != STOP_EARTH && target != STOP_PLUTO) {
        return;
    }
    for (int i = 0; i < MOON_COUNT; ++i) {
        if (MOONS[i].stop != target || MOONS[i].radius <= 1.0f) {
            continue;
        }
        moon_orbit_vel[i] += 1.1f;
        moon_spin_vel[i] += 1.4f;
    }
}

void solar_spin_hold() {
    spin_held = true;
}

void solar_spin_release() {
    spin_held = false;
}
