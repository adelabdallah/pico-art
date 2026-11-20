#pragma once

#include <cmath>
#include <cstdint>

// Value noise and a few octaves of it. Patterns sample this while they paint.
// The hash and the octave sum follow the drawing kit for the pieces at
// https://www.ascii-magic.com/ascii

inline float field_smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

inline float field_hash(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u;
    h += (uint32_t)y * 668265263u;
    h += (uint32_t)seed * 2147483647u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)h * (1.0f / 4294967296.0f);
}

inline float field_noise(float x, float y, int seed) {
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    float xf = field_smooth(x - (float)xi);
    float yf = field_smooth(y - (float)yi);
    float a = field_hash(xi, yi, seed);
    float b = field_hash(xi + 1, yi, seed);
    float c = field_hash(xi, yi + 1, seed);
    float d = field_hash(xi + 1, yi + 1, seed);
    float top = a + (b - a) * xf;
    float bottom = c + (d - c) * xf;
    return top + (bottom - top) * yf;
}

inline float field_fbm(float x, float y, int octaves, int seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * field_noise(x * freq, y * freq, seed + i * 17);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    if (norm == 0.0f) {
        return 0.0f;
    }
    return sum / norm;
}
