//=============================================================================
// noise_simd.h — SIMD-friendly noise helpers (auto-vectorized by compiler)
//
// No hand-written intrinsics — just clean loops that `-O3 -ffast-math
// -march=native` can auto-vectorize. Works on any CPU.
//
// Provides:
//   perlin3d()         — single-column 3D Perlin noise
//   fbm3d()            — single-column FBM (octave noise)
//   simd_chunk_heights — batch-compute 256 column heights into a heightmap
//=============================================================================
#ifndef NOISE_SIMD_H
#define NOISE_SIMD_H

// Skip if compiled alongside generator.cu (which has its own)
#if !defined(__CUDACC__)

#include <cstdint>
#include <cmath>

//=============================================================================
// 3D Perlin noise — single column
//=============================================================================
static inline float grad3d(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = (h < 8) ? x : y;
    float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

static inline float perlin3d(float x, float y, float z,
                              const unsigned char* perm_table) {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    float u = fade(x);
    float v = fade(y);
    float w = fade(z);

    int A  = perm_table[X]     + Y;
    int AA = perm_table[A]     + Z;
    int AB = perm_table[A + 1] + Z;
    int B  = perm_table[X + 1] + Y;
    int BA = perm_table[B]     + Z;
    int BB = perm_table[B + 1] + Z;

    float p0 = grad3d(perm_table[AA],     x,     y,     z);
    float p1 = grad3d(perm_table[BA],     x-1,   y,     z);
    float p2 = grad3d(perm_table[AB],     x,     y-1,   z);
    float p3 = grad3d(perm_table[BB],     x-1,   y-1,   z);
    float p4 = grad3d(perm_table[AA+1],   x,     y,     z-1);
    float p5 = grad3d(perm_table[BA+1],   x-1,   y,     z-1);
    float p6 = grad3d(perm_table[AB+1],   x,     y-1,   z-1);
    float p7 = grad3d(perm_table[BB+1],   x-1,   y-1,   z-1);

    float l0 = lerp(p0, p1, u);
    float l1 = lerp(p2, p3, u);
    float l2 = lerp(p4, p5, u);
    float l3 = lerp(p6, p7, u);
    float l4 = lerp(l0, l1, v);
    float l5 = lerp(l2, l3, v);
    return lerp(l4, l5, w);
}

//=============================================================================
// Fractal Brownian Motion (FBM) — 4-octave noise for terrain height
//=============================================================================
static inline float fbm3d(float x, float y, float z, int octaves,
                           const unsigned char* perm_table) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_val = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * perlin3d(x * frequency, y * frequency, z * frequency, perm_table);
        max_val += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value / max_val;
}

//=============================================================================
// Batch height computation — fills heights[bx][bz] for all 256 columns
//
// Structured as simple loops for compiler auto-vectorization.
// With -O3 -ffast-math -march=native, gcc/clang will vectorize the inner
// noise computation on most modern CPUs (NEON, AVX2, SSE).
//=============================================================================
static inline void compute_chunk_heights(int heights[16][16],
                                          int64_t chunk_x, int64_t chunk_z,
                                          int64_t seed,
                                          const unsigned char* perm_table) {
    float offset = (float)(seed % 65536) * 137.631f;
    float wx_base = (float)(chunk_x * 16);
    float wz_base = (float)(chunk_z * 16);

    for (int bx = 0; bx < 16; ++bx) {
        for (int bz = 0; bz < 16; ++bz) {
            float wx = wx_base + (float)bx;
            float wz = wz_base + (float)bz;

            float hn = fbm3d(wx * 0.02f + offset, wz * 0.02f + offset, 0.5f, 4, perm_table);
            int h = (int)((hn * 0.5f + 0.5f) * 44.0f + 4.0f);
            if (h > 256) h = 256;
            heights[bx][bz] = h;
        }
    }
}

#endif // !__CUDACC__
#endif // NOISE_SIMD_H