// The Waveshare panel driver calls CLAMP. This file supplies that name so the vendor sources stay untouched.

#include <math.h>

#ifndef CLAMP
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif
