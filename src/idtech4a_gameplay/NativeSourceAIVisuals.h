// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_AI_VISUALS_H
#define WOLF2009_NATIVE_SOURCE_AI_VISUALS_H

#include <memory>
#include <string>
#include <vector>

class idAnimatedEntity;
class idEntity;
class idStr;
class WolfSourceDeclarationSet;
namespace wolf2009 { struct EntityRecord; }

// SourceAIScriptedAnimV1 body clips: exact logical names bound to exact HKXA
// clips of one map HKXA, registered as full-pose V2 animations on the shell
// body's own native mesh. The private native modelDef carries no flags, frame
// commands or loops; those stay with the caller's source anim-block profile.
struct WolfSourceAIClipSelection { std::string logicalName, clipName; };
struct WolfSourceAIClipRequest {
    std::string animationResource, animationSkeletonResource, animationSkeletonName;
    std::vector<WolfSourceAIClipSelection> clips;
};
struct WolfSourceAIClip {
    std::string logicalName, clipName, animationAlias;
    int animNumber = 0, frames = 0, lengthMs = 0;
    double duration = 0;
};

// Owns the native bodies, heads and rigid accessories for the measured
// trainyard SourceAIShellV1 profile. Entities are destroyed child-first.
class WolfSourceAIVisuals {
public:
    ~WolfSourceAIVisuals();
    idAnimatedEntity* Body() const;
    idAnimatedEntity* Head() const;
    unsigned AccessoryCount() const;
    idEntity* Accessory(unsigned index) const;
    void SetHidden(bool hidden);
    bool UpdatePose(idStr& reason);
    // Body clips admitted at spawn (empty for the reference-pose profile).
    const std::vector<WolfSourceAIClip>& Clips() const;

private:
    struct Impl;
    explicit WolfSourceAIVisuals(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisualsOnBody(
        const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&, const std::string&, idAnimatedEntity*, idStr&);
    friend std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisualsWithClips(
        const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&, const std::string&, const WolfSourceAIClipRequest&, idStr&);
};

std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisuals(
    const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&,
    const std::string& materialArchive, idStr& reason);
// Greeter only: binds the head and accessories to a body owned elsewhere
// (the greeter route's actor owner). The body is never deleted here.
std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisualsOnBody(
    const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&,
    const std::string& materialArchive, idAnimatedEntity* externalBody, idStr& reason);
// SourceAIScriptedAnimV1: as Wolf2009_SpawnSourceAIVisuals, but the owned body
// uses a private native modelDef with the requested source clips.
std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisualsWithClips(
    const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&,
    const std::string& materialArchive, const WolfSourceAIClipRequest&, idStr& reason);
#endif
