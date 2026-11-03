// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameplayFixture.h"
namespace wolf2009 {
namespace {
StaticCollisionBox Box(CollisionVec3 lo, CollisionVec3 hi, std::uint32_t contents=1) {
    StaticCollisionBox b;
    b.minimum=lo; b.maximum=hi; b.contents=contents;
    return b;
}
}
GameplayMap MakeGameplayFixture() {
    GameplayMap map;
    map.worldBoxes.push_back(Box({-256,-256,-16},{256,256,0}));
    map.worldBoxes.push_back(Box({160,-256,0},{176,256,160}));
    GameplayEntity player;
    player.kind=GameplayEntityKind::PlayerStart; player.name="runtime_start";
    map.entities.push_back(player);
    GameplayEntity mover;
    mover.kind=GameplayEntityKind::Mover; mover.name="runtime_platform";
    mover.origin={-128,-128,16};
    mover.localBoxes.push_back(Box({-32,-32,-8},{32,32,0}));
    mover.moveOnSpawn=true; mover.moveDestination={-128,-64,16}; mover.moveSpeed=32;
    map.entities.push_back(mover);
    GameplayEntity victim;
    victim.kind=GameplayEntityKind::Static; victim.name="runtime_remove_me";
    victim.origin={-128,128,0};
    victim.localBoxes.push_back(Box({-8,-8,0},{8,8,16}));
    map.entities.push_back(victim);
    GameplayEntity remove;
    remove.kind=GameplayEntityKind::RemoveTargets; remove.name="runtime_remove_targets";
    remove.targets.push_back(victim.name);
    map.entities.push_back(remove);
    GameplayEntity trigger;
    trigger.kind=GameplayEntityKind::Trigger; trigger.name="runtime_touch";
    trigger.origin={64,0,0}; trigger.triggerWait=-1;
    trigger.localBoxes.push_back(Box({-8,-48,0},{8,48,96},4096));
    trigger.targets.push_back(remove.name);
    map.entities.push_back(trigger);
    return map;
}
}
