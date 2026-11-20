// Coral, rock, and open water, with fish and larger animals swimming through.
// Sunbeams enter from the top left corner and spread across the panel.
// They stop when they meet rock, so the shelf throws a shadow.
// Seaweed, anemones, and tube worms bend from a fixed base.
// The finished picture then shifts sideways by about a pixel, more so toward the bottom.
// region[] is the map later animals will read. This slice uses it for light, bubbles, and water colour.

#include "pattern.hpp"

#include <cstring>

namespace {

constexpr int NEAR_REEF = 3;
constexpr int OVER_REEF = 16;
constexpr int GROUP_CAP = 4;
constexpr int BUBBLE_CAP = 8;

enum Region : uint8_t {
    RegionOpen = 0,
    RegionSurface,
    RegionOverReef,
    RegionNearReef,
    RegionEel,
    RegionRock,
};

struct Mound {
    float cx;
    float cy;
    float rx;
    float ry;
};

struct Pocket {
    int x0;
    int x1;
    int y0;
    int y1;
};

struct Ink {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct Sprite {
    int w;
    int h;
    const uint8_t *cells;
    const Ink *ink;
};

struct Place {
    int x;
    int sprite;
    float phase;
    float sway;
};

struct Weed {
    int x;
    int len;
    int green;
    float phase;
    int clump;
};

struct Waver {
    int x;
    int len;
    float phase;
    int kind;
};

struct Beam {
    float origin_x;
    float angle;
    float drift;
    float speed;
    float phase;
    int half;
};

struct Bubble {
    float x;
    float y;
    float speed;
    float phase;
    bool alive;
};

struct Group {
    Bubble bubbles[BUBBLE_CAP];
    int count;
    int wait_ms;
    bool alive;
};

constexpr Mound MOUNDS[] = {
    {10.0f, 55.0f, 14.0f, 28.0f},
    {30.0f, 55.0f, 12.0f, 24.0f},
    {48.0f, 63.0f, 14.0f, 22.0f},
    {60.0f, 67.0f, 12.0f, 22.0f},
};

constexpr Pocket POCKETS[] = {
    {3, 8, 54, 60},
    {16, 22, 56, 61},
    {33, 39, 55, 61},
    {46, 51, 58, 62},
};

constexpr uint8_t PINK_CELLS[] = {
    0, 0, 1, 0, 0,
    0, 1, 2, 1, 0,
    0, 0, 2, 0, 0,
    1, 2, 2, 2, 1,
    0, 0, 2, 0, 0,
    0, 2, 2, 2, 0,
    0, 0, 2, 0, 0,
    0, 2, 2, 2, 0,
    0, 0, 3, 0, 0,
};
constexpr Ink PINK_INK[] = {
    {255, 176, 196},
    {232, 64, 128},
    {148, 28, 72},
};

constexpr uint8_t ORANGE_CELLS[] = {
    0, 1, 0, 1, 0,
    0, 2, 0, 2, 0,
    1, 2, 2, 2, 1,
    0, 2, 3, 2, 0,
    0, 0, 2, 0, 0,
    0, 2, 2, 2, 0,
    0, 3, 2, 3, 0,
    0, 0, 3, 0, 0,
};
constexpr Ink ORANGE_INK[] = {
    {255, 196, 96},
    {240, 112, 32},
    {160, 56, 16},
};

constexpr uint8_t PURPLE_CELLS[] = {
    0, 1, 1, 2, 1, 1, 0,
    1, 2, 2, 2, 2, 2, 1,
    1, 2, 3, 2, 3, 2, 1,
    2, 2, 2, 2, 2, 2, 2,
    2, 3, 2, 3, 2, 3, 2,
    0, 3, 3, 3, 3, 3, 0,
};
constexpr Ink PURPLE_INK[] = {
    {232, 176, 255},
    {168, 64, 214},
    {96, 24, 128},
};

constexpr uint8_t YELLOW_CELLS[] = {
    1, 0, 1, 0, 1,
    2, 0, 2, 0, 2,
    2, 0, 2, 0, 2,
    2, 1, 2, 1, 2,
    0, 2, 0, 2, 0,
    2, 2, 2, 2, 2,
    3, 2, 3, 2, 3,
    0, 3, 0, 3, 0,
};
constexpr Ink YELLOW_INK[] = {
    {255, 236, 120},
    {240, 196, 32},
    {168, 120, 16},
};

constexpr uint8_t RED_CELLS[] = {
    1, 0, 1, 0, 1, 0, 1,
    2, 1, 2, 1, 2, 1, 2,
    0, 2, 2, 2, 2, 2, 0,
    0, 0, 2, 3, 2, 0, 0,
    0, 2, 3, 2, 3, 2, 0,
    0, 3, 2, 3, 2, 3, 0,
    0, 0, 3, 3, 3, 0, 0,
};
constexpr Ink RED_INK[] = {
    {255, 120, 112},
    {220, 40, 48},
    {128, 16, 28},
};

constexpr uint8_t CREAM_CELLS[] = {
    0, 1, 1, 1, 1, 0,
    1, 2, 1, 2, 1, 1,
    1, 1, 2, 1, 2, 1,
    2, 3, 2, 3, 2, 2,
    0, 3, 3, 3, 3, 0,
};
constexpr Ink CREAM_INK[] = {
    {255, 236, 214},
    {232, 196, 160},
    {176, 132, 96},
};

constexpr uint8_t TUBE_CELLS[] = {
    1, 2, 1,
    2, 0, 2,
    2, 0, 2,
    2, 0, 2,
    2, 3, 2,
    2, 3, 2,
    3, 3, 3,
};
constexpr Ink TUBE_YELLOW[] = {
    {255, 220, 72},
    {214, 164, 28},
    {140, 96, 16},
};
constexpr Ink TUBE_RED[] = {
    {255, 96, 72},
    {196, 40, 40},
    {120, 16, 24},
};

constexpr uint8_t BALL_CELLS[] = {
    0, 1, 2, 1, 0,
    1, 2, 2, 2, 1,
    1, 2, 3, 2, 1,
    0, 3, 3, 3, 0,
};
constexpr Ink BALL_INK[] = {
    {255, 176, 48},
    {214, 112, 24},
    {140, 56, 16},
};

constexpr uint8_t CLAM_CELLS[] = {
    0, 1, 1, 0,
    1, 2, 2, 1,
    3, 3, 3, 3,
};
constexpr Ink CLAM_INK[] = {
    {186, 214, 196},
    {120, 168, 150},
    {64, 96, 88},
};

constexpr uint8_t SNAIL_CELLS[] = {
    0, 1, 1, 0,
    1, 2, 3, 1,
    1, 2, 2, 1,
    0, 3, 3, 0,
};
constexpr Ink SNAIL_INK[] = {
    {232, 176, 96},
    {196, 120, 48},
    {96, 56, 32},
};

constexpr Sprite SPRITES[] = {
    {5, 9, PINK_CELLS, PINK_INK},
    {5, 8, ORANGE_CELLS, ORANGE_INK},
    {7, 6, PURPLE_CELLS, PURPLE_INK},
    {5, 8, YELLOW_CELLS, YELLOW_INK},
    {7, 7, RED_CELLS, RED_INK},
    {6, 5, CREAM_CELLS, CREAM_INK},
    {3, 7, TUBE_CELLS, TUBE_YELLOW},
    {3, 7, TUBE_CELLS, TUBE_RED},
    {5, 4, BALL_CELLS, BALL_INK},
    {4, 3, CLAM_CELLS, CLAM_INK},
    {4, 4, SNAIL_CELLS, SNAIL_INK},
};

constexpr Place GROWTH[] = {
    {6, 6, 0.2f, 0.20f},
    {21, 8, 0.0f, 0.0f},
    {36, 7, 0.4f, 0.15f},
    {50, 8, 0.8f, 0.0f},
    {9, 0, 0.3f, 0.75f},
    {15, 1, 1.1f, 0.80f},
    {18, 4, 2.3f, 0.55f},
    {24, 3, 1.9f, 0.65f},
    {27, 5, 1.5f, 0.30f},
    {32, 2, 0.6f, 0.35f},
    {46, 1, 2.7f, 0.50f},
    {54, 0, 0.9f, 0.45f},
    {57, 3, 1.8f, 0.60f},
    {61, 4, 2.4f, 0.40f},
};

constexpr Place SHELLS[] = {
    {4, 9, 0.0f, 0.0f},
    {13, 10, 0.0f, 0.0f},
    {29, 9, 0.0f, 0.0f},
    {43, 10, 0.0f, 0.0f},
    {58, 9, 0.0f, 0.0f},
};

constexpr Weed WEEDS[] = {
    {7, 7, 0, 0.2f, 0},
    {12, 8, 1, 1.0f, 1},
    {20, 14, 2, 2.1f, 0},
    {30, 8, 0, 0.7f, 0},
    {41, 10, 2, 1.8f, 0},
    {47, 18, 1, 1.4f, 1},
    {57, 22, 2, 0.6f, 0},
    {63, 9, 0, 0.3f, 0},
};

constexpr Ink GREENS[] = {
    {36, 150, 48},
    {72, 186, 40},
    {24, 110, 40},
};
constexpr Ink GREEN_DARK[] = {
    {16, 90, 28},
    {32, 120, 24},
    {12, 70, 28},
};

constexpr Waver WAVERS[] = {
    {11, 5, 0.4f, 0},
    {17, 6, 1.2f, 1},
    {25, 5, 2.0f, 0},
    {38, 4, 0.7f, 1},
    {48, 6, 1.6f, 0},
    {62, 4, 2.5f, 1},
    {14, 7, 0.5f, 2},
    {23, 6, 1.7f, 2},
    {35, 8, 2.6f, 2},
    {52, 5, 1.1f, 2},
};

// Angles fan out from the top left corner. Steeper rays reach the right side.
constexpr Beam BEAMS[] = {
    {0.0f, 0.07f, 1.2f, 0.00028f, 0.3f, 2},
    {1.0f, 0.26f, 1.4f, 0.00022f, 1.2f, 2},
    {0.5f, 0.52f, 1.2f, 0.00025f, 2.2f, 2},
    {1.5f, 0.82f, 1.0f, 0.00020f, 3.1f, 1},
    {0.0f, 1.18f, 0.8f, 0.00018f, 4.2f, 1},
};

static_assert(sizeof PINK_CELLS == 5 * 9, "pink coral");
static_assert(sizeof ORANGE_CELLS == 5 * 8, "orange coral");
static_assert(sizeof PURPLE_CELLS == 7 * 6, "purple coral");
static_assert(sizeof YELLOW_CELLS == 5 * 8, "yellow coral");
static_assert(sizeof RED_CELLS == 7 * 7, "red coral");
static_assert(sizeof CREAM_CELLS == 6 * 5, "cream coral");
static_assert(sizeof TUBE_CELLS == 3 * 7, "sponge tube");
static_assert(sizeof BALL_CELLS == 5 * 4, "sponge ball");
static_assert(sizeof CLAM_CELLS == 4 * 3, "clam");
static_assert(sizeof SNAIL_CELLS == 4 * 4, "snail");

static uint8_t region[PANEL_PIXELS];
static uint8_t scene[PANEL_PIXELS * 3];
static uint8_t beam_gain[PANEL_PIXELS];
static int rock_top[PANEL_WIDTH];
static Group groups[GROUP_CAP];
static uint32_t rng = 0x5EEDC0A1u;

static uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int poster(int value) {
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return (value & ~15) + 8;
}

static int cell_index(int x, int y) {
    return y * PANEL_WIDTH + x;
}

static bool rock_near(int x, int y) {
    for (int dy = -NEAR_REEF; dy <= NEAR_REEF; ++dy) {
        int yy = y + dy;
        if (yy < 0 || yy >= PANEL_HEIGHT) {
            continue;
        }
        for (int dx = -NEAR_REEF; dx <= NEAR_REEF; ++dx) {
            int xx = x + dx;
            if (xx < 0 || xx >= PANEL_WIDTH) {
                continue;
            }
            if (region[cell_index(xx, yy)] == RegionRock) {
                return true;
            }
        }
    }
    return false;
}

static bool rock_under(int x, int y) {
    int last = y + OVER_REEF;
    if (last >= PANEL_HEIGHT) {
        last = PANEL_HEIGHT - 1;
    }
    for (int yy = y + 1; yy <= last; ++yy) {
        if (region[cell_index(x, yy)] == RegionRock) {
            return true;
        }
    }
    return false;
}

static int mound_top(const Mound &mound, int x) {
    float dx = (x + 0.5f) - mound.cx;
    float nx = dx / mound.rx;
    if (nx < -1.0f || nx > 1.0f) {
        return PANEL_HEIGHT;
    }
    float ny = sqrtf(1.0f - nx * nx);
    int top = (int)(mound.cy - mound.ry * ny + 0.5f);
    if (top < 0) {
        return 0;
    }
    return top;
}

static void build_mask() {
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        rock_top[x] = PANEL_HEIGHT;
    }
    int mound_count = (int)(sizeof MOUNDS / sizeof MOUNDS[0]);
    for (int i = 0; i < mound_count; ++i) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int top = mound_top(MOUNDS[i], x);
            if (top < rock_top[x]) {
                rock_top[x] = top;
            }
        }
    }
    for (int x = 0; x < PANEL_WIDTH; ++x) {
        int top = (rock_top[x] / 2) * 2;
        // Row 28 is the highest rock, so the water above the shelf is about 55 percent of the panel.
        if (top < 28) {
            top = 28;
        }
        rock_top[x] = top;
    }

    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            if (y >= rock_top[x]) {
                region[cell_index(x, y)] = RegionRock;
            } else {
                region[cell_index(x, y)] = RegionOpen;
            }
        }
    }

    int pocket_count = (int)(sizeof POCKETS / sizeof POCKETS[0]);
    for (int i = 0; i < pocket_count; ++i) {
        const Pocket &pocket = POCKETS[i];
        for (int y = pocket.y0; y <= pocket.y1; ++y) {
            for (int x = pocket.x0; x <= pocket.x1; ++x) {
                bool corner = (x == pocket.x0 || x == pocket.x1) && (y == pocket.y0 || y == pocket.y1);
                if (corner) {
                    continue;
                }
                if (region[cell_index(x, y)] == RegionRock) {
                    region[cell_index(x, y)] = RegionEel;
                }
            }
        }
    }

    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = cell_index(x, y);
            if (region[index] == RegionRock || region[index] == RegionEel) {
                continue;
            }
            if (y <= 1) {
                region[index] = RegionSurface;
            } else if (rock_near(x, y)) {
                region[index] = RegionNearReef;
            } else if (rock_under(x, y)) {
                region[index] = RegionOverReef;
            } else {
                region[index] = RegionOpen;
            }
        }
    }
}

