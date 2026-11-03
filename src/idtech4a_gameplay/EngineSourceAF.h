// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_SOURCE_AF_H
#define WOLF2009_ENGINE_SOURCE_AF_H
// Game-private opaque C++98 seam. No object crosses GetGameAPI/API8.
class idAnimatedEntity;
class idStr;
class idVec3;
class idMat3;
class WolfSourceDeclarationSet;
class WolfSourceActorBinding;
namespace wolf2009 { class SourceEntityPlan; }

enum WolfSourceAFProfile {
    WolfSourceAF_Unspecified=0,
    WolfSourceAF_FixedRootWorldHingeNativeConservativeV1=1
};
struct WolfSourceAFRequest {
    WolfSourceAFProfile profile;
    const char* exactResourcePath; // explicit same-mount typed HKXR resource
    const char* authoredRagdollReference; // exact resolved havok_ragdoll text
    const char* exactRootVariant;
    const char* restAnimation; // selected logical clip matching authored af_pose
    unsigned long long expectedBytes,expectedFingerprint; // FNV identity, not authentication
    int nativeContents,nativeClipMask; // explicit native policy, never raw HK bits
    WolfSourceAFRequest():profile(WolfSourceAF_Unspecified),exactResourcePath(0),authoredRagdollReference(0),
        exactRootVariant(0),restAnimation(0),expectedBytes(0),expectedFingerprint(0),
        nativeContents(0),nativeClipMask(0){}
};
struct WolfSourceAFState {
    bool active,atRest,collisionCoverageComplete,rootCollisionExcluded;
    int sourceBodies,nativeBodies,hinges,jointMods;
    float hingeAngleDegrees,maximumRoundingExcess,handoffAnchorResidual;
    // Compatibility uses source mass and a conservative hull's native inertia.
    // Torque friction is capped explicitly; no source response-bit inference.
    float mass,frictionTorqueGameUnits;
    // Fixed source target axis; the reported anchor follows the native moving
    // body's hinge anchor so a caller can measure constraint drift directly.
    float hingeAxisWorld[3],hingeAnchorWorld[3],assetOriginWorld[3];
    float minimumHingeAngleDegrees,maximumHingeAngleDegrees,initialHingeAngleDegrees;
    WolfSourceAFState():active(false),atRest(false),collisionCoverageComplete(false),
        rootCollisionExcluded(false),sourceBodies(0),nativeBodies(0),hinges(0),jointMods(0),
        hingeAngleDegrees(0),maximumRoundingExcess(0),handoffAnchorResidual(0),mass(0),frictionTorqueGameUnits(0),
        minimumHingeAngleDegrees(0),maximumHingeAngleDegrees(0),initialHingeAngleDegrees(0){
        for(int i=0;i<3;++i)hingeAxisWorld[i]=hingeAnchorWorld[i]=assetOriginWorld[i]=0;
    }
};
class WolfSourceAFPrepared {
  public:
    ~WolfSourceAFPrepared();
  private:
    struct Impl;Impl* impl;
    explicit WolfSourceAFPrepared(Impl*);
    WolfSourceAFPrepared(const WolfSourceAFPrepared&);
    WolfSourceAFPrepared& operator=(const WolfSourceAFPrepared&);
    friend WolfSourceAFPrepared* Wolf2009_PrepareSourceAF(const WolfSourceDeclarationSet&,
        const wolf2009::SourceEntityPlan&,const WolfSourceActorBinding&,
        const WolfSourceAFRequest&,idStr&);
    friend class WolfSourceAF;
};
// Before allocating the entity: reads/owns checked HKXR, resolves complete core
// and part defaults, verifies native model/rest-pose and selected policy. Failure
// creates no entity/physics/joint modifications. Caller owns returned pointer.
WolfSourceAFPrepared* Wolf2009_PrepareSourceAF(const WolfSourceDeclarationSet&,
    const wolf2009::SourceEntityPlan&,const WolfSourceActorBinding&,
    const WolfSourceAFRequest&,idStr& reason);

class WolfSourceAF {
  public:
    ~WolfSourceAF(); // detach/restore owner physics before deleting AF/clip models
    // Takes its own immutable prepared-data copy. Owner must outlive this object.
    static WolfSourceAF* Bind(const WolfSourceAFPrepared&,idAnimatedEntity&,idStr& reason);
    bool Active() const;
    // Current animation pose is captured first. All native constraints are built
    // and checked before SetPhysics/ClearAllAnims/joint effects. On failure the
    // current native animation and owner physics remain unchanged.
    bool Activate(idStr& reason);
    // Active Think route: damping update, ordinary owner.RunPhysics, then model-
    // space WORLD_OVERRIDE joint modifications. Caller then UpdateAnimation and
    // Present. Does not run independent wall-clock simulation or a fake tick.
    bool Step(int timeStepMs,idStr& reason);
    bool ApplyJointMods(idStr& reason);
    // Entity override delegates here so the visual asset frame remains stable
    // while the sole native dynamic body's COM moves around its world hinge.
    bool GetPhysicsToVisualTransform(idVec3& origin,idMat3& axis) const;
    bool Query(WolfSourceAFState&) const;
  private:
    struct Impl;Impl* impl;
    explicit WolfSourceAF(Impl*);
    WolfSourceAF(const WolfSourceAF&);
    WolfSourceAF& operator=(const WolfSourceAF&);
};
#endif
