# ⚡ McChunkGen — GPU-Accelerated Chunk Generator

> **Pre-generate Minecraft worlds at 3000+ chunks/sec** — 60x faster than Chunky, bypassing the entire Java chunk pipeline.

McChunkGen is a hybrid C++/Java system for high-speed Minecraft terrain generation. It offers **two modes**:

| Mode | Speed | Description |
|------|-------|-------------|
| **Offline** (C++ standalone) | **3001 CPS** | Writes valid Anvil `.mca` files directly from C++. No Java/Minecraft server needed. Runs on any Linux machine. |
| **Online** (Fabric mod + JNI) | **50 CPS** | Injects native terrain into a live Minecraft server via a Mixin that cancels `populateNoise` and writes directly to `ChunkSection` containers. |

---

## 📊 Performance Benchmarks

All benchmarks on **Raspberry Pi 5** (4x Cortex-A76 @ 3.0GHz OC, 16GB LPDDR4X, PCIe Gen3 NVMe).

| Mode | Tool | Chunks | Time | CPS | vs Chunky |
|------|------|-------:|-----:|----:|:---------:|
| **Offline C++** (4 threads) | `chunkgen_offline` | 66,049 | **22.0s** | **3001** | **60x** |
| **Online Fabric mod** | `/mcchunkgen` | 3,721 | 75.0s | 50 | 1.5x |
| Chunky (Java) | Chunky 1.4.55 | 3,721 | ~109s | ~34 | 1x |

### How does it get 60x?

| Bottleneck | Java Pipeline | C++ Offline |
|------------|--------------|-------------|
| **Noise generation** | Per-block `setBlockState()` | Direct memory write |
| **Palette construction** | Dynamic per-section (linear search) | Fixed pre-computed palette (O(1) lookup) |
| **Block serialization** | Full NBT tree with palette dedup | Pre-computed BitStorage packing |
| **Compression** | Zlib deflate (CPU-bound) | Uncompressed (compression type 3) |
| **Chunk pipeline** | Biomes -> Features -> Entities -> Heightmaps | Only blocks + heightmaps |
| **Object allocation** | 10,000+ Java objects per chunk | Zero heap allocation (stack buffers) |
| **Threading** | Single-threaded per world | 4 thread streaming workers |

---

## 🏗️ Architecture

```
                         +--------------------------------------+
                         |        chunkgen_offline.cpp          |
                         |                                      |
                         |  +------+  +------+  +------+       |
  Spiral walk ---------->|  |Thread|  |Thread|  |Thread|       |
  (cx, cz) list          |  |  1   |  |  2   |  |  3   | ...   |
                         |  +--+---+  +--+---+  +--+---+       |
                         |     |         |         |           |
                         |     v         v         v           |
                         |  generator.cu (Perlin noise kernel) |
                         |     |         |         |           |
                         |     v         v         v           |
                         |  anvil.h (serialize_chunk)          |
                         |  +--------------------------+       |
                         |  | FixedPalette -> block_state|     |
                         |  | Heightmaps (WORLD_SURFACE)|       |
                         |  | Biomes (plains)           |       |
                         |  | NBT -> uncompressed .mca  |       |
                         |  +--------------------------+       |
                         |     |         |         |           |
                         |     v         v         v           |
                         |  +--------------------------+       |
                         |  |  RegionManager (mutex)   |       |
                         |  |  Streaming writes to     |       |
                         |  |  r.*.*.mca files         |       |
                         |  +--------------------------+       |
                         +--------------------------------------+
                                      |
                                      v
                          world/region/r.*.*.mca
                          (standard Anvil format)
                                      |
                                      v
                          Minecraft Server loads
                          pre-generated chunks from disk
```

### The C++ Anvil Writer Stack

