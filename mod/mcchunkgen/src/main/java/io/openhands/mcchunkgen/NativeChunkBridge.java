package io.openhands.mcchunkgen;

/**
 * JNI bridge to the native CUDA/C++ chunk generator.
 * Loads libmcchunkgen.so and exposes native terrain generation to Java.
 */
public final class NativeChunkBridge {
    private static boolean loaded = false;

    static {
        try {
            System.loadLibrary("mcchunkgen");
            loaded = true;
            McChunkGen.LOGGER.info("libmcchunkgen.so loaded successfully");
        } catch (UnsatisfiedLinkError e) {
            McChunkGen.LOGGER.error("Failed to load libmcchunkgen.so: " + e.getMessage());
            McChunkGen.LOGGER.info("Falling back to vanilla chunk generation");
        }
    }

    /** Returns true if the native library was loaded. */
    public static boolean isAvailable() {
        return loaded;
    }

    /**
     * Generate a single chunk's terrain.
     *
     * @param seed   World seed
     * @param chunkX Chunk X coordinate
     * @param chunkZ Chunk Z coordinate
     * @param outBlocks Byte array of length CHUNK_VOLUME (16*256*16) to fill
     * @return true on success
     */
    public static native boolean nativeGenerateChunk(long seed, int chunkX, int chunkZ, byte[] outBlocks);

    /**
     * Get a block type at a specific coordinate (debugging).
     *
     * @return Minecraft block ID string (e.g. "minecraft:stone")
     */
    public static native String nativeGetBlockAt(long seed, int chunkX, int chunkZ, int x, int y, int z);

    /**
     * Batch-generate multiple chunks.
     *
     * @param seed   World seed
     * @param chunkXs  Array of chunk X coordinates
     * @param chunkZs  Array of chunk Z coordinates
     * @param outBlocksArr Array of byte arrays to fill
     * @return true on success
     */
    public static native boolean nativeGenerateBatch(long seed, int[] chunkXs, int[] chunkZs, byte[][] outBlocksArr);

    /** Chunk volume constant matching the C++ side. */
    public static final int CHUNK_VOLUME = 16 * 256 * 16;
}