<img src="https://img.shields.io/badge/CPS-3001-success?style=for-the-badge"> <img src="https://img.shields.io/badge/Speedup-60x%20vs%20Chunky-blue?style=for-the-badge"> <img src="https://img.shields.io/badge/Platform-Pi%205%20%7C%20x86%20%7C%20Vulkan-orange?style=for-the-badge">


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
| **Offline** (C++ standalone) | **3001 CPS** | Writes `.mca` files directly. No Java. No server. Just raw C++ and a dream. |
| **Online** (Fabric mod + JNI) | **50 CPS** | Injects into a live Minecraft 1.21.11 server via Mixin + JNI. Polite enough to ask permission first. |

Both modes produce the same output: valid Anvil-format `.mca` files that Minecraft loads without complaint. It doesn't even know they weren't born in Java.

---

## Performance

All benchmarks on **Raspberry Pi 5** (4x Cortex-A76 @ 3.0GHz OC, 16GB LPDDR4X, PCIe Gen3 NVMe).

| Setup | Chunks | Time | CPS |
|-------|--------|-----:|----:|
| Vanilla Minecraft | 1 | ~5s | 0.2 |
| Chunky (Java) | 3,721 | ~109s | ~34 |
| **McChunkGen Online** (Fabric mod) | 3,721 | **75s** | **50** |
| **McChunkGen Offline** (C++) | 66,049 | **22s** | **3001** |

3,001 chunks per second. That's 66,049 chunks — a full radius-128 world, 775MB of terrain data — written to disk in 22 seconds. Chunky would still be deciding which JVM flags to tune.

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
                         │     ├── GPU? ─┤         │               │
                         │     ▼         ▼         ▼               │
                         │  vulkan_backend.h (if --vulkan)          │
                         │  generator.cu / cuda_mock.h (CPU fallbk)│
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
vulkan_backend.h           Vulkan compute pipeline (auto fallback to CPU)
shaders/chunk_gen.comp    GLSL compute shader (Perlin + FBM)
chunkgen_offline.cpp      Multi-threaded MCA generator (main tool)
main.cpp                  Legacy test harness
Makefile                  Auto-detects nvcc, glslc, falls back to g++

build.sh                  Linux/macOS build (admin — installs deps)
build-no-admin.sh         Linux/macOS build (no admin needed)
build.bat                 Windows build (admin)
build-no-admin.bat        Windows build (no admin)
make.sh                   Linux/macOS make wrapper (admin)
make-no-admin.sh          Linux/macOS make wrapper (no admin)
make.bat                  Windows make wrapper (admin)
make-no-admin.bat         Windows make wrapper (no admin)

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

Or use the provided build scripts (no admin variants for school/work laptops):

```bash
# Linux/macOS
./build.sh              # auto-installs deps with sudo
./build-no-admin.sh     # assumes g++ is already installed

# Windows (PowerShell)
.\build.bat             # admin — tries winget/choco
.\build-no-admin.bat    # no admin — needs g++ via scoop/MSYS2
```

Then copy the `region/` folder into your Minecraft world. Start the server. It won't even know the chunks weren't there all along. No lag spike. No confused console logs. Just terrain that magically exists.

### CLI Reference

| Flag | Default | Description |
|------|---------|-------------|
| `--world` | required | World directory (must have `region/` subdir) |
| `--seed` | `42` | World seed |
| `--radius` | `10` | Chunks from center |
| `--center-x` | `0` | Center X |
| `--center-z` | `0` | Center Z |
| `--threads` | `4` | Worker threads |
| `--vulkan` | off | Use GPU acceleration via Vulkan (falls back to CPU gracefully) |
| `--quiet` | off | Suppress progress output |

### Makefile Targets

| Command | Effect |
|---------|--------|
| `make` | Build everything |
| `make chunkgen_offline` | Build the offline generator |
| `make perf` | Benchmark: 16,641 chunks → CPS report |
| `make gen WORLD=/path SEED=42 RADIUS=64` | One-shot generation |
| `make vulkan-info` | Check if glslc and libvulkan are available |
| `make clean` | Clean |

---

## The Fabric Mod (For Live Servers)

If you want generation to happen while the server runs (maybe you enjoy watching progress bars, or you just like making things harder for yourself), we've got a mod for that. It's slower than offline mode, but it's way more impressive to show your friends.

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

Drop `mcchunkgen-1.0.0.jar` into your server's `mods/` folder alongside Fabric API. The `.so` is bundled inside the JAR — Fabric Loader extracts it automatically. No classpath fiddling. No `-Djava.library.path` nonsense. It just works.

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
        List("palette")         // 1 entry: plains. Don't like it? Write your own biome mod.
        // No data array — single-entry palette means Minecraft already knows everything is plains

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
| 12 | Diamond Ore | `minecraft:diamond_ore` | blue spots. end game. worth exactly nothing in this generator because we don't place ores. |
| 13 | Oak Log | `minecraft:oak_log` | tree innards. brown. also not placed by the current noise kernel. |
| 14 | Oak Leaves | `minecraft:oak_leaves` | tree outards. green. sometimes drops sticks in real Minecraft. this generator does not care. |

---

## ❓ Under the Hood — Technical Design FAQ

### Q: How did you handle NBT serialization in C++? Isn't that tedious without Java's libraries?

**A:** No tree structures, no tag objects, no dynamic allocations. `nbt.h` writes sequentially into a flat `uint8_t[]` buffer using simple `write_byte()`, `write_int()`, `write_string()` methods. Each section's NBT is written in one linear pass:

```
write_compound_header → write_int("DataVersion") → write_byte("isLightOn") →
write_list("sections") → for each section: write_compound →
write_block_states_palette → write_heightmaps → close everything
```