static void blend(uint8_t *buf, int x, int y, int red, int green, int blue, float cover) {
    if (cover <= 0.001f || x < 0 || y < 0 || x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    uint8_t *pixel = buf + cell_index(x, y) * 3;
    if (cover >= 0.999f) {
        write_bgr(pixel, clamp_channel((float)red), clamp_channel((float)green), clamp_channel((float)blue));
        return;
    }
    float keep = 1.0f - cover;
    int out_b = clamp_channel(blue * cover + pixel[0] * keep);
    int out_g = clamp_channel(green * cover + pixel[1] * keep);
    int out_r = clamp_channel(red * cover + pixel[2] * keep);
    write_bgr(pixel, out_r, out_g, out_b);
}

static void paint(float x, int y, int red, int green, int blue) {
    if (y < 0 || y >= PANEL_HEIGHT) {
        return;
    }
    int x0 = (int)floorf(x);
    float frac = x - x0;
    int sx = x0;
    if (frac >= 0.5f) {
        sx = x0 + 1;
    }
    if (sx >= 0 && sx < PANEL_WIDTH && beam_gain[cell_index(sx, y)] > 40) {
        red += 34;
        green += 30;
        blue += 16;
    }
    blend(scene, x0, y, red, green, blue, 1.0f - frac);
    blend(scene, x0 + 1, y, red, green, blue, frac);
}

static float row_shift(int y, uint32_t anim_ms) {
    float amp = 0.70f + 0.40f * ((float)y / (float)(PANEL_HEIGHT - 1));
    return sinf(anim_ms * 0.00115f + y * 0.20f) * amp;
}

static void cast_beams(uint32_t anim_ms) {
    memset(beam_gain, 0, sizeof beam_gain);
    int beam_count = (int)(sizeof BEAMS / sizeof BEAMS[0]);
    for (int i = 0; i < beam_count; ++i) {
        const Beam &beam = BEAMS[i];
        bool stopped[PANEL_WIDTH] = {};
        float wobble = sinf(anim_ms * beam.speed + beam.phase);
        float origin = beam.origin_x + wobble * beam.drift;
        float angle = beam.angle + wobble * 0.04f;
        float spread = tanf(angle);
        for (int y = 0; y < PANEL_HEIGHT; ++y) {
            float bx = origin + spread * (float)y;
            int depth = 255 - (y * 220 / PANEL_HEIGHT);
            if (depth < 36) {
                depth = 36;
            }
            int centre = (int)floorf(bx + 0.5f);
            for (int dx = -beam.half - 1; dx <= beam.half + 1; ++dx) {
                int x = centre + dx;
                if (x < 0 || x >= PANEL_WIDTH || stopped[x]) {
                    continue;
                }
                int index = cell_index(x, y);
                bool edge = dx < -beam.half || dx > beam.half;
                int gain = edge ? depth / 4 : depth;
                if (region[index] == RegionRock) {
                    int lit = gain / 3;
                    if (lit > beam_gain[index]) {
                        beam_gain[index] = (uint8_t)lit;
                    }
                    stopped[x] = true;
                    continue;
                }
                int sum = beam_gain[index] + gain;
                if (sum > 255) {
                    sum = 255;
                }
                beam_gain[index] = (uint8_t)sum;
            }
        }
    }
}

static void draw_water(uint32_t anim_ms) {
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        float depth = (y / 3) / 21.0f;
        if (depth > 1.0f) {
            depth = 1.0f;
        }
        int base_r = (int)(10 + (1.0f - depth) * 16);
        int base_g = (int)(36 + (1.0f - depth) * 80);
        int base_b = (int)(64 + (1.0f - depth) * 150);
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = cell_index(x, y);
            uint8_t kind = region[index];
            int red = base_r;
            int green = base_g;
            int blue = base_b;
            if (kind == RegionEel) {
                red = 6;
                green = 12;
                blue = 18;
            } else if (kind == RegionSurface) {
                float shimmer = 0.5f + 0.5f * sinf(x * 0.62f + anim_ms * 0.004f);
                red = 168 + (int)(shimmer * 64);
                green = 214 + (int)(shimmer * 28);
                blue = 236;
            } else if (kind == RegionNearReef) {
                red += 6;
                green += 22;
                blue -= 28;
            } else if (kind == RegionOverReef) {
                green += 10;
                blue -= 12;
            }
            if (kind != RegionEel) {
                int gain = beam_gain[index];
                red += gain * 3 / 5;
                green += gain * 4 / 5;
                blue += gain / 2;
            }
            write_bgr(scene + index * 3, poster(red), poster(green), poster(blue));
        }
    }
}

static void draw_rocks() {
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            int index = cell_index(x, y);
            if (region[index] != RegionRock) {
                continue;
            }
            bool crest = y == rock_top[x];
            bool eel_under = y + 1 < PANEL_HEIGHT && region[cell_index(x, y + 1)] == RegionEel;
            bool beside = false;
            if (x > 0 && region[cell_index(x - 1, y)] == RegionEel) {
                beside = true;
            }
            if (x + 1 < PANEL_WIDTH && region[cell_index(x + 1, y)] == RegionEel) {
                beside = true;
            }
            bool crack = false;
            int pocket_count = (int)(sizeof POCKETS / sizeof POCKETS[0]);
            for (int i = 0; i < pocket_count; ++i) {
                int mid = (POCKETS[i].x0 + POCKETS[i].x1) / 2;
                if (x == mid && y < POCKETS[i].y0 && y >= POCKETS[i].y0 - 8) {
                    crack = true;
                }
            }
            int edge = 0;
            if (x > 0) {
                int drop = rock_top[x] - rock_top[x - 1];
                if (drop < 0) {
                    drop = -drop;
                }
                if (drop > edge) {
                    edge = drop;
                }
            }
            if (x + 1 < PANEL_WIDTH) {
                int drop = rock_top[x] - rock_top[x + 1];
                if (drop < 0) {
                    drop = -drop;
                }
                if (drop > edge) {
                    edge = drop;
                }
            }
            bool seam = edge >= 2 && y < rock_top[x] + 8;
            int red = 112;
            int green = 88;
            int blue = 72;
            if (eel_under || beside || crack) {
                red -= 48;
                green -= 40;
                blue -= 32;
            } else if (crest) {
                red += 56;
                green += 44;
                blue += 20;
            } else if (seam) {
                red -= 24;
                green -= 20;
                blue -= 16;
            }
            if (beam_gain[index] > 24) {
                int lift = beam_gain[index] / 4;
                red += lift;
                green += lift;
                blue += lift / 2;
            }
            write_bgr(scene + index * 3, poster(red), poster(green), poster(blue));
        }
    }
}

