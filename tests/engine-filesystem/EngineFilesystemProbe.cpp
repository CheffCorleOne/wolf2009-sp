// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(WOLF2009_PROBE_RUNNER)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc != 3) {
        fprintf(stderr, "usage: qa-runner /absolute/libdoom3-probe.so /absolute/testroot\n");
        return 2;
    }
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "QA_DLOPEN_FAIL %s\n", dlerror());
        return 3;
    }
    int (*run)(const char*) = (int (*)(const char*))dlsym(library, "Wolf2009_FilesystemProbe");
    if (!run) {
        fprintf(stderr, "QA_ENTRY_FAIL %s\n", dlerror());
        return 4;
    }
    const int result = run(argv[2]);
    printf("QA_RUNNER_RESULT=%d\n", result);
    fflush(NULL);
    _exit(result);
}

#else
// SPDX-License-Identifier: GPL-3.0-or-later
// One-shot actual-core filesystem test; no renderer or game module initialization.
#include "idlib/precompiled.h"
#include "sys/posix/posix_public.h"
#include "framework/Wolf2009Streams.h"
#include <android/native_window.h>
#include "sys/android/sys_android.h"
// Synthetic expected bytes only.
#define QA_LOOSE_SIZE 27
#define QA_LOOSE_CRC 2564727484u
#define QA_STANDARD_SIZE 22
#define QA_STANDARD_CRC 174781362u
#define QA_WOLF_SIZE 18
#define QA_WOLF_CRC 3877160309u
#define QA_NESTED_SIZE 39
#define QA_NESTED_CRC 417189285u

extern "C" void GetIDTechAPI(void* api);

