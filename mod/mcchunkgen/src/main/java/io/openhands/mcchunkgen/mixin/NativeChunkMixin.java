package io.openhands.mcchunkgen.mixin;

import io.openhands.mcchunkgen.NativeChunkBridge;
import io.openhands.mcchunkgen.NativeChunkControl;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.ChunkSection;
import net.minecraft.world.chunk.PalettedContainer;
import net.minecraft.world.gen.StructureAccessor;
import net.minecraft.world.gen.chunk.Blender;
import net.minecraft.world.gen.chunk.NoiseChunkGenerator;
import net.minecraft.world.gen.noise.NoiseConfig;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import java.util.concurrent.CompletableFuture;

@Mixin(NoiseChunkGenerator.class)
public class NativeChunkMixin {
    @Unique
    private static final BlockState[] PALETTE = new BlockState[256];
    static {
        PALETTE[0]  = Blocks.AIR.getDefaultState();
        PALETTE[1]  = Blocks.STONE.getDefaultState();
        PALETTE[2]  = Blocks.GRASS_BLOCK.getDefaultState();
        PALETTE[3]  = Blocks.DIRT.getDefaultState();
        PALETTE[4]  = Blocks.WATER.getDefaultState();
        PALETTE[5]  = Blocks.SAND.getDefaultState();
        PALETTE[6]  = Blocks.GRAVEL.getDefaultState();
        PALETTE[7]  = Blocks.SNOW_BLOCK.getDefaultState();
        PALETTE[8]  = Blocks.COBBLESTONE.getDefaultState();
        PALETTE[9]  = Blocks.BEDROCK.getDefaultState();
        PALETTE[10] = Blocks.COAL_ORE.getDefaultState();
        PALETTE[11] = Blocks.IRON_ORE.getDefaultState();
        PALETTE[12] = Blocks.GOLD_ORE.getDefaultState();
        PALETTE[13] = Blocks.DIAMOND_ORE.getDefaultState();
        PALETTE[14] = Blocks.OAK_LOG.getDefaultState();
        PALETTE[15] = Blocks.OAK_LEAVES.getDefaultState();
        for (int i = 16; i < 256; i++) {
            PALETTE[i] = Blocks.STONE.getDefaultState();
        }
    }

    @Inject(method = "populateNoise", at = @At("HEAD"), cancellable = true)
    private void onPopulateNoise(Blender blender, NoiseConfig noiseConfig,
                                 StructureAccessor structureAccessor,
                                 Chunk chunk, CallbackInfoReturnable<CompletableFuture<Chunk>> cir) {
        if (!NativeChunkControl.isEnabled()) return;
        cir.setReturnValue(CompletableFuture.completedFuture(injectNativeTerrain(chunk)));
    }

    @Unique
    private Chunk injectNativeTerrain(Chunk chunk) {
        byte[] blockData = new byte[16 * 256 * 16];
        if (!NativeChunkBridge.nativeGenerateChunk(NativeChunkControl.getActiveSeed(), chunk.getPos().x, chunk.getPos().z, blockData)) {
            return chunk;
        }

        int minY = chunk.getBottomY();
        ChunkSection[] sections = chunk.getSectionArray();

        // Group native Y levels (0..255, worldY=0..255) by section.
        // Section index = (worldY - minY) >> 4.
        // Native 0..255 covers sections: sectionIdxStart..sectionIdxEnd
        int sec0 = (0 - minY) >> 4;
        int sec1 = (255 - minY) >> 4;

        for (int si = sec0; si <= sec1 && si < sections.length; si++) {
            ChunkSection section = sections[si];
            if (section == null) continue;

            int sectionBaseWorldY = minY + (si << 4);
            int localYStart = 0 - sectionBaseWorldY;        // could be negative
            int localYEnd = Math.min(15, 255 - sectionBaseWorldY); // cap at native 255

            if (localYStart > 15 || localYEnd < 0) continue;

            PalettedContainer<BlockState> container = section.getBlockStateContainer();
            container.lock();
            try {
                for (int ly = localYStart; ly <= localYEnd; ly++) {
                    if (ly < 0) continue;
                    int ny = sectionBaseWorldY + ly; // native Y level
                    if (ny < 0 || ny > 255) continue;

                    for (int x = 0; x < 16; x++) {
                        for (int z = 0; z < 16; z++) {
                            int idx = (ny * 16 + z) * 16 + x;
                            int byteVal = blockData[idx] & 0xFF;
                            BlockState state = PALETTE[byteVal % PALETTE.length];
                            container.set(x, ly, z, state);
                        }
                    }
                }
            } finally {
                container.unlock();
            }
        }

        return chunk;
    }
}