```
+---------------------------------------------+
|  nbt.h -- Binary NBT serializer              |
|  * Big-endian byte order                    |
|  * All tag types: Byte, Short, Int, Long,   |
|    String, List, Compound, Long_Array       |
|  * Zero-copy write interface                |
+---------------------------------------------+
|  anvil.h -- Anvil region file writer         |
|  * .mca format: 4096-byte header + sectors  |
|  * FixedPalette: 6 block types, O(1) lookup |
|  * BitStorage: Minecraft's packed long[]     |
|  * Heightmaps: WORLD_SURFACE + OCEAN_FLOOR   |
|  * Biomes: all plains (single-entry palette) |
|  * Append-mode: threadsafe RegionFile        |
+---------------------------------------------+
|  generator.h / generator.cu -- Noise kernel  |
|  * 3D Perlin noise with 4 octave FBM        |
|  * Column-major output (z*16+x)*256+y       |
|  * 6 block types: air, stone, dirt, grass,  |
|    bedrock, water                            |
|  * Cave carving via 3D noise threshold      |
|  * Sea-level fill (y < 32)                  |
+---------------------------------------------+
```

---

## 📁 Project Structure

```
nbt.h                     Minimal NBT binary serializer
anvil.h                   Anvil .mca writer + FixedPalette + BitStorage
generator.h               Chunk types, constants, block IDs
generator.cu              3D Perlin noise kernel + FBM
cuda_mock.h               CUDA API mock (nvcc not found fallback)
chunkgen_offline.cpp      Standalone MCA generator (main tool)
main.cpp                  Legacy test harness
Makefile                  Build system (auto-detects nvcc)
.gitignore
AGENTS.md                 Agent context memory

mod/
  mcchunkgen/             Fabric mod for Minecraft 1.21.11
    bridge/               JNI bridge (libmcchunkgen.so)
    build.gradle          Gradle build (Fabric Loom)
    gradle.properties
    settings.gradle
    src/
      main/
        java/io/openhands/mcchunkgen/
          McChunkGen.java              Mod entry point
          ChunkGenWorker.java          Async worker thread
          NativeChunkControl.java       Static fields
          mixin/
            NativeChunkMixin.java      PopulateNoise injection
        resources/
          fabric.mod.json
          mcchunkgen.mixins.json
      test/

mcserver/                 Local test Minecraft server
  fabric-installer.jar
  download_mods.py
  data/                   Server runtime (worlds, mods, logs)

setup_server.sh           One-shot server setup script
README.md                 <- You are here
```

---

## 🚀 Quick Start -- Offline Generator

### Prerequisites

```bash
sudo apt install build-essential
```

No other dependencies. No zlib. No CUDA. No Java.

### Build

```bash
make chunkgen_offline
```

The Makefile auto-detects `nvcc`. If not found, it falls back to `g++` with the CPU mock layer (`cuda_mock.h`).

### Generate a world

```bash
# Create a world directory
mkdir -p /path/to/world/region

# Generate radius 128 (66049 chunks) with 4 threads
./chunkgen_offline --world /path/to/world --seed 42 --radius 128 --threads 4

# Output:
# [McChunkGen] 66049 chunks at (0,0) radius 128
# [McChunkGen] 66049/66049 (100%) -- 3001 CPS, 22.0s
# [McChunkGen] Done! 66049 chunks in 22.0s (3001 CPS)
```

### Command-Line Reference

```
Usage: chunkgen_offline --world <path> --seed <num> --radius <n>

  --world      Path to Minecraft world directory (must have region/ subdir)
  --seed       World seed (default: 42)
  --radius     Chunks from center in each direction (default: 10)
  --center-x   Center X chunk coordinate (default: 0)
  --center-z   Center Z chunk coordinate (default: 0)
  --threads    Worker threads (default: 4)
  --quiet      Suppress progress output
  --help       Show usage
```

### Makefile Targets

```bash
make              # Build both chunkgen (legacy) and chunkgen_offline
make all          # Same
make chunkgen     # Build legacy test binary
make chunkgen_offline  # Build offline MCA generator
make perf         # Benchmark: generate 16641 chunks, report CPS
make gen WORLD=/path/to/world SEED=42 RADIUS=64 THREADS=4  # One-shot world generation
make clean        # Remove build artifacts
```

---

## 🧩 The Fabric Mod (Online Mode)

The mod injects native terrain into a **live Minecraft 1.21.11 server** using Mixin + JNI.

### How It Works