static void draw_frond(int x, int len, int green, float phase, uint32_t anim_ms) {
    if (x < 0 || x >= PANEL_WIDTH || len < 2) {
        return;
    }
    int foot = rock_top[x];
    if (foot <= 1 || foot >= PANEL_HEIGHT) {
        return;
    }
    const Ink &ink = GREENS[green];
    const Ink &shade = GREEN_DARK[green];
    for (int i = 0; i < len; ++i) {
        float along = i / (float)(len - 1);
        float ox = sinf(anim_ms * 0.0022f + phase + i * 0.38f) * along * 2.0f;
        int y = foot - 1 - i;
        if (y < 1) {
            break;
        }
        int sx = (int)floorf(x + ox + 0.5f);
        if (sx >= 0 && sx < PANEL_WIDTH && y >= rock_top[sx]) {
            break;
        }
        paint((float)x + ox, y, ink.r, ink.g, ink.b);
        if (along < 0.65f) {
            paint((float)x + ox + 1.0f, y, shade.r, shade.g, shade.b);
        }
    }
}

static void draw_weeds(uint32_t anim_ms) {
    int count = (int)(sizeof WEEDS / sizeof WEEDS[0]);
    for (int i = 0; i < count; ++i) {
        const Weed &weed = WEEDS[i];
        draw_frond(weed.x, weed.len, weed.green, weed.phase, anim_ms);
        if (!weed.clump) {
            continue;
        }
        int left = weed.len - 4;
        int right = weed.len - 2;
        if (left < 6) {
            left = 6;
        }
        if (right < 6) {
            right = 6;
        }
        int other = weed.green + 1;
        if (other > 2) {
            other = 0;
        }
        draw_frond(weed.x - 1, left, other, weed.phase + 1.2f, anim_ms);
        draw_frond(weed.x + 1, right, weed.green, weed.phase + 2.1f, anim_ms);
    }
}

static void draw_sprite(const Place &place, uint32_t anim_ms) {
    if (place.x < 0 || place.x >= PANEL_WIDTH) {
        return;
    }
    int foot = rock_top[place.x];
    if (foot < 0 || foot >= PANEL_HEIGHT) {
        return;
    }
    const Sprite &sprite = SPRITES[place.sprite];
    for (int sy = 0; sy < sprite.h; ++sy) {
        int y = foot - (sprite.h - 1 - sy);
        float along = 0.0f;
        if (sprite.h > 1) {
            along = (sprite.h - 1 - sy) / (float)(sprite.h - 1);
        }
        float ox = sinf(anim_ms * 0.0020f + place.phase + sy * 0.40f) * place.sway * along;
        for (int sx = 0; sx < sprite.w; ++sx) {
            uint8_t id = sprite.cells[sy * sprite.w + sx];
            if (id == 0) {
                continue;
            }
            const Ink &ink = sprite.ink[id - 1];
            float px = (float)(place.x - sprite.w / 2 + sx) + ox;
            paint(px, y, ink.r, ink.g, ink.b);
        }
    }
}

static void draw_places(const Place *places, int count, uint32_t anim_ms) {
    for (int i = 0; i < count; ++i) {
        draw_sprite(places[i], anim_ms);
    }
}

static void draw_anemone(const Waver &waver, uint32_t anim_ms) {
    int x = waver.x;
    int foot = rock_top[x];
    if (foot <= 2 || foot >= PANEL_HEIGHT) {
        return;
    }
    for (int dx = -1; dx <= 1; ++dx) {
        paint((float)(x + dx), foot, 140, 24, 72);
        paint((float)(x + dx), foot - 1, 214, 48, 112);
    }
    int tip_r = 255;
    int tip_g = 210;
    int tip_b = 160;
    if (waver.kind == 1) {
        tip_r = 170;
        tip_g = 230;
        tip_b = 80;
    }
    for (int t = 0; t < 5; ++t) {
        for (int i = 0; i < waver.len; ++i) {
            float along = (i + 1) / (float)waver.len;
            float spread = (t - 2) * 0.55f;
            float ox = spread + sinf(anim_ms * 0.0032f + waver.phase + t * 1.3f + i * 0.5f) * along * 1.7f;
            int y = foot - 2 - i;
            if (y < 2) {
                continue;
            }
            if (i == waver.len - 1) {
                paint((float)x + ox, y, tip_r, tip_g, tip_b);
            } else {
                paint((float)x + ox, y, 255, 140, 180);
            }
        }
    }
}

static void draw_worm(const Waver &waver, uint32_t anim_ms) {
    int x = waver.x;
    int foot = rock_top[x];
    if (foot <= 2 || foot >= PANEL_HEIGHT) {
        return;
    }
    for (int i = 0; i < waver.len - 1; ++i) {
        float along = (i + 1) / (float)waver.len;
        float ox = sinf(anim_ms * 0.0016f + waver.phase + i * 0.25f) * along * 1.2f;
        int y = foot - 1 - i;
        if (y < 2) {
            break;
        }
        paint((float)x + ox, y, 132, 78, 42);
    }
    float ox = sinf(anim_ms * 0.0024f + waver.phase + waver.len) * 2.0f;
    int y = foot - waver.len;
    if (y < 2) {
        y = 2;
    }
    paint((float)x + ox, y, 255, 150, 40);
    paint((float)x + ox - 1.0f, y + 1, 230, 50, 28);
    paint((float)x + ox + 1.0f, y + 1, 230, 50, 28);
    paint((float)x + ox, y + 1, 255, 96, 32);
}

static void draw_wavers(uint32_t anim_ms) {
    int count = (int)(sizeof WAVERS / sizeof WAVERS[0]);
    for (int i = 0; i < count; ++i) {
        if (WAVERS[i].kind == 2) {
            draw_worm(WAVERS[i], anim_ms);
        } else {
            draw_anemone(WAVERS[i], anim_ms);
        }
    }
}

static void spawn_group(Group &group, bool midway) {
    int x = 2 + (int)(rnd() % 60);
    int foot = rock_top[x];
    if (foot > 58) {
        foot = 58;
    }
    int y = foot - 4;
    if (midway) {
        int span = foot - 10;
        if (span < 1) {
            span = 1;
        }
        y -= (int)(rnd() % (uint32_t)span);
    }
    if (y < 6) {
        y = 6;
    }
    group.count = 4 + (int)(rnd() % 5);
    group.alive = true;
    group.wait_ms = 0;
    for (int i = 0; i < group.count; ++i) {
        Bubble &bubble = group.bubbles[i];
        bubble.x = (float)(x + (int)(rnd() % 7) - 3);
        bubble.y = (float)(y + (int)(rnd() % 6));
        bubble.speed = 5.0f + (float)(rnd() % 7);
        bubble.phase = (rnd() % 100) * 0.07f;
        bubble.alive = true;
        if (bubble.x < 1.0f) {
            bubble.x = 1.0f;
        }
        if (bubble.x > 62.0f) {
            bubble.x = 62.0f;
        }
        int bx = (int)bubble.x;
        if (bx >= 0 && bx < PANEL_WIDTH && bubble.y >= rock_top[bx] - 1.0f) {
            bubble.y = (float)rock_top[bx] - 2.0f;
        }
        if (bubble.y < 6.0f) {
            bubble.y = 6.0f;
        }
    }
}

static void init_bubbles() {
    for (int i = 0; i < GROUP_CAP; ++i) {
        spawn_group(groups[i], true);
    }
}

static void update_bubbles(int dt_ms) {
    for (int g = 0; g < GROUP_CAP; ++g) {
        Group &group = groups[g];
        if (!group.alive) {
            group.wait_ms -= dt_ms;
            if (group.wait_ms <= 0) {
                spawn_group(group, false);
            }
            continue;
        }
        bool any = false;
        for (int i = 0; i < group.count; ++i) {
            Bubble &bubble = group.bubbles[i];
            if (!bubble.alive) {
                continue;
            }
            bubble.y -= bubble.speed * (float)dt_ms * 0.001f;
            if (bubble.y < 2.0f) {
                bubble.alive = false;
                continue;
            }
            any = true;
        }
        if (!any) {
            group.alive = false;
            group.wait_ms = 800 + (int)(rnd() % 1400);
        }
    }
}

// Painted after the sway, so each bubble stays one bright pixel.
static void paint_bubbles(uint8_t *frame, uint32_t anim_ms) {
    for (int g = 0; g < GROUP_CAP; ++g) {
        Group &group = groups[g];
        if (!group.alive) {
            continue;
        }
        for (int i = 0; i < group.count; ++i) {
            Bubble &bubble = group.bubbles[i];
            if (!bubble.alive) {
                continue;
            }
            float wx = bubble.x + sinf(anim_ms * 0.004f + bubble.phase) * 1.1f;
            int y = (int)floorf(bubble.y + 0.5f);
            int sx = (int)floorf(wx + 0.5f);
            if (sx < 0 || sx >= PANEL_WIDTH || y < 2 || y >= PANEL_HEIGHT) {
                continue;
            }
            if (region[cell_index(sx, y)] == RegionRock) {
                continue;
            }
            int x = (int)floorf(wx + row_shift(y, anim_ms) + 0.5f);
            blend(frame, x, y, 248, 252, 255, 1.0f);
        }
    }
}

static void sway_into(uint8_t *frame, uint32_t anim_ms) {
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        float shift = row_shift(y, anim_ms);
        const uint8_t *row = scene + y * PANEL_WIDTH * 3;
        uint8_t *out = frame + y * PANEL_WIDTH * 3;
        for (int x = 0; x < PANEL_WIDTH; ++x) {
            float sx = x - shift;
            int x0 = (int)floorf(sx);
            float frac = sx - x0;
            int x1 = x0 + 1;
            if (x0 < 0) {
                x0 = 0;
                frac = 0.0f;
            }
            if (x0 >= PANEL_WIDTH) {
                x0 = PANEL_WIDTH - 1;
                frac = 0.0f;
            }
            if (x1 < 0) {
                x1 = 0;
            }
            if (x1 >= PANEL_WIDTH) {
                x1 = PANEL_WIDTH - 1;
            }
            const uint8_t *left = row + x0 * 3;
            const uint8_t *right = row + x1 * 3;
            for (int channel = 0; channel < 3; ++channel) {
                float mixed = left[channel] + (right[channel] - left[channel]) * frac;
                out[channel] = (uint8_t)(mixed + 0.5f);
            }
            out += 3;
        }
    }
}

