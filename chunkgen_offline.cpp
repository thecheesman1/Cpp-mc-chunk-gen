//=============================================================================
// chunkgen_offline.cpp — Standalone Minecraft chunk pre-generator
// Writes valid Anvil (.mca) region files directly from C++.
// Uses streaming writes (no memory accumulation) to handle any radius.
// Supports optional Vulkan GPU acceleration via --vulkan flag.
//=============================================================================
#include "generator.h"
#include "nbt.h"
#include "anvil.h"
#include "vulkan_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

//=============================================================================
// Config
//=============================================================================
static constexpr int BATCH_SIZE = 256;       // chunks per region-file flush
static constexpr int MAX_THREADS = 8;

struct Config {
    std::string world_path;
    int64_t seed = 0;
    int radius = 128;
    int center_x = 0;
    int center_z = 0;
    int threads = 4;
    bool quiet = false;
    bool vulkan = false;
};

//=============================================================================
// Progress tracking
//=============================================================================
struct Progress {
    std::atomic<int64_t> written{0};
    std::atomic<int64_t> total{0};
    std::chrono::steady_clock::time_point start;

    Progress() : start(std::chrono::steady_clock::now()) {}

    void log(const Config& cfg) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        int64_t w = written.load();
        double cps = elapsed > 0 ? w / elapsed : 0;
        int pct = (int)(100.0 * w / total.load());
        if (!cfg.quiet) {
            printf("\r[McChunkGen] %lld/%lld (%d%%) — %.0f CPS, %.1fs",
                   (long long)w, (long long)total.load(), pct, cps, elapsed);
            fflush(stdout);
        }
    }
};

//=============================================================================
// Region file manager — thread-safe streaming writer with file caching
// Keeps region files open across chunks to avoid fopen/fclose per chunk.
//=============================================================================
class RegionManager {
    std::mutex mtx_;
    std::string world_path_;

    // Cache of open region files: key = "rx,rz"
    struct CachedRegion {
        RegionFile file;
        int rx, rz;
        bool dirty = false;
    };
    std::vector<CachedRegion> cache_;

    CachedRegion* find_cached(int rx, int rz) {
        for (auto& cr : cache_) {
            if (cr.rx == rx && cr.rz == rz) return &cr;
        }
        return nullptr;
    }

    void flush_all() {
        for (auto& cr : cache_) {
            if (cr.dirty) {
                cr.file.close();
                cr.dirty = false;
            }
        }
        cache_.clear();
    }

public:
    RegionManager(const std::string& wp) : world_path_(wp) {
        std::string rd = wp + "/region";
#ifdef _WIN32
        _mkdir(rd.c_str());
#else
        mkdir(rd.c_str(), 0755);
#endif
        cache_.reserve(16);
    }

    ~RegionManager() { flush_all(); }

    void write_chunk(int cx, int cz, const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(mtx_);
        int rx = cx >> 5, rz = cz >> 5;
        int lcx = cx & 31, lcz = cz & 31;

        CachedRegion* cr = find_cached(rx, rz);
        if (!cr) {
            // Evict old entries if cache is too large
            if (cache_.size() >= 16) flush_all();

            char path[512];
            snprintf(path, sizeof(path), "%s/region/r.%d.%d.mca",
                     world_path_.c_str(), rx, rz);

            CachedRegion new_cr;
            new_cr.rx = rx;
            new_cr.rz = rz;
            if (!new_cr.file.open(path)) {
                fprintf(stderr, "\n[ERROR] Cannot open %s\n", path);
                return;
            }
            cache_.push_back(std::move(new_cr));
            cr = &cache_.back();
        }

        cr->file.write_chunk(lcx, lcz, data, size);
        cr->dirty = true;
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mtx_);
        flush_all();
    }
};

//=============================================================================
// Worker thread — generates + serializes chunks in streaming batches
//=============================================================================
struct ChunkJob {
    int cx, cz;
};

static void worker_thread(int64_t seed, const std::vector<ChunkJob>& jobs,
                          RegionManager& rm, Progress& progress, const Config& cfg,
                          VkChunkGenerator* vk_gen = nullptr) {
    std::vector<uint8_t> blocks(CHUNK_VOLUME);
    // Uncompressed NBT: ~130KB per chunk, allocate 256KB for safety
    std::vector<uint8_t> nbt_buf(512 * 1024);

#ifdef __CUDACC__
    // Pre-allocate device memory once per thread for CUDA
    block_t* device_buf = nullptr;
    cudaError_t err = cudaMalloc(&device_buf, CHUNK_VOLUME * sizeof(block_t));
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA device alloc failed: %d\n", err);
        device_buf = nullptr;
    }
#else
    block_t* device_buf = nullptr; // unused in CPU mode
