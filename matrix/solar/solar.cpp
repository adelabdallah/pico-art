// An orrery on the wide view, and a close portrait of each world.
// The encoder walks the stops. Red flings the portrait's spin, or the whole orrery on the wide view.
// Orbit spacing is even so the rings stay apart. Periods follow the real order, with the outer worlds compressed into a few minutes.
// Each portrait carries one landmark large enough to name the body on a 64 by 64 panel.

#include "pattern.hpp"

#include <cmath>
#include <cstdint>
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
constexpr float SQUASH = 0.70f;
constexpr float PORTRAIT_R = 13.0f;
constexpr float SPRING_K = 80.0f;
constexpr float SPRING_D = 11.0f;
constexpr float SPIN_HOLD = 3.14159265f;
constexpr float ORRERY_HOLD = 0.78539816f;
constexpr float SPIN_KICK = 2.2f;
constexpr float ORRERY_KICK = 0.85f;
constexpr int WIDE_STARS = 36;
constexpr int PORTRAIT_STARS = 24;
constexpr int BELT_DOTS = 32;
constexpr int ROCKS = 12;

// Semi-major axis in pixels. The sun sits at the centre. Pluto's ring clips the side edges.
constexpr float ORBIT_R[STOP_COUNT] = {
    0.0f, 0.0f, 6.0f, 9.2f, 12.4f, 15.6f, 18.8f, 22.0f, 25.2f, 28.4f, 31.6f, 34.8f,
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
};

