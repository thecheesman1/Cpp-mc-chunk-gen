#!/usr/bin/env python3
"""
compress_mca.py — Post-process .mca files to compress chunks with zstd or zlib.

Usage:
  python3 compress_mca.py <world_dir>           # compress all .mca files (default: zlib)
  python3 compress_mca.py <world_dir> --zstd     # use zstd instead of zlib
  python3 compress_mca.py <world_dir> --level 9  # compression level (default: 6)

The generator writes uncompressed chunks (type 3) for maximum speed.
This script reads those files, compresses each chunk's NBT data, and rewrites
the .mca with compression type 1 (zlib) or 2 (zstd, if available).

Minecraft 1.21+ supports both. zstd is faster and compresses better, but
requires the 'zstandard' Python package (pip install zstandard).
"""

import os
import sys
import struct
import zlib
import shutil

# Try to import zstd
try:
    import zstandard as zstd
    HAS_ZSTD = True
except ImportError:
    HAS_ZSTD = False


# Minecraft Anvil constants
SECTOR_SIZE = 4096
LOCATION_ENTRIES = 1024


def read_region_header(f):
    """Read 1024 location entries (4 bytes each = 4096 bytes)."""
    locations = []
    for i in range(LOCATION_ENTRIES):
        data = f.read(4)
        if len(data) < 4:
            break
        offset = (data[0] << 16) | (data[1] << 8) | data[2]
        count = data[3]
        locations.append((offset, count))
    # Skip 4096 bytes of timestamps
    f.seek(SECTOR_SIZE, os.SEEK_SET)
    return locations


def read_chunk(f, offset, count):
    """Read a chunk at the given sector offset."""
    if offset == 0 or count == 0:
        return None
    f.seek(offset * SECTOR_SIZE, os.SEEK_SET)
    # First 4 bytes: total payload length (includes compression byte)
    raw = f.read(4)
    if len(raw) < 4:
        return None
    length = struct.unpack('>I', raw)[0]
    # Read payload: compression type (1 byte) + NBT data
    payload = f.read(length)
    if len(payload) < length:
        return None
    compression_type = payload[0]
    nbt_data = payload[1:]
    return compression_type, nbt_data, length + 4


def write_chunk(f, compression_type, nbt_data):
    """Write a compressed chunk at the current position, returns (sector_offset, sector_count)."""
    # Build payload: 1 byte compression type + NBT data
    payload = bytes([compression_type]) + nbt_data
    total_length = len(payload)

    # Write length (4 bytes big-endian) + payload
    f.write(struct.pack('>I', total_length))
    f.write(payload)

    # Pad to sector boundary
    end_pos = f.tell()
    padding = (SECTOR_SIZE - (end_pos % SECTOR_SIZE)) % SECTOR_SIZE
    if padding > 0:
        f.write(b'\x00' * padding)

    sector_offset = (end_pos - total_length - 4) // SECTOR_SIZE
    sector_count = (total_length + 4 + SECTOR_SIZE - 1) // SECTOR_SIZE
    return sector_offset, sector_count


