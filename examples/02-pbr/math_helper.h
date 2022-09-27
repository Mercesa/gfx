#pragma once

#include <cinttypes>
#include <math.h>

uint32_t divide_up(float n, float d) {
    return static_cast<uint32_t>(ceilf(n / d));
}