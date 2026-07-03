package io.openhands.mcchunkgen;

import net.minecraft.registry.RegistryKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.ChunkStatus;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class ChunkGenWorker implements Runnable {
    private final MinecraftServer server;
    private final long seed;
    private final int radius;
    private final int centerX;
    private final int centerZ;
    private final int totalChunks;

    private final AtomicBoolean running = new AtomicBoolean(true);
    private final AtomicInteger generated = new AtomicInteger(0);

    private static final int FLUSH_INTERVAL = 64;
    private static final int LOG_INTERVAL = 4096;

    public ChunkGenWorker(MinecraftServer server, int radius, int centerX, int centerZ) {
        this.server = server;
        this.seed = server.getOverworld().getSeed();
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
        ServerWorld world = server.getOverworld();

        // Square spiral from center
        int cx = 0, cz = 0, dx = 0, dz = -1;
        int maxI = (2 * radius + 1) * (2 * radius + 1);

        long t0 = System.currentTimeMillis();

        for (int i = 0; i < maxI && running.get(); i++) {
            if (-radius <= cx && cx <= radius && -radius <= cz && cz <= radius) {
                int chunkX = centerX + cx;
                int chunkZ = centerZ + cz;

                // Force the chunk to be fully loaded + generated
                // The mixin intercepts populateNoise to inject native terrain
                Chunk chunk = world.getChunkManager().getChunk(chunkX, chunkZ, ChunkStatus.FULL, true);

                int done = generated.incrementAndGet();

                // Periodic flush — tell the chunk manager to save
                if (done % FLUSH_INTERVAL == 0) {
                    world.getChunkManager().save(false);
                }

                // Periodic log
                if (done % LOG_INTERVAL == 0) {
                    long elapsed = (System.currentTimeMillis() - t0) / 1000;
                    double cps = elapsed > 0 ? (double) done / elapsed : 0;
                    System.out.println("[McChunkGen] " + done + "/" + totalChunks
                        + " chunks (" + String.format("%.1f", progress() * 100)
                        + "%) at " + String.format("%.0f", cps) + " CPS");
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

        // Final save
        world.getChunkManager().save(true);
        long elapsed = (System.currentTimeMillis() - t0) / 1000;
        double cps = elapsed > 0 ? (double) generated.get() / elapsed : 0;

        System.out.println("[McChunkGen] Finished. Generated " + generated.get()
            + " chunks in " + elapsed + "s (" + String.format("%.0f", cps) + " CPS)");
    }
}