// Schools, reef animals, sharks, and one large visitor stay on screen together.
// Each keeps its place for 1 to 5 minutes, then swims off and a new one takes the slot.
// A hammerhead squad is the short visit. It crosses once, over about a minute.

constexpr int LIFE_CAP = 14;
constexpr int MEMBER_CAP = 14;
constexpr int FISH_CAP = 80;

enum LifeKind : uint8_t {
    LifeFish = 0,
    LifeOctopus,
    LifeEel,
    LifeTurtle,
    LifeJelly,
    LifeCrab,
    LifeShrimp,
    LifeWhite,
    LifeTiger,
    LifeThresher,
    LifeWhaleShark,
    LifeHammer,
    LifeDolphin,
    LifeOrca,
    LifeBlue,
    LifeManta,
    LifeMosa,
};

enum SlotKind : uint8_t {
    SlotSchool = 0,
    SlotSpecial,
    SlotShark,
    SlotLarge,
};

enum SwimMode : uint8_t {
    ModeSwim = 0,
    ModeLinger,
    ModeLeave,
    ModeCross,
};

struct Member {
    float ox;
    float oy;
    float vx;
    float vy;
    float phase;
};

struct Life {
    float x;
    float y;
    float vx;
    float vy;
    float phase;
    float speed;
    int timer_ms;
    int mode_ms;
    int count;
    int face;
    int size;
    uint8_t slot;
    uint8_t kind;
    uint8_t mode;
    uint8_t pattern;
    uint8_t colour;
    uint8_t colour2;
    bool alive;
    Member members[MEMBER_CAP];
};

constexpr Ink FISH_COLOURS[] = {
    {255, 196, 32},
    {32, 144, 255},
    {255, 96, 32},
    {236, 240, 255},
    {186, 64, 255},
    {64, 214, 112},
    {255, 120, 168},
    {255, 232, 96},
};

static Life lives[LIFE_CAP];
static int life_count = 0;

static bool cruises(int kind) {
    return kind >= LifeWhite;
}

static int reach_of(int kind, int size) {
    if (kind == LifeFish) {
        return size ? 4 : 3;
    }
    if (kind == LifeBlue) {
        return 16;
    }
    if (kind == LifeMosa) {
        return 12;
    }
    if (kind == LifeWhaleShark) {
        return 10;
    }
    if (kind == LifeManta || kind >= LifeWhite) {
        return 8;
    }
    if (kind == LifeEel) {
        return 6;
    }
    if (kind == LifeTurtle) {
        return 4;
    }
    return 3;
}

static bool home_ok(int kind, float x, float y) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    if (kind == LifeEel) {
        return ix >= 0 && ix < PANEL_WIDTH && iy >= 50 && iy < PANEL_HEIGHT;
    }
    if (ix < 0 || iy < 2 || ix >= PANEL_WIDTH || iy >= PANEL_HEIGHT) {
        return false;
    }
    uint8_t here = region[cell_index(ix, iy)];
    if (here == RegionRock || here == RegionSurface) {
        return false;
    }
    if (kind == LifeOctopus || kind == LifeCrab || kind == LifeShrimp) {
        return here == RegionNearReef || here == RegionOverReef || here == RegionEel;
    }
    if (kind == LifeDolphin || kind == LifeOrca || kind == LifeBlue || kind == LifeManta || kind == LifeMosa) {
        return here == RegionOpen || here == RegionOverReef;
    }
    return true;
}

static bool path_clear(int kind, float x, float y, float vx, float vy, int size) {
    float mag = sqrtf(vx * vx + vy * vy);
    float hx = 1.0f;
    float hy = 0.0f;
    if (mag > 0.01f) {
        hx = vx / mag;
        hy = vy / mag;
    }
    int reach = reach_of(kind, size);
    for (int i = -1; i <= 1; ++i) {
        float px = x + hx * (i * reach * 0.5f);
        float py = y + hy * (i * reach * 0.35f);
        if (!home_ok(kind, px, py)) {
            return false;
        }
    }
    return true;
}

static int fish_on_screen(const Life *skip) {
    int total = 0;
    for (int i = 0; i < life_count; ++i) {
        if (&lives[i] == skip || !lives[i].alive || lives[i].kind != LifeFish) {
            continue;
        }
        total += lives[i].count;
    }
    return total;
}

static void place_members(Life &life, float spacing) {
    int cols = 5;
    if (life.count < cols) {
        cols = life.count;
    }
    if (cols < 1) {
        cols = 1;
    }
    for (int i = 0; i < life.count; ++i) {
        int col = i % cols;
        int row = i / cols;
        life.members[i].ox = (col - (cols - 1) * 0.5f) * spacing;
        life.members[i].oy = (row - 0.5f) * (spacing * 0.75f);
        life.members[i].vx = 0.0f;
        life.members[i].vy = 0.0f;
        life.members[i].phase = (rnd() % 100) * 0.07f;
    }
}

static void pick_heading(Life &life) {
    float speed = life.speed;
    if (speed < 1.0f) {
        speed = 4.0f;
    }
    for (int t = 0; t < 12; ++t) {
        float ang;
        if (cruises(life.kind)) {
            ang = (rnd() & 1) ? 0.18f : 2.96f;
            ang += ((int)(rnd() % 30) - 15) * 0.03f;
        } else {
            ang = (rnd() % 628) * 0.01f;
        }
        float vx = cosf(ang) * speed;
        float vy = sinf(ang) * speed;
        if (path_clear(life.kind, life.x, life.y, vx, vy, life.size)) {
            life.vx = vx;
            life.vy = vy;
            if (vx > 0.4f) {
                life.face = 1;
            }
            if (vx < -0.4f) {
                life.face = -1;
            }
            return;
        }
    }
    life.vx = (float)life.face * speed;
    life.vy = 0.0f;
}

static void aim_eel(Life &life) {
    int which = (int)(rnd() % 4);
    int pocket_count = (int)(sizeof POCKETS / sizeof POCKETS[0]);
    const Pocket &pocket = POCKETS[which % pocket_count];
    float tx = (pocket.x0 + pocket.x1) * 0.5f;
    float ty = (pocket.y0 + pocket.y1) * 0.5f;
    float dx = tx - life.x;
    float dy = ty - life.y;
    if (dx * dx + dy * dy < 16.0f) {
        const Pocket &other = POCKETS[(which + 1) % pocket_count];
        tx = (other.x0 + other.x1) * 0.5f;
        ty = (other.y0 + other.y1) * 0.5f;
        dx = tx - life.x;
        dy = ty - life.y;
    }
    life.vx = dx / 2.8f;
    life.vy = dy / 2.8f;
    life.mode = ModeSwim;
    life.mode_ms = 2800;
    if (life.vx >= 0.0f) {
        life.face = 1;
    } else {
        life.face = -1;
    }
}

static bool find_spot(int kind, bool high, float &x, float &y) {
    for (int t = 0; t < 28; ++t) {
        int ix = (int)(rnd() % PANEL_WIDTH);
        int top = rock_top[ix];
        if (top < 8) {
            continue;
        }
        int span = top - 6;
        if (span < 1) {
            span = 1;
        }
        int iy = 3 + (int)(rnd() % (uint32_t)span);
        if (high && iy > 26) {
            continue;
        }
        if (!home_ok(kind, (float)ix, (float)iy)) {
            continue;
        }
        x = (float)ix;
        y = (float)iy;
        return true;
    }
    return false;
}

static void begin_leave(Life &life) {
    life.mode = ModeLeave;
    float speed = life.speed;
    if (speed < 6.0f) {
        speed = 6.0f;
    }
    if (life.kind < LifeWhite && speed < 10.0f) {
        speed = 10.0f;
    }
    float left = life.x;
    float right = (PANEL_WIDTH - 1) - life.x;
    float up = life.y;
    float down = (PANEL_HEIGHT - 1) - life.y;
    if (life.kind == LifeEel || (left <= right && left <= up && left <= down)) {
        life.vx = (left < right) ? -speed : speed;
        life.vy = 0.0f;
    } else if (right <= up && right <= down) {
        life.vx = speed;
        life.vy = 0.0f;
    } else if (up <= down) {
        life.vx = 0.0f;
        life.vy = -speed;
    } else {
        life.vx = 0.0f;
        life.vy = speed;
    }
    if (life.vx > 0.4f) {
        life.face = 1;
    }
    if (life.vx < -0.4f) {
        life.face = -1;
    }
}

