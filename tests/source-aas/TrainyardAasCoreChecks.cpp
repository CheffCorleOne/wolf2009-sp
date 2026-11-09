// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <dlfcn.h>
#include "WolfAas140Native.h"
#include "idlib/precompiled.h"
#include "tools/compilers/aas/AASFile.h"
#include "TrainyardAasCoreChecks.h"

namespace {
const char kTrainyardAas[] = "wolf2009/main/proc/maps/game/trainyard/trainyard.aas28";

bool Refuse(const char* reason) {
    fprintf(stderr, "WOLF2009_TRAINYARD_AAS_QA_FAIL reason=%s\n", reason);
    return false;
}
}

bool Wolf2009_TrainyardAasCheckRequested() {
    idFile* flag = fileSystem->OpenFileRead("fixtures/trainyard-aas.enabled", false);
    if (!flag) return false;
    fileSystem->CloseFile(flag);
    return true;
}

bool Wolf2009_CheckTrainyardAasQA(const char* gameLibrary, unsigned expectedLoads) {
    const bool expectedLoaded = expectedLoads != 0;
    unsigned live, loads, frees, refusals;
    Wolf2009_Aas140Counters(live, loads, frees, refusals);
    if (live != unsigned(expectedLoaded) || loads != expectedLoads ||
        frees != (expectedLoaded ? expectedLoads - 1 : 0) || refusals != 1)
        return Refuse("AAS manager counts do not match refusal, load or reload");

    const idAASFile* file = nullptr;
    Wolf2009Aas140Info info;
    if (Wolf2009_Aas140Lookup(kTrainyardAas, file, info) != expectedLoaded)
        return Refuse("AAS registry does not match the requested profile");
    if (expectedLoaded) {
        if (info.profile != "WolfAas140V1" || info.fileCrc != 1 || info.mapCrc != 0)
            return Refuse("AAS profile or CRC policy differs");
        std::vector<std::pair<int, std::string> > locations;
        const auto trapDoor = std::make_pair(26, std::string("trap_door_room"));
        if (!Wolf2009_Aas140AreaLocations(file, 8040, locations) ||
            std::find(locations.begin(), locations.end(), trapDoor) == locations.end())
            return Refuse("area 8040 has no trap_door_room location 26");
    }

    void* handle = dlopen(gameLibrary, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) return Refuse("game module is not loaded");
    using Entry = int (*)(int);
    const auto entry = reinterpret_cast<Entry>(dlsym(handle, "Wolf2009_CheckTrainyardAasQA"));
    const bool ok = entry && entry(expectedLoaded ? 1 : 0) == 1;
    dlclose(handle);
    if (!ok) return Refuse("game-side route or unprofiled check failed");
    printf("WOLF2009_TRAINYARD_AAS_QA_PHASE loads=%u frees=%u refusals=%u live=%u location26=%d\n",
           loads, frees, refusals, live, int(expectedLoaded));
    return true;
}

bool Wolf2009_FinishTrainyardAasQA() {
    unsigned live, loads, frees, refusals;
    Wolf2009_Aas140Counters(live, loads, frees, refusals);
    if (live != 0 || loads != 2 || frees != 2 || refusals != 1)
        return Refuse("ordinary engine shutdown did not release both AAS generations");
    printf("WOLF2009_TRAINYARD_AAS_QA_OK loads=2 frees=2 refusals=1 live=0\n");
    return true;
}
