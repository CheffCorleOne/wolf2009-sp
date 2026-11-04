// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_SCRIPTED_ACTOR_H
#define WOLF2009_NATIVE_SOURCE_SCRIPTED_ACTOR_H
// Game-private C++11 ownership. No new GetGameAPI entry or cross-DSO objects.
#include "SourceActorBinding.h"
#include "TrainDoorSignal.h"
#include "Entities.h"
#include <cstdint>
#include <memory>
#include <string>
class idAnimatedEntity;
class idStr;
class WolfSourceDeclarationSet;
struct WolfSourceActorStartBinding;
enum class WolfSourceScriptedActorPolicy {
    Unspecified=0,
    NativeFullPoseNoExtractionNoFrameCommandsV1=1
};
struct WolfSourceScriptedActorRequest {
    WolfSourceScriptedActorPolicy policy=WolfSourceScriptedActorPolicy::Unspecified;
    std::string exactEntitiesPath,materialArchive;
    unsigned entityIndex=0,expectedJoints=0,expectedFrames=0;
    std::uint64_t levelGeneration=0,expectedEntitiesBytes=0,expectedEntitiesFingerprint=0;
    // Caller supplies the already decoded exact signal profile. Its raw keys,
    // selected node and later unsupported branches remain independently owned.
    wolf2009::TrainDoorSignalPlan signal;
    wolf2009::actor::ResourceSelection resources;
    wolf2009::SourceDefinitionContext definitionContext;
    // Explicit native visual hold, NOT automatic interpretation of ENTS frame.
    int initialSourceFrame=0;
};
enum class WolfSourceScriptedActorPhase { Prepared, Held, Playing, Completed };
struct WolfSourceScriptedActorStartAck {
    wolf2009::TrainDoorCommand command;
    std::uint64_t levelGeneration=0;
    unsigned nativeSpawnId=0,commandSerial=0;
    int committedTimeMs=-1;
    bool committed=false,duplicate=false,actualPoseEvaluated=false;
};
struct WolfSourceScriptedActorState {
    WolfSourceScriptedActorPhase phase=WolfSourceScriptedActorPhase::Prepared;
    std::string sourceName,sourceClass,sourceModelDefinition,authoredSkin,authoredBrain,
        logicalClip,nativeModel,nativeModelDefinition,nativeSkin,animationAlias,
        declaredSkeleton,selectedMeshSkeleton;
    std::uint64_t levelGeneration=0,eventId=0,entitiesBytes=0,entitiesFingerprint=0;
    unsigned entityIndex=0,nativeSpawnId=0,commandSerial=0,joints=0,frames=0,lengthMs=0;
    unsigned startCommits=0,duplicateStarts=0,completions=0,skinMatches=0,skinChanges=0;
    unsigned retainedFrameCommands=0,retainedAnimationOptions=0,retainedChannelFacts=0,
        retainedAttachmentFields=0,unimplementedBrainNodes=0;
    int nativeStartTimeMs=-1,nativeEndTimeMs=-1,completionObservedTimeMs=-1;
    bool hasAuthoredFrame=false,authoredNoOpSkin=false,sharedGeometryUnchanged=false,
        actualStartPoseEvaluated=false,bodyOnly=true,retailAiImplemented=false,
        rootMotionExtractionImplemented=false,headAttachmentsImplemented=false,
        frameCommandsImplemented=false,retailShaderParity=false,saveRestoreImplemented=false;
    double authoredFrame=0,startPaletteError=0,completionPaletteError=0;
};
class WolfSourceScriptedActor {
  public:
    ~WolfSourceScriptedActor();
    // Preparation publishes no native entity and never starts playback.
    // Spawn holds the caller-selected source frame, with no start ACK.
    bool SpawnInactive(idStr& reason);
    bool Start(const wolf2009::TrainDoorCommand&,WolfSourceScriptedActorStartAck&,idStr& reason);
    // Polls native held/playing state; completion requires its actual final
    // hold, never merely an elapsed timer. It executes no later BRAI outputs.
    bool Query(WolfSourceScriptedActorState&,idStr& reason);
    bool Preflight(idStr& reason) const;
    // Owner-issued authority for the bound door. Preparation may query it
    // before SpawnInactive; its native identity becomes fixed at that spawn.
    // The descriptor borrows this owner's initial ref; consumers retain it
    // before releasing the owner. Query never acknowledges a new start.
    bool StartBinding(WolfSourceActorStartBinding&,idStr& reason) const;
    idAnimatedEntity* NativeEntity() const;
    const wolf2009::EntityRecord& SourceInstance() const;
    const wolf2009::ResolvedSourceDefinition& SourceEntity() const;
    const wolf2009::ResolvedSourceModelAnimations& SourceModel() const;
    WolfSourceScriptedActor(const WolfSourceScriptedActor&)=delete;
    WolfSourceScriptedActor& operator=(const WolfSourceScriptedActor&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceScriptedActor(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceScriptedActor> Wolf2009_PrepareSourceScriptedActor(
        const WolfSourceDeclarationSet&,const WolfSourceScriptedActorRequest&,idStr&);
};
// Owns the exact selected compiled entity/model facts, original resource
// capabilities, animation refs, explicit source skin and native entity. Release
// all owner/authority refs before source mounts/declarations; the last ref
// removes the native actor first. It does not implement an AI/controller, head, attachments,
// frame-command dispatch, root extraction, voice playback or BRAI continuation.
std::unique_ptr<WolfSourceScriptedActor> Wolf2009_PrepareSourceScriptedActor(
    const WolfSourceDeclarationSet&,const WolfSourceScriptedActorRequest&,idStr& reason);
int Wolf2009_LiveSourceScriptedActors();
#endif
