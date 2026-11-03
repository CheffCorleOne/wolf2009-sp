// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BOOTSTRAP_H
#define WOLF2009_BOOTSTRAP_H

// A bounded, headless Android host for the normal GPL Doom3 lifecycle.
// Run once in a new process. It supplies no native window or retail code.
struct Wolf2009BootstrapResult {
    unsigned structSize;
    int commonInitialized;
    int gameLoaded;
    int sessionWorldsAllocated;
    int mapLoaded;
    int commonFrames;
    int gameTimeBefore;
    int gameTimeAfter;
    int gameUnloaded;
    int filesystemShutdown;
    int rendererShutdown;
};

extern "C" int Wolf2009_RunHeadlessRuntime(const char* dataRoot, const char* gameLibrary,
                                         const char* mapName, int frames,
                                         Wolf2009BootstrapResult* result);
#endif
