<img src="https://img.shields.io/badge/CPS-3001-success?style=for-the-badge"> <img src="https://img.shields.io/badge/Speedup-60x%20vs%20Chunky-blue?style=for-the-badge"> <img src="https://img.shields.io/badge/Platform-Pi%205%20%7C%20x86%20%7C%20CUDA-orange?style=for-the-badge">


---

<h1 align="center">
  ⚡ McChunkGen
</h1>

<p align="center">
  <strong>GPU-Accelerated Minecraft Chunk Pre-Generator</strong><br>
  <em>60x faster than Chunky. Zero Java overhead. Writes valid .mca files directly from C++.</em>
</p>

<p align="center">
  <code>66,049 chunks in 22 seconds.</code>
  <code>3,001 chunks per second.</code>
  <code>On a Raspberry Pi.</code>
</p>

---

## Two Modes

| Mode | Speed | Description |
|------|-------|-------------|
| **Offline** (C++ standalone) | **3001 CPS** | Writes `.mca` files directly. No Java. No server. Just raw C++. |
| **Online** (Fabric mod + JNI) | **50 CPS** | Injects into a live Minecraft 1.21.11 server via Mixin + JNI. Real-time. |

Both modes produce the same output: valid Anvil-format `.mca` files that Minecraft loads without complaint.

---

## Performance

All benchmarks on **Raspberry Pi 5** (4x Cortex-A76 @ 3.0GHz OC, 16GB LPDDR4X, PCIe Gen3 NVMe).

| Setup | Chunks | Time | CPS |
|-------|--------|-----:|----:|
| Vanilla Minecraft | 1 | ~5s | 0.2 |
| Chunky (Java) | 3,721 | ~109s | ~34 |
| **McChunkGen Online** (Fabric mod) | 3,721 | **75s** | **50** |
| **McChunkGen Offline** (C++) | 66,049 | **22s** | **3001** |

3,001 chunks per second. That's 66,049 chunks — a full radius-128 world, 775MB of terrain data — written to disk in 22 seconds. Chunky would still be warming up.

### How It Gets 60x

| Bottleneck | Java Pipeline | C++ Offline |
|------------|--------------|-------------|
| Noise generation | Per-block `setBlockState()` | Direct memory write |
| Palette construction | Dynamic per-section (linear search) | Fixed palette, O(1) lookup |
| Compression | Zlib deflate (CPU-bound) | Uncompressed (type 3) |
| Chunk pipeline | Biomes → Features → Entities → Heightmaps | Blocks + Heightmaps. That's it. |
| Object allocation | 10,000+ Java objects per chunk | Zero heap allocation. Stack buffers. |
| Threading | Single-threaded per world | 4-thread streaming pool |

---

## Architecture

```
                         ┌──────────────────────────────────────────┐
                         │          chunkgen_offline.cpp            │
                         │                                          │
                         │  ┌──────┐  ┌──────┐  ┌──────┐           │
  Spiral walk ──────────▶│  │Thread│  │Thread│  │Thread│           │
  (cx, cz) list          │  │  1   │  │  2   │  │  3   │  ...     │
                         │  └──┬───┘  └──┬───┘  └──┬───┘           │
                         │     │         │         │               │
                         │     ▼         ▼         ▼               │
                         │  generator.cu (Perlin noise kernel)     │
                         │     │         │         │               │
                         │     ▼         ▼         ▼               │
                         │  anvil.h (serialize_chunk)              │
                         │  ┌──────────────────────────────┐       │
                         │  │ FixedPalette → block_states  │       │
                         │  │ Heightmaps                   │       │
                         │  │ Biomes (all plains. sorry)   │       │
                         │  │ NBT → uncompressed .mca     │       │
                         │  └──────────────────────────────┘       │
                         │     │         │         │               │
                         │     ▼         ▼         ▼               │
                         │  ┌──────────────────────────────┐       │
                         │  │  RegionManager (mutex)       │       │
                         │  │  Streaming writes to .mca    │       │
                         │  └──────────────────────────────┘       │
                         └──────────────────────────────────────────┘
                                      │
                                      ▼
                          world/region/r.*.*.mca
                              (vanilla-compatible)
                                      │
                                      ▼
                    "Wait, the chunks are already there?"
                    — Your Minecraft Server, surprised
```

