#ifndef CUDA_MOCK_H
#define CUDA_MOCK_H

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>

//=============================================================================
// CUDA Mock / Wrapper Header
//
// When compiled with nvcc (__CUDACC__ defined):
//   Includes real <cuda_runtime.h> and provides LAUNCH_KERNEL using <<<>>>.
//
// When compiled without nvcc (__CUDACC__ not defined):
//   Stubs out all CUDA API calls on plain heap memory and translates kernel
//   launches into multi-threaded CPU execution loops.
//=============================================================================

#ifdef __CUDACC__
// ---------------------------------------------------------------------------
// REAL CUDA MODE — include the actual runtime
// ---------------------------------------------------------------------------
#include <cuda_runtime.h>

// LAUNCH_KERNEL wraps real <<<>>> with error checking
#define LAUNCH_KERNEL(kernel, grid, block, stream, ...)                \
    do {                                                               \
        kernel<<<grid, block, 0, stream>>>(__VA_ARGS__);               \
        cudaError_t _err = cudaGetLastError();                         \
        if (_err != cudaSuccess) {                                     \
            fprintf(stderr, "CUDA error: %d at %s:%d\n",               \
                    (int)_err, __FILE__, __LINE__);                    \
            exit(1);                                                   \
        }                                                              \
    } while (0)

#else  // !__CUDACC__ — mock mode

#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Mock CUDA scalar types
// ---------------------------------------------------------------------------
using cudaError_t = int;

static constexpr int cudaSuccess              = 0;
static constexpr int cudaErrorInvalidValue    = 1;
static constexpr int cudaErrorMemoryAllocation = 2;

// ---------------------------------------------------------------------------
// Mock dim3 — used for grid/block dimensions
// ---------------------------------------------------------------------------
struct dim3 {
    unsigned int x, y, z;
    dim3() : x(1), y(1), z(1) {}
    dim3(unsigned int x_) : x(x_), y(1), z(1) {}
    dim3(unsigned int x_, unsigned int y_) : x(x_), y(y_), z(1) {}
    dim3(unsigned int x_, unsigned int y_, unsigned int z_) : x(x_), y(y_), z(z_) {}
};

// ---------------------------------------------------------------------------
// Mock CUDA stream
// ---------------------------------------------------------------------------
using cudaStream_t = void*;

// ---------------------------------------------------------------------------
// Mock memory-management API
// ---------------------------------------------------------------------------
inline cudaError_t cudaMalloc(void** devPtr, size_t size) {
    *devPtr = std::malloc(size);
    if (!*devPtr) return cudaErrorMemoryAllocation;
    return cudaSuccess;
}

inline cudaError_t cudaFree(void* devPtr) {
    std::free(devPtr);
    return cudaSuccess;
}

inline cudaError_t cudaMallocHost(void** ptr, size_t size) {
    *ptr = std::malloc(size);
    if (!*ptr) return cudaErrorMemoryAllocation;
    return cudaSuccess;
}

inline cudaError_t cudaFreeHost(void* ptr) {
    std::free(ptr);
    return cudaSuccess;
}

// ---------------------------------------------------------------------------
// Mock memory-transfer API
// ---------------------------------------------------------------------------
enum cudaMemcpyKind {
    cudaMemcpyHostToHost   = 0,
    cudaMemcpyHostToDevice = 1,
    cudaMemcpyDeviceToHost = 2,
    cudaMemcpyDeviceToDevice = 3
};

inline cudaError_t cudaMemcpy(void* dst, const void* src, size_t count,
                               cudaMemcpyKind /*kind*/) {
    std::memcpy(dst, src, count);
    return cudaSuccess;
}

inline cudaError_t cudaMemcpyAsync(void* dst, const void* src, size_t count,
                                    cudaMemcpyKind kind, cudaStream_t /*stream*/ = 0) {
    return cudaMemcpy(dst, src, count, kind);
}

// ---------------------------------------------------------------------------
// Mock stream / synchronisation API
// ---------------------------------------------------------------------------
inline cudaError_t cudaStreamCreate(cudaStream_t* stream) {
    *stream = nullptr;
    return cudaSuccess;
}

