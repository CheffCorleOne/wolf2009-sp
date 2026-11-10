// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_AI_SCRIPTED_ANIM_H
#define WOLF2009_NATIVE_SOURCE_AI_SCRIPTED_ANIM_H
// SourceAIScriptedAnimV1 (docs/AI-PRESENTATION.md §2-3, docs/AI-ACTOR.md
// stage 2): one native scripted-animation, frame-command and death owner per
// source AI body. Game thread only; the owner never outlives its shell actor.
//
// Recovered contracts (Gamex86 7cb1cbed..., Static): ScriptedAnim +0x468,
// clip lookup 0x20017300, AnimPlay/AnimCycle with FRAME2MS(blend), the
// State_ScriptedAnim / State_ScriptedStop pair with a fixed 4-frame AnimDone,
// EndScripted +0x460, <kill> 0x20184e50, the death predicate +0x5e4 and the
// death choice +0x69c. The frame-command caller lives in Wolf2.exe and was
// not read: this owner models it on GPL Doom 3 idAnimBlend::CallFrameCommands
// (explicit start at from = -1, one (from, to] call per serviced interval,
// an explicit two-call wrap for cycles), serviced on the primary blend from
// the actor's own Think after its animation update.
#include "SourceAnimCommands.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class idAnimatedEntity;
class idEntity;

struct WolfSourceAIClipProfile {
    std::string logicalName;            // exact source logical name
    int animNumber = 0, frames = 0, lengthMs = 0;
    wolf2009::SourceAnimBlock block;    // from the compiled model declaration
};
struct WolfSourceFrameCommandReceipt {
    std::string actor, clip, command, argument, outcome, detail;
    int frame = -1, timeMs = -1, clipTimeMs = -1;
    unsigned play = 0;
};
// Spawn keys read for the death predicate; the bit mapping (ai+0x956 0x4 and
// 0x10) is Inferred from the key names.
struct WolfSourceAIDeathKeys {
    bool playDeathAnim = false, ragdollOnDeath = false;
    std::string deathAnimOverride;
};
struct WolfSourceAIScriptedAnimState {
    std::string profile;
    bool scripted = false, endWithIdle = false, dead = false, waitingDeathAnim = false;
    std::string currentClip;
    int currentAnim = 0, playStartMs = -1, playEndMs = -1, playBlendMs = 0, playLoops = 0;
    bool playCycle = false;
    unsigned plays = 0, scriptedAnims = 0, scriptedFailures = 0, scriptedStops = 0;
    int scriptedStartMs = -1, scriptedStopMs = -1, scriptedStopRemainingMs = -1;
    std::string lastLookup, lastMissing;
    std::vector<std::string> statePosts;
    unsigned kills = 0;
    int killFlags = 0, killTimeMs = -1;
    bool deathPredicate = false, deathClipPlayed = false;
    std::string deathChoice, deathChoiceSource, deathPlayback, ragdoll;
    std::vector<WolfSourceFrameCommandReceipt> frameCommands;
    unsigned serviceCalls = 0, idleRefusals = 0;
    std::uint32_t lcgSeed = 0;
};

class WolfSourceAIScriptedAnimator {
  public:
    WolfSourceAIScriptedAnimator(const std::string &actor, idAnimatedEntity *body,
                                 std::vector<WolfSourceAIClipProfile> clips, std::vector<std::string> declaredClips,
                                 wolf2009::SourceGameLcg &lcg, const WolfSourceAIDeathKeys &keys);
    ~WolfSourceAIScriptedAnimator();
    // ScriptedAnim +0x468 after the actor's own Begin side effects (Show).
    // Returns the original's result; effect names every recorded step.
    bool ScriptedAnim(const std::string &name, int blendFrames, int numLoops, bool endWithIdle, const std::string &task,
                      std::string &effect);
    // +0x33c: true when the current clip does not match `name` (full or base
    // name, case-insensitive) or its remaining time is <= FRAME2MS(blend).
    bool AnimDoneByName(const std::string &name, int blendFrames, int &remainingMs, std::string &current) const;
    // <kill> with damage flags 0x1 (0x11 bypassed), then the death path.
    bool Kill(bool bypassDeathAnim, const std::string &deathOverride, std::string &effect);
    // Per Think, after the brain tick: state thread, native animation update,
    // then the frame-command service for (last serviced, now].
    void Update(bool scriptedManual);
    const WolfSourceAIScriptedAnimState &State() const;
    bool Dead() const;
    bool Alive() const;
    WolfSourceAIScriptedAnimator(const WolfSourceAIScriptedAnimator &) = delete;
    WolfSourceAIScriptedAnimator &operator=(const WolfSourceAIScriptedAnimator &) = delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
int Wolf2009_LiveSourceAIScriptedAnimators();
#endif
