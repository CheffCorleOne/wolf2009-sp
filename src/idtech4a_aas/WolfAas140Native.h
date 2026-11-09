// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_AAS140_NATIVE_H
#define WOLF2009_AAS140_NATIVE_H
// Core-side adapter: converts a Wolfenstein (2009) AASS member (DewmAAS 1.40)
// into the pinned engine's idAASFileLocal for the ordinary idAASFileManager.
//
// Requests named `wolf2009/<label>/PROC/<map>.aas28` are reserved: they are
// served from the typed resource `wolf2009/<label>/AASS/<map>` under the
// profile selected by the `wolf2009_aasProfile` cvar, or refused. They never
// fall through to the Doom 3 text loader. Only `WolfAas140V1` is admitted.
#include <string>
#include <utility>
#include <vector>
class idAASFile;

// Called by the patched idAASFileManagerLocal (C++98 boundary).
bool Wolf2009_IsAas140Request(const char *fileName);
idAASFile *Wolf2009_LoadAas140(const char *fileName, unsigned int mapFileCRC);
void Wolf2009_ReleaseAas140(idAASFile *file);

// Observations for native QA. They describe the converted file; they do not
// alter it.
struct Wolf2009Aas140Info {
    std::string name, profile;
    unsigned fileCrc = 0, mapCrc = 0;
    unsigned areas = 0, reachabilities = 0, linked = 0, linkedCrossCluster = 0;
    unsigned droppedArea0 = 0, droppedPortalBorder = 0, unboundedReachable = 0, wallWindings = 0;
};
bool Wolf2009_Aas140Lookup(const char *name, const idAASFile *&file, Wolf2009Aas140Info &info);
// Location numbers and names listed by an area (raw locationIndex order).
bool Wolf2009_Aas140AreaLocations(const idAASFile *file, int area, std::vector<std::pair<int, std::string> > &out);
void Wolf2009_Aas140Counters(unsigned &live, unsigned &loads, unsigned &frees, unsigned &refusals);
#endif
