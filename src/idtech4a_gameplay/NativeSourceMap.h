// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_MAP_H
#define WOLF2009_NATIVE_SOURCE_MAP_H
#include <memory>
#include <string>
class idEntity;class idStr;class WolfSourceDeclarationSet;class WolfSourceDirector;class WolfSourceGreeterRoute;class WolfSourceTrainyardExit;
class WolfSourceAIShell;class WolfSourcePickups;class WolfSourceRotatingDoor;
struct WolfSourceMapRequest {
    std::string sourceMapName,exactEntities,exactCollision,materialArchive;
    // The first observed binding profile is intentionally selected explicitly.
    // Shared inventory/collision/mover systems do not infer mission support.
    std::string profile;
    // Empty preserves the boot-only checkpoint. The extra ordinary gameplay
    // prefix must be selected explicitly by the source-session owner.
    std::string arrivalProfile;
    // Empty keeps the greeter/door absent. MeasuredTrainyardGreeterRouteV1 also
    // requires the arrival profile and explicit greeter detail/global resources.
    std::string greeterProfile,greeterModel,greeterRig,greeterVoiceLocale;
    // The shell and the earlier greeter/door route own the same greeter name.
    std::string aiProfile;
    // The exit relay delivers to the selected greeter owner.
    std::string exitProfile;
    // SourcePickupInventoryV1: the trainyard weapon pickups and their relays.
    std::string pickupProfile;
    // SourceRotatingDoorOldV1: the trainyard sewer trapdoor (ENTS 2136).
    std::string doorProfile;
};
struct WolfSourceMapState {
    unsigned bodies=0,entities=0,instanceBodies=0,resourceBodies=0,sourceMovers=0;
    unsigned collisionShapes=0,unimplementedEntities=0;
    unsigned omittedModulationSurfaces=0,omittedModulationTriangles=0;
    unsigned sourceStatics=0,sourceRelays=0,sourceDirectors=0,sourceGreeterRoutes=0,sourceExits=0,sourceTriggers=0;
    unsigned sourceAIActors=0,sourceRotatingDoors=0;
    bool spawned=false,sourceSkin=false,retailRuntimeFilters=false;
};
class WolfSourceMapRuntime {
  public:
    ~WolfSourceMapRuntime();
    bool Spawn(idStr&);
    bool Query(WolfSourceMapState&,idStr&)const;
    idEntity* PrimaryMover()const;
    WolfSourceDirector* Director()const;
    WolfSourceGreeterRoute* GreeterRoute()const;
    WolfSourceTrainyardExit* TrainyardExit()const;
    WolfSourceAIShell* AIShell()const;
    WolfSourcePickups* Pickups()const;
    WolfSourceRotatingDoor* RotatingDoor()const;
  private:
    struct Impl;std::unique_ptr<Impl> impl;
    explicit WolfSourceMapRuntime(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceMapRuntime> Wolf2009_PrepareSourceMapRuntime(const WolfSourceDeclarationSet&,const WolfSourceMapRequest&,idStr&);
};
std::unique_ptr<WolfSourceMapRuntime> Wolf2009_PrepareSourceMapRuntime(const WolfSourceDeclarationSet&,const WolfSourceMapRequest&,idStr&);
#endif