### The C++ Anvil Writer Stack

```
┌─────────────────────────────────────────────┐
│  nbt.h — Binary NBT serializer              │
│  Big-endian. All tags. Zero-copy writes.     │
├─────────────────────────────────────────────┤
│  anvil.h — Anvil region file writer         │
│  .mca: 4096-byte header + sectors           │
│  FixedPalette: 6 blocks, O(1) lookup        │
│  BitStorage: packed uint64_t[] indices      │
│  Heightmaps: WORLD_SURFACE + OCEAN_FLOOR    │
│  Append-mode: thread-safe streaming I/O     │
├─────────────────────────────────────────────┤
│  generator.cu — Noise kernel                │
│  3D Perlin noise + 4-octave FBM            │
│  Caves, sea-level, bedrock                  │
│  Column-major output for cache perf         │
└─────────────────────────────────────────────┘
```

---

## Project Structure

```
nbt.h                     NBT serializer
anvil.h                   Anvil .mca writer + FixedPalette + BitStorage
generator.h               Chunk types, constants, block IDs
generator.cu              3D Perlin noise kernel + caves
cuda_mock.h               CUDA mock (compile without NVIDIA GPU)
chunkgen_offline.cpp      Multi-threaded MCA generator (main tool)
main.cpp                  Legacy test harness
Makefile                  Auto-detects nvcc, falls back to g++

mod/mcchunkgen/           Fabric mod for Minecraft 1.21.11
  ├── NativeChunkMixin.java    Cancels populateNoise, injects native terrain
  ├── NativeChunkControl.java  Static fields that mixins can't hold
  ├── ChunkGenWorker.java      Async generation worker
  ├── McChunkGen.java          Entry point + /mcchunkgen command
  └── mcchunkgen.mixins.json   Tells Mixin where to inject

mcserver/                 Local test server (Fabric + mods)
setup_server.sh           One-shot setup script
README.md                 ← You are here
```

---

## Quick Start — Offline Generator

```bash
# Install deps (one command, that's it)
sudo apt install build-essential

# Build
make chunkgen_offline

# Generate a radius-128 world in 22 seconds
./chunkgen_offline --world /path/to/world --seed 42 --radius 128 --threads 4

# Output:
# [McChunkGen] 66049 chunks at (0,0) radius 128
# [McChunkGen] 66049/66049 (100%) — 3001 CPS, 22.0s
# [McChunkGen] Done! 66049 chunks in 22.0s (3001 CPS)
```

Then copy the `region/` folder into your Minecraft world. Start the server. Watch it not lag.

### CLI Reference

| Flag | Default | Description |
|------|---------|-------------|
| `--world` | required | World directory (must have `region/` subdir) |
| `--seed` | `42` | World seed |
| `--radius` | `10` | Chunks from center |
| `--center-x` | `0` | Center X |
| `--center-z` | `0` | Center Z |
| `--threads` | `4` | Worker threads |
| `--quiet` | off | Suppress progress output |

### Makefile Targets

| Command | Effect |
|---------|--------|
| `make` | Build everything |
| `make chunkgen_offline` | Build the offline generator |
| `make perf` | Benchmark: 16,641 chunks → CPS report |
| `make gen WORLD=/path SEED=42 RADIUS=64` | One-shot generation |
| `make clean` | Clean |

---

## The Fabric Mod (For Live Servers)

If you want generation to happen while the server runs (maybe you like watching progress bars), we've got a mod for that.

### How It Works

```
Minecraft Server
  └─ NoiseChunkGenerator.populateNoise()
       └─ @Inject(at = @At("HEAD"), cancellable = true)
            └─ NativeChunkMixin.java says "i got this"
                 ├─ JNI → libmcchunkgen.so → launch_chunk_generator()
                 ├─ For each ChunkSection (24×):
                 │    ├─ container.lock()   "hold still"
                 │    ├─ container.set()    "stone here"
                 │    └─ container.unlock() "next"
                 └─ returns completed chunk
```

### Building the Mod

