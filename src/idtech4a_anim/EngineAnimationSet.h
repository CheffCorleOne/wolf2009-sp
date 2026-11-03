// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ANIMATION_SET_H
#define WOLF2009_ENGINE_ANIMATION_SET_H
#include <string>
#include <vector>

class idStr;
class idDeclModelDef;
class idAnim;

// Game/model owner thread only. Private to the C++11 adapter, not API8. Exact
// logical keys and already qualified source-v2 aliases, not filenames inferred
// from an original model declaration. All entries must use one actual model.
struct WolfSourceAnimationSetEntry {
    std::string logicalName;
    std::string animationAlias;
    // Empty in the original NoCommands factory. The additive actor factory
    // admits exactly one authored one-based frame1 sound key per clip.
    std::string frameOneSoundKey;
};
bool Wolf2009_CreateSourceAnimationSet(const std::vector<WolfSourceAnimationSetEntry>& entries,
    const char* appearanceAlias, idStr& modelDefinition, idStr& reason);
bool Wolf2009_CreateSourceActorAnimationSet(const std::vector<WolfSourceAnimationSetEntry>& entries,
    const char* appearanceAlias, idStr& modelDefinition, idStr& reason);
// No parsing or channel mutation. Only the immutable additive actor-set
// registry qualifies the native fixed-frame/cycle guards; the old factory and
// source-v1 animations retain their previous refusal behavior.
bool Wolf2009_IsQualifiedSourceActorAnimation(const idDeclModelDef*,const idAnim*);
bool Wolf2009_IsQualifiedSourceFrameSound(const idDeclModelDef*,const idAnim*);
// Native declarations and animation refs keep their ordinary manager ownership.
// Discovery succeeds only for unchanged factory declarations. Forgetting a set
// disables discovery, retaining immutable ownership metadata until shutdown so
// the same unchanged set can be registered again without taking over a foreign
// native declaration. References held by a bound entity remain valid.
const idDeclModelDef* Wolf2009_FindRegisteredSourceAnimationSet(const char* modelDefinition);
bool Wolf2009_ForgetSourceAnimationSet(const char* modelDefinition);
void Wolf2009_ClearAnimationSetRegistry();
#endif
