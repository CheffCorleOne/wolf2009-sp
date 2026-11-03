// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_IDTECH4A_RENDER_H
#define WOLF2009_IDTECH4A_RENDER_H
// This boundary is C++98-compatible; include after the engine precompiled header.
class idRenderModelStatic;
class idRenderWorldLocal;
class idStr;
void Wolf2009_InitRenderCommands();
void Wolf2009_ShutdownRenderCommands();
bool Wolf2009_IsProcPath(const char* path);
bool Wolf2009_IsStaticModelPath(const char* path);
bool Wolf2009_IsTexturePath(const char* path);
bool Wolf2009_LoadStaticModel(idRenderModelStatic* model, const char* path);
bool Wolf2009_LoadRenderWorld(idRenderWorldLocal* world, const char* path);
bool Wolf2009_LoadMaterialCatalog(const char* relativeArchivePath);
void Wolf2009_ClearRenderCatalog();
// modelName is an original PROC model name (for example _area0 or a submodel).
void Wolf2009_InlineModelName(const char* procPath, const char* modelName, idStr& out);
bool Wolf2009_LoadImage(const char* path, unsigned char** pic, int* width, int* height, ID_TIME_T* timestamp);
#endif