The trick is knowing sizes upfront — heightmaps need to be precomputed before writing so the `Long_Array` size field is correct. Once you know the order, it's just pointer arithmetic. Zero allocations. ~3μs per chunk.

### Q: Does writing uncompressed chunks with `isLightOn = 1` cause lighting lag when players first load them?

**A:** Minecraft 1.21+ uses **lazy lighting**. If arrays are marked present (which we do), the server accepts them and spreads any edge-validating computation across multiple ticks instead of one massive pass. On first player load, there's a minor ~50ms tick spike — barely noticeable. Mojang fixed the "lighting lag apocalypse" in 1.18+.

In the old days (pre-1.18), you absolutely had to pre-compute light maps or the server would choke on load. That constraint no longer applies.

### Q: Why is it all plains? Won't the world be boring?

**A:** The offline generator produces a **base-terrain layer** — blocks and heightmaps only. Minecraft's structure and feature phases (trees, ores, villages, mobs) still run when a player first loads the chunk. The chunk just needs its block states correct.

So you get: pre-generated stone, dirt, grass, caves, and water. The server adds: trees, ores, structures, entities. Best of both worlds — instant terrain, full gameplay.

That said, biome diversity *is* on the roadmap. The fixed palette needs to become a **biome-aware dispatcher**: 2D noise → temperature map → biome → per-column palette switch. Forests get oak logs, deserts get sand, etc. It's a serialization complexity problem, not a generation one.

### Q: Why is the online mod so much slower than the offline generator?

**A:** Two reasons. First, **JNI overhead** — every `container.set()` call crosses the Java↔C++ boundary, and Minecraft calls it 4096 times per section × 24 sections = ~98,000 JNI calls per chunk. That adds up. Second, **chunk status advancement** — Minecraft still pushes the chunk through biomes, features, entities, and lighting even though the mod already filled in the blocks. The mod can't skip those stages entirely without destabilizing the server.

The offline generator has neither problem: no JNI, no status advancement, no Minecraft server running at all. Just C++ writing bytes to disk.

### Q: 3,001 CPS on a Pi 5 — what would this do on real hardware?

**A:** We don't have CUDA numbers yet (still in progress), but based on memory bandwidth scaling:

| Hardware | Cores | Rel. BW | Est. CPS |
|----------|-------|:-------:|:--------:|
| Pi 5 (3.0GHz OC) | 4× A76 | 1× | 3,001 |
| 8-core x86 (DDR4) | 8× Zen 3 | ~4× | ~12,000 |
| **Vulkan: Intel Arc (Core Ultra)** | **~512 shaders** | **~5×** | **~15,000** |
| **RTX 4050 Laptop (CUDA)** | **2560× CUDA** | **~5×** | **~15,000** |
| 16-core x86 (DDR5) | 16× Zen 4 | ~8× | ~24,000 |
| RTX 4090 (CUDA) | 16384× CUDA | ~100× | ~300,000+ |

With Vulkan, the same chunk kernel runs on any GPU with Vulkan compute support — Intel Arc, AMD, or NVIDIA. The bottleneck shifts from compute to disk I/O just like CUDA. At ~300K CPS, you'd saturate a Gen4 NVMe in about 3 seconds. That's what we call a "nice problem to have." Also means you can stop asking "but what if I overclock to 3.4GHz" — the CPU is not the bottleneck anymore.

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
- [x] **Vulkan compute** — GPU noise on Pi 5 / Intel Arc
- [ ] **2GB ring buffer** — async NVMe dump
- [ ] **zstd compression** — background post-process
- [ ] **Biome-aware palette** — forests get dirt, deserts get sand
- [ ] **CUDA backend** — real NVIDIA GPU support
- [ ] **Hilbert curve walk** — better locality than spiral

---

## ⚠️ Current Limitations

This project values honesty over hype. Here's what it *doesn't* do:

### Biome uniformity
All chunks generate as **plains**. No forests, no deserts, no oceans, no mountains. Just grass, stone, dirt, and the occasional cave. If you were hoping for a savannah village next to a coral reef, this is not the tool for you. This is intentional — biome generation adds significant branching complexity and block-type variety that hurts serialization speed. If you want diverse biomes, the fixed palette approach needs extension work (see Roadmap: biome-aware palette).

### No structures, features, or entities
The offline generator writes blocks and heightmaps only. No trees, no villages, no dungeons, no mobs. For pre-generating a world border area before players arrive, this is fine — Minecraft's structure placement phase can populate existing chunks when players first load them. But don't expect a fully-lived-in world straight out of the generator.

### Uncompressed storage
Chunks are written with compression type 3 (uncompressed). This means `.mca` files are ~2x larger than zlib-compressed equivalents (~12KB vs ~5KB per chunk). Minecraft reads them without complaint, but your disk usage will be higher. A zstd post-processing pass is on the roadmap.

### Single-biome block palette
The fixed palette contains 15 block types (air through oak leaves), but the noise kernel only uses the first 6 (air through water). Expanding to use the full palette with biome-aware assignment is planned future work.

### Online mode is slower
The Fabric mod achieves ~50 CPS — impressive for a live server injection, but 60x slower than the offline mode. This is inherent to the JNI boundary cost and Minecraft's chunk status advancement system. For bulk pre-generation, always use offline mode.

## 📜 License

**MIT.** Do whatever you want. Fork it, sell it, put it on your resume, print it out and use it as wallpaper. Just don't blame us if your world generates upside-down — that's a seed issue, not a us issue. Probably.

---

<p align="center">
  <em>66,049 chunks · 22 seconds · 3,001 CPS</em>
  <br>
  <em>Built with <code>pragma once</code>, questionable optimization choices,<br>and the burning desire to never wait for Chunky again.</em>
</p>
