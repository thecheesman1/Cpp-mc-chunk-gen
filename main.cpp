//=============================================================================
// main.cpp — Asynchronous double-buffered chunk generation pipeline
//
// Architecture (the "Ping-Pong" Buffer):
//   Buffer A  ←── GPU writes noise into A (stream A)
//   Buffer B  ←── GPU writes noise into B (stream B)
//   While A is being filled, B's previous contents are copied back to host
//   and flushed to disk as a raw binary append stream.
//=============================================================================
#include "generator.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

//=============================================================================
// Configuration
//=============================================================================
static constexpr int64_t DEFAULT_SEED        = 42;
static constexpr unsigned int N_CHUNKS        = 16384;   // total chunks to generate
static constexpr unsigned int CHUNKS_PER_FLUSH = 256;     // disk flush granularity
static const char* DEFAULT_OUTPUT = "terrain.bin";

//=============================================================================
// Raw binary database writer
//
// Format (contiguous append-only):
//   [header: magic(4B) | n_chunks(4B) | chunk_size(4B)]
//   [chunk_0: x(8B) | z(8B) | blocks(CHUNK_VOLUME * 1B)]
//   [chunk_1: ... ]
//   ...
//=============================================================================
struct BinaryWriter {
    FILE* fp = nullptr;
    size_t total_chunks_written = 0;

    bool open(const char* path) {
        fp = std::fopen(path, "wb");
        if (!fp) {
            std::perror("fopen");
            return false;
        }
        // Write placeholder header (12 bytes); we'll rewrite it at close
        unsigned int zero = 0;
        std::fwrite("MCCH", 1, 4, fp);          // magic
        std::fwrite(&zero, 4, 1, fp);           // n_chunks (placeholder)
        std::fwrite(&zero, 4, 1, fp);           // chunk_size (placeholder)
        return true;
    }

    void append_chunk(const block_t* blocks, int64_t cx, int64_t cz) {
        assert(fp);
        std::fwrite(&cx, sizeof(cx), 1, fp);
        std::fwrite(&cz, sizeof(cz), 1, fp);
        std::fwrite(blocks, 1, CHUNK_VOLUME, fp);
        ++total_chunks_written;
    }

    void flush() {
        std::fflush(fp);
    }

    void close() {
        if (fp) {
            // Rewrite header with final counts
            std::fseek(fp, 4, SEEK_SET);
            unsigned int n = (unsigned int)total_chunks_written;
            unsigned int sz = CHUNK_VOLUME;
            std::fwrite(&n, 4, 1, fp);
            std::fwrite(&sz, 4, 1, fp);
            std::fclose(fp);
            fp = nullptr;
        }
    }

    ~BinaryWriter() { if (fp) close(); }
};

//=============================================================================
// Double-buffered pipeline state
//=============================================================================
struct PingPongBuffers {
    // Device (mock = heap) buffers
    ChunkBuffer dev_A;
    ChunkBuffer dev_B;

    // Host staging buffers for transfers
    block_t* host_A = nullptr;
    block_t* host_B = nullptr;

    // CUDA streams (nullptr in mock mode)
    cudaStream_t stream_gen_A  = nullptr;
    cudaStream_t stream_gen_B  = nullptr;
    cudaStream_t stream_xfer   = nullptr;

    bool init() {
        // Allocate "device" buffers (cudaMalloc → malloc in mock mode)
        if (cudaMalloc((void**)&dev_A.data, dev_A.bytes()) != cudaSuccess) return false;
        if (cudaMalloc((void**)&dev_B.data, dev_B.bytes()) != cudaSuccess) return false;

        // Allocate host-pinned (or plain) staging buffers
        host_A = (block_t*)std::malloc(dev_A.bytes());
        host_B = (block_t*)std::malloc(dev_B.bytes());
        if (!host_A || !host_B) return false;

        // Create streams
        cudaStreamCreate(&stream_gen_A);
        cudaStreamCreate(&stream_gen_B);
        cudaStreamCreate(&stream_xfer);
        return true;
    }

    void destroy() {
        cudaStreamDestroy(stream_gen_A);
        cudaStreamDestroy(stream_gen_B);
        cudaStreamDestroy(stream_xfer);
        cudaFree(dev_A.data);
        cudaFree(dev_B.data);
        std::free(host_A);
        std::free(host_B);
    }
};

