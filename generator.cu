//=============================================================================
// generator.cu — GPU / CPU 3D terrain noise kernel and chunk generation
//
// Compiled with nvcc:   real GPU execution
// Compiled with g++:    CPU mock execution via cuda_mock.h (include path)
//=============================================================================
#include "generator.h"
#include <cmath>
#include <cstdint>
#include <cstring>

//=============================================================================
// Permutation table for Perlin noise (256 values, Ken Perlin's original)
//=============================================================================
__device__ static const unsigned char perm[256] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
};

//=============================================================================
// 3D Perlin noise — single-threaded building block
//=============================================================================
__device__ __host__ static inline float grad3d(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = (h < 8) ? x : y;
    float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

__device__ __host__ static inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

__device__ __host__ static inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

__device__ __host__ static inline float perlin3d(float x, float y, float z,
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

    float p[8] = {
        grad3d(perm_table[AA],     x,     y,     z),
        grad3d(perm_table[BA],     x-1,   y,     z),
        grad3d(perm_table[AB],     x,     y-1,   z),
        grad3d(perm_table[BB],     x-1,   y-1,   z),
        grad3d(perm_table[AA+1],   x,     y,     z-1),
        grad3d(perm_table[BA+1],   x-1,   y,     z-1),
        grad3d(perm_table[AB+1],   x,     y-1,   z-1),
        grad3d(perm_table[BB+1],   x-1,   y-1,   z-1),
    };

    float l0 = lerp(p[0], p[1], u);
    float l1 = lerp(p[2], p[3], u);
    float l2 = lerp(p[4], p[5], u);
    float l3 = lerp(p[6], p[7], u);
    float l4 = lerp(l0, l1, v);
    float l5 = lerp(l2, l3, v);
    return lerp(l4, l5, w);
}

//=============================================================================
// Octave noise (fractal Brownian Motion) for richer terrain
//=============================================================================
__device__ __host__ static float fbm3d(float x, float y, float z, int octaves,
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
    return value / max_val;  // Normalise to [-1, 1]
}

//=============================================================================
// Chunk generation kernel — one thread per column (x, z) in the chunk
// Fills a CHUNK_VOLUME array of block IDs.
//=============================================================================
__global__ void generate_chunk_kernel(ChunkBuffer buffer, int64_t chunk_x, int64_t chunk_z,
                                       int64_t seed) {
    unsigned int bx = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int bz = blockIdx.y * blockDim.y + threadIdx.y;

    if (bx >= CHUNK_SIZE_X || bz >= CHUNK_SIZE_Z) return;

    // World-space coordinates for this column
    float wx = (float)(chunk_x * CHUNK_SIZE_X + bx);
    float wz = (float)(chunk_z * CHUNK_SIZE_Z + bz);

    // Add a seeded offset so different seeds produce different terrain
    float offset = (float)(seed % 65536) * 137.631f;

    // Generate height using 2 octaves of noise
    float height_noise = fbm3d(wx * 0.02f + offset, wz * 0.02f + offset, 0.5f, 4, perm);
    // Map noise [-1, 1] to a height in [4, 48]
    int height = (int)((height_noise * 0.5f + 0.5f) * 44.0f + 4.0f);
    if (height > CHUNK_SIZE_Y) height = CHUNK_SIZE_Y;

    for (unsigned int y = 0; y < CHUNK_SIZE_Y; ++y) {
        unsigned int idx = (bz * CHUNK_SIZE_X + bx) * CHUNK_SIZE_Y + y;
        unsigned char block = BLOCK_AIR;

        if (y <= 1) {
            block = BLOCK_BEDROCK;
        } else if (y < height - 3) {
            block = BLOCK_STONE;
            // Simple cave carving: if 3D noise is above threshold, carve air
            float c = fbm3d(wx * 0.05f + offset, (float)y * 0.08f, wz * 0.05f + offset, 3, perm);
            if (c > 0.35f && y > 6) {
                block = BLOCK_AIR;
            }
        } else if (y < height - 1) {
            block = BLOCK_DIRT;
        } else if (y < height) {
            block = BLOCK_GRASS;
        }

        // Below sea-level (y=32) fill with stone/water if air
        if (block == BLOCK_AIR && y < 32 && y > height) {
            block = (y < 30) ? BLOCK_STONE : BLOCK_WATER;
        }

        buffer.data[idx] = block;
    }
}

//=============================================================================
// Host-callable chunk generator — launches the kernel and syncs
//=============================================================================
void launch_chunk_generator(ChunkBuffer d_buffer, int64_t chunk_x, int64_t chunk_z,
                             int64_t seed, void* device_buf,
                             cudaStream_t stream) {
#ifndef __CUDACC__
    // CPU mock mode: call the kernel directly as a simple double loop,
    // bypassing the expensive mock CUDA thread-launch machinery.
    // The caller already provides multi-threading (chunkgen_offline.cpp
    // spawns N worker threads), so spawning another pool per chunk
    // just creates thread-creation overhead — especially slow on Windows.
    for (unsigned int bx = 0; bx < CHUNK_SIZE_X; ++bx) {
        for (unsigned int bz = 0; bz < CHUNK_SIZE_Z; ++bz) {
            // World-space coordinates for this column
            float wx = (float)(chunk_x * CHUNK_SIZE_X + bx);
            float wz = (float)(chunk_z * CHUNK_SIZE_Z + bz);

            float offset = (float)(seed % 65536) * 137.631f;

            float height_noise = fbm3d(wx * 0.02f + offset, wz * 0.02f + offset, 0.5f, 4, perm);
            int height = (int)((height_noise * 0.5f + 0.5f) * 44.0f + 4.0f);
            if (height > CHUNK_SIZE_Y) height = CHUNK_SIZE_Y;

            for (unsigned int y = 0; y < CHUNK_SIZE_Y; ++y) {
                unsigned int idx = (bz * CHUNK_SIZE_X + bx) * CHUNK_SIZE_Y + y;
                unsigned char block = BLOCK_AIR;

                if (y <= 1) {
                    block = BLOCK_BEDROCK;
                } else if (y < height - 3) {
                    block = BLOCK_STONE;
                    float c = fbm3d(wx * 0.05f + offset, (float)y * 0.08f, wz * 0.05f + offset, 3, perm);
                    if (c > 0.35f && y > 6) {
                        block = BLOCK_AIR;
                    }
                } else if (y < height - 1) {
                    block = BLOCK_DIRT;
                } else if (y < height) {
                    block = BLOCK_GRASS;
                }

                if (block == BLOCK_AIR && y < 32 && y > height) {
                    block = (y < 30) ? BLOCK_STONE : BLOCK_WATER;
                }

                d_buffer.data[idx] = block;
            }
        }
    }
#else
    // CUDA mode: use pre-allocated device buffer or allocate one
    block_t* d_ptr = (block_t*)device_buf;
    bool own_alloc = (d_ptr == nullptr);
    if (own_alloc) {
        cudaError_t err = cudaMalloc(&d_ptr, CHUNK_VOLUME * sizeof(block_t));
        if (err != cudaSuccess) {
            fprintf(stderr, "CUDA error: %d at %s:%d\n", err, __FILE__, __LINE__);
            return;
        }
    }

    ChunkBuffer dev_buf;
    dev_buf.data = d_ptr;
    dev_buf.size = d_buffer.size;

    // 2D grid of 8×8 blocks, each block processes 2×2 columns (4×32 threads = 128)
    dim3 block_dim(2, 32, 1);
    dim3 grid_dim(
        (CHUNK_SIZE_X + block_dim.x - 1) / block_dim.x,
        (CHUNK_SIZE_Z + block_dim.y - 1) / block_dim.y,
        1
    );

    LAUNCH_KERNEL(generate_chunk_kernel, grid_dim, block_dim, stream,
                  dev_buf, chunk_x, chunk_z, seed);

    cudaError_t err = cudaMemcpy(d_buffer.data, d_ptr, CHUNK_VOLUME * sizeof(block_t),
                     cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA memcpy error: %d at %s:%d\n", err, __FILE__, __LINE__);
    }

    if (own_alloc) cudaFree(d_ptr);
#endif
}