static void spawn_into(Life &life, int slot) {
    memset(&life, 0, sizeof life);
    life.slot = (uint8_t)slot;
    life.alive = true;
    life.face = 1;
    life.phase = (rnd() % 100) * 0.06f;
    life.timer_ms = (1 + (int)(rnd() % 5)) * 60000;
    life.mode = ModeSwim;
    life.mode_ms = 1200 + (int)(rnd() % 1800);
    life.count = 1;
    life.colour = (uint8_t)(rnd() % 8);
    life.colour2 = (uint8_t)((life.colour + 3) % 8);
    life.pattern = (uint8_t)(rnd() % 4);
    life.size = (int)(rnd() % 2);

    if (slot == SlotSchool) {
        life.kind = LifeFish;
        int room = FISH_CAP - fish_on_screen(&life);
        int count = 5 + (int)(rnd() % 10);
        if (count > room) {
            count = room;
        }
        if (count < 1) {
            count = 1;
        }
        life.count = count;
        life.speed = 5.5f + (float)(rnd() % 3);
        place_members(life, life.size ? 3.4f : 2.8f);
        if (!find_spot(LifeFish, false, life.x, life.y)) {
            life.x = 20.0f;
            life.y = 12.0f;
        }
    } else if (slot == SlotSpecial) {
        int roll = (int)(rnd() % 10);
        if (roll < 3) {
            life.kind = LifeEel;
        } else if (roll < 5) {
            life.kind = LifeOctopus;
        } else if (roll == 5) {
            life.kind = LifeTurtle;
        } else if (roll == 6) {
            life.kind = LifeJelly;
        } else if (roll == 7) {
            life.kind = LifeCrab;
        } else if (roll == 8) {
            life.kind = LifeShrimp;
        } else {
            life.kind = LifeJelly;
        }
        life.members[0].phase = life.phase;
        if (life.kind == LifeEel) {
            int which = (int)(rnd() % 4);
            const Pocket &pocket = POCKETS[which];
            life.x = (pocket.x0 + pocket.x1) * 0.5f;
            life.y = (pocket.y0 + pocket.y1) * 0.5f;
            life.mode = ModeLinger;
            life.mode_ms = 8000 + (int)(rnd() % 8000);
            life.speed = 8.0f;
            life.vx = 0.0f;
            life.vy = 0.0f;
            return;
        }
        if (life.kind == LifeOctopus) {
            life.speed = 4.0f;
        } else if (life.kind == LifeTurtle) {
            life.speed = 4.5f;
        } else if (life.kind == LifeJelly) {
            life.speed = 3.0f;
        } else if (life.kind == LifeCrab) {
            life.speed = 3.0f;
        } else {
            life.speed = 5.0f;
        }
        if (!find_spot(life.kind, false, life.x, life.y)) {
            life.x = 24.0f;
            life.y = (float)rock_top[24] - 3.0f;
        }
    } else if (slot == SlotShark) {
        int roll = (int)(rnd() % 100);
        if (roll < 10) {
            life.kind = LifeHammer;
        } else if (roll < 32) {
            life.kind = LifeWhaleShark;
        } else if (roll < 54) {
            life.kind = LifeThresher;
        } else if (roll < 76) {
            life.kind = LifeTiger;
        } else {
            life.kind = LifeWhite;
        }
        if (life.kind == LifeHammer) {
            life.count = 5 + (int)(rnd() % 3);
            life.speed = 2.6f;
            life.mode = ModeCross;
            life.timer_ms = 70000;
            bool from_left = (rnd() & 1) != 0;
            life.vx = from_left ? 2.6f : -2.6f;
            life.vy = 0.0f;
            life.face = from_left ? 1 : -1;
            life.x = from_left ? -8.0f : (float)PANEL_WIDTH + 8.0f;
            life.y = 7.0f + (float)(rnd() % 8);
            for (int i = 0; i < life.count; ++i) {
                life.members[i].ox = -i * 11.0f;
                life.members[i].oy = (float)((i % 3) - 1) * 4.0f;
                life.members[i].phase = (rnd() % 100) * 0.05f;
            }
            return;
        }
        life.speed = life.kind == LifeWhaleShark ? 7.0f : 11.0f;
        life.members[0].phase = life.phase;
        if (!find_spot(life.kind, false, life.x, life.y)) {
            life.x = 30.0f;
            life.y = 12.0f;
        }
    } else {
        int roll = (int)(rnd() % 100);
        if (roll < 8) {
            life.kind = LifeMosa;
            life.speed = 8.0f;
        } else if (roll < 20) {
            life.kind = LifeBlue;
            life.speed = 4.5f;
        } else if (roll < 46) {
            life.kind = LifeManta;
            life.speed = 7.0f;
        } else if (roll < 72) {
            life.kind = LifeDolphin;
            life.speed = 12.0f;
            life.count = 3 + (int)(rnd() % 3);
        } else {
            life.kind = LifeOrca;
            life.speed = 10.0f;
            life.count = 2 + (int)(rnd() % 4);
        }
        if (life.kind == LifeDolphin) {
            place_members(life, 8.0f);
        } else if (life.kind == LifeOrca) {
            place_members(life, 9.0f);
        } else {
            life.members[0].phase = life.phase;
        }
        if (!find_spot(life.kind, true, life.x, life.y)) {
            life.x = 28.0f;
            life.y = 10.0f;
        }
    }
    pick_heading(life);
    if (life.kind == LifeFish) {
        float ang = atan2f(life.vy, life.vx);
        if (ang > 0.30f && ang < 3.1415926f - 0.30f) {
            ang = ang < 1.5707963f ? 0.30f : 3.1415926f - 0.30f;
        } else if (ang < -0.30f && ang > -(3.1415926f - 0.30f)) {
            ang = ang > -1.5707963f ? -0.30f : -(3.1415926f - 0.30f);
        }
        if (life.x < 20.0f && cosf(ang) < 0.0f) {
            ang = ang > 0.0f ? 3.1415926f - ang : -3.1415926f - ang;
        } else if (life.x > (float)PANEL_WIDTH - 20.0f && cosf(ang) > 0.0f) {
            ang = ang > 0.0f ? 3.1415926f - ang : -3.1415926f - ang;
        }
        if (life.y < 12.0f && sinf(ang) < 0.0f) {
            ang = -ang;
        } else if (life.y > 24.0f && sinf(ang) > 0.0f) {
            ang = -ang;
        }
        life.vx = cosf(ang) * life.speed;
        life.vy = sinf(ang) * life.speed;
        if (life.vx >= 0.0f) {
            life.face = 1;
        } else {
            life.face = -1;
        }
        for (int i = 0; i < life.count; ++i) {
            float jx = ((int)(rnd() % 5) - 2) * 0.35f;
            float jy = ((int)(rnd() % 5) - 2) * 0.3f;
            float x = life.x + life.members[i].ox + jx;
            float y = life.y + life.members[i].oy + jy;
            if (!home_ok(LifeFish, x, y)) {
                x = life.x;
                y = life.y;
            }
            life.members[i].ox = x;
            life.members[i].oy = y;
            life.members[i].vx = life.vx;
            life.members[i].vy = life.vy;
        }
    }
}

static bool can_step(const Life &life, float x, float y) {
    if (life.mode == ModeLeave || life.mode == ModeCross) {
        if (x < -40.0f || x > PANEL_WIDTH + 40.0f || y < -40.0f || y > PANEL_HEIGHT + 40.0f) {
            return true;
        }
        if (life.kind == LifeEel) {
            return y >= 48.0f && y < (float)PANEL_HEIGHT;
        }
        int ix = (int)floorf(x);
        int iy = (int)floorf(y);
        if (ix < 0 || iy < 0 || ix >= PANEL_WIDTH || iy >= PANEL_HEIGHT) {
            return true;
        }
        return region[cell_index(ix, iy)] != RegionRock;
    }
    return path_clear(life.kind, x, y, life.vx, life.vy, life.size);
}

static bool gone(const Life &life) {
    float back = 8.0f;
    for (int i = 0; i < life.count; ++i) {
        float ox = life.members[i].ox;
        if (ox < 0.0f && -ox > back) {
            back = -ox;
        }
        if (ox > back) {
            back = ox;
        }
    }
    if (life.kind == LifeBlue) {
        back += 20.0f;
    } else if (life.kind == LifeMosa || life.kind == LifeWhaleShark) {
        back += 14.0f;
    } else {
        back += 8.0f;
    }
    if (life.mode == ModeCross) {
        if (life.vx > 0.0f) {
            return life.x - back > (float)PANEL_WIDTH + 4.0f;
        }
        return life.x + back < -4.0f;
    }
    if (life.x < -back || life.x > PANEL_WIDTH + back) {
        return true;
    }
    if (life.y < -back || life.y > PANEL_HEIGHT + back) {
        return true;
    }
    return false;
}

