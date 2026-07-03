package io.openhands.mcchunkgen;

import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.World;
import net.minecraft.world.gen.chunk.ChunkGenerator;

import java.io.*;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class ChunkGenWorker implements Runnable {
    private final ServerWorld world;
    private final long seed;
    private final int radius;
    private final int centerX;
    private final int centerZ;
    private final int totalChunks;

    private final AtomicBoolean running = new AtomicBoolean(true);
    private final AtomicInteger generated = new AtomicInteger(0);

    private File outputFile;
    private DataOutputStream outputStream;

    private static final int FLUSH_INTERVAL = 128;

    public ChunkGenWorker(long seed, int radius, int centerX, int centerZ) {
        this.world = null;
        this.seed = seed;
        this.radius = radius;
        this.centerX = centerX;
        this.centerZ = centerZ;
        this.totalChunks = (2 * radius + 1) * (2 * radius + 1);
    }

    public boolean isRunning() { return running.get(); }
    public int generated()     { return generated.get(); }
    public int total()         { return totalChunks; }
    public double progress()   { return totalChunks > 0 ? (double) generated.get() / totalChunks : 0; }
    public void stop()         { running.set(false); }

    @Override
    public void run() {
        byte[] chunkData = new byte[NativeChunkBridge.CHUNK_VOLUME];

        // Square spiral from center
        int cx = 0, cz = 0, dx = 0, dz = -1;
        int maxI = (2 * radius + 1) * (2 * radius + 1);

        for (int i = 0; i < maxI && running.get(); i++) {
            if (-radius <= cx && cx <= radius && -radius <= cz && cz <= radius) {
                int chunkX = centerX + cx;
                int chunkZ = centerZ + cz;

                NativeChunkBridge.nativeGenerateChunk(seed, chunkX, chunkZ, chunkData);

                int done = generated.incrementAndGet();
                if (done % FLUSH_INTERVAL == 0 && done % 1000 != 0) {
                    // Periodically yield
                    Thread.yield();
                }
            }

            // Square spiral
            if (cx == cz || (cx < 0 && cx == -cz) || (cx > 0 && cx == 1 - cz)) {
                int tmp = dx;
                dx = -dz;
                dz = tmp;
            }
            cx += dx;
            cz += dz;
        }

        System.out.println("[McChunkGen] Finished. Generated " + generated.get() + " chunks.");
    }
}
