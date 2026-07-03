//=============================================================================
// jni_bridge.c — JNI bridge between Minecraft (Java) and the native chunk
// generator.  Compiled into libmcchunkgen.so.
//=============================================================================
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../../generator.h"

// Java class: io.openhands.mcchunkgen.NativeChunkBridge
#define JNI_CLASS "io/openhands/mcchunkgen/NativeChunkBridge"

//=============================================================================
// Helper: convert our block IDs to Minecraft block state strings
//=============================================================================
static const char* block_to_mc_block(unsigned char block) {
    switch (block) {
        case BLOCK_AIR:     return "minecraft:air";
        case BLOCK_STONE:   return "minecraft:stone";
        case BLOCK_DIRT:    return "minecraft:dirt";
        case BLOCK_GRASS:   return "minecraft:grass_block";
        case BLOCK_BEDROCK: return "minecraft:bedrock";
        case BLOCK_WATER:   return "minecraft:water";
        default:            return "minecraft:air";
    }
}

//=============================================================================
// JNI: public static native boolean nativeGenerateChunk(
//     long seed, int chunkX, int chunkZ, byte[] outBlocks);
//
// Generates one chunk into the provided byte array (must be CHUNK_VOLUME).
// Returns true on success.
//=============================================================================
JNIEXPORT jboolean JNICALL
Java_io_openhands_mcchunkgen_NativeChunkBridge_nativeGenerateChunk(
    JNIEnv* env, jclass cls, jlong seed, jint chunk_x, jint chunk_z,
    jbyteArray out_blocks)
{
    jsize len = (*env)->GetArrayLength(env, out_blocks);
    if (len < (jsize)CHUNK_VOLUME) {
        return JNI_FALSE;
    }

    // Allocate a device/host buffer and generate
    block_t* blocks = (block_t*)malloc(CHUNK_VOLUME);
    if (!blocks) return JNI_FALSE;

    ChunkBuffer buf = { blocks, CHUNK_VOLUME };
    launch_chunk_generator(buf, (int64_t)chunk_x, (int64_t)chunk_z, (int64_t)seed, nullptr);
    cudaDeviceSynchronize();

    // Copy into the Java byte array
    jbyte* jbytes = (*env)->GetPrimitiveArrayCritical(env, out_blocks, NULL);
    if (jbytes) {
        memcpy(jbytes, blocks, CHUNK_VOLUME);
        (*env)->ReleasePrimitiveArrayCritical(env, out_blocks, jbytes, 0);
    }

    free(blocks);
    return JNI_TRUE;
}

//=============================================================================
// JNI: public static native String nativeGetBlockAt(
//     long seed, int chunkX, int chunkZ, int x, int y, int z);
//
// Returns the block type at a specific world block coordinate within a chunk.
// Useful for debugging / single-block queries.
//=============================================================================
JNIEXPORT jstring JNICALL
Java_io_openhands_mcchunkgen_NativeChunkBridge_nativeGetBlockAt(
    JNIEnv* env, jclass cls, jlong seed, jint chunk_x, jint chunk_z,
    jint x, jint y, jint z)
{
    block_t* blocks = (block_t*)malloc(CHUNK_VOLUME);
    if (!blocks) return NULL;

    ChunkBuffer buf = { blocks, CHUNK_VOLUME };
    launch_chunk_generator(buf, (int64_t)chunk_x, (int64_t)chunk_z, (int64_t)seed, nullptr);
    cudaDeviceSynchronize();

    unsigned int idx = buf.index((unsigned int)x, (unsigned int)y, (unsigned int)z);
    const char* mc_block = block_to_mc_block(blocks[idx]);

    free(blocks);
    return (*env)->NewStringUTF(env, mc_block);
}

//=============================================================================
// JNI: public static native boolean nativeGenerateBatch(
//     long seed, int[] chunkXs, int[] chunkZs, byte[][] outBlocks);
//
// Batch-generates multiple chunks.  In GPU mode this will pipeline them.
//=============================================================================
JNIEXPORT jboolean JNICALL
Java_io_openhands_mcchunkgen_NativeChunkBridge_nativeGenerateBatch(
    JNIEnv* env, jclass cls, jlong seed, jintArray chunk_xs, jintArray chunk_zs,
    jobjectArray out_blocks_arr)
{
    jsize n = (*env)->GetArrayLength(env, chunk_xs);
    if (n == 0) return JNI_TRUE;

    jint* cx = (*env)->GetIntArrayElements(env, chunk_xs, NULL);
    jint* cz = (*env)->GetIntArrayElements(env, chunk_zs, NULL);
    if (!cx || !cz) {
        if (cx) (*env)->ReleaseIntArrayElements(env, chunk_xs, cx, 0);
        return JNI_FALSE;
    }

    // Process sequentially (the pipeline inside launch_chunk_generator
    // handles async transfers internally)
    for (jsize i = 0; i < n; ++i) {
        jbyteArray jba = (*env)->GetObjectArrayElement(env, out_blocks_arr, i);
        if (!jba) continue;

        jbyte* jbytes = (*env)->GetPrimitiveArrayCritical(env, jba, NULL);
        if (jbytes) {
            block_t* blocks = (block_t*)malloc(CHUNK_VOLUME);
            if (blocks) {
                ChunkBuffer buf = { blocks, CHUNK_VOLUME };
                launch_chunk_generator(buf, (int64_t)cx[i], (int64_t)cz[i], (int64_t)seed, nullptr);
                cudaDeviceSynchronize();
                memcpy(jbytes, blocks, CHUNK_VOLUME);
                free(blocks);
            }
            (*env)->ReleasePrimitiveArrayCritical(env, jba, jbytes, 0);
        }
        (*env)->DeleteLocalRef(env, jba);
    }

    (*env)->ReleaseIntArrayElements(env, chunk_xs, cx, 0);
    (*env)->ReleaseIntArrayElements(env, chunk_zs, cz, 0);
    return JNI_TRUE;
}