// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_ENTITY_CHECKS_H
#define WOLF2009_NATIVE_SOURCE_ENTITY_CHECKS_H
#include <memory>
class WolfSourceDeclarationSet;
class WolfSourceActorBinding;
class idStr;
// Game-module QA helper, called AFTER the existing mover/player probes. Its
// caller supplies the real startup set and source-backed locker actor binding.
// No STL/private objects cross the core/game boundary.
bool Wolf2009_CheckSourceEntityQA(const WolfSourceDeclarationSet&,
    std::shared_ptr<const WolfSourceActorBinding>,const char* exactEntitiesPath);
#endif
