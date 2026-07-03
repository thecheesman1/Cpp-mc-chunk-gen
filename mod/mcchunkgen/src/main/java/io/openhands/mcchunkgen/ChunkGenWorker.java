package io.openhands.mcchunkgen;

import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.util.WorldSavePath;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.world.chunk.ProtoChunk;
import net.minecraft.world.chunk.WorldChunk;
import net.minecraft.world.gen.chunk.ChunkGenerator;

import java.io.*;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.GZIPOutputStream;

/**
 * Background worker that drives the native chunk generator and writes
 * results to disk as a raw binary database. The main server thread
 * reads from this database when chunks are loaded.
 */
public class ChunkGenWorker implements Runnable {
    private final MinecraftServer server;
    private final ServerPlayerEntity player;
    private final long seed;
    private final int radius;
    private final int centerX;
    private final int centerZ;
    private final int totalChunks;

    private final AtomicBoolean running = new AtomicBoolean(true);
    private final AtomicInteger generated = new AtomicInteger(0);

    private File outputFile;
    private DataOutputStream outputStream;

    /** How many chunks to cache between serialization flushes. */
    private static final int FLUSH_INTERVAL = 128;

    public ChunkGenWorker(MinecraftServer server, ServerPlayerEntity player,
                          long seed, int radius, int centerX, int centerZ) {
        this.server = server;
        this.player = player;
        this.seed = seed;
        this.radius = radius;
        this.centerX = centerX;
        this.centerZ = centerZ;
        this.totalChunks = (2 * radius) * (2 * radius);
    }

    public boolean isRunning() { return running.get(); }
    public int generated()     { return generated.get(); }
    public int total()         { return totalChunks; }

    public int progressPercent() {
        if (totalChunks == 0) return 100;
        return Math.min(100, generated.get() * 100 / totalChunks);
    }

    public void stop() { running.set(false); }

    @Override
    public void run() {
        ServerWorld overworld = server.getWorld(ServerWorld.OVERWORLD);
        if (overworld == null) {
            McChunkGen.LOGGER.error("Overworld not found");
            return;
        }

        // Output database path
        Path savePath = server.getSavePath(WorldSavePath.ROOT);
        outputFile = savePath.resolve("mcchunkgen_" + seed + ".bin").toFile();

        try {
            outputStream = new DataOutputStream(
                new BufferedOutputStream(new FileOutputStream(outputFile)));

            // Write header placeholder (12 bytes)
            outputStream.writeInt(0x4D434348); // "MCCH" magic
            outputStream.writeInt(totalChunks); // n_chunks
            outputStream.writeInt(NativeChunkBridge.CHUNK_VOLUME); // chunk_size

            byte[] chunkData = new byte[NativeChunkBridge.CHUNK_VOLUME];

            // Generate in a square spiral outward from center
            int cx = 0, cz = 0, dx = 0, dz = -1;
            int t = Math.max(radius, radius);
            int maxI = t * t;

            for (int i = 0; i < maxI && running.get(); i++) {
                if (-radius <= cx && cx <= radius && -radius <= cz && cz <= radius) {
                    int chunkX = centerX + cx;
                    int chunkZ = centerZ + cz;

                    boolean ok = NativeChunkBridge.nativeGenerateChunk(
                        seed, chunkX, chunkZ, chunkData);

                    if (ok) {
                        // Write to disk: chunkX (int), chunkZ (int), block data
                        outputStream.writeInt(chunkX);
                        outputStream.writeInt(chunkZ);
                        outputStream.write(chunkData, 0, NativeChunkBridge.CHUNK_VOLUME);
                    }

                    int done = generated.incrementAndGet();
                    if (done % FLUSH_INTERVAL == 0) {
                        outputStream.flush();
                    }

                    // Log progress every 5000 chunks
                    if (done % 5000 == 0) {
                        int pct = done * 100 / totalChunks;
                        McChunkGen.LOGGER.info(
                            "McChunkGen: {} / {} chunks ({}%)",
                            done, totalChunks, pct);
                    }
                }

                // Square spiral step
                if (cx == cz || (cx < 0 && cx == -cz) || (cx > 0 && cx == 1 - cz)) {
                    int tmp = dx;
                    dx = -dz;
                    dz = tmp;
                }
                cx += dx;
                cz += dz;
            }

            outputStream.close();
            McChunkGen.LOGGER.info(
                "McChunkGen: Finished. {} chunks written to {}",
                generated.get(), outputFile.getAbsolutePath());

        } catch (IOException e) {
            McChunkGen.LOGGER.error("McChunkGen I/O error: " + e.getMessage(), e);
        } finally {
            try {
                if (outputStream != null) outputStream.close();
            } catch (IOException ignored) {}
        }
    }
}