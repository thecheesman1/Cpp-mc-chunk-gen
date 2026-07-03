# McChunkGen — GPU-Accelerated Chunk Generator

A massively parallel 3D voxel terrain generator in C++/CUDA, designed to
pre-generate Minecraft chunks at speeds 1000x+ faster than vanilla.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Pipeline (Ping-Pong)                      │
│                                                              │
│   CUDA Stream A → generate into Buffer A                     │
│                    while Buffer B → cudaMemcpyAsync → Host   │
│   CUDA Stream B → generate into Buffer B                     │
│                    while Buffer A → cudaMemcpyAsync → Host   │
│                                          ↓                    │
│                              Raw binary append → NVMe disk   │
└──────────────────────────────────────────────────────────────┘
```

## Performance

| Mode           | Hardware      | CPS         | vs Chunky |
|----------------|---------------|-------------|-----------|
| Chunky (Java)  | Any CPU       | ~44 CPS     | 1×        |
| CPU mock       | Pi 5 / x86-64 | 675-1332 CPS| 15-30×    |
| GTX 1660 Ti*   | Turing (CUDA) | ~400K CPS   | ~9000×    |
| RTX 4090*      | Ada (CUDA)    | ~3M CPS     | ~68000×   |

*Estimated — actual results depend on memory bandwidth and clock speeds.

## Project Structure

```
├── cuda_mock.h          CUDA mock layer (CPU fallback for testing)
├── generator.h          Chunk types, constants, block IDs
├── generator.cu         3D Perlin noise kernel + chunk generation
├── main.cpp             Async double-buffered pipeline + binary writer
├── Makefile             Build system (auto-detects nvcc)
├── mod/
│   └── mcchunkgen/      Fabric mod for Minecraft 1.21.11
│       ├── bridge/      JNI library (libmcchunkgen.so)
│       ├── build.gradle
│       └── src/         Mod Java sources
└── README.md
```

## Quick Start (CPU Mock)

```bash
make
./chunkgen terrain.bin 42 65536
```

## Building with GPU

Install the CUDA Toolkit, then:

```bash
make clean && make
```

The Makefile auto-detects `nvcc` and compiles with real CUDA support.

## Block ID Reference

| ID | Block              |
|----|--------------------|
| 0  | Air                |
| 1  | Stone              |
| 2  | Dirt               |
| 3  | Grass              |
| 4  | Bedrock            |
| 5  | Water              |

## License

MIT