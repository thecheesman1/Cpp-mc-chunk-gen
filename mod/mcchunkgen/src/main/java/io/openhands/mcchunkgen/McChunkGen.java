package io.openhands.mcchunkgen;

import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.text.Text;
import net.minecraft.util.WorldSavePath;
import net.minecraft.world.World;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * McChunkGen — GPU-Accelerated Chunk Pre-Generator
 *
 * A Fabric mod that uses native CUDA/C++ code to pre-generate Minecraft
 * terrain, bypassing vanilla noise generation for massive speedups.
 *
 * Commands:
 *   /mcchunkgen start <radius> [<centerX> <centerZ>]
 *   /mcchunkgen status
 *   /mcchunkgen stop
 */
public class McChunkGen implements ModInitializer {
    public static final String MOD_ID = "mcchunkgen";
    public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

    private ChunkGenWorker worker = null;

    @Override
    public void onInitialize() {
        LOGGER.info("McChunkGen initializing...");

        if (NativeChunkBridge.isAvailable()) {
            LOGGER.info("Native CUDA/C++ backend is available");
        } else {
            LOGGER.warn("Native backend NOT loaded — /mcchunkgen commands will fail");
        }

        // Register the /mcchunkgen command
        CommandRegistrationCallback.EVENT.register((dispatcher, registryAccess, env) -> {
            dispatcher.register(
                CommandManager.literal("mcchunkgen")
                    .requires(src -> src.hasPermissionLevel(2))

                    // ---- start subcommand ----
                    .then(CommandManager.literal("start")
                        .then(CommandManager.argument("radius", net.minecraft.command.argument.IntegerArgumentType.integer(1, 512))
                            .executes(ctx -> {
                                int radius = net.minecraft.command.argument.IntegerArgumentType.getInteger(ctx, "radius");
                                return startGeneration(ctx.getSource(), radius, 0, 0);
                            })
                            .then(CommandManager.argument("centerX", net.minecraft.command.argument.IntegerArgumentType.integer())
                                .then(CommandManager.argument("centerZ", net.minecraft.command.argument.IntegerArgumentType.integer())
                                    .executes(ctx -> {
                                        int radius = net.minecraft.command.argument.IntegerArgumentType.getInteger(ctx, "radius");
                                        int cx = net.minecraft.command.argument.IntegerArgumentType.getInteger(ctx, "centerX");
                                        int cz = net.minecraft.command.argument.IntegerArgumentType.getInteger(ctx, "centerZ");
                                        return startGeneration(ctx.getSource(), radius, cx, cz);
                                    })
                                )
                            )
                        )
                    )

                    // ---- status subcommand ----
                    .then(CommandManager.literal("status")
                        .executes(ctx -> statusCommand(ctx.getSource()))
                    )

                    // ---- stop subcommand ----
                    .then(CommandManager.literal("stop")
                        .executes(ctx -> stopCommand(ctx.getSource()))
                    )
            );
        });

        LOGGER.info("McChunkGen initialized. Use /mcchunkgen start <radius> to begin.");
    }

    private int startGeneration(ServerCommandSource source, int radius, int centerX, int centerZ) {
        if (!NativeChunkBridge.isAvailable()) {
            source.sendError(Text.of("Native chunk generator not available. Check logs."));
            return 0;
        }

        if (worker != null && worker.isRunning()) {
            source.sendError(Text.of("Generation already in progress! Use /mcchunkgen stop first."));
            return 0;
        }

        long seed = source.getWorld().getSeed();
        worker = new ChunkGenWorker(source.getServer(), source.getPlayer(), seed, radius, centerX, centerZ);
        Thread t = new Thread(worker, "mcchunkgen-worker");
        t.setDaemon(true);
        t.start();

        source.sendMessage(Text.of(
            "§aMcChunkGen: Starting pre-generation of §e" + (radius * 2 * radius * 2) +
            "§a chunks (radius=" + radius + ")"));
        return 1;
    }

    private int statusCommand(ServerCommandSource source) {
        if (worker == null || !worker.isRunning()) {
            source.sendMessage(Text.of("§eMcChunkGen: No generation in progress."));
        } else {
            int pct = worker.progressPercent();
            source.sendMessage(Text.of(
                "§aMcChunkGen: §e" + worker.generated() + "§a/§e" + worker.total() +
                "§a chunks generated (" + pct + "%)"));
        }
        return 1;
    }

    private int stopCommand(ServerCommandSource source) {
        if (worker != null && worker.isRunning()) {
            worker.stop();
            source.sendMessage(Text.of("§cMcChunkGen: Generation stopped."));
        } else {
            source.sendMessage(Text.of("§eMcChunkGen: No generation in progress."));
        }
        return 1;
    }
}