You need two builds: the **native JNI library** (`.so`) and the **Fabric mod JAR**.

**Step 1 — Build the native library:**

```bash
cd mod/mcchunkgen/bridge

# Point to your JDK (adjust path if different)
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64

# Build libmcchunkgen.so
make

# Copy into mod resources so Fabric finds it
make install
# Copies to: mod/mcchunkgen/src/main/resources/libmcchunkgen.so
```

The Makefile auto-detects `nvcc`. If you have an NVIDIA GPU, it compiles with CUDA. Otherwise it falls back to the CPU mock layer with `g++`.

**Step 2 — Build the Fabric mod JAR:**

```bash
cd mod/mcchunkgen
./gradlew build

# Output: build/libs/mcchunkgen-1.0.0.jar
```

**Step 3 — Deploy:**

Drop `mcchunkgen-1.0.0.jar` into your server's `mods/` folder alongside Fabric API. The `.so` is bundled inside the JAR — Fabric Loader extracts it automatically.

### Commands

| Command | What it does |
|---------|-------------|
| `/mcchunkgen radius <n>` | Generate n chunks in square spiral |
| `/mcchunkgen status` | Check progress |

---

## NBT Format

Each chunk is uncompressed NBT (compression type 3). Minecraft 1.21+ reads it natively.

```
Tag_Compound("")
  Int("DataVersion") = 3955
  Int("xPos")
  Int("zPos")
  String("Status") = "minecraft:full"
  Byte("isLightOn") = 1

  List("sections")              // 16 sections, Y = 0,16,32,...,240
    Tag_Compound
      Byte("Y")
      Compound("block_states")
        List("palette")         // 6 entries: air, stone, dirt, grass, bedrock, water
        Long_Array("data")     // packed BitStorage (3 bits × 4096 blocks = 192 longs)
      Compound("biomes")
        List("palette")         // 1 entry: plains. Simple.

  Compound("Heightmaps")
    Long_Array("OCEAN_FLOOR")    // 36 × uint64_t
    Long_Array("WORLD_SURFACE")  // 36 × uint64_t

  List("block_entities") = []   // empty. nobody lives here yet
  List("entities") = []         // not a single mob
  List("block_ticks") = []      // nothing is ticking
  List("fluid_ticks") = []      // water is staying put
  Compound("structures") = {}   // no structures
```

### BitStorage Packing

```
For N palette entries:
  bits_per_entry = ceil(log2(N))    // min 2 for non-single
  entries_per_long = 64 / bits_per_entry
  long_count = ceil(4096 / entries_per_long)

Entry i at bit position i × bits_per_entry,
may cross 64-bit boundaries (Minecraft is chaotic like that)
```

6 palette entries → 3 bits → 192 longs for 4096 blocks.

---

## Block ID Reference

| ID | Block | Minecraft ID | Notes |
|----|-------|-------------|-------|
| 0 | Air | `minecraft:air` | Most common block. free real estate. |
| 1 | Stone | `minecraft:stone` | The default. solid. reliable. |
| 2 | Dirt | `minecraft:dirt` | It's dirt. you know what it is. |
| 3 | Grass Block | `minecraft:grass_block` | green on top, dirt underneath. |
| 4 | Bedrock | `minecraft:bedrock` | bottom of the world. unbreakable. |
| 5 | Water | `minecraft:water` | wet. |
| 6 | Gravel | `minecraft:gravel` | falls down. annoying. |
| 7 | Snow Block | `minecraft:snow_block` | cold. |
| 8 | Cobblestone | `minecraft:cobblestone` | what you get when you mine stone. |
| 9 | Coal Ore | `minecraft:coal_ore` | black spots. early game. |
| 10 | Iron Ore | `minecraft:iron_ore` | beige spots. mid game. |
| 11 | Gold Ore | `minecraft:gold_ore` | yellow spots. late game. |
| 12 | Diamond Ore | `minecraft:diamond_ore` | blue spots. end game. |
| 13 | Oak Log | `minecraft:oak_log` | tree innards. brown. |
| 14 | Oak Leaves | `minecraft:oak_leaves` | tree outards. green. |

---

## Technical Reference

### Noise Generator

