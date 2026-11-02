// SPDX-License-Identifier: GPL-3.0-or-later
package org.wolf2009.viewer;

final class NativeBridge {
    static { System.loadLibrary("wolf2009-viewer"); }
    private NativeBridge() {}
    static native String initialize();
    static native String loadMpk(String privateReadOnlyCopy);
    static native String loadMaterials(String privateReadOnlyCopy);
    static native String demo();
    static native String draw(int width, int height);
    static native void gesture(float yaw, float pitch, float zoom, float panX, float panY);
    static native void move(float right, float forward);
    static native void camera(boolean flight);
    static native void reset();
    static native void requestStatus();
}