constexpr Moon MOONS[] = {
    {STOP_EARTH, 20.5f, 20.0f, 0.2f, 200, 196, 180},
    {STOP_MARS, 17.5f, 9.0f, 1.0f, 150, 130, 110},
    {STOP_MARS, 22.0f, 16.0f, 2.4f, 110, 100, 90},
    {STOP_JUPITER, 17.0f, 7.0f, 0.3f, 210, 196, 160},
    {STOP_JUPITER, 19.5f, 12.0f, 1.5f, 180, 196, 210},
    {STOP_JUPITER, 22.0f, 18.0f, 3.0f, 220, 200, 170},
    {STOP_JUPITER, 25.0f, 30.0f, 4.2f, 160, 150, 140},
    {STOP_SATURN, 18.0f, 14.0f, 0.6f, 210, 190, 150},
    {STOP_SATURN, 21.0f, 22.0f, 2.1f, 180, 170, 145},
    {STOP_SATURN, 24.0f, 34.0f, 4.0f, 150, 165, 175},
    {STOP_URANUS, 18.5f, 16.0f, 0.8f, 180, 220, 220},
    {STOP_URANUS, 22.5f, 26.0f, 2.6f, 160, 200, 210},
    {STOP_NEPTUNE, 20.0f, 18.0f, 1.2f, 170, 190, 210},
    {STOP_PLUTO, 19.0f, 14.0f, 0.5f, 220, 205, 195},
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
static bool red_held = false;
static float orrery_vel = 0.0f;
static float spin_angle[STOP_COUNT];
static float spin_vel[STOP_COUNT];
static float orbit_angle[STOP_COUNT];
static float belt_anchor = 0.8f;
static float belt_dot[BELT_DOTS];
static float belt_period[BELT_DOTS];
static float belt_jitter[BELT_DOTS];
static float rock_x[ROCKS];
static float rock_y[ROCKS];
static float rock_speed[ROCKS];
static float rock_depth[ROCKS];
static float rock_rad[ROCKS];
static int rock_crater[ROCKS];
static float moon_angle[MOON_COUNT];

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

static bool in_oval(float lon, float lat, float lon0, float lat0, float rx, float ry) {
    float dx = wrap_pi(lon - lon0) / rx;
    float dy = (lat - lat0) / ry;
    return dx * dx + dy * dy <= 1.0f;
}

static bool on_land(float lon, float lat, int &col) {
    float turns = wrap_tau(lon) / TWO_PI;
    col = (int)(turns * 24.0f);
    if (col < 0) {
        col = 0;
    }
    if (col > 23) {
        col = 23;
    }
    float south = (lat + HALF_PI) / PI;
    int row_s = (int)(south * 12.0f);
    if (row_s < 0) {
        row_s = 0;
    }
    if (row_s > 11) {
        row_s = 11;
    }
    int row = 11 - row_s;
    return EARTH_ROWS[row][col] == '#';
}

static bool earth_cloud(float lon, float lat) {
    constexpr float fleck[][3] = {
        {5.60f, 0.45f, 0.15f},
        {3.60f, -0.35f, 0.14f},
        {3.30f, 0.70f, 0.13f},
        {1.30f, -0.25f, 0.14f},
        {4.20f, -0.55f, 0.12f},
    };
    for (int i = 0; i < 5; ++i) {
        float dx = wrap_pi(lon - fleck[i][0]);
        float dy = lat - fleck[i][1];
        float reach = fleck[i][2];
        if (dx * dx + dy * dy < reach * reach) {
            return true;
        }
    }
    return false;
}

static void colour_earth(float lon, float lat, int &red, int &green, int &blue) {
    int col = 0;
    bool land = on_land(lon, lat, col);
    if (land) {
        bool africa = col <= 2 || col == 23;
        if (lat > 0.15f && lat < 0.55f && africa) {
            red = 176;
            green = 142;
            blue = 72;
        } else if (lat > 0.55f) {
            red = 132;
            green = 108;
            blue = 58;
        } else {
            red = 42;
            green = 132;
            blue = 52;
        }
    } else {
        red = 16;
        green = 54;
        blue = 176;
    }
    if (earth_cloud(lon, lat)) {
        red = 226;
        green = 232;
        blue = 238;
    }
}

static void colour_sun(float lon, float lat, float nz, int &red, int &green, int &blue) {
    float grain = sinf(lon * 5.0f) * sinf(lat * 4.0f);
    float glow = 0.75f + 0.25f * nz;
    float warm = 0.70f + 0.30f * grain;
    red = clamp_channel((220.0f + warm * 35.0f) * glow);
    green = clamp_channel((120.0f + warm * 110.0f) * glow);
    blue = clamp_channel((24.0f + warm * 50.0f) * glow);
    for (int i = 0; i < 4; ++i) {
        const Oval &spot = SUNSPOTS[i];
        if (in_oval(lon, lat, spot.lon, spot.lat, spot.rx, spot.ry)) {
            red = clamp_channel(92 * glow);
            green = clamp_channel(42 * glow);
            blue = clamp_channel(16 * glow);
        }
    }
}

static void colour_mercury(float lon, float lat, int &red, int &green, int &blue) {
    red = 156;
    green = 150;
    blue = 144;
    for (int i = 0; i < 5; ++i) {
        const Oval &crater = CRATERS[i];
        float dx = wrap_pi(lon - crater.lon) / crater.rx;
        float dy = (lat - crater.lat) / crater.ry;
        float d2 = dx * dx + dy * dy;
        if (d2 <= 1.0f) {
            red = 72;
            green = 68;
            blue = 64;
        }
    }
}

static void colour_venus(float lon, float lat, int &red, int &green, int &blue) {
    float band = sinf(lat * 5.0f + sinf(lon * 2.0f) * 0.7f);
    float lift = 0.5f + 0.5f * band;
    red = clamp_channel(168 + lift * 80);
    green = clamp_channel(104 + lift * 110);
    blue = clamp_channel(32 + lift * 48);
}

static void colour_mars(float lon, float lat, int &red, int &green, int &blue) {
    red = 198;
    green = 74;
    blue = 36;
    if (in_oval(lon, lat, 0.45f, 0.15f, 0.55f, 0.32f)) {
        red = 112;
        green = 42;
        blue = 28;
    }
}

static void colour_bands(float lon, float lat, const int bands[5][3], int &red, int &green, int &blue) {
    float u = (lat + HALF_PI) / PI;
    int band = (int)(u * 5.0f);
    if (band < 0) {
        band = 0;
    }
    if (band > 4) {
        band = 4;
    }
    float ripple = 0.92f + 0.08f * sinf(lon * 3.0f + band);
    red = clamp_channel(bands[band][0] * ripple);
    green = clamp_channel(bands[band][1] * ripple);
    blue = clamp_channel(bands[band][2] * ripple);
}

static void colour_jupiter(float lon, float lat, int &red, int &green, int &blue) {
    constexpr int bands[5][3] = {
        {176, 96, 40},
        {236, 208, 158},
        {168, 84, 34},
        {244, 216, 172},
        {186, 104, 46},
    };
    colour_bands(lon, lat, bands, red, green, blue);
    // About 5 px wide, inside the southern cream band.
    if (in_oval(lon, lat, 0.15f, -0.62f, 0.46f, 0.15f)) {
        red = 214;
        green = 62;
        blue = 40;
    }
}

static void colour_saturn(float lon, float lat, int &red, int &green, int &blue) {
    constexpr int bands[5][3] = {
        {196, 164, 108},
        {228, 204, 150},
        {186, 154, 98},
        {236, 214, 164},
        {204, 174, 118},
    };
    colour_bands(lon, lat, bands, red, green, blue);
    if (in_oval(lon, lat, 0.4f, 0.32f, 0.22f, 0.10f)) {
        red = 244;
        green = 228;
        blue = 186;
    }
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
    float hood = smooth_unit(0.78f, 0.96f, pole);
    float band = 0.78f + 0.22f * sinf(pole * 7.0f);
    red = clamp_channel(148 * band);
    green = clamp_channel(214 * band);
    blue = clamp_channel(206 * band);
    if (fabsf(wrap_pi(lon)) < 0.20f) {
        red = clamp_channel(red + 28);
        green = clamp_channel(green + 24);
        blue = clamp_channel(blue + 22);
    }
    red = (int)(red + (232 - red) * hood);
    green = (int)(green + (248 - green) * hood);
    blue = (int)(blue + (244 - blue) * hood);
    light = light + hood * (1.05f - light);
}

static void colour_neptune(float lon, float lat, int &red, int &green, int &blue) {
    red = 24;
    green = 58;
    blue = 196;
    if (in_oval(lon, lat, 0.25f, 0.12f, 0.30f, 0.16f)) {
        red = 10;
        green = 22;
        blue = 78;
    }
    float dx = wrap_pi(lon - 1.1f);
    if (fabsf(dx) < 0.55f && fabsf(lat - 0.48f) < 0.07f) {
        red = 226;
        green = 236;
        blue = 248;
    }
}

static bool pluto_heart(float lon, float lat) {
    // Two lobes, then a point. Each cell is large enough to survive the disc.
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
    int col = (int)col_f;
    int row = (int)row_f;
    if (col < 0 || col > 8 || row < 0 || row > 7) {
        return false;
    }
    return rows[row][col] == '#';
}

static void colour_pluto(float lon, float lat, int &red, int &green, int &blue) {
    float mottled = sinf(lon * 5.0f) * sinf(lat * 4.0f);
    red = clamp_channel(108 + mottled * 16);
    green = clamp_channel(78 + mottled * 10);
    blue = clamp_channel(62 + mottled * 8);
    if (pluto_heart(lon, lat)) {
        red = 236;
        green = 204;
        blue = 190;
    }
}

static void shade_body(int stop, float nx, float ny, float nz, int &red, int &green, int &blue) {
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
}

static void focus_world(int focus, float &wx, float &wy) {
    if (focus <= STOP_SUN) {
        wx = CX;
        wy = CY;
        return;
    }
    float angle = focus == STOP_BELT ? belt_anchor : orbit_angle[focus];
    float radius = ORBIT_R[focus];
    wx = CX + cosf(angle) * radius;
    wy = CY + sinf(angle) * radius * SQUASH;
}

// zoom 0 leaves every world where it is. zoom 1 parks the focus body on the centre and spreads the rings.
static void project_world(float wx, float wy, float fx, float fy, float zoom_now, float roll, float &sx, float &sy) {
    float scale = 1.0f + zoom_now * 2.4f;
    sx = fx + (wx - fx) * scale + (CX - fx) * zoom_now;
    sy = fy + (wy - fy) * scale + (CY - fy) * zoom_now;
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
        float wx = (float)(n % 64u);
        float wy = (float)((n >> 8) % 64u);
        float scale = 1.0f + zoom_now * 0.35f * 2.4f;
        float sx = CX + (wx - CX) * scale;
        float sy = CY + (wy - CY) * scale;
        float dx = sx - CX;
        float dy = sy - CY;
        float c = cosf(roll);
        float s = sinf(roll);
        sx = CX + dx * c - dy * s;
        sy = CY + dx * s + dy * c;
        int level = star_level(n, anim_ms);
        int blue = level + (int)(n % 3u);
        paint_f(frame, sx, sy, level, level, blue, alpha);
    }
}