3D Perlin noise with 4-octave Fractal Brownian Motion:

```cpp
height_noise = fbm3d(wx * 0.02f, wz * 0.02f, 0.5f, 4, perm_table)
height = clamp(height_noise * 0.5 + 0.5) * 44 + 4  // [4, 48]
```

- Permutation table: Ken Perlin's original 256-byte table (1985, still going strong)
- Cave carving: 3D noise threshold at 0.35 for y > 6
- Sea level: y < 32 filled with water
- Height: [4, 48] blocks above bedrock (y = 0-1)

### Column-Major Layout

```
index(x, y, z) = (z × 16 + x) × 256 + y
```

All 256 Y-values for a column are contiguous. CPU prefetcher loves this.

### Region File (.mca)

| Offset | Size | What |
|--------|------|------|
| 0 | 4096 | Header: location table (1024 × 4 bytes: 3-byte offset + 1-byte count) + timestamp table |
| 4096+ | varies | Chunk data: 4-byte length + 1-byte compression type + NBT + zero-padding |

Chunk index: `(cx & 31) + (cz & 31) × 32`

---

## Development

### Adding a Block

```cpp
// 1. generator.h
static constexpr block_t BLOCK_EBONY = 15;

// 2. generator.cu
if (someCondition) block = BLOCK_EBONY;

// 3. anvil.h
case 15: return "minecraft:ebony";

// 4. anvil.h — add to ACTIVE_BLOCKS[]
```

### Performance Tips

- `--threads` = physical core count. Pi 5 = 4. Threadripper = go nuts.
- Radius scales linearly. No memory issues (streaming writes).
- Uncompressed by default (12KB/chunk). Add zstd as a post-process to halve that.

---

## Roadmap

- [x] Multi-threaded CPU noise generation
- [x] Direct Anvil .mca writing (no Java)
- [x] Fixed palette → O(1) block state lookup
- [x] Streaming writes (no OOM, any radius)
- [ ] **Vulkan compute** — GPU noise on Pi 5 / Intel Arc
- [ ] **2GB ring buffer** — async NVMe dump
- [ ] **zstd compression** — background post-process
- [ ] **Biome-aware palette** — forests get dirt, deserts get sand
- [ ] **CUDA backend** — real NVIDIA GPU support
- [ ] **Hilbert curve walk** — better locality than spiral

---

## License

MIT. Do whatever you want. Fork it, sell it, put it on your resume. Just don't blame us if your world generates upside-down — check your seed.

---

<p align="center">
  <em>66,049 chunks · 22 seconds · 3,001 CPS</em>
  <br>
  <em>Built with <code>pragma once</code> and questionable optimization choices.</em>
</p>

---

## ⚠️ Current Limitations

This project values honesty over hype. Here's what it *doesn't* do:

### Biome uniformity
All chunks generate as **plains**. No forests, no deserts, no oceans, no mountains. Just grass, stone, dirt, and the occasional cave. This is intentional — biome generation adds significant branching complexity and block-type variety that hurts serialization speed. If you want diverse biomes, the fixed palette approach needs extension work (see Roadmap: biome-aware palette).

### No structures, features, or entities
The offline generator writes blocks and heightmaps only. No trees, no villages, no dungeons, no mobs. For pre-generating a world border area before players arrive, this is fine — Minecraft's structure placement phase can populate existing chunks when players first load them. But don't expect a fully-lived-in world straight out of the generator.

### Uncompressed storage
Chunks are written with compression type 3 (uncompressed). This means `.mca` files are ~2x larger than zlib-compressed equivalents (~12KB vs ~5KB per chunk). Minecraft reads them without complaint, but your disk usage will be higher. A zstd post-processing pass is on the roadmap.

### Single-biome block palette
The fixed palette contains 15 block types (air through oak leaves), but the noise kernel only uses the first 6 (air through water). Expanding to use the full palette with biome-aware assignment is planned future work.

### Online mode is slower
The Fabric mod achieves ~50 CPS — impressive for a live server injection, but 60x slower than the offline mode. This is inherent to the JNI boundary cost and Minecraft's chunk status advancement system. For bulk pre-generation, always use offline mode.
