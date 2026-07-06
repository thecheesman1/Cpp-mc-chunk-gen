#ifndef ANVIL_WRITER_H
#define ANVIL_WRITER_H

#include "nbt.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>

//=============================================================================
// Anvil region file writer (.mca)
// Format: 4096-byte header (1024 location + 1024 timestamp entries)
//         Followed by chunk data blocks
//
// For maximum speed, we write uncompressed NBT (compression type 3).
// Minecraft 1.21+ supports uncompressed chunks.
// Fallback to zlib via external helper if needed.
//=============================================================================

// Minecraft 1.21.11 data version
static constexpr int DATA_VERSION = 3955;

// Chunk section height: 16 blocks
static constexpr int SECTION_SIZE_Y = 16;
// Number of sections in native 0..255 range
static constexpr int NATIVE_SECTIONS = 16; // 256/16
// Chunk column dimensions
static constexpr int CHUNK_SIZE = 16;
// Blocks per section
static constexpr int SECTION_VOLUME = CHUNK_SIZE * CHUNK_SIZE * SECTION_SIZE_Y; // 4096

//=============================================================================
// BitStorage packing helpers (Minecraft PalettedContainer format)
//=============================================================================
struct BitStorage {
    std::vector<uint64_t> data;
    int bits_per_entry;
    int size;

    BitStorage(int bits, int num_entries) : bits_per_entry(bits), size(num_entries) {
        if (bits <= 0) return; // single value, no storage
        int bits_total = bits * num_entries;
        int long_count = (bits_total + 63) / 64;
        data.resize(long_count, 0);
    }

    void set(int index, int value) {
        if (bits_per_entry <= 0) return;
        int bit_index = index * bits_per_entry;
        int long_idx = bit_index / 64;
        int bit_off = bit_index % 64;
        uint64_t mask = (bits_per_entry >= 64) ? ~0ULL : ((1ULL << bits_per_entry) - 1);

        // First long
        data[long_idx] = (data[long_idx] & ~(mask << bit_off)) | ((uint64_t)value << bit_off);
        // If crossing long boundary
        int remaining = bit_off + bits_per_entry - 64;
        if (remaining > 0) {
            data[long_idx + 1] = (data[long_idx + 1] & ~(mask << (64 - bit_off))) |
                                 ((uint64_t)value >> (bits_per_entry - remaining));
        }
    }
};

//=============================================================================
// Palette builder — maps block IDs to palette indices
//=============================================================================
struct PaletteEntry {
    std::string name;
    block_t block_id;
};

struct Palette {
    std::vector<PaletteEntry> entries;
    std::vector<int> indices;  // index per block in section (SECTION_VOLUME)

    Palette() {}

    int add_or_get(block_t id, const std::string& name) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].block_id == id) return (int)i;
        }
        entries.push_back({name, id});
        return (int)(entries.size() - 1);
    }
};

//=============================================================================
// Heightmap helpers
//=============================================================================
// Compute WORLD_SURFACE heightmap from block data
// heightmap[y] = highest non-air block in column (x, z)
static void compute_heightmap(const uint8_t* blocks, int heightmap[CHUNK_SIZE][CHUNK_SIZE],
                               int min_y, int max_y) {
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int h = min_y - 1;
            for (int y = max_y - 1; y >= min_y; y--) {
                int idx = (z * CHUNK_SIZE + x) * 256 + y;
                if (blocks[idx] != BLOCK_AIR) {
                    h = y;
                    break;
                }
            }
            heightmap[x][z] = h;
        }
    }
}

// Pack 16x16 heightmap (256 values, 9 bits each) into 36 longs
static void pack_heightmap(const int hm[CHUNK_SIZE][CHUNK_SIZE], uint64_t out[36]) {
    memset(out, 0, 36 * sizeof(uint64_t));
    int bits_per_entry = 9;
    for (int i = 0; i < 256; i++) {
        int x = i / CHUNK_SIZE;
        int z = i % CHUNK_SIZE;
        int bit_index = i * bits_per_entry;
        int long_idx = bit_index / 64;
        int bit_off = bit_index % 64;
        out[long_idx] |= ((uint64_t)(uint16_t)hm[x][z]) << bit_off;
        int remaining = bit_off + bits_per_entry - 64;
        if (remaining > 0 && long_idx + 1 < 36) {
            out[long_idx + 1] |= (uint64_t)(hm[x][z]) >> (bits_per_entry - remaining);
        }
    }
}

