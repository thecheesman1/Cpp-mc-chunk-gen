package io.openhands.mcchunkgen;

public final class NativeChunkBridge {
    private static boolean loaded = false;

    public static void init() {
        // Already loaded via System.loadLibrary in McChunkGen
        loaded = true;
    }

    public static boolean isAvailable() { return loaded; }

    public static native boolean nativeGenerateChunk(long seed, int chunkX, int chunkZ, byte[] outBlocks);
    public static native String nativeGetBlockAt(long seed, int chunkX, int chunkZ, int x, int y, int z);
    public static native boolean nativeGenerateBatch(long seed, int[] chunkXs, int[] chunkZs, byte[][] outBlocksArr);

    public static final int CHUNK_VOLUME = 16 * 256 * 16;
}