//=============================================================================
// Progress bar helper
//=============================================================================
static void print_progress(unsigned int done, unsigned int total) {
    if (total == 0) return;
    int pct = (int)(100ULL * done / total);
    printf("\r  Progress: %u / %u  [%d%%]", done, total, pct);
    std::fflush(stdout);
    if (done == total) printf("\n");
}

//=============================================================================
// Main pipeline
//=============================================================================
int main(int argc, char** argv) {
    const char* output_path = (argc > 1) ? argv[1] : DEFAULT_OUTPUT;
    int64_t seed = (argc > 2) ? std::atol(argv[2]) : DEFAULT_SEED;
    unsigned int n_chunks = (argc > 3) ? (unsigned int)std::atoi(argv[3]) : N_CHUNKS;

    printf("=== GPU-Accelerated Chunk Generator (C++/CUDA) ===\n");
    printf("  Output   : %s\n", output_path);
    printf("  Seed     : %ld\n", (long)seed);
    printf("  Chunks   : %u\n", n_chunks);
#ifdef __CUDACC__
    printf("  Mode     : CUDA (real GPU)\n");
#else
    printf("  Mode     : CPU (mock, no nvcc detected)\n");
#endif
    printf("\n");

    // --- Initialise ---
    PingPongBuffers bufs;
    if (!bufs.init()) {
        fprintf(stderr, "ERROR: failed to allocate buffers\n");
        return 1;
    }

    BinaryWriter writer;
    if (!writer.open(output_path)) {
        bufs.destroy();
        return 1;
    }

    // --- Prime the pipeline ---
    // Launch first generation into Buffer A (chunk 0)
    int64_t cx = 0;
    launch_chunk_generator(bufs.dev_A, cx, 0, seed, bufs.stream_gen_A);
    cudaStreamSynchronize(bufs.stream_gen_A);

    // --- Main ping-pong loop ---
    auto t_start = std::chrono::high_resolution_clock::now();

    // We generate N chunks in total. Chunk index i is generated into
    // either buffer A or B depending on parity.
    for (unsigned int i = 0; i < n_chunks; ++i) {
        // Determine which buffer is being read-back / written
        bool use_A = (i % 2 == 0);
        ChunkBuffer& dev_fill  = use_A ? bufs.dev_B : bufs.dev_A;
        block_t*     host_read = use_A ? bufs.host_A : bufs.host_B;
        cudaStream_t stream_fill = use_A ? bufs.stream_gen_B : bufs.stream_gen_A;

        int64_t next_cx = (int64_t)(i + 1);  // chunk x-index for the NEXT generation

        // If there's a next chunk, launch its generation asynchronously
        // into the *other* buffer while we transfer + write this one.
        if (i + 1 < n_chunks) {
            launch_chunk_generator(dev_fill, next_cx, 0, seed, stream_fill);
        }

        // Copy the *previous* (completed) chunk from device → host
        ChunkBuffer& dev_completed = use_A ? bufs.dev_A : bufs.dev_B;
        cudaMemcpyAsync(host_read, dev_completed.data, dev_completed.bytes(),
                        cudaMemcpyDeviceToHost, bufs.stream_xfer);
        cudaStreamSynchronize(bufs.stream_xfer);

        // Flush to disk (write-ahead buffered, batched every CHUNKS_PER_FLUSH)
        writer.append_chunk(host_read, i, 0);

        if ((i + 1) % CHUNKS_PER_FLUSH == 0 || i + 1 == n_chunks) {
            writer.flush();
        }

        // Ensure the next generation is done before we loop
        if (i + 1 < n_chunks) {
            cudaStreamSynchronize(stream_fill);
        }

        print_progress(i + 1, n_chunks);
    }

    auto t_end = std::chrono::high_resolution_clock::now();

    // --- Finalise ---
    writer.close();
    bufs.destroy();

    // --- Stats ---
    double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();
    double cps = (double)n_chunks / elapsed_s;
    double mib_written = (double)n_chunks * (double)CHUNK_VOLUME / (1024.0 * 1024.0);

    printf("\n=== Results ===\n");
    printf("  Chunks generated  : %u\n", n_chunks);
    printf("  Time              : %.4f s\n", elapsed_s);
    printf("  Throughput        : %.2f CPS\n", cps);
    printf("  Data written      : %.2f MiB\n", mib_written);
    printf("  Output file       : %s\n", output_path);
    printf("================\n");

    return 0;
}