//=============================================================================
// Chunk → NBT serialization for 1.21 format
//=============================================================================
// Block state name mapper — needed by FixedPalette and by the palette lookup
static const char* block_name(block_t id) {
    switch (id) {
        case 0:  return "minecraft:air";
        case 1:  return "minecraft:stone";
        case 2:  return "minecraft:dirt";
        case 3:  return "minecraft:grass_block";
        case 4:  return "minecraft:bedrock";
        case 5:  return "minecraft:water";
        case 6:  return "minecraft:gravel";
        case 7:  return "minecraft:snow_block";
        case 8:  return "minecraft:cobblestone";
        case 9:  return "minecraft:coal_ore";
        case 10: return "minecraft:iron_ore";
        case 11: return "minecraft:gold_ore";
        case 12: return "minecraft:diamond_ore";
        case 13: return "minecraft:oak_log";
        case 14: return "minecraft:oak_leaves";
        default: return "minecraft:stone";
    }
}

// Pre-computed block state palette lookup: block_id → palette entry index
// This avoids linear search during palette construction.
static int palette_lookup[256];
static std::string palette_names[256];
static int palette_size = 0;

__attribute__((constructor))
static void init_palette() {
    for (int i = 0; i < 256; i++) {
        palette_names[i] = block_name(i);
        // Deduplicate: if this block name equals a previous one, reuse the index
        int found = -1;
        for (int j = 0; j < i; j++) {
            if (palette_names[j] == palette_names[i]) { found = j; break; }
        }
        if (found >= 0) palette_lookup[i] = palette_lookup[found];
        else palette_lookup[i] = palette_size++;
    }
}

// Fixed palette entries for blocks that actually appear in the generator
static const block_t ACTIVE_BLOCKS[] = {
    BLOCK_AIR, BLOCK_STONE, BLOCK_DIRT, BLOCK_GRASS, BLOCK_BEDROCK, BLOCK_WATER
};
static constexpr int NUM_ACTIVE_BLOCKS = 6;

// Pre-computed palette for the generator's output
// This avoids building a new palette per section
struct FixedPalette {
    std::string names[16]; // max 6 used but allocate 16
    int name_count;
    int lookup[256]; // block_id → palette index

    FixedPalette() {
        name_count = NUM_ACTIVE_BLOCKS;
        for (int i = 0; i < NUM_ACTIVE_BLOCKS; i++) {
            names[i] = block_name(ACTIVE_BLOCKS[i]);
        }
        // Build lookup: which palette entry for each block_id
        for (int i = 0; i < 256; i++) {
            lookup[i] = 0; // default: air
            for (int j = 0; j < NUM_ACTIVE_BLOCKS; j++) {
                if (i == ACTIVE_BLOCKS[j]) { lookup[i] = j; break; }
            }
        }
    }

    void write_palette(NBTBuffer& nb) const {
        nb.begin_list("palette", TAG_Compound, (uint32_t)name_count);
        for (int i = 0; i < name_count; i++) {
            nb.begin_compound();
            nb.tag_string("Name", names[i]);
            // Grass block needs snowy=false property
            if (names[i] == "minecraft:grass_block") {
                nb.begin_compound("properties");
                nb.tag_string("snowy", "false");
                nb.end_compound();
            }
            nb.end_compound();
        }
        nb.end_list();
    }
};

static const FixedPalette GLOBAL_PALETTE;

// Write block_states for one section using the fixed global palette
static void write_block_states_fast(NBTBuffer& nb, const uint8_t* section_blocks) {
    nb.begin_compound("block_states");

    // Use the fixed global palette
    GLOBAL_PALETTE.write_palette(nb);

    // Always write data: sections always have >1 block type (at minimum air + stone),
    // so the BitStorage is always needed. Skip the unique-count pass.
    int bits = 1;
    while ((1U << bits) < (unsigned)GLOBAL_PALETTE.name_count) bits++;
    if (bits < 2) bits = 2;

    BitStorage storage(bits, SECTION_VOLUME);
    for (int i = 0; i < SECTION_VOLUME; i++) {
        storage.set(i, GLOBAL_PALETTE.lookup[section_blocks[i]]);
    }
    nb.tag_long_array("data", storage.data.data(), (uint32_t)storage.data.size());

    nb.end_compound();
}

// Write biomes for one section (simple: all plains)
static void write_biomes(NBTBuffer& nb) {
    nb.begin_compound("biomes");
    nb.begin_list("palette", TAG_Compound, 1);
    nb.begin_compound();
    nb.tag_string("Name", "minecraft:plains");
    nb.end_compound();
    nb.end_list(); // palette
    // Single palette entry → no data array needed
    nb.end_compound(); // biomes
}

