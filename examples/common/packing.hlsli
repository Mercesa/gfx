#ifndef PACKING_HLSLI
#define PACKING_HLSLI

uint pack_2x16_uint(float2 f) {
    return f32tof16(f.x) | f32tof16(f.y) << 16u;
}

float2 unpack_uint_2x16(uint u) {
    return float2(f16tof32(u & 0xffff),
        f16tof32((u >> 16) & 0xffff));
}

float roughness_to_perceptual_roughness(float r) {
    return sqrt(r);
}

float perceptual_roughness_to_roughness(float r) {
    return r * r;
}
#endif