// A school keeps one slow heading. Each fish eases toward that heading, the neighbours around it, and a little open water, so the group curves instead of darting.
static void update_school(Life &life, uint32_t anim_ms, int dt_ms) {
    float dt = dt_ms * 0.001f;
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (life.mode != ModeLeave) {
        life.timer_ms -= dt_ms;
        if (life.timer_ms <= 0) {
            begin_leave(life);
            float mag = sqrtf(life.vx * life.vx + life.vy * life.vy);
            if (mag > 0.01f) {
                life.vx = life.vx / mag * life.speed;
                life.vy = life.vy / mag * life.speed;
            }
        }
    }

    float school_x = life.vx;
    float school_y = life.vy;
    if (life.mode != ModeLeave) {
        float ang = atan2f(life.vy, life.vx);
        float turn = sinf(anim_ms * 0.00035f + life.phase) * 0.22f;
        turn += sinf(anim_ms * 0.00013f + life.phase * 1.7f) * 0.10f;
        float hx = cosf(ang);
        float hy = sinf(ang);
        float nose_x = life.x;
        float nose_y = life.y;
        float best = 0.0f;
        int nfish = life.count;
        if (nfish > MEMBER_CAP) {
            nfish = MEMBER_CAP;
        }
        for (int i = 0; i < nfish; ++i) {
            float proj = (life.members[i].ox - life.x) * hx + (life.members[i].oy - life.y) * hy;
            if (proj > best) {
                best = proj;
                nose_x = life.members[i].ox;
                nose_y = life.members[i].oy;
            }
        }
        float look_x = nose_x + hx * 6.0f;
        float look_y = nose_y + hy * 3.0f;
        float ax = hx;
        float ay = hy;
        // The fish at the front meets the edge first, so the whole school starts its curve there.
        if (nose_x < 16.0f && ax < 0.25f) {
            ax = (16.0f - nose_x) * 0.08f;
        } else if (nose_x > (float)PANEL_WIDTH - 16.0f && ax > -0.25f) {
            ax = -((nose_x - ((float)PANEL_WIDTH - 16.0f)) * 0.08f);
        }
        if (nose_y < 10.0f && ay < 0.25f) {
            ay = (10.0f - nose_y) * 0.10f;
        } else if (nose_y > 28.0f && ay > -0.1f) {
            ay = -((nose_y - 28.0f) * 0.10f);
        }
        float desired = atan2f(ay, ax);
        float diff = desired - ang;
        if (diff > 3.1415926f) {
            diff -= 6.2831853f;
        } else if (diff < -3.1415926f) {
            diff += 6.2831853f;
        }
        float max_rate = 0.65f;
        if (nose_x < 8.0f || nose_x > (float)PANEL_WIDTH - 8.0f || nose_y < 6.0f) {
            max_rate = 1.15f;
        }
        if (!home_ok(LifeFish, look_x, look_y)) {
            if (max_rate < 1.05f) {
                max_rate = 1.05f;
            }
            float left = ang + 0.8f;
            float right = ang - 0.8f;
            bool open_left = home_ok(LifeFish, nose_x + cosf(left) * 6.0f, nose_y + sinf(left) * 4.0f);
            bool open_right = home_ok(LifeFish, nose_x + cosf(right) * 6.0f, nose_y + sinf(right) * 4.0f);
            if (open_left && !open_right) {
                diff = 0.85f;
            } else if (open_right && !open_left) {
                diff = -0.85f;
            }
        }
        float rate_ang = turn + diff;
        if (rate_ang > max_rate) {
            rate_ang = max_rate;
        } else if (rate_ang < -max_rate) {
            rate_ang = -max_rate;
        }
        ang += rate_ang * dt;
        // In open water a steep dive eases back toward level. An edge turn is left alone so the curve can finish.
        bool in_margin = nose_x < 18.0f || nose_x > (float)PANEL_WIDTH - 18.0f || nose_y < 12.0f;
        float pitch = fabsf(ang);
        if (pitch > 1.5707963f) {
            pitch = 3.1415926f - pitch;
        }
        if (!in_margin && pitch > 0.40f) {
            float step = 0.9f * dt;
            if (ang > 0.0f && ang <= 1.5707963f) {
                ang -= step;
            } else if (ang > 1.5707963f && ang < 3.1415926f) {
                ang += step;
            } else if (ang < 0.0f && ang >= -1.5707963f) {
                ang += step;
            } else {
                ang -= step;
            }
        }
        school_x = cosf(ang) * life.speed;
        school_y = sinf(ang) * life.speed;
        life.vx = school_x;
        life.vy = school_y;
    }

    float rate = 3.4f * dt;
    if (rate > 1.0f) {
        rate = 1.0f;
    }
    int count = life.count;
    if (count > MEMBER_CAP) {
        count = MEMBER_CAP;
    }
    for (int i = 0; i < count; ++i) {
        Member &me = life.members[i];
        float sep_x = 0.0f;
        float sep_y = 0.0f;
        float ali_x = 0.0f;
        float ali_y = 0.0f;
        float coh_x = 0.0f;
        float coh_y = 0.0f;
        int near = 0;
        int crowded = 0;
        float nearest = 99.0f;
        for (int j = 0; j < count; ++j) {
            if (i == j) {
                continue;
            }
            float dx = life.members[j].ox - me.ox;
            float dy = life.members[j].oy - me.oy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < 0.04f) {
                sep_x += (float)(i - j);
                crowded += 1;
                nearest = 0.0f;
                continue;
            }
            float dist = sqrtf(dist2);
            if (dist < nearest) {
                nearest = dist;
            }
            if (dist < 5.5f) {
                float push = (5.5f - dist) / dist;
                sep_x -= dx * push;
                sep_y -= dy * push;
                crowded += 1;
            }
            if (dist < 12.0f) {
                ali_x += life.members[j].vx;
                ali_y += life.members[j].vy;
                coh_x += life.members[j].ox;
                coh_y += life.members[j].oy;
                near += 1;
            }
        }
        float want_x = school_x;
        float want_y = school_y;
        if (near > 0) {
            float inv = 1.0f / (float)near;
            float ax = ali_x * inv;
            float ay = ali_y * inv;
            float am = sqrtf(ax * ax + ay * ay);
            if (am > 0.01f) {
                want_x += ax / am * life.speed * 0.85f;
                want_y += ay / am * life.speed * 0.85f;
            }
            float cx = coh_x * inv - me.ox;
            float cy = coh_y * inv - me.oy;
            float cm = sqrtf(cx * cx + cy * cy);
            if (cm > 5.0f && nearest > 3.5f) {
                float pull = cm > 9.0f ? 1.15f : 0.35f;
                want_x += cx / cm * life.speed * pull;
                want_y += cy / cm * life.speed * pull;
            }
        }
        if (crowded > 0) {
            float sm = sqrtf(sep_x * sep_x + sep_y * sep_y);
            if (sm > 0.01f) {
                float sep_weight = nearest < 2.6f ? 4.2f : 1.6f;
                want_x += sep_x / sm * life.speed * sep_weight;
                want_y += sep_y / sm * life.speed * sep_weight;
            }
        }
        float to_x = life.x - me.ox;
        float to_y = life.y - me.oy;
        float tm = sqrtf(to_x * to_x + to_y * to_y);
        if (tm > 14.0f) {
            want_x += to_x / tm * life.speed * 1.4f;
            want_y += to_y / tm * life.speed * 1.4f;
        }
        float ahead_x = me.ox + me.vx * 0.45f;
        float ahead_y = me.oy + me.vy * 0.45f;
        if (life.mode != ModeLeave && !home_ok(LifeFish, ahead_x, ahead_y)) {
            want_x += -school_y * 0.45f;
            want_y += school_x * 0.45f;
        }
        float wm = sqrtf(want_x * want_x + want_y * want_y);
        if (wm > 0.01f) {
            want_x = want_x / wm * life.speed;
            want_y = want_y / wm * life.speed;
        }
        me.vx += (want_x - me.vx) * rate;
        me.vy += (want_y - me.vy) * rate;
        float mm = sqrtf(me.vx * me.vx + me.vy * me.vy);
        if (mm > 0.01f) {
            me.vx = me.vx / mm * life.speed;
            me.vy = me.vy / mm * life.speed;
        }
        float nx = me.ox + me.vx * dt;
        float ny = me.oy + me.vy * dt;
        if (life.mode == ModeLeave || home_ok(LifeFish, nx, ny)) {
            me.ox = nx;
            me.oy = ny;
        } else if (home_ok(LifeFish, nx, me.oy)) {
            me.ox = nx;
            me.vy *= 0.55f;
        } else if (home_ok(LifeFish, me.ox, ny)) {
            me.oy = ny;
            me.vx *= 0.55f;
        } else {
            float rx = me.vx * 0.86f - me.vy * 0.30f;
            float ry = me.vy * 0.86f + me.vx * 0.30f;
            me.vx = rx;
            me.vy = ry;
        }
    }

    // Nudge overlapping fish apart. Steering alone lets a turning school pile into one blob.
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            float dx = life.members[j].ox - life.members[i].ox;
            float dy = life.members[j].oy - life.members[i].oy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 >= 7.84f) {
                continue;
            }
            float dist = sqrtf(dist2);
            float gap = 2.8f - dist;
            float ux = 1.0f;
            float uy = 0.0f;
            if (dist > 0.05f) {
                ux = dx / dist;
                uy = dy / dist;
            } else if ((i & 1) == 0) {
                ux = -1.0f;
                uy = 0.35f;
            }
            float push = gap * (dist < 1.2f ? 0.40f : 0.18f);
            float ix = life.members[i].ox - ux * push;
            float iy = life.members[i].oy - uy * push;
            float jx = life.members[j].ox + ux * push;
            float jy = life.members[j].oy + uy * push;
            bool leaving = life.mode == ModeLeave;
            if (leaving || home_ok(LifeFish, ix, iy)) {
                life.members[i].ox = ix;
                life.members[i].oy = iy;
            } else if (home_ok(LifeFish, ix, life.members[i].oy)) {
                life.members[i].ox = ix;
            } else if (home_ok(LifeFish, life.members[i].ox, iy)) {
                life.members[i].oy = iy;
            }
            if (leaving || home_ok(LifeFish, jx, jy)) {
                life.members[j].ox = jx;
                life.members[j].oy = jy;
            } else if (home_ok(LifeFish, jx, life.members[j].oy)) {
                life.members[j].ox = jx;
            } else if (home_ok(LifeFish, life.members[j].ox, jy)) {
                life.members[j].oy = jy;
            }
        }
    }

    float sum_x = 0.0f;
    float sum_y = 0.0f;
    int visible = 0;
    for (int i = 0; i < count; ++i) {
        sum_x += life.members[i].ox;
        sum_y += life.members[i].oy;
        float x = life.members[i].ox;
        float y = life.members[i].oy;
        if (x > -4.0f && x < (float)PANEL_WIDTH + 4.0f && y > -4.0f && y < (float)PANEL_HEIGHT + 4.0f) {
            visible += 1;
        }
    }
    if (count > 0) {
        life.x = sum_x / (float)count;
        life.y = sum_y / (float)count;
    }
    if (life.vx > 0.4f) {
        life.face = 1;
    } else if (life.vx < -0.4f) {
        life.face = -1;
    }
    if (life.mode == ModeLeave && visible == 0) {
        int slot = life.slot;
        spawn_into(life, slot);
    }
}

static void update_one(Life &life, uint32_t anim_ms, int dt_ms) {
    if (!life.alive) {
        return;
    }
    if (life.kind == LifeFish) {
        update_school(life, anim_ms, dt_ms);
        return;
    }
    if (life.mode != ModeLeave && life.mode != ModeCross) {
        life.timer_ms -= dt_ms;
        if (life.timer_ms <= 0) {
            begin_leave(life);
        }
    }
    if (life.mode != ModeLeave && life.mode != ModeCross) {
        life.mode_ms -= dt_ms;
        if (life.mode == ModeSwim && life.mode_ms <= 0) {
            if (life.kind == LifeEel) {
                life.mode = ModeLinger;
                life.mode_ms = 8000 + (int)(rnd() % 10000);
                life.vx = 0.0f;
                life.vy = 0.0f;
            } else if (cruises(life.kind)) {
                pick_heading(life);
                life.mode_ms = 2200 + (int)(rnd() % 2800);
            } else {
                life.mode = ModeLinger;
                life.mode_ms = 500 + (int)(rnd() % 1200);
            }
        } else if (life.mode == ModeLinger && life.mode_ms <= 0) {
            if (life.kind == LifeEel) {
                aim_eel(life);
            } else {
                life.mode = ModeSwim;
                life.mode_ms = 900 + (int)(rnd() % 2200);
                pick_heading(life);
            }
        }
    }

    float scale = 1.0f;
    float sink = 0.0f;
    if (life.mode == ModeLinger) {
        scale = life.kind == LifeEel ? 0.0f : 0.18f;
        if (life.kind == LifeJelly) {
            sink = 2.2f;
        } else if (life.kind <= LifeShrimp && life.kind != LifeEel) {
            sink = 3.5f;
        }
    } else if (life.kind == LifeJelly && life.mode == ModeSwim) {
        sink = -1.5f;
    }
    float step = dt_ms * 0.001f;
    float nx = life.x + life.vx * scale * step;
    float ny = life.y + (life.vy * scale + sink) * step;
    if (can_step(life, nx, ny)) {
        life.x = nx;
        life.y = ny;
    } else if (can_step(life, nx, life.y)) {
        life.x = nx;
    } else if (can_step(life, life.x, ny)) {
        life.y = ny;
    } else if (life.mode == ModeLeave || life.mode == ModeCross) {
        life.x = nx;
        life.y = ny;
    } else {
        pick_heading(life);
    }
    if (life.vx > 0.4f) {
        life.face = 1;
    }
    if (life.vx < -0.4f) {
        life.face = -1;
    }
    if ((life.mode == ModeLeave || life.mode == ModeCross) && gone(life)) {
        int slot = life.slot;
        spawn_into(life, slot);
    }
}

static void init_life() {
    life_count = 0;
    int schools = 4 + (int)(rnd() % 3);
    int specials = 3 + (int)(rnd() % 3);
    int sharks = (rnd() % 5 == 0) ? 2 : 1;
    for (int i = 0; i < schools; ++i) {
        spawn_into(lives[life_count], SlotSchool);
        life_count += 1;
    }
    for (int i = 0; i < specials; ++i) {
        spawn_into(lives[life_count], SlotSpecial);
        life_count += 1;
    }
    for (int i = 0; i < sharks; ++i) {
        spawn_into(lives[life_count], SlotShark);
        life_count += 1;
    }
    spawn_into(lives[life_count], SlotLarge);
    life_count += 1;
}

