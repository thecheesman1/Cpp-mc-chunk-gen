package io.openhands.mcchunkgen;

/**
 * Control interface for enabling/disabling native chunk generation.
 * Separate from the mixin class because mixins cannot have public static methods.
 */
public final class NativeChunkControl {
    private static long activeSeed = 0;
    private static boolean enabled = false;

    public static void setActiveSeed(long seed) { activeSeed = seed; }
    public static long getActiveSeed() { return activeSeed; }
    public static boolean isEnabled() { return enabled; }
    public static void setEnabled(boolean e) { enabled = e; }
}