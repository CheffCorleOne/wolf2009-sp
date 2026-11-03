// SPDX-License-Identifier: GPL-3.0-or-later
// This entry calls the same common Init/Frame/Shutdown used by Android main.cpp.
// net_serverDedicated=1 is the engine's existing way to omit GL/audio hardware.
#include "idlib/precompiled.h"
#include "sys/posix/posix_public.h"
#include "sys/android/sys_android.h"
#include "Wolf2009Bootstrap.h"
#ifdef WOLF2009_GAMEPLAY
#include "GameplayRuntimeProbe.h"
#include "EngineGameplay.h"
#include "framework/Wolf2009Streams.h"
#endif
#include <stdarg.h>
#include <unistd.h>

extern "C" void GetIDTechAPI(void* api);
extern void Q3E_Start(void);
extern void Q3E_End(void);
#ifdef WOLF2009_RUNTIME_RENDER_QA
extern bool Wolf2009_RunRenderChecks();
#endif
#ifdef WOLF2009_RUNTIME_GL_QA
extern bool Wolf2009_RunOffscreenChecks();
#endif

namespace {
class HostLifetime {
    Wolf2009BootstrapResult* result;
    bool active;
public:
    explicit HostLifetime(Wolf2009BootstrapResult* value) : result(value), active(true) {}
    ~HostLifetime() { Close(); }
    void FailedRendererTransition() {
        // A failed upstream InitOpenGL can leave partially constructed FBOs;
        // its normal renderer Shutdown is not valid in that state. Report a
        // failed run and let the required isolated-process exit release it.
        if (asyncThread.threadHandle) Sys_DestroyThread(asyncThread);
        active = false;
        fprintf(stderr, "WOLF2009_RUNTIME_PARTIAL_RENDERER_INIT process_exit_required=1\n");
    }
    void Close() {
        if (!active) return;
        active = false;
        // The stock FS worker is process-owned. Stop the normal async clock
        // before idLib teardown, including when an optional check fails.
        if (asyncThread.threadHandle) Sys_DestroyThread(asyncThread);
        if (common->IsInitialized()) common->Shutdown();
        result->gameUnloaded = game == NULL && gameEdit == NULL;
#ifdef WOLF2009_GAMEPLAY
        result->gameUnloaded = Wolf2009_CheckNativeCollisionFixtureReleased() && result->gameUnloaded;
#endif
        result->filesystemShutdown = !fileSystem->IsInitialized();
        result->rendererShutdown = !renderSystem->IsOpenGLRunning();
        Q3E_End();
        printf("WOLF2009_RUNTIME_SHUTDOWN game_unloaded=%d filesystem=%d renderer=%d\n",
               result->gameUnloaded, result->filesystemShutdown, result->rendererShutdown);
    }
};
void IgnoreInt(int) {}
void IgnoreVoid() {}
void IgnoreString(const char*) {}
void IgnoreAudio(void*, int) {}
int NoAudio(int, int) { return 0; }
int NoInput(int) { return 0; }
char* NoClipboard() { return NULL; }
FILE* PrivateTmpfile() { return tmpfile(); }
int RejectDialog(const char*, const char* message, int, const char*[]) {
    fprintf(stderr, "WOLF2009_RUNTIME_DIALOG %s\n", message ? message : "");
    return -1;
}
void Log(const char* tag, const char* format, ...) {
    // Upstream passes an already formatted string as the format argument.
    // Do not interpret '%' from engine/resource diagnostic text a second time.
    fprintf(stderr, "%s: %s\n", tag ? tag : "idTech4", format ? format : "");
}
bool SafeMapName(const char* name) {
    if (!name || !name[0]) return true;
    int length = 0;
    for (const char* p = name; *p; ++p, ++length) {
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '/')) return false;
    }
    return length < 180 && name[0] != '/' && name[length - 1] != '/';
}
void RunCommand(const char* command, const char* argument = NULL) {
    idCmdArgs args;
    args.AppendArg(command);
    if (argument) args.AppendArg(argument);
    cmdSystem->BufferCommandArgs(CMD_EXEC_NOW, args);
    cmdSystem->ExecuteCommandBuffer();
}
bool TypedMapName(const char* name) {
#ifdef WOLF2009_GAMEPLAY
    idStr path(name);
    path += ".map";
    return Wolf2009_IsGameplayMapPath(path.c_str());
#else
    return false;
#endif
}
#ifdef WOLF2009_GAMEPLAY
bool MountTypedMap(const char* name) {
    if (!TypedMapName(name)) return true;
    idStr path(name);
    const int labelEnd = path.Find('/', 9);
    const idStr label = path.Mid(9, labelEnd - 9);
    idStr package = path.Mid(labelEnd + 6, path.Length() - (labelEnd + 6));
    package += ".mpk";
    const bool mounted = Wolf2009_Mount(label.c_str(), package.c_str());
    printf("WOLF2009_RUNTIME_MAP_MOUNT label=%s package=%s mounted=%d\n",
           label.c_str(), package.c_str(), mounted);
    // The mount remains owned by the filesystem through game/world teardown.
    return mounted;
}
bool CheckRealEmptyPlayer() {
    idEntity* player = gameEdit->FindEntity("player1");
    if (!player || !gameEdit->PlayerIsValid()) return false;
    const idDict* args = gameEdit->EntityGetSpawnArgs(player);
    if (!args || idStr::Icmp(args->GetString("spawnclass"), "idPlayer")) return false;
    usercmd_t commands[MAX_ASYNC_CLIENTS];
    memset(commands, 0, sizeof(commands));
    for (int i = 0; i < 30; ++i) game->RunFrame(commands);
    idVec3 start, end;
    gameEdit->EntityGetOrigin(player, start);
    commands[0].forwardmove = 127;
    commands[0].buttons = BUTTON_RUN;
    for (int i = 0; i < 240; ++i) game->RunFrame(commands);
    gameEdit->EntityGetOrigin(player, end);
    // HKXS's original empty room has its inside plane at X184. The explicit
    // Doom3 baseline player is 32 units wide; the engine adds its clip epsilon.
    const bool stopped = idMath::Fabs(start.x) < .5f && idMath::Fabs(start.y) < .5f &&
        end.x > 167 && end.x < 169 && idMath::Fabs(end.y - start.y) < .5f &&
        end.z > -.5f && end.z < 1;
    printf("WOLF2009_NATIVE_REAL_EMPTY player=idPlayer half_width=16 wall_plane_x=184 "
           "start=(%.4f,%.4f,%.4f) stopped=(%.4f,%.4f,%.4f) valid=%d\n",
           start.x, start.y, start.z, end.x, end.y, end.z, stopped);
    if (!stopped) return false;
    printf("WOLF2009_ACTUAL_REAL_EMPTY_GAMEPLAY_QA_OK\n");
    return true;
}
#endif
} // namespace