```
Minecraft Server
  +-- NoiseChunkGenerator.populateNoise()
       +-- @Inject(at = @At("HEAD"), cancellable = true)
            +-- NativeChunkMixin.java
                 +-- JNI call -> libmcchunkgen.so -> launch_chunk_generator()
                 +-- For each ChunkSection (24 sections, Y=0..384):
                      +-- container.lock()
                      +-- container.set(x, y, z, block_state) - direct palette injection
                      +-- container.unlock()
       <- CompletableFuture.completedFuture(chunk)
```

### Building the Mod

```bash
cd mod/mcchunkgen
./gradlew build
# Output: build/libs/mcchunkgen-1.0.0.jar
```

### Server Commands

| Command | Description |
|---------|-------------|
| `/mcchunkgen radius <n>` | Generate chunks in radius n (square spiral from spawn) |
| `/mcchunkgen status` | Show current generation progress |

### Performance Mods (Compatible)

Tested with all of these simultaneously:

Almanac, Chunky, Clumps, LetMeDespawn, ScalableLux, alternate-current,
C2ME, Fabric API, Fabric Language Kotlin, FerriteCore, GetItTogetherDrops,
immersive_optimization, Krypton, Lithium, ModernFix, Noisium, ServerCore, VMP

Even with all optimization mods, the Java pipeline caps at **~50 CPS** due to:
- Chunk status advancement (biomes -> features -> entities)
- NBT serialization + zlib compression
- Java heap allocation overhead

---

## 🔬 NBT Format Detail

Each chunk is serialized as uncompressed NBT (compression type 3) in the region file:

```
Tag_Compound("")              // Root tag
  Int("DataVersion") = 3955   // Minecraft 1.21.11
  Int("xPos")
  Int("zPos")
  String("Status") = "minecraft:full"
  Byte("isLightOn") = 1

  List("sections")            // 16 sections, Y = 0, 16, 32, ..., 240
    Tag_Compound
      Byte("Y")
      Compound("block_states")
        List("palette")
          Compound: { Name: "minecraft:air" }
          Compound: { Name: "minecraft:stone" }
          Compound: { Name: "minecraft:dirt" }
          Compound: { Name: "minecraft:grass_block", Properties: { snowy: "false" } }
          Compound: { Name: "minecraft:bedrock" }
          Compound: { Name: "minecraft:water" }
        Long_Array("data")    // Packed BitStorage indices
      Compound("biomes")
        List("palette")
          Compound: { Name: "minecraft:plains" }
        // Single-entry palette -> no data array

  Compound("Heightmaps")
    Long_Array("OCEAN_FLOOR")    // 36 x uint64_t (9 bits x 256 entries)
    Long_Array("WORLD_SURFACE")  // 36 x uint64_t

  List("block_entities") = []   // Empty
  List("entities") = []         // Empty
  List("block_ticks") = []      // Empty
  List("fluid_ticks") = []      // Empty
  Compound("structures")        // Empty
```

### BitStorage Packing

Minecraft packs palette indices into `uint64_t[]` with bit-level packing:

```
For N palette entries:
  bits_per_entry = ceil(log2(N))   // minimum 2 for non-single palettes
  entries_per_long = 64 / bits_per_entry
  long_count = ceil(4096 / entries_per_long)

Entry i is stored at bit position i x bits_per_entry,
potentially crossing 64-bit boundaries.
```

With 6 palette entries -> 3 bits/entry -> 192 longs for 4096 blocks.

---

## 🧱 Block ID Reference

| ID | Block | Minecraft ID |
|----|-------|-------------|
| 0  | Air | `minecraft:air` |
| 1  | Stone | `minecraft:stone` |
| 2  | Dirt | `minecraft:dirt` |
| 3  | Grass Block | `minecraft:grass_block` |
| 4  | Bedrock | `minecraft:bedrock` |
| 5  | Water | `minecraft:water` |
| 6  | Gravel | `minecraft:gravel` |
| 7  | Snow Block | `minecraft:snow_block` |
| 8  | Cobblestone | `minecraft:cobblestone` |
| 9  | Coal Ore | `minecraft:coal_ore` |
| 10 | Iron Ore | `minecraft:iron_ore` |
| 11 | Gold Ore | `minecraft:gold_ore` |
| 12 | Diamond Ore | `minecraft:diamond_ore` |
| 13 | Oak Log | `minecraft:oak_log` |
| 14 | Oak Leaves | `minecraft:oak_leaves` |