inline cudaError_t cudaStreamDestroy(cudaStream_t /*stream*/) {
    return cudaSuccess;
}

inline cudaError_t cudaStreamSynchronize(cudaStream_t /*stream*/) {
    return cudaSuccess;
}

inline cudaError_t cudaDeviceSynchronize() {
    return cudaSuccess;
}

inline cudaError_t cudaGetLastError() {
    return cudaSuccess;
}

// ---------------------------------------------------------------------------
// Thread-local CUDA built-in variables (set by LAUNCH_KERNEL)
// Reuse dim3 for all so assignment from dim3 literals works.
// inline (C++17) prevents multiple-definition linker errors.
// ---------------------------------------------------------------------------
inline thread_local dim3 threadIdx{0, 0, 0};
inline thread_local dim3 blockIdx{0, 0, 0};
inline thread_local dim3 blockDim{1, 1, 1};
inline thread_local dim3 gridDim{1, 1, 1};

// ---------------------------------------------------------------------------
// Strip CUDA function qualifiers for mock mode
// ---------------------------------------------------------------------------
#define __global__
#define __device__
#define __host__

// ---------------------------------------------------------------------------
// LAUNCH_KERNEL macro — translates kernel<<<grid, block, 0, stream>>>(args)
// into multi-threaded CPU execution over the grid x block index space.
//
// Usage:  LAUNCH_KERNEL(my_kernel, grid, block, stream, arg1, arg2, ...)
// ---------------------------------------------------------------------------
namespace cuda_mock_detail {

template <typename Func, typename... Args>
void launch_kernel(Func&& kernel, dim3 grid, dim3 block,
                   cudaStream_t /*stream*/, Args&&... args) {
    // Spawn one std::thread per block-group for parallelism,
    // each block's threads run sequentially within.
    unsigned int n_blocks = grid.x * grid.y * grid.z;
    unsigned int n_threads_per_block = block.x * block.y * block.z;
    unsigned int total_threads = n_blocks * n_threads_per_block;

    // Use a reasonable number of hardware threads
    unsigned int hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 4;
    unsigned int threads_to_spawn = std::min(n_blocks, hw_threads);

    auto worker = [&](unsigned int block_start, unsigned int block_end) {
        for (unsigned int bid = block_start; bid < block_end; ++bid) {
            unsigned int bz = bid / (grid.x * grid.y);
            unsigned int rem = bid % (grid.x * grid.y);
            unsigned int by = rem / grid.x;
            unsigned int bx = rem % grid.x;

            for (unsigned int tid = 0; tid < n_threads_per_block; ++tid) {
                unsigned int tz = tid / (block.x * block.y);
                unsigned int trem = tid % (block.x * block.y);
                unsigned int ty = trem / block.x;
                unsigned int tx = trem % block.x;

                // Set thread-local CUDA built-ins
                threadIdx = {tx, ty, tz};
                blockIdx  = {bx, by, bz};
                blockDim  = block;
                gridDim   = grid;

                // Call the kernel
                kernel(args...);
            }
        }
    };

    if (threads_to_spawn <= 1 || n_blocks <= 1) {
        worker(0, n_blocks);
    } else {
        std::vector<std::thread> pool;
        unsigned int blocks_per_thread = n_blocks / threads_to_spawn;
        unsigned int remainder = n_blocks % threads_to_spawn;
        unsigned int start = 0;
        for (unsigned int i = 0; i < threads_to_spawn; ++i) {
            unsigned int count = blocks_per_thread + (i < remainder ? 1 : 0);
            pool.emplace_back(worker, start, start + count);
            start += count;
        }
        for (auto& t : pool) t.join();
    }
}

} // namespace cuda_mock_detail

#define LAUNCH_KERNEL(kernel, grid, block, stream, ...)               \
    do {                                                               \
        cuda_mock_detail::launch_kernel(                               \
            [](auto&... _args) { kernel(_args...); },                  \
            (grid), (block), (stream), ##__VA_ARGS__                   \
        );                                                             \
    } while (0)

#endif // __CUDACC__

#endif // CUDA_MOCK_H