// Serialize a single 16x256x16 chunk into NBT format for 1.21
// Uses the pre-computed fixed palette for maximum speed.
// Returns uncompressed NBT size (0 on failure)
static size_t serialize_chunk(uint8_t* blocks, int cx, int cz,
                               uint8_t* out_buf, size_t out_cap) {
    // Estimate: each section ~8KB + headers ~2KB = ~130KB uncompressed for 16 sections
    // We need out_cap >= ~150KB

    NBTBuffer nb;

    // Root compound
    nb.begin_compound("");
    nb.tag_int("DataVersion", DATA_VERSION);
    nb.tag_int("xPos", cx);
    nb.tag_int("zPos", cz);
    nb.tag_int("yPos", 0);   // min section Y (our world: y=0..255 → Y=0)
    nb.tag_string("Status", "minecraft:full");
    nb.tag_byte("isLightOn", 1);

    // Generate sections (16 sections covering y=0..255)
    nb.begin_list("sections", TAG_Compound, NATIVE_SECTIONS);

    // Scratch buffer to reorder column-major blocks into section order
    uint8_t section_scratch[SECTION_VOLUME];

    for (int sec = 0; sec < NATIVE_SECTIONS; sec++) {
        int base_y = sec * SECTION_SIZE_Y;

        // Reorder from column-major (z*16+x)*256+y to section order
        for (int x = 0; x < 16; x++) {
            for (int z = 0; z < 16; z++) {
                for (int ly = 0; ly < 16; ly++) {
                    int src_idx = (z * 16 + x) * 256 + (base_y + ly);
                    int dst_idx = (x * 16 + z) * 16 + ly; // or (z*16+x)*16+ly for z-major
                    // Actually, Minecraft's section format is YZX order:
                    // index = (y * 16 + z) * 16 + x
                    int mc_idx = ly * 256 + z * 16 + x;
                    section_scratch[mc_idx] = blocks[src_idx];
                }
            }
        }

        nb.begin_compound();
        nb.tag_byte("Y", (int8_t)base_y);

        // Use the fast fixed-palette writer
        write_block_states_fast(nb, section_scratch);
        write_biomes(nb);
        nb.end_compound();
    }
    nb.end_list();

    // Heightmaps
    int hm_world_surface[16][16], hm_ocean_floor[16][16];
    compute_heightmap(blocks, hm_world_surface, 0, 256);
    compute_heightmap(blocks, hm_ocean_floor, 0, 256);
    uint64_t packed_surface[36], packed_ocean[36];
    pack_heightmap(hm_world_surface, packed_surface);
    pack_heightmap(hm_ocean_floor, packed_ocean);

    nb.begin_compound("Heightmaps");
    nb.tag_long_array("OCEAN_FLOOR", packed_ocean, 36);
    nb.tag_long_array("WORLD_SURFACE", packed_surface, 36);
    nb.end_compound();

    // Empty entities, block_entities, ticks, structures
    nb.begin_list("block_entities", TAG_Compound, 0);
    nb.end_list();
    nb.begin_list("entities", TAG_Compound, 0);
    nb.end_list();
    nb.begin_list("block_ticks", TAG_Compound, 0);
    nb.end_list();
    nb.begin_list("fluid_ticks", TAG_Compound, 0);
    nb.end_list();
    nb.begin_compound("structures");
    nb.begin_list("References", TAG_Compound, 0);
    nb.end_list();
    nb.begin_list("Starts", TAG_Compound, 0);
    nb.end_list();
    nb.end_compound();
    nb.end_compound();

    // Copy uncompressed NBT to output buffer
    if (nb.size() > out_cap) return 0;
    memcpy(out_buf, nb.data(), nb.size());
    return nb.size();
}

//=============================================================================
// Region file writer (.mca)
// Supports appending to existing files.
//=============================================================================
class RegionFile {
    FILE* fp_;
    uint8_t locations_[4096];
    uint32_t timestamps_[1024];
    int next_sector_;
    std::string path_;

public:
    RegionFile() : fp_(nullptr), next_sector_(2) {
        memset(locations_, 0, sizeof(locations_));
        memset(timestamps_, 0, sizeof(timestamps_));
    }

    RegionFile(const std::string& path) : fp_(nullptr), next_sector_(2), path_(path) {
        memset(locations_, 0, sizeof(locations_));
        memset(timestamps_, 0, sizeof(timestamps_));
    }

