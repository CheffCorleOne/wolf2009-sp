// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_AI_SHELL_H
#define WOLF2009_NATIVE_SOURCE_AI_SHELL_H

#include <cstdint>
#include <memory>
#include <string>

class idEntity;
class idStr;
class WolfNativeSourceRegistry;
class WolfSourceDeclarationSet;
class WolfSourceDirector;

struct WolfSourceAIActorState {
    std::string name, classname, brainResource, pendingKey, reason, poseReason, relayReason;
    std::string lastActivator;
    std::uint64_t levelGeneration = 0;
    unsigned spawnId = 0, brainTicks = 0, rootRefusals = 0, wakes = 0, activations = 0;
    unsigned useNotifications = 0, rejectedUses = 0, stopRoot = 0, stopNode = 0, stopType = 0;
    unsigned relayDeliveries = 0;
    int wakeTimeMs = -1, firstBrainTimeMs = -1, lastPlayerUseTimeMs = -1;
    bool hidden = false, noUnhideOnUse = false, brainBound = false, pendingUse = false;
    bool activated = false, thinking = false;
};

// Exact, map-owned trainyard actor profile. Events run on the game thread and
// always use the captured native entity generations. Stop receipts report
// selection at the first unsupported task, without entering it.
class WolfSourceAIShell {
  public:
    WolfSourceAIShell();
    ~WolfSourceAIShell();
    bool Spawn(const WolfSourceDeclarationSet&, const std::string& exactEntities,
               const std::string& materialArchive, std::uint64_t generation,
               WolfSourceDirector*, std::shared_ptr<WolfNativeSourceRegistry>, idStr&);
    bool Activate(const std::string& target, idEntity* activator, idStr&);
    bool Use(const std::string& target, const std::string& key, idEntity* activator, idStr&);
    bool Query(const std::string& name, WolfSourceAIActorState&, idStr&) const;
    unsigned Count() const;
    idEntity* Body(const std::string& name) const;
    idEntity* Head(const std::string& name) const;
    unsigned AccessoryCount(const std::string& name) const;
    idEntity* Accessory(const std::string& name, unsigned index) const;
    WolfSourceAIShell(const WolfSourceAIShell&) = delete;
    WolfSourceAIShell& operator=(const WolfSourceAIShell&) = delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

int Wolf2009_LiveSourceAIShells();
int Wolf2009_LiveSourceAIActors();
int Wolf2009_LiveSourceAIBrains();
int Wolf2009_LiveSourceAIPendingUses();
#endif