static void update_life(uint32_t anim_ms, int dt_ms) {
    for (int i = 0; i < life_count; ++i) {
        update_one(lives[i], anim_ms, dt_ms);
    }
}

static void stamp(float x, float y, int red, int green, int blue, bool over) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float fx = x - (float)x0;
    float fy = y - (float)y0;
    float across[2] = {1.0f - fx, fx};
    float down[2] = {1.0f - fy, fy};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int px = x0 + i;
            int py = y0 + j;
            if (px < 0 || py < 0 || px >= PANEL_WIDTH || py >= PANEL_HEIGHT) {
                continue;
            }
            int index = cell_index(px, py);
            if (!over && region[index] == RegionRock) {
                continue;
            }
            int rr = red;
            int gg = green;
            int bb = blue;
            if (beam_gain[index] > 40) {
                rr += 22;
                gg += 20;
                bb += 10;
            }
            blend(scene, px, py, rr, gg, bb, across[i] * down[j]);
        }
    }
}

static void fish_rgb(const Life &life, int along, int row, uint32_t anim_ms, int &red, int &green, int &blue) {
    const Ink &first = FISH_COLOURS[life.colour % 8];
    const Ink &second = FISH_COLOURS[life.colour2 % 8];
    if (life.pattern == 3) {
        float mix = 0.5f + 0.5f * sinf(anim_ms * 0.0025f + life.phase + along * 0.7f);
        red = (int)(first.r + (second.r - first.r) * mix);
        green = (int)(first.g + (second.g - first.g) * mix);
        blue = (int)(first.b + (second.b - first.b) * mix);
        return;
    }
    bool alt = false;
    if (life.pattern == 0) {
        alt = (along % 2) == 0;
    } else if (life.pattern == 1) {
        alt = along % 3 == 1 && row == 0;
    } else {
        alt = ((along * 3 + row * 5 + life.colour) % 4) == 0;
    }
    const Ink &ink = alt ? second : first;
    red = ink.r;
    green = ink.g;
    blue = ink.b;
}

static void draw_fish(const Life &life, float x, float y, int face, float phase, uint32_t anim_ms) {
    int len = life.size ? 5 : 4;
    float wag = sinf(anim_ms * 0.012f + phase) * 0.35f;
    for (int i = 0; i < len; ++i) {
        float along = (float)i;
        float tail = 1.0f - along / (float)(len - 1);
        float px = x + face * (along - (len - 1) * 0.5f);
        float py = y + wag * tail;
        int red;
        int green;
        int blue;
        fish_rgb(life, i, 0, anim_ms, red, green, blue);
        stamp(px, py, red, green, blue, false);
        if (i > 0 && i < len - 1) {
            fish_rgb(life, i, 1, anim_ms, red, green, blue);
            stamp(px, py - 1.0f, red, green, blue, false);
        }
        if (i == 0) {
            stamp(px - face * 0.7f, py + 0.5f, red, green, blue, false);
        }
    }
    float hx = x + face * ((len - 1) * 0.5f);
    stamp(hx, y - 0.15f, 16, 16, 24, false);
}

static void draw_jelly(const Life &life, float x, float y, uint32_t anim_ms) {
    int red = life.colour & 1 ? 180 : 120;
    int green = life.colour & 1 ? 140 : 210;
    int blue = 255;
    for (int i = -2; i <= 2; ++i) {
        stamp(x + i, y, red, green, blue, false);
        if (i >= -1 && i <= 1) {
            stamp(x + i, y - 1.0f, 230, 240, 255, false);
        }
    }
    stamp(x - 2.0f, y + 1.0f, red, green, blue, false);
    stamp(x + 2.0f, y + 1.0f, red, green, blue, false);
    for (int t = 0; t < 3; ++t) {
        for (int i = 0; i < 5; ++i) {
            float sway = sinf(anim_ms * 0.004f + life.phase + t + i * 0.4f) * i * 0.28f;
            stamp(x + (t - 1) * 1.2f + sway, y + 2.0f + i, red, green, blue, false);
        }
    }
}

static void draw_octopus(const Life &life, float x, float y, uint32_t anim_ms) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            stamp(x + dx, y + dy, 168, 56, 96, false);
        }
    }
    stamp(x - 0.4f, y - 0.3f, 230, 220, 180, false);
    for (int arm = 0; arm < 5; ++arm) {
        for (int i = 0; i < 4; ++i) {
            float spread = (arm - 2) * 0.85f;
            float sway = sinf(anim_ms * 0.0045f + life.phase + arm) * i * 0.32f;
            stamp(x + spread + sway, y + 2.0f + i, 140, 40, 78, false);
        }
    }
}

static void draw_turtle(const Life &life, float x, float y, uint32_t anim_ms) {
    int face = life.face;
    float flap = sinf(anim_ms * 0.007f + life.phase) * 0.8f;
    for (int i = -2; i <= 2; ++i) {
        stamp(x + i, y, 56, 140, 72, false);
        if (i >= -1 && i <= 1) {
            stamp(x + i, y - 1.0f, 72, 168, 80, false);
            stamp(x + i, y + 1.0f, 40, 100, 56, false);
        }
    }
    stamp(x + 1.0f, y - 1.0f, 210, 180, 90, false);
    stamp(x + face * 3.0f, y, 186, 150, 72, false);
    stamp(x + face * 4.0f, y, 210, 170, 90, false);
    stamp(x - 1.0f, y - 2.0f + flap, 40, 110, 64, false);
    stamp(x + 1.0f, y - 2.0f - flap, 40, 110, 64, false);
    stamp(x - 1.0f, y + 2.0f - flap, 40, 110, 64, false);
    stamp(x + 1.0f, y + 2.0f + flap, 40, 110, 64, false);
}

static void draw_crab(const Life &life, float x, float y, uint32_t anim_ms) {
    float claw = sinf(anim_ms * 0.006f + life.phase) * 0.5f;
    for (int i = -2; i <= 2; ++i) {
        stamp(x + i, y, 210, 64, 36, false);
        if (i >= -1 && i <= 1) {
            stamp(x + i, y + 1.0f, 160, 40, 24, false);
        }
    }
    stamp(x - 3.0f, y - 1.0f + claw, 230, 90, 40, false);
    stamp(x - 3.5f, y - 2.0f + claw, 240, 120, 48, false);
    stamp(x + 3.0f, y - 1.0f - claw, 230, 90, 40, false);
    stamp(x + 3.5f, y - 2.0f - claw, 240, 120, 48, false);
    stamp(x, y - 1.0f, 20, 16, 16, false);
}

static void draw_shrimp(const Life &life, float x, float y, uint32_t anim_ms) {
    int face = life.face;
    float curl = sinf(anim_ms * 0.01f + life.phase) * 0.4f;
    for (int i = 0; i < 6; ++i) {
        float bow = (i - 2) * (i - 2) * 0.12f;
        stamp(x + face * (i - 2), y + bow + curl, 255, 130, 120, false);
    }
    stamp(x - face * 3.0f, y + 1.0f, 255, 170, 150, false);
    stamp(x - face * 3.5f, y, 255, 170, 150, false);
    stamp(x + face * 3.5f, y - 0.5f, 255, 200, 180, false);
    stamp(x + face * 4.5f, y - 1.0f, 255, 210, 190, false);
}

static void draw_eel(const Life &life, float x, float y, uint32_t anim_ms) {
    float hx = (float)life.face;
    float hy = 0.0f;
    float mag = sqrtf(life.vx * life.vx + life.vy * life.vy);
    if (mag > 0.4f) {
        hx = life.vx / mag;
        hy = life.vy / mag;
    }
    int segs = 12;
    for (int i = 0; i < segs; ++i) {
        float t = i / (float)(segs - 1);
        float along = (t - 0.55f) * 12.0f;
        float side = sinf(t * 5.0f + anim_ms * 0.006f + life.phase) * 0.9f;
        float px = x + hx * along - hy * side;
        float py = y + hy * along + hx * side;
        int red = 150;
        int green = 190;
        int blue = 70;
        if (i > 7) {
            red = 110;
            green = 140;
            blue = 48;
        }
        stamp(px, py, red, green, blue, true);
    }
}

static void draw_shark(const Life &life, float x, float y, uint32_t anim_ms) {
    int face = life.face;
    int style = 0;
    int len = 16;
    if (life.kind == LifeTiger) {
        style = 1;
    } else if (life.kind == LifeThresher) {
        style = 2;
        len = 12;
    } else if (life.kind == LifeWhaleShark) {
        style = 3;
        len = 20;
    } else if (life.kind == LifeHammer) {
        style = 4;
        len = 14;
    }
    int body_r = 176;
    int body_g = 186;
    int body_b = 196;
    int belly_r = 230;
    int belly_g = 236;
    int belly_b = 240;
    if (style == 1) {
        body_r = 196;
        body_g = 150;
        body_b = 64;
        belly_r = 230;
        belly_g = 200;
        belly_b = 130;
    } else if (style == 2) {
        body_r = 150;
        body_g = 176;
        body_b = 198;
    } else if (style == 3) {
        body_r = 64;
        body_g = 110;
        body_b = 150;
        belly_r = 180;
        belly_g = 200;
        belly_b = 210;
    } else if (style == 4) {
        body_r = 150;
        body_g = 164;
        body_b = 176;
    }
    float wag = sinf(anim_ms * 0.01f + life.phase) * 0.6f;
    for (int i = 0; i < len; ++i) {
        float tail = 1.0f - i / (float)(len - 1);
        float px = x + face * (i - len * 0.45f);
        float py = y + wag * tail * 0.8f;
        int rad = 1;
        if (i > len / 6 && i < len * 3 / 5) {
            rad = 2;
        }
        if (style == 3 && i > len / 5 && i < len * 2 / 3) {
            rad = 3;
        }
        if (i > len * 4 / 5) {
            rad = 0;
        }
        for (int dy = -rad; dy <= rad; ++dy) {
            bool belly = dy == rad && rad > 0;
            bool stripe = style == 1 && (i % 3) == 0;
            bool spot = style == 3 && ((i * 5 + dy * 3) % 7) == 0;
            int red = belly ? belly_r : body_r;
            int green = belly ? belly_g : body_g;
            int blue = belly ? belly_b : body_b;
            if (stripe) {
                red = 96;
                green = 70;
                blue = 32;
            }
            if (spot) {
                red = 230;
                green = 220;
                blue = 160;
            }
            stamp(px, py + dy, red, green, blue, false);
        }
        if (i == len / 3) {
            stamp(px, py - rad - 1.0f, body_r, body_g, body_b, false);
            stamp(px, py - rad - 2.0f, body_r, body_g, body_b, false);
        }
        if (i == len / 2) {
            stamp(px, py + rad + 1.0f, body_r, body_g, body_b, false);
            stamp(px + face, py + rad + 2.0f, body_r, body_g, body_b, false);
        }
        if (i == 0) {
            stamp(px, py - 1.0f, body_r, body_g, body_b, false);
            stamp(px, py - 2.0f, body_r, body_g, body_b, false);
            stamp(px, py + 1.0f, body_r, body_g, body_b, false);
            if (style == 2) {
                for (int k = 1; k <= 8; ++k) {
                    stamp(px - face * k, py - k * 0.85f, body_r, body_g, body_b, false);
                }
            }
        }
    }
    if (style == 4) {
        float hx = x + face * (len * 0.5f);
        for (int dy = -3; dy <= 3; ++dy) {
            stamp(hx, y + dy, body_r, body_g, body_b, false);
        }
        stamp(hx + face, y - 3.0f, 20, 20, 24, false);
        stamp(hx + face, y + 3.0f, 20, 20, 24, false);
    }
}

