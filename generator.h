#ifndef GENERATOR_H
#define GENERATOR_H

#include "cuda_mock.h"
#include <cstdint>
#include <cstddef>

//=============================================================================
// Chunk dimensions (standard Minecraft column)
//=============================================================================
static constexpr unsigned int CHUNK_SIZE_X = 16;
static constexpr unsigned int CHUNK_SIZE_Y = 256;
static constexpr unsigned int CHUNK_SIZE_Z = 16;
static constexpr unsigned int CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;

//=============================================================================
// Block type IDs (simplified palette)
//=============================================================================
using block_t = unsigned char;

static constexpr block_t BLOCK_AIR     = 0;
static constexpr block_t BLOCK_STONE   = 1;
static constexpr block_t BLOCK_DIRT    = 2;
static constexpr block_t BLOCK_GRASS   = 3;
static constexpr block_t BLOCK_BEDROCK = 4;
static constexpr block_t BLOCK_WATER   = 5;

//=============================================================================
// Chunk buffer — a device/host memory block holding one chunk's blocks
//=============================================================================
struct ChunkBuffer {
    block_t* data = nullptr;  // CHUNK_VOLUME elements
    unsigned int size = CHUNK_VOLUME;

    // Size in bytes
    size_t bytes() const { return (size_t)CHUNK_VOLUME * sizeof(block_t); }

    // Index helper: column-major, y varies fastest
    unsigned int index(unsigned int x, unsigned int y, unsigned int z) const {
        return (z * CHUNK_SIZE_X + x) * CHUNK_SIZE_Y + y;
    }
};

//=============================================================================
// Host-callable chunk generator
// Launches the CUDA kernel (or mock) that fills d_buffer with terrain.
//=============================================================================
void launch_chunk_generator(ChunkBuffer d_buffer, int64_t chunk_x, int64_t chunk_z,
                             int64_t seed, void* device_buf = nullptr,
                             cudaStream_t stream = nullptr);

#endif // GENERATOR_H