#define QA_CHECK(value, message)                                                                             \
    do {                                                                                                     \
        if (!(value)) {                                                                                      \
            fprintf(stderr, "QA_FAIL %s\n", message);                                                        \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)

static bool ReadMatches(const char* path, int length, unsigned expected) {
    idFile* file = fileSystem->OpenFileRead(path, false);
    if (!file) {
        fprintf(stderr, "QA_OPEN_FAIL %s\n", path);
        return false;
    }
    unsigned crc = 0xffffffffu;
    unsigned char buffer[4096];
    int count, total = 0;
    const bool sizeOk = file->Length() == length;
    while ((count = file->Read(buffer, sizeof(buffer))) > 0) {
        total += count;
        for (int i = 0; i < count; ++i) {
            crc ^= buffer[i];
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ (0xedb88320u & unsigned(-int(crc & 1)));
            }
        }
    }
    const bool seekOk = file->Seek(0, FS_SEEK_SET) == 0 && file->Tell() == 0;
    fileSystem->CloseFile(file);
    printf("QA_READ path=%s bytes=%d crc=%u\n", path, total, ~crc);
    return sizeOk && total == length && ~crc == expected && seekOk;
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_FilesystemProbe(const char* root) {
    printf("QA_SCOPE actual engine filesystem only; renderer and game initialization are not called\n");
    QA_CHECK(root && root[0], "missing test root");
    Q3E_Interface_t api;
    GetIDTechAPI(&api);
    Q3E_InitialContext_t context;
    memset(&context, 0, sizeof(context));
    context.nativeLibraryDir = "";
    context.gameDataDir = root;
    context.appHomeDir = root;
    context.noHandleSignals = 1;
    context.openGL_version = 0x30000;
    api.setInitialContext(&context);
    Posix_EarlyInit();
    idLib::sys = sys;
    idLib::common = common;
    idLib::cvarSystem = cvarSystem;
    idLib::fileSystem = fileSystem;
    idLib::Init();
    cmdSystem->Init();
    cvarSystem->Init();
    idCVar::RegisterStaticVars();
    idKeyInput::Init();
    console->Init();
    cvarSystem->SetCVarString("fs_basepath", root);
    cvarSystem->SetCVarString("fs_savepath", root);
    cvarSystem->SetCVarString("fs_cdpath", root);
    cvarSystem->SetCVarString("fs_devpath", root);
    cvarSystem->SetCVarString("fs_game", "");
    cvarSystem->SetCVarString("fs_game_base", "");
    cvarSystem->SetCVarString("harm_fs_basepath_extras", "");
    cvarSystem->SetCVarString("harm_fs_addon_extras", "");
    cvarSystem->SetCVarString("harm_fs_game_base_extras", "");
    cvarSystem->SetCVarBool("fs_copyfiles", false);
    cvarSystem->SetCVarBool("fs_restrict", false);
    cvarSystem->SetCVarInteger("logFile", 0);
    fileSystem->Init();
    QA_CHECK(fileSystem->IsInitialized() && !common->IsInitialized() && !renderSystem->IsOpenGLRunning() &&
                 game == NULL,
             "unexpected renderer/game startup scope");
    QA_CHECK(ReadMatches("ordinary.txt", QA_LOOSE_SIZE, QA_LOOSE_CRC), "ordinary loose read");
    QA_CHECK(ReadMatches("standard-probe.txt", QA_STANDARD_SIZE, QA_STANDARD_CRC), "ordinary ZIP read");
    QA_CHECK(ReadMatches("wolf-probe.txt", QA_WOLF_SIZE, QA_WOLF_CRC), "custom-footer ZIP read");
    QA_CHECK(Wolf2009_Mount("main", "maps/game/trainyard/trainyard.mpk"), "actual FS main mount");
    QA_CHECK(Wolf2009_Mount("detail", "maps/game/trainyard/trainyard_pak01.spk"), "actual FS detail mount");
    QA_CHECK(Wolf2009_Mount("character", "streampacks/train_engine.spk"), "actual FS character mount");
    const char* detail = "wolf2009/detail/MODL/models/architecture/train_station/station_support_conc.md5r";
    QA_CHECK(ReadMatches(detail, 54457, 4087530177u), "detail resource");
    QA_CHECK(ReadMatches("wolf2009/main/MODL/models/architecture/train_station/station_support_conc.md5r",
                         20508, 3329825089u),
             "explicit main resource");
    QA_CHECK(ReadMatches("wolf2009/character/MODL/models/vehicles/train_engine/train_engine_anim.md5r",
                         1347166, 1800302708u),
             "character resource");
    QA_CHECK(Wolf2009_Mount("zipstandard", "fixtures/standard.spk"), "SPK inside ordinary engine ZIP");
    QA_CHECK(Wolf2009_Mount("zipwolf", "fixtures/wolf.spk"), "SPK inside custom-footer engine ZIP");
    QA_CHECK(ReadMatches("wolf2009/zipstandard/TEST/synthetic/value", QA_NESTED_SIZE, QA_NESTED_CRC),
             "nested standard ZIP idFile adapter");
    QA_CHECK(ReadMatches("wolf2009/zipwolf/TEST/synthetic/value", QA_NESTED_SIZE, QA_NESTED_CRC),
             "nested custom ZIP idFile adapter");
    QA_CHECK(fileSystem->OpenFileRead(detail, false, "other_game") == NULL, "gamedir scope bypass");
    cvarSystem->SetCVarBool("fs_restrict", true);
    QA_CHECK(fileSystem->OpenFileRead(detail, false) == NULL, "restricted-mode bypass");
    cvarSystem->SetCVarBool("fs_restrict", false);
    QA_CHECK(fileSystem->OpenFileWrite(detail) == NULL && fileSystem->OpenFileAppend(detail) == NULL,
             "reserved namespace allowed writes");
    idFile* owned = fileSystem->OpenFileRead(detail, false);
    QA_CHECK(owned != NULL && Wolf2009_Unmount("detail"), "owned lifetime setup");
    int value = 0;
    QA_CHECK(owned->ReadInt(value) == 4 && owned->Tell() == 4, "actual engine ReadInt");
    QA_CHECK(owned->Seek(-1, FS_SEEK_SET) == -1 && owned->Tell() == 4 && owned->Write("x", 1) == 0,
             "read-only resource seek/write");
    fileSystem->CloseFile(owned);
    QA_CHECK(fileSystem->OpenFileRead(detail, false) == NULL, "reserved miss fell through");
    printf("QA_ENGINE_STATE filesystem=1 common_initialized=%d renderer_initialized=%d game_loaded=%d\n",
           common->IsInitialized(), renderSystem->IsOpenGLRunning(), game != NULL);
    fileSystem->Shutdown(false);
    printf("WOLF2009_ACTUAL_ENGINE_FILESYSTEM_QA_OK\n");
    fflush(stdout);
    // Runner exits the one-shot process; no claim of renderer/game lifecycle,
    // and no idLib teardown while the stock FS background thread is still idle.
    return 0;
}

#endif
