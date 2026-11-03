// SPDX-License-Identifier: GPL-3.0-or-later
#include "Wolf2009Bootstrap.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

namespace {
void RuntimeTimeout(int) {
    static const char message[]="RUNTIME_WATCHDOG_TIMEOUT seconds=300\n";
    write(STDERR_FILENO,message,sizeof(message)-1);
    _exit(124);
}
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "usage: runtime-runner /absolute/libdoom3.so /absolute/data /absolute/libgame.so [map]\n");
        return 2;
    }
    signal(SIGALRM,RuntimeTimeout);
    alarm(300);
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "RUNTIME_DLOPEN_FAIL %s\n", dlerror()); return 3; }
    typedef int (*Run)(const char*, const char*, const char*, int, Wolf2009BootstrapResult*);
    Run run = reinterpret_cast<Run>(dlsym(library, "Wolf2009_RunHeadlessRuntime"));
    if (!run) { fprintf(stderr, "RUNTIME_ENTRY_FAIL %s\n", dlerror()); return 4; }
    Wolf2009BootstrapResult result;
    memset(&result, 0, sizeof(result));
    result.structSize = sizeof(result);
    int status = run(argv[2], argv[3], argc == 5 ? argv[4] : "", 8, &result);
    printf("RUNTIME_RUNNER_RESULT=%d common=%d game=%d session=%d map=%d frames=%d game_time=%d->%d game_unloaded=%d fs_shutdown=%d renderer_shutdown=%d\n",
           status, result.commonInitialized, result.gameLoaded, result.sessionWorldsAllocated,
           result.mapLoaded, result.commonFrames, result.gameTimeBefore, result.gameTimeAfter,
           result.gameUnloaded, result.filesystemShutdown, result.rendererShutdown);
    fflush(NULL);
    // Upstream filesystem worker remains owned by the isolated process.
    _exit(status);
}
