// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_MAP_H
#define WOLF2009_NATIVE_SOURCE_MAP_H
#include <memory>
#include <string>
class idEntity;class idStr;class WolfSourceDeclarationSet;
struct WolfSourceMapRequest {
    std::string sourceMapName,exactEntities,exactCollision,materialArchive;
    // The first observed binding profile is intentionally selected explicitly.
    // Shared inventory/collision/mover systems do not infer mission support.
    std::string profile;
};
struct WolfSourceMapState {
    unsigned bodies=0,entities=0,instanceBodies=0,resourceBodies=0,sourceMovers=0;
    unsigned collisionShapes=0,unimplementedEntities=0;
    unsigned omittedModulationSurfaces=0,omittedModulationTriangles=0;
    bool spawned=false,sourceSkin=false,retailRuntimeFilters=false;
};
class WolfSourceMapRuntime {
  public:
    ~WolfSourceMapRuntime();
    bool Spawn(idStr&);
    bool Query(WolfSourceMapState&,idStr&)const;
    idEntity* PrimaryMover()const;
  private:
    struct Impl;std::unique_ptr<Impl> impl;
    explicit WolfSourceMapRuntime(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceMapRuntime> Wolf2009_PrepareSourceMapRuntime(const WolfSourceDeclarationSet&,const WolfSourceMapRequest&,idStr&);
};
std::unique_ptr<WolfSourceMapRuntime> Wolf2009_PrepareSourceMapRuntime(const WolfSourceDeclarationSet&,const WolfSourceMapRequest&,idStr&);
#endif