    // Move-only: FILE* ownership must be transferred explicitly
    RegionFile(RegionFile&& other) noexcept
        : fp_(other.fp_), next_sector_(other.next_sector_), path_(std::move(other.path_)) {
        memcpy(locations_, other.locations_, sizeof(locations_));
        memcpy(timestamps_, other.timestamps_, sizeof(timestamps_));
        other.fp_ = nullptr;
    }
    RegionFile& operator=(RegionFile&& other) noexcept {
        if (this != &other) {
            if (fp_) close();
            fp_ = other.fp_;
            next_sector_ = other.next_sector_;
            path_ = std::move(other.path_);
            memcpy(locations_, other.locations_, sizeof(locations_));
            memcpy(timestamps_, other.timestamps_, sizeof(timestamps_));
            other.fp_ = nullptr;
        }
        return *this;
    }
    RegionFile(const RegionFile&) = delete;
    RegionFile& operator=(const RegionFile&) = delete;

    ~RegionFile() { if (fp_) close(); }

    bool open(const std::string& path) {
        path_ = path;
        // Try opening existing file for append
        fp_ = fopen(path_.c_str(), "r+b");
        if (fp_) {
            // Read existing header
            fread(locations_, 1, 4096, fp_);
            // Find end of file to know next sector
            fseek(fp_, 0, SEEK_END);
            long file_size = ftell(fp_);
            next_sector_ = (int)((file_size + 4095) / 4096);
            // Also rebuild next_sector from header for safety
            int max_sector = 2;
            for (int i = 0; i < 1024; i++) {
                int off = (locations_[i*4] << 16) | (locations_[i*4+1] << 8) | locations_[i*4+2];
                int cnt = locations_[i*4+3];
                if (cnt > 0) {
                    int end = off + cnt;
                    if (end > max_sector) max_sector = end;
                }
            }
            if (max_sector > next_sector_) next_sector_ = max_sector;
            return true;
        }
        // Create new file
        fp_ = fopen(path_.c_str(), "wb");
        if (!fp_) return false;
        fwrite(locations_, 1, 4096, fp_);
        next_sector_ = 2;
        return true;
    }

    bool open() { return open(path_); }

    // Write a chunk at local coordinates (lcx, lcz) within region (0-31 each)
    bool write_chunk(int lcx, int lcz, const uint8_t* compressed_data, size_t compressed_size) {
        if (lcx < 0 || lcx >= 32 || lcz < 0 || lcz >= 32) return false;
        if (!fp_) return false;

        // Each sector is 4096 bytes
        int needed_sectors = (int)((compressed_size + 4 + 4095) / 4096); // +4 for length field

        // Write chunk data: length (4 bytes) + compression type (1 byte) + uncompressed NBT
        uint32_t total_size = (uint32_t)(compressed_size + 1); // +1 for compression byte
        uint8_t hdr[4];
        be32(hdr, total_size);

        // Sector-aligned write
        long pos = (long)next_sector_ * 4096;
        fseek(fp_, pos, SEEK_SET);
        fwrite(hdr, 1, 4, fp_);
        uint8_t compression_type = 3; // uncompressed (Minecraft 1.21+)
        fwrite(&compression_type, 1, 1, fp_);
        fwrite(compressed_data, 1, compressed_size, fp_);

        // Pad to sector boundary
        long end_pos = ftell(fp_);
        int padding = (int)((4096 - (end_pos % 4096)) % 4096);
        if (padding > 0) {
            static const uint8_t zeros[4096] = {};
            fwrite(zeros, 1, padding, fp_);
        }

        // Update location table
        int idx = lcx + lcz * 32;
        locations_[idx * 4 + 0] = (next_sector_ >> 16) & 0xFF;
        locations_[idx * 4 + 1] = (next_sector_ >> 8) & 0xFF;
        locations_[idx * 4 + 2] = next_sector_ & 0xFF;
        locations_[idx * 4 + 3] = (uint8_t)needed_sectors;

        timestamps_[idx] = (uint32_t)time(nullptr);

        next_sector_ += needed_sectors;
        return true;
    }

    void close() {
        if (!fp_) return;
        // Write final header
        fseek(fp_, 0, SEEK_SET);
        fwrite(locations_, 1, 4096, fp_);
        // Write timestamps
        for (int i = 0; i < 1024; i++) {
            uint8_t ts[4];
            be32(ts, timestamps_[i]);
            fwrite(ts, 1, 4, fp_);
        }
        fclose(fp_);
        fp_ = nullptr;
    }

    bool is_open() const { return fp_ != nullptr; }
    const std::string& path() const { return path_; }
};

//=============================================================================
// Helper: determine region file path from chunk coordinates
//=============================================================================
static std::string region_path(const std::string& world_dir, int cx, int cz) {
    int rx = cx >> 5;
    int rz = cz >> 5;
    char path[512];
    snprintf(path, sizeof(path), "%s/region/r.%d.%d.mca", world_dir.c_str(), rx, rz);
    return std::string(path);
}

#endif // ANVIL_WRITER_H