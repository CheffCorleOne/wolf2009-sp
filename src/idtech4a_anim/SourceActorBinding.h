// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_ACTOR_BINDING_H
#define WOLF2009_SOURCE_ACTOR_BINDING_H
#include "SourceDefinitions.h"
#include "CompiledDeclarationNamespace.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace wolf2009 { namespace actor {
typedef std::vector<unsigned char> Bytes;
enum class Failure { InvalidData, Unsupported, LimitExceeded };
class Error : public std::runtime_error {
    Failure category_;
  public:
    Error(Failure category,const char* reason):std::runtime_error(reason),category_(category){}
    Failure Category() const { return category_; }
};
// This is a caller-selected visual binding profile, not a complete retail
// namespace/default/interaction claim. A selected explicit tree is refused.
enum class TreeProfile : std::uint32_t {
    Unspecified=0, SelectedMissingExplicitDefaultSingleKeyframeV1=1
};
enum class GeometryProfile : std::uint32_t { Weighted64=1, WeightedOrRigid64=2 };
enum class LodPolicy : std::uint32_t { Authored=1, SingleResourceWithoutLodRecords=2, RootAuthoredMeshUnspecifiedSlotV1=3 };
enum class RigPolicy : std::uint32_t { CallerIdentityFullLocalPose=1 };
struct ExpectedSource {
    std::uint64_t epoch=0; // Zero explicitly requests the current mounted epoch.
    bool byteIdentityKnown=false;
    std::uint64_t bytes=0,fingerprint=0;
};
struct SourceIdentity { std::uint64_t epoch=0,bytes=0,fingerprint=0; };
struct FrameSound {
    std::uint32_t frame=1; // Authored one-based frame. Only frame1 is admitted.
    std::string soundKey,sourceSound;
};
struct ClipSelection {
    std::string logicalName;
    // Both exact resources/names are explicit. No extension rewrite or rig
    // selection is inferred from a model's .md5anim declaration token.
    std::string animationResource,animationSkeletonResource;
    std::string clipName,animationSkeletonName;
    TreeProfile treeProfile=TreeProfile::Unspecified;
    RigPolicy rigPolicy=RigPolicy::CallerIdentityFullLocalPose;
    std::array<ExpectedSource,2> expected; // HKXA, animation SKEL.
};
struct ResourceSelection {
    std::string modelResource,meshSkeletonResource,meshSkeletonName;
    std::uint32_t levelOfDetail=0;
    GeometryProfile geometryProfile=GeometryProfile::Weighted64;
    LodPolicy lodPolicy=LodPolicy::Authored;
    std::array<ExpectedSource,2> expected; // MODL, mesh SKEL.
    std::vector<ClipSelection> clips;
};
// The request is private, bounded, serialized data. Only its bytes/path and a
// readonly idFile cross API8; none of these C++ objects cross the DSO boundary.
struct RequestClip : ClipSelection {
    std::string authoredReference;
    std::vector<FrameSound> frameSounds;
};
struct Request {
    std::string entityDefinition,modelDefinition,meshReference,skeletonReference;
    ResourceSelection resources;
    std::vector<RequestClip> clips; // resources.clips MUST be empty on the wire.
};
struct CapabilityClip {
    std::string animationAlias;
    // Canonical existing AnimationWireV2::Metadata, compared with the actual
    // separately opened V2 descriptor. There is no second V2 metadata parser.
    Bytes animationMetadata;
    std::array<SourceIdentity,4> sources;
    std::uint32_t frames=0,bones=0,lengthMs=0;
};
struct Capability {
    Request request;
    std::string modelAlias;
    std::vector<CapabilityClip> clips;
};
const std::size_t MaximumRequestBytes=32u*1024u;
const std::size_t MaximumCapabilityBytes=8u*1024u*1024u;
const std::size_t MaximumClips=64;
const char* RequestPrefix();
std::size_t MaximumRequestPathBytes();
Bytes SerializeRequest(const Request&);
Request ParseRequest(const Bytes&);
std::string RequestPath(const Request&);
Request ParseRequestPath(const std::string&);
Bytes SerializeCapability(const Capability&);
Capability ParseCapability(const Bytes&);
std::string CapabilityName(const Capability&);
bool Same(const Request&,const Request&);
struct CycleSample {
    double seconds=0;
    int completedCycles=0;
    bool finished=false;
};
// Explicit native millisecond visual clock. Positive counts are finite; -1 is
// infinite. The period is the source duration, never its rounded Length().
CycleSample CycleAtMilliseconds(int elapsedMs,int cycles,double sourceDuration);

