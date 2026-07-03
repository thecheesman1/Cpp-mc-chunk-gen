package io.openhands.mcchunkgen;

import com.mojang.brigadier.arguments.IntegerArgumentType;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.text.Text;

import static net.minecraft.server.command.CommandManager.*;

public class McChunkGen implements ModInitializer {
    public static final String MOD_ID = "mcchunkgen";
    private ChunkGenWorker worker;

    @Override
    public void onInitialize() {
        System.loadLibrary("mcchunkgen");
        NativeChunkBridge.init();

        CommandRegistrationCallback.EVENT.register((dispatcher, registryAccess, env) -> {
            dispatcher.register(literal("mcchunkgen")
                .then(literal("start")
                    .then(argument("radius", IntegerArgumentType.integer(1, 512))
                        .executes(ctx -> {
                            int radius = IntegerArgumentType.getInteger(ctx, "radius");
                            worker = new ChunkGenWorker(
                                ctx.getSource().getWorld().getSeed(),
                                radius, 0, 0);
                            new Thread(worker).start();
                            ctx.getSource().sendMessage(
                                Text.literal("§a[McChunkGen] Started pre-generating "
                                    + ((2L*radius+1)*(2L*radius+1)) + " chunks"));
                            return 1;
                        })
                        .then(argument("centerX", IntegerArgumentType.integer())
                            .then(argument("centerZ", IntegerArgumentType.integer())
                                .executes(ctx -> {
                                    int radius = IntegerArgumentType.getInteger(ctx, "radius");
                                    int cx = IntegerArgumentType.getInteger(ctx, "centerX");
                                    int cz = IntegerArgumentType.getInteger(ctx, "centerZ");
                                    worker = new ChunkGenWorker(
                                        ctx.getSource().getWorld().getSeed(),
                                        radius, cx, cz);
                                    new Thread(worker).start();
                                    ctx.getSource().sendMessage(
                                        Text.literal("§a[McChunkGen] Started pre-generating"));
                                    return 1;
                                })
                            )
                        )
                    )
                )
                .then(literal("stop")
                    .executes(ctx -> {
                        if (worker != null) {
                            worker.stop();
                            ctx.getSource().sendMessage(
                                Text.literal("§e[McChunkGen] Worker stopped"));
                        } else {
                            ctx.getSource().sendMessage(
                                Text.literal("§c[McChunkGen] No worker running"));
                        }
                        return 1;
                    })
                )
                .then(literal("status")
                    .executes(ctx -> {
                        if (worker != null) {
                            ctx.getSource().sendMessage(
                                Text.literal(String.format(
                                    "§a[McChunkGen] %d/%d chunks (%.1f%%)",
                                    worker.generated(), worker.total(),
                                    worker.progress() * 100)));
                        } else {
                            ctx.getSource().sendMessage(
                                Text.literal("§c[McChunkGen] No active generation"));
                        }
                        return 1;
                    })
                )
            );
        });
    }
}