def compress_file(mca_path, use_zstd=False, level=6):
    """Compress all chunks in a single .mca file. Returns (total, compressed, saved_bytes)."""
    tmp_path = mca_path + '.tmp'

    total_chunks = 0
    compressed_chunks = 0
    original_bytes = 0
    new_bytes = 0

    zstd_compressor = None
    if use_zstd and HAS_ZSTD:
        zstd_compressor = zstd.ZstdCompressor(level=level)

    with open(mca_path, 'rb') as fin:
        locations = read_region_header(fin)

        with open(tmp_path, 'wb') as fout:
            # Reserve header space (will rewrite at end)
            header = b'\x00' * SECTOR_SIZE * 2
            fout.write(header)

            new_locations = []

            for idx, (offset, count) in enumerate(locations):
                if offset == 0 or count == 0:
                    new_locations.append((0, 0))
                    continue

                result = read_chunk(fin, offset, count)
                if result is None:
                    new_locations.append((0, 0))
                    continue

                compression_type, nbt_data, total_size = result
                total_chunks += 1
                original_bytes += total_size

                if compression_type != 3:
                    # Already compressed, write as-is
                    chunk_data = fin.read(total_size)
                    # Actually re-read from the right position
                    fin.seek(offset * SECTOR_SIZE, os.SEEK_SET)
                    raw = fin.read(total_size)
                    new_off, new_cnt = write_chunk(fout, compression_type, nbt_data)
                    new_locations.append((new_off, new_cnt))
                    new_bytes += total_size + 4 + ((SECTOR_SIZE - (total_size + 4) % SECTOR_SIZE) % SECTOR_SIZE)
                    continue

                # Compress the NBT data
                try:
                    if use_zstd and HAS_ZSTD:
                        compressed = zstd_compressor.compress(nbt_data)
                        new_type = 2  # zstd
                    else:
                        compressed = zlib.compress(nbt_data, level)
                        new_type = 1  # zlib
                except Exception as e:
                    print(f"    [WARN] Compression failed for chunk {idx}: {e}", file=sys.stderr)
                    new_locations.append((0, 0))
                    continue

                new_off, new_cnt = write_chunk(fout, new_type, compressed)
                new_locations.append((new_off, new_cnt))
                compressed_chunks += 1
                new_bytes += (len(compressed) + 5 + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE

            # Rewrite header with new locations
            fout.seek(0, os.SEEK_SET)
            new_header = bytearray(SECTOR_SIZE * 2)
            for idx, (off, cnt) in enumerate(new_locations):
                if off == 0:
                    continue
                new_header[idx * 4 + 0] = (off >> 16) & 0xFF
                new_header[idx * 4 + 1] = (off >> 8) & 0xFF
                new_header[idx * 4 + 2] = off & 0xFF
                new_header[idx * 4 + 3] = cnt & 0xFF
                # Timestamp: keep original or set current time
                ts_offset = SECTOR_SIZE + idx * 4
                struct.pack_into('>I', new_header, ts_offset, int(os.path.getmtime(mca_path)))
            fout.write(bytes(new_header))

    # Replace original with compressed
    shutil.move(tmp_path, mca_path)

    return total_chunks, compressed_chunks, original_bytes - new_bytes


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='Compress McChunkGen .mca files (post-processing)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 compress_mca.py D:\\bench\\world
  python3 compress_mca.py /world --zstd --level 9
  python3 compress_mca.py . --level 3
        """)
    parser.add_argument('world_dir', help='World directory containing region/')
    parser.add_argument('--zstd', action='store_true', help='Use zstd instead of zlib')
    parser.add_argument('--level', type=int, default=6, help='Compression level (default: 6)')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be done without doing it')

    args = parser.parse_args()

    region_dir = os.path.join(args.world_dir, 'region')
    if not os.path.isdir(region_dir):
        print(f"[ERROR] {region_dir}/ does not exist")
        sys.exit(1)

    if args.zstd and not HAS_ZSTD:
        print("[WARN] zstandard not installed. Falling back to zlib.")
        print("  Install with: pip install zstandard")
        args.zstd = False

    codec = 'zstd' if args.zstd else 'zlib'
    print(f"Compressing .mca files in {region_dir}/ using {codec} (level {args.level})...")
    print()

    mca_files = sorted([f for f in os.listdir(region_dir) if f.endswith('.mca')])
    if not mca_files:
        print("No .mca files found.")
        return

    total_chunks = 0
    total_compressed = 0
    total_saved = 0
    files_processed = 0

    for fname in mca_files:
        mca_path = os.path.join(region_dir, fname)
        size_before = os.path.getsize(mca_path)

        if args.dry_run:
            print(f"  {fname}: {size_before / 1024 / 1024:.1f} MB -> ? (dry run)")
            continue

        chunks, comped, saved = compress_file(mca_path, args.zstd, args.level)
        size_after = os.path.getsize(mca_path)
        saved_mb = (size_before - size_after) / (1024 * 1024)
        ratio = size_after / size_before * 100 if size_before > 0 else 0

        print(f"  {fname}: {size_before / 1024 / 1024:.1f} MB -> {size_after / 1024 / 1024:.1f} MB"
              f" ({ratio:.0f}%)  |  {comped}/{chunks} chunks compressed"
              f"  |  saved {saved_mb:.1f} MB")

        total_chunks += chunks
        total_compressed += comped
        total_saved += size_before - size_after
        files_processed += 1

    print()
    print(f"Done! {files_processed} files, {total_chunks} chunks, "
          f"{total_saved / (1024*1024):.1f} MB saved "
          f"({total_saved / (total_chunks or 1):.0f} KB/chunk avg)")


if __name__ == '__main__':
    main()