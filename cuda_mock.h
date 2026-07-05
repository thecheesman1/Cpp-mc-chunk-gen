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
//
// Uses a getter-function pattern to avoid TLS init symbol conflicts on
// MinGW (GCC on Windows). Function-local statics have well-defined TLS
// semantics across translation units even when MinGW's inline variable
// support chokes on thread_local.
// ---------------------------------------------------------------------------
namespace cuda_mock_tls {

struct CudaTls {
    dim3 threadIdx{0, 0, 0};
    dim3 blockIdx{0, 0, 0};
    dim3 blockDim{1, 1, 1};
    dim3 gridDim{1, 1, 1};
};

inline CudaTls& get() {
    static thread_local CudaTls tls;
    return tls;
}

} // namespace cuda_mock_tls

#define threadIdx (cuda_mock_tls::get().threadIdx)
#define blockIdx  (cuda_mock_tls::get().blockIdx)
#define blockDim  (cuda_mock_tls::get().blockDim)
#define gridDim   (cuda_mock_tls::get().gridDim)

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
    // Run all blocks sequentially in the calling thread.
    // Do NOT spawn threads here — parallelism is already provided by
    // the worker_thread pool.  On Windows/MinGW, CreateThread is
    // expensive (~10 µs per spawn) and destroys performance at scale.
    unsigned int n_blocks = grid.x * grid.y * grid.z;
    unsigned int n_threads_per_block = block.x * block.y * block.z;

    for (unsigned int bid = 0; bid < n_blocks; ++bid) {
        unsigned int bz = bid / (grid.x * grid.y);
        unsigned int rem = bid % (grid.x * grid.y);
        unsigned int by = rem / grid.x;
        unsigned int bx = rem % grid.x;

        for (unsigned int tid = 0; tid < n_threads_per_block; ++tid) {
            unsigned int tz = tid / (block.x * block.y);
            unsigned int trem = tid % (block.x * block.y);
            unsigned int ty = trem / block.x;
            unsigned int tx = trem % block.x;

            threadIdx = {tx, ty, tz};
            blockIdx  = {bx, by, bz};
            blockDim  = block;
            gridDim   = grid;

            kernel(args...);
        }
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