struct PlanClip {
    RequestClip request;
    SourceModelAnimationReference declaration;
    std::vector<SourceDefinitionFact> commandOrigins;
    // One exact selected SFX entry per frameSounds element. Name equality is
    // insufficient to authorize an owner from another selected namespace.
    std::vector<CompiledCatalogEntry> soundDeclarations;
};
// Owning immutable preparation. All referenced declarations, packet pools and
// selected input applications survive destruction of the input set/catalog.
class Plan {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Plan(std::shared_ptr<const Impl>);
    friend Plan Prepare(const SourceDefinitions&,const CompiledDeclarationNamespace&,
        const std::string&,const std::string&,const SourceDefinitionContext&,const ResourceSelection&);
  public:
    Plan(){}
    bool Valid() const {return bool(impl_);}
    const Request& ResourceRequest() const;
    const ResolvedSourceDefinition& Entity() const;
    const ResolvedSourceModelAnimations& Model() const;
    const SourceDefinitionContext& Context() const;
    const std::vector<CompiledNamespaceApplication>& SelectedInputs() const;
    std::size_t ClipCount() const;
    const PlanClip& Clip(std::size_t index) const;
    const PlanClip* FindClip(const std::string& exactLogicalName) const;
    bool MatchesEntityModel(const std::string& entityDefinition,const std::string& effectiveModel,
        const SourceDefinitionContext&) const;
};
// effectiveModel is explicit (possibly supplied by selected ENTS); empty uses
// the qualified source entityDef model field. This performs no native effects.
Plan Prepare(const SourceDefinitions&,const CompiledDeclarationNamespace&,
    const std::string& entityDefinition,const std::string& effectiveModel,
    const SourceDefinitionContext&,const ResourceSelection&);
} }

// C++98-compatible router declarations. Root dispatches the reserved prefix
// before entering the generic stream mutex or ordinary filename rewriting.
class idFile;
bool Wolf2009_IsSourceActorRequest(const char* path);
idFile* Wolf2009_OpenSourceActorBinding(const char* path);

#ifdef WOLF2009_SOURCE_ACTOR_GAME
class idStr;
class idDeclModelDef;
class idMD5Anim;
class WolfSourceDeclarationSet;
class WolfSourceSoundBinding;
struct WolfSourceActorSoundBinding {
    std::string sourceKey,sourceSound,nativeSoundAlias;
    std::shared_ptr<const WolfSourceSoundBinding> owner;
};
struct WolfSourceActorClip {
    std::string logicalName,authoredReference,animationAlias;
    int frames=0,lengthMs=0;
    std::vector<wolf2009::actor::FrameSound> frameSounds;
};
class WolfSourceActorBinding {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceActorBinding(std::unique_ptr<Impl>);
    friend std::shared_ptr<const WolfSourceActorBinding> Wolf2009_BindSourceActor(
        const wolf2009::actor::Plan&,const std::vector<WolfSourceActorSoundBinding>&,idStr&);
  public:
    ~WolfSourceActorBinding();
    const wolf2009::actor::Plan& SourcePlan() const;
    const char* NativeModelDefinition() const;
    const char* NativeModel() const;
    const char* CapabilityName() const;
    std::size_t ClipCount() const;
    const WolfSourceActorClip& Clip(std::size_t) const;
    const WolfSourceActorClip* FindClip(const char* exactLogicalName) const;
    const std::vector<WolfSourceActorSoundBinding>& Sounds() const;
    bool MatchesNative(idStr& reason) const;
    bool MatchesEntityModel(const std::string&,const std::string&,
        const wolf2009::SourceDefinitionContext&) const;
    WolfSourceActorBinding(const WolfSourceActorBinding&)=delete;
    WolfSourceActorBinding& operator=(const WolfSourceActorBinding&)=delete;
};
// Opens the request through ordinary API8, validates all returned aliases and
// descriptor identities, then creates an immutable ordinary native modelDef.
// Each authored frame sound requires an explicitly qualified native mapping;
// missing/unknown/conflicting commands or sounds refuse before decl effects.
std::shared_ptr<const WolfSourceActorBinding> Wolf2009_BindSourceActor(
    const wolf2009::actor::Plan&,const std::vector<WolfSourceActorSoundBinding>&,idStr& reason);
#endif
#endif