static void draw_dolphin(const Life &life, float x, float y, bool orca, uint32_t anim_ms) {
    int face = life.face;
    int len = orca ? 12 : 10;
    int body_r = orca ? 16 : 186;
    int body_g = orca ? 16 : 190;
    int body_b = orca ? 22 : 198;
    float wag = sinf(anim_ms * 0.014f + life.phase) * 0.55f;
    for (int i = 0; i < len; ++i) {
        float tail = 1.0f - i / (float)(len - 1);
        float arch = sinf(i / (float)(len - 1) * 3.14159f) * 0.8f;
        float px = x + face * (i - len * 0.45f);
        float py = y - arch + wag * tail;
        stamp(px, py, body_r, body_g, body_b, false);
        if (i > 1 && i < len - 2) {
            stamp(px, py + 1.0f, orca ? 230 : body_r, orca ? 230 : body_g, orca ? 236 : body_b, false);
        }
        if (orca && i == len - 4) {
            stamp(px, py - 1.0f, 240, 240, 245, false);
            stamp(px + face, py - 1.0f, 240, 240, 245, false);
        }
        if (i == len / 3) {
            int fin = orca ? 3 : 2;
            for (int k = 1; k <= fin; ++k) {
                stamp(px, py - k, body_r, body_g, body_b, false);
            }
        }
        if (i == 0) {
            stamp(px - face, py - 1.0f, body_r, body_g, body_b, false);
            stamp(px - face, py + 1.0f, body_r, body_g, body_b, false);
        }
        if (i == len - 1) {
            stamp(px + face, py, body_r, body_g, body_b, false);
        }
    }
}

static void draw_whale(float x, float y, int face, uint32_t anim_ms, float phase) {
    int len = 38;
    float wag = sinf(anim_ms * 0.006f + phase) * 0.5f;
    for (int i = 0; i < len; ++i) {
        float tail = 1.0f - i / (float)(len - 1);
        float px = x + face * (i - len * 0.5f);
        float py = y + wag * tail;
        int rad = 2;
        if (i > 8 && i < 26) {
            rad = 3;
        }
        if (i > 30) {
            rad = 1;
        }
        if (i > 34) {
            rad = 0;
        }
        for (int dy = -rad; dy <= rad; ++dy) {
            int red = 70;
            int green = 130;
            int blue = 190;
            if (dy == rad) {
                red = 170;
                green = 200;
                blue = 220;
            }
            if (i > 10 && i < 18 && dy > 0 && (i % 2) == 0) {
                red = 40;
                green = 80;
                blue = 120;
            }
            stamp(px, py + dy, red, green, blue, false);
        }
        if (i == 7) {
            stamp(px, py - rad - 1.0f, 60, 110, 170, false);
        }
        if (i == 0) {
            stamp(px - face, py - 2.0f, 60, 110, 170, false);
            stamp(px - face * 2.0f, py - 2.0f, 60, 110, 170, false);
            stamp(px - face, py + 2.0f, 60, 110, 170, false);
            stamp(px - face * 2.0f, py + 2.0f, 60, 110, 170, false);
        }
    }
}

static void draw_manta(float x, float y, int face, uint32_t anim_ms, float phase) {
    float flap = sinf(anim_ms * 0.004f + phase);
    for (int i = -8; i <= 8; ++i) {
        float tip = (i / 8.0f) * (i / 8.0f);
        float lift = flap * tip * 2.2f;
        stamp(x + i, y + lift, 36, 52, 78, false);
        if (i > -5 && i < 5) {
            stamp(x + i, y + 1.0f + lift * 0.4f, 28, 40, 64, false);
        }
        if (i > -2 && i < 2) {
            stamp(x + i, y - 1.0f, 220, 220, 230, false);
        }
    }
    stamp(x - face * 2.0f, y + 1.0f, 36, 52, 78, false);
    stamp(x - face * 4.0f, y + 1.5f, 36, 52, 78, false);
    stamp(x + face * 2.0f, y, 20, 24, 32, false);
}

static void draw_mosa(float x, float y, int face, uint32_t anim_ms, float phase) {
    int len = 26;
    float wag = sinf(anim_ms * 0.009f + phase) * 0.7f;
    for (int i = 0; i < len; ++i) {
        float tail = 1.0f - i / (float)(len - 1);
        float px = x + face * (i - len * 0.45f);
        float py = y + wag * tail;
        int rad = 1;
        if (i > 6 && i < 16) {
            rad = 2;
        }
        if (i > 20) {
            rad = 0;
        }
        for (int dy = -rad; dy <= rad; ++dy) {
            int red = 96;
            int green = 118;
            int blue = 58;
            if (dy == rad) {
                red = 186;
                green = 170;
                blue = 110;
            }
            stamp(px, py + dy, red, green, blue, false);
        }
        if (i == 16 || i == 10) {
            stamp(px, py - rad - 1.0f, 80, 96, 48, false);
            stamp(px + face, py - rad - 2.0f, 80, 96, 48, false);
            stamp(px, py + rad + 1.0f, 80, 96, 48, false);
            stamp(px + face, py + rad + 2.0f, 80, 96, 48, false);
        }
        if (i > len - 4) {
            stamp(px, py + 1.0f, 230, 220, 170, false);
        }
        if (i == 0) {
            stamp(px - face, py - 2.0f, 80, 100, 48, false);
            stamp(px - face, py + 2.0f, 80, 100, 48, false);
            stamp(px - face * 2.0f, py - 1.0f, 80, 100, 48, false);
        }
    }
}

static void member_pos(const Life &life, int index, float &x, float &y, uint32_t anim_ms) {
    float mag = sqrtf(life.vx * life.vx + life.vy * life.vy);
    float hx = (float)life.face;
    float hy = 0.0f;
    if (mag > 0.5f) {
        hx = life.vx / mag;
        hy = life.vy / mag;
    }
    const Member &member = life.members[index];
    float bob = sinf(anim_ms * 0.006f + member.phase) * 0.45f;
    x = life.x + hx * member.ox - hy * member.oy;
    y = life.y + hy * member.ox + hx * member.oy + bob;
}

static int draw_band(int kind) {
    if (kind == LifeBlue || kind == LifeMosa || kind == LifeManta || kind == LifeDolphin || kind == LifeOrca) {
        return 0;
    }
    if (kind >= LifeWhite && kind <= LifeHammer) {
        return 1;
    }
    return 2;
}

static void draw_member(const Life &life, float x, float y, uint32_t anim_ms) {
    if (life.kind == LifeFish) {
        draw_fish(life, x, y, life.face, life.phase, anim_ms);
    } else if (life.kind == LifeJelly) {
        draw_jelly(life, x, y, anim_ms);
    } else if (life.kind == LifeOctopus) {
        draw_octopus(life, x, y, anim_ms);
    } else if (life.kind == LifeTurtle) {
        draw_turtle(life, x, y, anim_ms);
    } else if (life.kind == LifeCrab) {
        draw_crab(life, x, y, anim_ms);
    } else if (life.kind == LifeShrimp) {
        draw_shrimp(life, x, y, anim_ms);
    } else if (life.kind == LifeEel) {
        draw_eel(life, x, y, anim_ms);
    } else if (life.kind == LifeDolphin) {
        draw_dolphin(life, x, y, false, anim_ms);
    } else if (life.kind == LifeOrca) {
        draw_dolphin(life, x, y, true, anim_ms);
    } else if (life.kind == LifeBlue) {
        draw_whale(x, y, life.face, anim_ms, life.phase);
    } else if (life.kind == LifeManta) {
        draw_manta(x, y, life.face, anim_ms, life.phase);
    } else if (life.kind == LifeMosa) {
        draw_mosa(x, y, life.face, anim_ms, life.phase);
    } else {
        draw_shark(life, x, y, anim_ms);
    }
}

static void draw_life(uint32_t anim_ms) {
    for (int band = 0; band < 3; ++band) {
        for (int i = 0; i < life_count; ++i) {
            const Life &life = lives[i];
            if (!life.alive || draw_band(life.kind) != band) {
                continue;
            }
            int count = life.count;
            if (count > MEMBER_CAP) {
                count = MEMBER_CAP;
            }
            if (count < 1) {
                count = 1;
            }
            if (life.kind == LifeFish) {
                for (int m = 0; m < count; ++m) {
                    const Member &member = life.members[m];
                    int face = member.vx < 0.0f ? -1 : 1;
                    draw_fish(life, member.ox, member.oy, face, member.phase, anim_ms);
                }
                continue;
            }
            for (int m = 0; m < count; ++m) {
                float x;
                float y;
                member_pos(life, m, x, y, anim_ms);
                draw_member(life, x, y, anim_ms);
            }
        }
    }
}

}  // namespace

void reef_start() {
    rng = 0x5EEDC0A1u;
    build_mask();
    init_bubbles();
    init_life();
}

void reef_draw(uint8_t *frame, uint32_t anim_ms, int dt_ms) {
    cast_beams(anim_ms);
    draw_water(anim_ms);
    draw_rocks();
    draw_weeds(anim_ms);
    draw_places(GROWTH, (int)(sizeof GROWTH / sizeof GROWTH[0]), anim_ms);
    draw_wavers(anim_ms);
    draw_places(SHELLS, (int)(sizeof SHELLS / sizeof SHELLS[0]), anim_ms);
    update_life(anim_ms, dt_ms);
    draw_life(anim_ms);
    update_bubbles(dt_ms);
    sway_into(frame, anim_ms);
    paint_bubbles(frame, anim_ms);
}
