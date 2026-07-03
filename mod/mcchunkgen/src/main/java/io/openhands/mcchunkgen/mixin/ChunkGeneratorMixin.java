package io.openhands.mcchunkgen.mixin;

import io.openhands.mcchunkgen.McChunkGen;
import io.openhands.mcchunkgen.NativeChunkBridge;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.ProtoChunk;
import net.minecraft.world.gen.chunk.ChunkGenerator;
import net.minecraft.world.gen.chunk.ChunkGeneratorContext;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Intercepts ChunkGenerator.generate() to replace vanilla terrain with
 * native-generated data from our CUDA/C++ backend.
 *
 * Target: Minecraft 1.21.11 (Yarn mappings).
 * If your mappings differ, adjust the method signature and @At target.
 */
@Mixin(ChunkGenerator.class)
public abstract class ChunkGeneratorMixin {

    @Inject(method = "generate", at = @At("HEAD"), cancellable = true)
    private void mcchunkgen$onGenerate(
            ChunkGeneratorContext context,
            ServerWorld world,
            ChunkPos pos,
            Chunk chunk,
            CallbackInfoReturnable<Chunk> cir) {

        if (!NativeChunkBridge.isAvailable()) return;

        long seed = world.getSeed();
        byte[] blocks = new byte[NativeChunkBridge.CHUNK_VOLUME];

        if (NativeChunkBridge.nativeGenerateChunk(seed, pos.x, pos.z, blocks)) {
            McChunkGen.LOGGER.info(
                "McChunkGen: Generated chunk ({}, {}) via native backend", pos.x, pos.z);
            mcchunkgen$populateChunk(chunk, pos, blocks);
            cir.setResult(chunk);
        }
    }

    @Unique
    private void mcchunkgen$populateChunk(Chunk chunk, ChunkPos pos, byte[] blocks) {
        // Converts native block IDs to Minecraft BlockStates and applies
        // them to the chunk. This requires access to Block.STATE_IDS and
        // the ChunkSection palette system.
        //
        // Block ID mapping:
        //   0 = AIR        1 = STONE     2 = DIRT
        //   3 = GRASS      4 = BEDROCK   5 = WATER
        //
        // Implementation sketch:
        //   for (int x = 0; x < 16; x++)
        //     for (int z = 0; z < 16; z++)
        //       for (int y = 0; y < 256; y++) {
        //         int idx = (z * 16 + x) * 256 + y;
        //         BlockState state = BLOCK_MAP[blocks[idx] & 0xFF];
        //         chunk.setBlockState(new BlockPos(x, y, z), state, false);
        //       }
        McChunkGen.LOGGER.debug("Chunk population stub — implement block mapping");
    }
}