extern "C" __attribute__((visibility("default")))
int Wolf2009_RunHeadlessRuntime(const char* dataRoot, const char* gameLibrary,
                               const char* mapName, int frames,
                               Wolf2009BootstrapResult* result) {
    static bool used = false;
    if (used || !result || result->structSize != sizeof(*result) ||
        !dataRoot || dataRoot[0] != '/' || !gameLibrary || gameLibrary[0] != '/' ||
        !SafeMapName(mapName) || frames < 1 || frames > 600) return 2;
    used = true;
    memset(result, 0, sizeof(*result));
    result->structSize = sizeof(*result);

    Q3E_Interface_t api;
    GetIDTechAPI(&api);
    Q3E_Callback_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.AudioTrack_init = IgnoreAudio;
    callbacks.AudioTrack_write = NoAudio;
    callbacks.AudioTrack_shutdown = IgnoreVoid;
    callbacks.Input_grabMouse = IgnoreInt;
    callbacks.Input_pullEvent = NoInput;
    callbacks.Input_setupSmoothJoystick = IgnoreInt;
    callbacks.Sys_attachThread = IgnoreVoid;
    callbacks.Sys_tmpfile = PrivateTmpfile;
    callbacks.Sys_copyToClipboard = IgnoreString;
    callbacks.Sys_getClipboardText = NoClipboard;
    callbacks.Sys_openKeyboard = IgnoreVoid;
    callbacks.Sys_closeKeyboard = IgnoreVoid;
    callbacks.Sys_openURL = IgnoreString;
    callbacks.Sys_exitFinish = IgnoreVoid;
    callbacks.Sys_showCursor = IgnoreInt;
    callbacks.Gui_ShowToast = IgnoreString;
    callbacks.Gui_openDialog = RejectDialog;
    callbacks.set_state = IgnoreInt;
    callbacks.Log_Print = Log;
    api.setCallbacks(&callbacks);

    Q3E_InitialContext_t context;
    memset(&context, 0, sizeof(context));
    context.nativeLibraryDir = "";
    context.gameDataDir = dataRoot;
    context.appHomeDir = dataRoot;
    context.noHandleSignals = 1;
    context.openGL_version = 0x30000;
    api.setInitialContext(&context);
    if (chdir(dataRoot) != 0) return 3;
    Q3E_Start();
    Posix_EarlyInit();
    HostLifetime lifetime(result);

    const char* argv[] = {
        "+set", "net_serverDedicated", "1",
        "+set", "net_ip", "127.0.0.1",
        "+set", "net_port", "0",
        "+set", "fs_basepath", dataRoot,
        "+set", "fs_savepath", dataRoot,
        "+set", "fs_cdpath", dataRoot,
        "+set", "fs_devpath", dataRoot,
        "+set", "fs_restrict", "0",
        "+set", "fs_game", "",
        "+set", "harm_fs_gameLibPath", gameLibrary,
        "+set", "harm_com_consoleHistory", "0",
        "+set", "s_noSound", "1",
        "+set", "harm_r_openglVersion", "GLES3.0",
        "+set", "r_multithread", "0",
        "+set", "in_tty", "0",
        "+set", "logFile", "0",
        "+echo", "wolf2009_headless_runtime"
    };
    printf("WOLF2009_RUNTIME_SCOPE common->Init GPL_Doom3_GetGameAPI_%d headless=1\n", GAME_API_VERSION);
    common->Init(sizeof(argv) / sizeof(argv[0]), argv, NULL);
    result->commonInitialized = common->IsInitialized();
    result->gameLoaded = game != NULL && gameEdit != NULL;
    result->sessionWorldsAllocated = session->rw != NULL && session->sw != NULL;
    if (!result->commonInitialized || !result->gameLoaded ||
        !result->sessionWorldsAllocated || renderSystem->IsOpenGLRunning()) return 4;

    bool offscreenTransition = false;
    try {
    Posix_LateInit();
    if (mapName && mapName[0]) {
#ifdef WOLF2009_GAMEPLAY
        if (!MountTypedMap(mapName)) return 10;
#endif
        RunCommand("devmap", mapName);
        idStr expected(TypedMapName(mapName) ? "" : "maps/");
        expected += mapName;
        result->mapLoaded = !idStr::Icmp(session->GetCurrentMapName(), expected.c_str()) &&
                            gameEdit->PlayerIsValid();
        if (!result->mapLoaded) return 5;
        RunCommand("listEntities");
#ifdef WOLF2009_GAMEPLAY
        if (!idStr::Icmp(mapName, "wolf2009/runtime") && !Wolf2009_RunGameplayFixtureProbe()) return 8;
        if (!idStr::Icmp(mapName, "wolf2009/main/PROC/maps/game/empty/empty") &&
            !CheckRealEmptyPlayer()) return 11;
#endif
    }
    result->gameTimeBefore = game->GetTimeGroupTime(TIME_GROUP1);
    for (int frame = 0; frame < frames; ++frame) {
        common->Frame();
        ++result->commonFrames;
    }
    result->gameTimeAfter = game->GetTimeGroupTime(TIME_GROUP1);
    printf("WOLF2009_RUNTIME_STATE common=%d game=%d session=%d map=%d frames=%d gl=%d\n",
           result->commonInitialized, result->gameLoaded, result->sessionWorldsAllocated,
           result->mapLoaded, result->commonFrames, renderSystem->IsOpenGLRunning());
    printf("WOLF2009_RUNTIME_GAME_TIME before=%d after=%d\n", result->gameTimeBefore, result->gameTimeAfter);
    if (result->mapLoaded && result->gameTimeAfter <= result->gameTimeBefore) return 7;

    idFile* renderFlag = fileSystem->OpenFileRead("fixtures/render-checks.enabled", false);
    if (renderFlag) {
        fileSystem->CloseFile(renderFlag);
#ifdef WOLF2009_RUNTIME_RENDER_QA
        if (!Wolf2009_RunRenderChecks()) return 9;
#else
        fprintf(stderr, "WOLF2009_RUNTIME_FAIL renderer QA requested but not compiled\n");
        return 9;
#endif
    }
    idFile* offscreenFlag = fileSystem->OpenFileRead("fixtures/offscreen-checks.enabled", false);
    if (offscreenFlag) {
        fileSystem->CloseFile(offscreenFlag);
#ifdef WOLF2009_RUNTIME_GL_QA
        offscreenTransition = true;
        if (!Wolf2009_RunOffscreenChecks()) return 12;
        offscreenTransition = false;
#else
        fprintf(stderr, "WOLF2009_RUNTIME_FAIL offscreen QA requested but not compiled\n");
        return 12;
#endif
    }

    // The caller must exit its isolated process instead of dlclose/restart.
    lifetime.Close();
    if (!result->gameUnloaded || !result->filesystemShutdown || !result->rendererShutdown) return 6;
    printf("WOLF2009_ACTUAL_ENGINE_RUNTIME_QA_OK\n");
    fflush(stdout);
    return 0;
    } catch (idException& failure) {
        fprintf(stderr, "WOLF2009_RUNTIME_EXCEPTION %s\n", failure.error);
        if (offscreenTransition) lifetime.FailedRendererTransition();
        return 13;
    }
}