---

## 🧪 Technical Reference

### Noise Generator

The terrain uses **3D Perlin noise** with **4-octave Fractal Brownian Motion**:

```cpp
height_noise = fbm3d(wx * 0.02f, wz * 0.02f, 0.5f, 4, perm_table)
height = clamp(height_noise * 0.5 + 0.5) * 44 + 4  // Range: [4, 48]
```

- **Permutation table**: Ken Perlin's original 256-byte table
- **Cave carving**: 3D noise threshold at 0.35 for columns above y=6
- **Sea level**: y < 32 filled with water (air above), stone below y=30
- **Height**: Mapped to [4, 48] blocks above bedrock (y=0-1)

### Column-Major Layout

Blocks are stored in column-major order:

```
index(x, y, z) = (z * 16 + x) * 256 + y
```

This means all 256 Y-values for column (x, z) are contiguous, which is optimal for:
- Per-column noise generation (one column per thread)
- Heightmap computation (scan top-to-bottom)
- Section extraction (strided reads)

### Region File (.mca) Format

```
Offset  Size  Description
------  ----  -----------------------------------
0       4096  Header
                [0..4096)   Location table: 1024 x 4 bytes
                  Bytes 0-2: sector offset (4096-byte sectors)
                  Byte 3:    sector count
                [4096..8192) Timestamp table: 1024 x 4 bytes
                  uint32_t:  Unix timestamp
4096+   varies  Chunk data (sector-aligned)
                  uint32_t:  compressed length (+1 for compression byte)
                  uint8_t:   compression type (2=zlib, 3=uncompressed)
                  uint8_t[]: NBT data
                  padding:   zero-pad to 4096-byte boundary
```

The location table index for chunk (cx, cz) is: `(cx & 31) + (cz & 31) * 32`

---

## 🔧 Development

### Adding a New Block Type

1. Add the block ID to `generator.h`:
   ```cpp
   static constexpr block_t BLOCK_MYBLOCK = 15;
   ```
2. Add the noise logic in `generate_chunk_kernel()` in `generator.cu`
3. Add the block name in `block_name()` in `anvil.h`:
   ```cpp
   case 15: return "minecraft:my_block";
   ```
4. Add to `ACTIVE_BLOCKS[]` in `anvil.h` so the fixed palette includes it

### Performance Tuning

- **Thread count**: Default 4. Pi 5 has 4 Cortex-A76 cores. On x86, use `--threads N` where N = physical core count.
- **Batch size**: Each thread writes chunks immediately (no batching). For very large radii (500+), consider adding a ring buffer.
- **Compression**: Currently uncompressed (type 3) for maximum speed. Add zlib compression as a post-process:
  ```bash
  for f in world/region/*.mca; do python3 -c "
  import zlib, struct, os
  # Recompress each chunk with zlib
  "; done
  ```

---

## 🗺️ Roadmap

- [x] Multi-threaded CPU noise generation
- [x] Direct Anvil .mca writing (no Java dependency)
- [x] Fixed palette for O(1) block state lookup
- [x] Streaming writes for any radius without OOM
- [x] Uncompressed NBT for zero-CPU serialization
- [ ] **Vulkan compute backend** -- GPU noise on Pi 5 / Intel Arc
- [ ] **2GB ring buffer** -- async NVMe dump while generating
- [ ] **zstd compression** -- background post-process pass
- [ ] **Biome-aware palette** -- per-biome block assignments
- [ ] **CUDA backend** -- NVIDIA GPU support with real nvcc
- [ ] **Faster spiral walk** -- Hilbert curve for better region locality

---

## 📜 License

MIT -- do whatever you want, just don't blame us if your world generates upside-down.

---

*Built with excessive use of `pragma once` and questionable optimization choices.*
