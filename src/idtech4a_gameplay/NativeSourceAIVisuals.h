// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_AI_VISUALS_H
#define WOLF2009_NATIVE_SOURCE_AI_VISUALS_H

#include <memory>
#include <string>

class idAnimatedEntity;
class idEntity;
class idStr;
class WolfSourceDeclarationSet;
namespace wolf2009 { struct EntityRecord; }

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

private:
    struct Impl;
    explicit WolfSourceAIVisuals(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisuals(
        const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&, const std::string&, idStr&);
};

std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisuals(
    const WolfSourceDeclarationSet&, const wolf2009::EntityRecord&,
    const std::string& materialArchive, idStr& reason);
#endif