static void draw_orbit(uint8_t *frame, float radius, int red, int green, int blue, float fx, float fy, float zoom_now, float roll, float alpha) {
    int steps = (int)(radius * 6.0f);
    if (steps < 48) {
        steps = 48;
    }
    for (int i = 0; i < steps; ++i) {
        float angle = TWO_PI * (float)i / (float)steps;
        float wx = CX + cosf(angle) * radius;
        float wy = CY + sinf(angle) * radius * SQUASH;
        float sx;
        float sy;
        project_world(wx, wy, fx, fy, zoom_now, roll, sx, sy);
        paint_f(frame, sx, sy, red, green, blue, alpha);
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

static void draw_orrery(uint8_t *frame, uint32_t anim_ms, float zoom_now, float roll, float alpha) {
    draw_wide_stars(frame, anim_ms, zoom_now, roll, alpha);
    int focus = target == STOP_WIDE ? depart : target;
    float fx;
    float fy;
    focus_world(focus, fx, fy);
    for (int stop = STOP_MERCURY; stop <= STOP_PLUTO; ++stop) {
        const Ink &ink = INK[stop];
        draw_orbit(frame, ORBIT_R[stop], ink.r * 4 / 10, ink.g * 4 / 10, ink.b * 4 / 10, fx, fy, zoom_now, roll, alpha);
    }
    for (int i = 0; i < BELT_DOTS; ++i) {
        float radius = ORBIT_R[STOP_BELT] + belt_jitter[i];
        float angle = belt_dot[i];
        float wx = CX + cosf(angle) * radius;
        float wy = CY + sinf(angle) * radius * SQUASH;
        float sx;
        float sy;
        project_world(wx, wy, fx, fy, zoom_now, roll, sx, sy);
        paint_f(frame, sx, sy, 132, 112, 74, alpha);
    }
    for (int stop = STOP_MERCURY; stop <= STOP_PLUTO; ++stop) {
        if (stop == STOP_BELT) {
            continue;
        }
        float angle = orbit_angle[stop];
        float wx = CX + cosf(angle) * ORBIT_R[stop];
        float wy = CY + sinf(angle) * ORBIT_R[stop] * SQUASH;
        float sx;
        float sy;
        project_world(wx, wy, fx, fy, zoom_now, roll, sx, sy);
        const Ink &ink = INK[stop];
        if (stop == STOP_JUPITER || stop == STOP_SATURN) {
            draw_blob(frame, sx, sy, 1, ink.r, ink.g, ink.b, alpha);
        } else {
            paint_f(frame, sx, sy, ink.r, ink.g, ink.b, alpha);
        }
        if (stop == STOP_SATURN) {
            for (int k = -2; k <= 2; ++k) {
                paint_f(frame, sx + k * 0.95f, sy + k * 0.32f, 210, 180, 120, alpha);
            }
        }
    }
    float sun_x;
    float sun_y;
    project_world(CX, CY, fx, fy, zoom_now, roll, sun_x, sun_y);
    draw_blob(frame, sun_x, sun_y, 2, 255, 210, 80, alpha);
}

static bool saturn_ring(float x, float y, float radius, bool &gap) {
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

static void draw_disc(uint8_t *frame, int stop, float body_cx, float body_cy, float radius, float roll, float alpha) {
    float scx;
    float scy;
    to_screen(0.0f, 0.0f, body_cx, body_cy, roll, scx, scy);
    float pad = stop == STOP_SATURN ? radius * 2.15f : radius + 6.0f;
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
            bool ring = false;
            if (stop == STOP_SATURN) {
                ring = saturn_ring(lx, ly, radius, gap);
            }
            bool far_ring = ring && ly <= 0.0f && dist > radius;
            if (far_ring && !gap) {
                paint(frame, x, y, 206, 176, 118, alpha * 0.92f);
            }
            if (stop == STOP_URANUS && dist > radius && uranus_ring(lx, ly, radius)) {
                paint(frame, x, y, 170, 230, 226, alpha);
            }
            float cover = 1.0f - smooth_unit(radius - 0.35f, radius + 0.55f, dist);
            if (cover > 0.0f) {
                float nx = lx / radius;
                float ny = ly / radius;
                float nz = sqrtf(fmaxf(0.0f, 1.0f - nx * nx - ny * ny));
                int red = 0;
                int green = 0;
                int blue = 0;
                shade_body(stop, nx, ny, nz, red, green, blue);
                paint(frame, x, y, red, green, blue, alpha * cover);
            } else if (stop == STOP_SUN && dist < radius + 5.5f && dist > radius) {
                float ang = atan2f(ly, lx);
                float ray = 0.62f + 0.38f * sinf(ang * 9.0f + spin_angle[STOP_SUN]);
                float fall = 1.0f - (dist - radius) / 5.5f;
                int glow = (int)(fall * ray * 70.0f);
                paint(frame, x, y, glow + 30, glow, glow / 5, alpha * fall);
            }
            bool near_ring = ring && ly > 0.0f;
            if (near_ring && !gap) {
                paint(frame, x, y, 214, 184, 124, alpha * 0.95f);
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

static void draw_moons(uint8_t *frame, int stop, float body_cx, float body_cy, float roll, float alpha) {
    for (int i = 0; i < MOON_COUNT; ++i) {
        if (MOONS[i].stop != stop) {
            continue;
        }
        float ang = moon_angle[i];
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
    if (stop != STOP_SUN && stop != STOP_MERCURY && stop != STOP_VENUS) {
        draw_moons(frame, stop, body_cx, body_cy, roll, alpha);
    }
    float radius = stop == STOP_SUN ? 14.0f : PORTRAIT_R;
    draw_disc(frame, stop, body_cx, body_cy, radius, roll, alpha);
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

static void step_motion(float dt) {
    if (red_held) {
        if (target == STOP_WIDE) {
            if (orrery_vel < ORRERY_HOLD) {
                orrery_vel = ORRERY_HOLD;
            }
        } else if (spin_vel[target] < SPIN_HOLD) {
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
    for (int i = 0; i < MOON_COUNT; ++i) {
        moon_angle[i] = wrap_tau(moon_angle[i] + TWO_PI / MOONS[i].period * dt);
    }
    if (!(red_held && target == STOP_WIDE)) {
        orrery_vel *= expf(-dt / 2.0f);
    }
    for (int stop = STOP_SUN; stop <= STOP_PLUTO; ++stop) {
        if (red_held && target == stop) {
            continue;
        }
        spin_vel[stop] *= expf(-dt / 0.45f);
    }
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
    red_held = false;
    orrery_vel = 0.0f;
    belt_anchor = PHASE0[STOP_BELT];
    for (int stop = 0; stop < STOP_COUNT; ++stop) {
        spin_angle[stop] = 0.0f;
        spin_vel[stop] = 0.0f;
        orbit_angle[stop] = PHASE0[stop];
    }
    for (int i = 0; i < MOON_COUNT; ++i) {
        moon_angle[i] = MOONS[i].phase;
    }
    init_field();
}

}  // namespace

void solar_start() {
    reset_motion();
}

void solar_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    advance(dt_ms);
    std::memset(frame, 0, PANEL_PIXELS * 3);
    float draw_zoom = clampf(zoom, 0.0f, 1.0f);
    float roll = sinf(draw_zoom * PI) * 0.21f;
    float orrery_alpha = 1.0f - smooth_unit(0.28f, 0.88f, draw_zoom);
    float portrait_alpha = smooth_unit(0.42f, 0.95f, draw_zoom);
    if (orrery_alpha > 0.02f) {
        draw_orrery(frame, anim_ms, draw_zoom, roll, orrery_alpha);
    }
    if (portrait_alpha > 0.02f) {
        draw_portraits(frame, portrait_alpha, roll, anim_ms);
    }
}

void solar_turn(int steps) {
    turn_stops(steps);
}

void solar_red_tap() {
    if (target == STOP_WIDE) {
        orrery_vel += ORRERY_KICK;
    } else if (target > STOP_WIDE && target < STOP_COUNT) {
        spin_vel[target] += SPIN_KICK;
    }
}

void solar_red_hold() {
    red_held = true;
}

void solar_red_release() {
    red_held = false;
}
