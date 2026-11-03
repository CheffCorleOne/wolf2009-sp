// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ANIMATION_SET_H
#define WOLF2009_ENGINE_ANIMATION_SET_H
#include <string>
#include <vector>

class idStr;
class idDeclModelDef;

// Game/model owner thread only. Private to the C++11 adapter, not API8. Exact
// logical keys and already qualified source-v2 aliases, not filenames inferred
// from an original model declaration. All entries must use one actual model.
struct WolfSourceAnimationSetEntry {
    std::string logicalName;
    std::string animationAlias;
};
bool Wolf2009_CreateSourceAnimationSet(const std::vector<WolfSourceAnimationSetEntry>& entries,
    const char* appearanceAlias, idStr& modelDefinition, idStr& reason);
// Native declarations and animation refs keep their ordinary manager ownership.
// Discovery succeeds only for unchanged factory declarations. Forgetting a set
// disables discovery, retaining immutable ownership metadata until shutdown so
// the same unchanged set can be registered again without taking over a foreign
// native declaration. References held by a bound entity remain valid.
const idDeclModelDef* Wolf2009_FindRegisteredSourceAnimationSet(const char* modelDefinition);
bool Wolf2009_ForgetSourceAnimationSet(const char* modelDefinition);
void Wolf2009_ClearAnimationSetRegistry();
#endif
