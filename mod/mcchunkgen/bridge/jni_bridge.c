//=============================================================================
// jni_bridge.c — JNI bridge between Minecraft (Java) and the native chunk
// generator.  Compiled into libmcchunkgen.so.
//
// C++ compilation: JDK jni.h defines JNIEnv as a class, so use env->Method().
//=============================================================================
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../../generator.h"

// Java class
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
//=============================================================================
extern "C" JNIEXPORT jboolean JNICALL
Java_io_openhands_mcchunkgen_NativeChunkBridge_nativeGenerateChunk(
    JNIEnv* env, jclass cls, jlong seed, jint chunk_x, jint chunk_z,
    jbyteArray out_blocks)
{
    jsize len = env->GetArrayLength(out_blocks);
    if (len < (jsize)CHUNK_VOLUME) return JNI_FALSE;

    block_t* blocks = (block_t*)malloc(CHUNK_VOLUME);
    if (!blocks) return JNI_FALSE;

    ChunkBuffer buf = { blocks, CHUNK_VOLUME };
    launch_chunk_generator(buf, (int64_t)chunk_x, (int64_t)chunk_z, (int64_t)seed, nullptr);
    cudaDeviceSynchronize();

    jbyte* jbytes = (jbyte*)env->GetPrimitiveArrayCritical(out_blocks, NULL);
    if (jbytes) {
        memcpy(jbytes, blocks, CHUNK_VOLUME);
        env->ReleasePrimitiveArrayCritical(out_blocks, jbytes, 0);
    }

    free(blocks);
    return JNI_TRUE;
}

//=============================================================================
// JNI: public static native String nativeGetBlockAt(...)
//=============================================================================
extern "C" JNIEXPORT jstring JNICALL
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
    return env->NewStringUTF(mc_block);
}

//=============================================================================
// JNI: public static native boolean nativeGenerateBatch(
//     long seed, int[] chunkXs, int[] chunkZs, byte[][] outBlocks);
//=============================================================================
extern "C" JNIEXPORT jboolean JNICALL
Java_io_openhands_mcchunkgen_NativeChunkBridge_nativeGenerateBatch(
    JNIEnv* env, jclass cls, jlong seed, jintArray chunk_xs, jintArray chunk_zs,
    jobjectArray out_blocks_arr)
{
    jsize n = env->GetArrayLength(chunk_xs);
    if (n == 0) return JNI_TRUE;

    jint* cx = env->GetIntArrayElements(chunk_xs, NULL);
    jint* cz = env->GetIntArrayElements(chunk_zs, NULL);
    if (!cx || !cz) {
        if (cx) env->ReleaseIntArrayElements(chunk_xs, cx, 0);
        return JNI_FALSE;
    }

    for (jsize i = 0; i < n; ++i) {
        jbyteArray jba = (jbyteArray)env->GetObjectArrayElement(out_blocks_arr, i);
        if (!jba) continue;

        jbyte* jbytes = (jbyte*)env->GetPrimitiveArrayCritical(jba, NULL);
        if (jbytes) {
            block_t* blocks = (block_t*)malloc(CHUNK_VOLUME);
            if (blocks) {
                ChunkBuffer buf = { blocks, CHUNK_VOLUME };
                launch_chunk_generator(buf, (int64_t)cx[i], (int64_t)cz[i], (int64_t)seed, nullptr);
                cudaDeviceSynchronize();
                memcpy(jbytes, blocks, CHUNK_VOLUME);
                free(blocks);
            }
            env->ReleasePrimitiveArrayCritical(jba, jbytes, 0);
        }
        env->DeleteLocalRef(jba);
    }

    env->ReleaseIntArrayElements(chunk_xs, cx, 0);
    env->ReleaseIntArrayElements(chunk_zs, cz, 0);
    return JNI_TRUE;
}