#endif

    for (const auto& job : jobs) {
        // Generate terrain (Vulkan if available, CPU fallback otherwise)
        ChunkBuffer buf;
        buf.data = blocks.data();
        if (vk_gen && vk_gen->is_available()) {
            vk_gen->generate(buf, job.cx, job.cz, seed);
        } else {
            launch_chunk_generator(buf, job.cx, job.cz, seed, device_buf);
        }

        // Serialize to NBT (uncompressed)
        size_t nbt_size = serialize_chunk(blocks.data(), job.cx, job.cz,
                                           nbt_buf.data(), nbt_buf.size());
        if (nbt_size == 0) {
            fprintf(stderr, "\n[ERROR] serialize_chunk failed (%d,%d)\n", job.cx, job.cz);
            continue;
        }

        // Write immediately to region file (thread-safe)
        rm.write_chunk(job.cx, job.cz, nbt_buf.data(), nbt_size);

        progress.written.fetch_add(1);
    }

#ifdef __CUDACC__
    if (device_buf) cudaFree(device_buf);
#endif
}

//=============================================================================
// Main
//=============================================================================
int main(int argc, char** argv) {
    Config cfg;
    cfg.world_path = ".";
    cfg.seed = 42;
    cfg.radius = 10;
    cfg.threads = 4;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--world" && i + 1 < argc) cfg.world_path = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) cfg.seed = atoll(argv[++i]);
        else if (arg == "--radius" && i + 1 < argc) cfg.radius = atoi(argv[++i]);
        else if (arg == "--center-x" && i + 1 < argc) cfg.center_x = atoi(argv[++i]);
        else if (arg == "--center-z" && i + 1 < argc) cfg.center_z = atoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) cfg.threads = atoi(argv[++i]);
        else if (arg == "--vulkan") cfg.vulkan = true;
        else if (arg == "--quiet") cfg.quiet = true;
        else if (arg == "--help") {
            printf("Usage: %s --world <path> --seed <num> --radius <n>\n", argv[0]);
            printf("  --center-x <n>  (default: 0)\n");
            printf("  --center-z <n>  (default: 0)\n");
            printf("  --threads <n>   (default: 4)\n");
            printf("  --vulkan        Use GPU acceleration via Vulkan (falls back to CPU)\n");
            printf("  --quiet\n");
            return 0;
        }
    }

    std::string region_dir = cfg.world_path + "/region";
    struct stat st;
    if (stat(region_dir.c_str(), &st) != 0) {
        fprintf(stderr, "[ERROR] %s/region/ does not exist\n", cfg.world_path.c_str());
        fprintf(stderr, "  mkdir -p %s\n", region_dir.c_str());
        return 1;
    }

    // Build spiral walk
    std::vector<ChunkJob> all_jobs;
    int64_t total = (int64_t)(2 * cfg.radius + 1) * (2 * cfg.radius + 1);
    all_jobs.reserve(total);
    int x = 0, z = 0, dx = 0, dz = -1;
    for (int64_t i = 0; i < total; i++) {
        if (-cfg.radius <= x && x <= cfg.radius && -cfg.radius <= z && z <= cfg.radius) {
            all_jobs.push_back({cfg.center_x + x, cfg.center_z + z});
        }
        if (x == z || (x < 0 && x == -z) || (x > 0 && x == 1 - z)) {
            int tmp = dx; dx = -dz; dz = tmp;
        }
        x += dx; z += dz;
    }

    Progress progress;
    progress.total.store((int64_t)all_jobs.size());

    if (!cfg.quiet) {
        printf("[McChunkGen] %lld chunks at (%d,%d) radius %d\n",
               (long long)total, cfg.center_x, cfg.center_z, cfg.radius);
        printf("[McChunkGen] World: %s | Seed: %lld | Threads: %d\n",
               cfg.world_path.c_str(), (long long)cfg.seed, cfg.threads);
    }

    // Divide work — capture job vectors by value (moved into lambda) to avoid dangling refs
    size_t jpt = (all_jobs.size() + cfg.threads - 1) / cfg.threads;
    std::vector<std::thread> threads;
    RegionManager rm(cfg.world_path);

    // Initialize Vulkan backend if requested
    VkChunkGenerator vk_gen;
    if (cfg.vulkan) {
        if (!vk_gen.init()) {
            printf("[McChunkGen] Vulkan init failed -- falling back to CPU\n");
        } else if (!cfg.quiet) {
            printf("[McChunkGen] Vulkan GPU acceleration active\n");
        }
    }

    for (int t = 0; t < cfg.threads; t++) {
        size_t start = t * jpt;
        size_t end = std::min(start + jpt, all_jobs.size());
        if (start >= end) break;
        // Move tjobs into the lambda so the thread owns its job list
        auto tjobs = std::make_shared<std::vector<ChunkJob>>(
            all_jobs.begin() + start, all_jobs.begin() + end);
        threads.emplace_back([&cfg, seed = cfg.seed, tjobs, &rm, &progress, &vk_gen]() {
            worker_thread(seed, *tjobs, rm, progress, cfg, &vk_gen);
        });
    }

    // Progress monitor
    std::atomic<bool> done{false};
    std::thread monitor([&]() {
        while (!done) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            progress.log(cfg);
        }
    });

    for (auto& t : threads) t.join();
    done = true;
    monitor.join();

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - progress.start).count();
    int64_t w = progress.written.load();
    double cps = elapsed > 0 ? w / elapsed : 0;

    if (!cfg.quiet) {
        printf("\n[McChunkGen] Done! %lld chunks in %.1fs (%.0f CPS)\n",
               (long long)w, elapsed, cps);
    }
    return 0;
}
