// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.engine;

public final class EngineNative {
    static { System.loadLibrary("wolf2009-enginehost"); }
    private EngineNative() {}
    // Start once per :engine process. The native engine owns EGL and its loop.
    static native String start(String libraries, String data, String save,
                               android.view.Surface surface, int width, int height,
                               int refreshRate, boolean trainyard, EngineCallbacks callbacks);
    static native void surface(android.view.Surface surface);
    public static native void pause();
    public static native void resume();
    public static native void requestQuit();
    public static native void clearInput();
    public static native void key(boolean down, int key, int character);
    public static native void analog(boolean enabled, float right, float forward);
    public static native void motion(float x, float y);
    // Native dispatch counters and state, suitable for actual-APK instrumentation.
    public static native long[] stats();
    public static native void queryView();
    public static native double[] view();
}
