// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_ENTITY_RUNTIME_H
#define WOLF2009_SOURCE_ENTITY_RUNTIME_H
#include "Entities.h"
#include "SourceDefinitions.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wolf2009 {
// A selected source class, never an automatic mapping of arbitrary classnames.
enum class SourceEntityPolicy { Unspecified, AnimatedContainerV1 };
struct SourceEntityLimits {
    std::size_t sourceBytes=64u<<20,instanceProperties=512,effectiveFields=4096;
};
struct SourceEntityIdentity {
    std::string resourcePath;
    std::size_t sourceBytes=0,entityIndex=0;
    std::uint64_t sourceFingerprint=0;
    // Byte identity only. Plain idFile does not supply a mount epoch.
};
struct SourceEntityValue {
    // Both facts remain accessible when an instance overrides a default.
    // These pointers are valid only while the immutable plan remains alive.
    const EntityProperty* instance=NULL;
    const SourceResolvedLiteral* declaration=NULL;
    bool Text(std::string&) const;
    bool Integer(std::int32_t&) const;
    bool Floats(std::vector<float>&) const;
};
class SourceEntityPlan {
  public:
    ~SourceEntityPlan();
    SourceEntityPlan(const SourceEntityPlan&)=delete;
    SourceEntityPlan& operator=(const SourceEntityPlan&)=delete;
    const SourceEntityIdentity& Identity() const;
    const EntityRecord& Instance() const;
    const ResolvedSourceDefinition& Defaults() const;
    SourceEntityValue Find(const std::string& key) const;
    const std::vector<std::string>& Keys() const;
    const std::string& Classname() const;
    const std::string& Spawnclass() const;
    const std::string& Name() const;
    const std::string& ModelDefinition() const;
    // Selection/typed merge alone never admits Spawn, AF, loot or animation.
    // Native class admission must account for every effective field first.
    SourceEntityPolicy Policy() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit SourceEntityPlan(std::unique_ptr<Impl>);
    friend std::shared_ptr<const SourceEntityPlan> PrepareSourceEntity(
        const std::vector<unsigned char>&,const std::string&,std::size_t,
        const SourceDefinitions&,SourceEntityPolicy,const SourceDefinitionContext&,
        const SourceEntityLimits&);
};
// Exact named ENTS resource, checked bytes, explicit selected index and full
// definition closure. Owns all facts after the input/frontend are destroyed.
// Unknown classes refuse; unknown fields are retained for class preflight.
// Does not rewrite asset extensions, infer rigs, execute Spawn or omit fields.
std::shared_ptr<const SourceEntityPlan> PrepareSourceEntity(
    const std::vector<unsigned char>& bytes,const std::string& exactResourcePath,
    std::size_t selectedIndex,const SourceDefinitions&,SourceEntityPolicy,
    const SourceDefinitionContext& = SourceDefinitionContext(),
    const SourceEntityLimits& = SourceEntityLimits());
enum class SourceEntityPlacementPolicy { Unspecified, NativeAffinePlacementV1, NativeNearUnitPlacementV1 };
struct SourceEntityPlacement {
    std::array<float,3> origin{{0,0,0}};
    // Basis rows throughout. The raw authored matrix is never normalized away.
    std::array<float,9> authoredAxis{{1,0,0,0,1,0,0,0,1}};
    std::array<float,9> physicsAxis{{1,0,0,0,1,0,0,0,1}};
    std::array<float,9> visualToPhysics{{1,0,0,0,1,0,0,0,1}};
    std::array<double,3> authoredRowLengths{{1,1,1}};
    bool authoredNonUnitAxes=false,placementApproximated=false;
    double maximumScaleError=0,matrixDifferenceNorm=0;
};
// Explicit native affine profile: proper positive near-orthogonal authored
// axes with bounded nonzero row scales. Rigid physics uses Gram-Schmidt rows;
// visualToPhysics=A*inverse(R) preserves authored affine A at the visual seam.
// It never transposes A or drops its scale. Reflection, degenerate axes, shear
// beyond1e-4 normalized dot and nonzero angle-only input refuse. Row lengths
// are bounded [1e-4,1024]. This is not retail affine physics/AF admission.
// NativeNearUnitPlacementV1 also requires row-scale error<=0.005; consumers
// explicitly use R for physics AND rendering and report the approximation.
// matrixDifferenceNorm is the Frobenius norm of A-R, a conservative operator
// bound. Radius*norm bounds local-point displacement, before float rounding.
SourceEntityPlacement PrepareSourceEntityPlacement(const SourceEntityPlan&,
    SourceEntityPlacementPolicy);
}

class idEntity;
class idStr;
class WolfSourceDeclarationSet;
class WolfSourceActorBinding;
struct WolfSourceAFRequest;
struct WolfSourceAFState;
enum WolfSourceEntityPhase { WolfSourceEntity_Idle, WolfSourceEntity_Playing,
    WolfSourceEntity_Completed, WolfSourceEntity_UnsupportedAFHandoff };
struct WolfSourceEntityState {
    WolfSourceEntityPhase phase=WolfSourceEntity_Idle;
    unsigned activations=0,animationCompletions=0,giveEvents=0,activatedItems=0,expiredOpeners=0;
    int sequence=0,animationEndMs=-1,pendingGiveEvents=0;
    bool usable=false,persisted=false,afHandoffUnsupported=false,highlightUnimplemented=false;
    bool useActorNotificationUnsupported=false;
    bool placementApproximated=false,placementBoundKnown=false;
    double placementScaleError=0,placementMatrixErrorNorm=0,placementModelRadius=0,placementDisplacementBound=0;
    int initialCycleCount=1073741824; // Measured source count, not a guessed FPS.
    std::uint32_t sourceGiveMask=0x9000;
    std::string animation,persistenceKey,reason;
};
// Private game-module API. No object/type here crosses API8. The native entry
// retains its read-only source file and owns its plan, binding and native refs.
bool Wolf2009_SpawnSourceEntity(const char* exactEntitiesPath,std::size_t index,
    const WolfSourceDeclarationSet&,std::shared_ptr<const WolfSourceActorBinding>,
    idEntity*& out,idStr& reason);
// The AF variant requires explicit source resource/identity/native policy.
// Preparation precedes entity allocation. The original factory remains the
// separately observable no-AF diagnostic profile, not a fallback on AF failure.
bool Wolf2009_SpawnSourceEntityWithAF(const char* exactEntitiesPath,std::size_t index,
    const WolfSourceDeclarationSet&,std::shared_ptr<const WolfSourceActorBinding>,
    const WolfSourceAFRequest&,idEntity*& out,idStr& reason);
bool Wolf2009_QuerySourceEntityAF(const idEntity*,WolfSourceAFState&,idStr& reason);
bool Wolf2009_IsSourceEntity(const idEntity*);
int Wolf2009_LiveSourceEntities();
bool Wolf2009_QuerySourceEntity(const idEntity*,WolfSourceEntityState&,idStr& reason);
bool Wolf2009_ActivateSourceEntity(idEntity*,idEntity* actor,idStr& reason);
// This particular inherited source Use route ignores key and dispatches
// Activate(actor). Caller eligibility/CanUse is a separate operation.
bool Wolf2009_UseSourceEntity(idEntity*,idEntity* actor,const char* key,idStr& reason);
bool Wolf2009_CanUseSourceEntity(const idEntity*);
#endif
