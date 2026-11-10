// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "SourceAnimCommands.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourceAIScriptedAnim.h"

namespace {
int liveAnimators = 0;
const char *const Profile = "SourceAIScriptedAnimV1";
bool FoldEqual(const std::string &a, const std::string &b) { return idStr::Icmp(a.c_str(), b.c_str()) == 0; }
std::string Join(const std::vector<std::string> &values) {
    std::string out;
    for (const auto &value : values) { if (!out.empty()) out += ' '; out += value; }
    return out;
}
} // namespace

struct WolfSourceAIScriptedAnimator::Impl {
    std::string actor;
    idEntityPtr<idAnimatedEntity> body;
    std::vector<WolfSourceAIClipProfile> clips;
    std::vector<std::unique_ptr<wolf2009::SourceFrameCommandList>> lists;
    std::vector<std::string> declared;
    wolf2009::SourceGameLcg &lcg;
    WolfSourceAIDeathKeys keys;
    WolfSourceAIScriptedAnimState state;
    enum class Thread { None, ScriptedAnim } thread = Thread::None;
    int scriptedIndex = -1;  // ai+0x38d4 (clip profile index)
    int playing = -1;        // clip profile index of the current play
    int servicedTo = -1;     // last serviced game time of the current play
    bool busy = false;

    Impl(const std::string &name, idAnimatedEntity *entity, std::vector<WolfSourceAIClipProfile> admitted,
         std::vector<std::string> names, wolf2009::SourceGameLcg &generator, const WolfSourceAIDeathKeys &death)
        : actor(name), clips(std::move(admitted)), declared(std::move(names)), lcg(generator), keys(death) {
        body = entity;
        for (const auto &clip : clips) lists.emplace_back(new wolf2009::SourceFrameCommandList(clip.block.commands));
        state.profile = Profile;
        state.lcgSeed = lcg.seed;
    }
    idAnimatedEntity *Body() const {
        auto *entity = body.GetEntity();
        return entity && entity->entityNumber >= 0 && entity->entityNumber < MAX_GENTITIES &&
                       gameLocal.entities[entity->entityNumber] == entity ? entity : nullptr;
    }
    int Find(const std::string &exact) const {
        for (std::size_t i = 0; i < clips.size(); ++i) if (clips[i].logicalName == exact) return int(i);
        return -1;
    }
    // Clip lookup 0x20017300 over the actor's declared model clips, then the
    // native admission of this profile.
    int Lookup(const std::string &name, std::string &why) {
        const auto found = wolf2009::LookupSourceClipV1(declared, name, lcg);
        state.lcgSeed = lcg.seed;
        state.lastLookup = std::string(found.exact ? "exact" : "base-name") + " lookup of '" + name + "': " +
                           (found.status == wolf2009::SourceClipLookup::Found ? "found " + found.clip : std::string("missing")) +
                           " candidates=" + std::to_string(found.candidates) + " lcg=" + std::to_string(found.seedBefore) +
                           "->" + std::to_string(found.seedAfter);
        if (found.status != wolf2009::SourceClipLookup::Found) { why = "missing"; return -1; }
        const int index = Find(found.clip);
        if (index < 0) { why = "declared clip '" + found.clip + "' is outside the admitted native clip set"; return -1; }
        return index;
    }
    void Play(int index, int blendFrames, int loops) {
        auto *entity = Body();
        auto *animator = entity->GetAnimator();
        const int now = gameLocal.time;
        const int blendMs = wolf2009::SourceFrameToMs(blendFrames);
        const auto &clip = clips[std::size_t(index)];
        if (loops) {
            animator->CycleAnim(ANIMCHANNEL_ALL, clip.animNumber, now, blendMs);
            animator->CurrentAnim(ANIMCHANNEL_ALL)->SetCycleCount(loops);
        } else {
            animator->PlayAnim(ANIMCHANNEL_ALL, clip.animNumber, now, blendMs);
        }
        auto *blend = animator->CurrentAnim(ANIMCHANNEL_ALL);
        blend->AllowMovement(false);       // V2 ordinary visual, no extraction
        blend->AllowFrameCommands(false);  // the native declaration has none; this owner services the source list
        animator->ForceUpdate();
        playing = index;
        servicedTo = now;
        ++state.plays;
        state.currentClip = clip.logicalName;
        state.currentAnim = clip.animNumber;
        state.playStartMs = blend->GetStartTime();
        state.playEndMs = blend->GetEndTime();
        state.playBlendMs = blendMs;
        state.playLoops = loops;
        state.playCycle = loops != 0;
        entity->UpdateAnimation();
        gameLocal.Printf("WOLF2009_SOURCE_AI_ANIM_PLAY actor=%s profile=%s clip=%s anim=%d mode=%s loops=%d blend_frames=%d "
                         "blend_ms=%d start=%d end=%d frames=%d length=%d play=%u\n",
                         actor.c_str(), Profile, clip.logicalName.c_str(), clip.animNumber, loops ? "cycle" : "play", loops,
                         blendFrames, blendMs, state.playStartMs, state.playEndMs, clip.frames, clip.lengthMs, state.plays);
    }
    // AnimDone(index, blend) +0x338: current clip on the channel is not index,
    // or its remaining time is <= FRAME2MS(blend).
    bool AnimDone(int index, int blendFrames, int &remaining) const {
        remaining = -1;
        auto *entity = Body();
        if (!entity || index < 0) return true;
        const auto *blend = entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL);
        if (!blend || blend->AnimNum() != clips[std::size_t(index)].animNumber) return true;
        if (blend->GetEndTime() < 0) return false; // infinite cycle
        remaining = blend->GetEndTime() - gameLocal.time;
        return wolf2009::SourceAnimRemainingDone(remaining, blendFrames);
    }
    void Receipt(const wolf2009::SourceFrameCommand &command, const char *outcome, const std::string &detail, int clipTime) {
        WolfSourceFrameCommandReceipt r;
        r.actor = actor;
        r.clip = playing >= 0 ? clips[std::size_t(playing)].logicalName : "";
        r.command = command.name;
        r.argument = Join(command.arguments);
        r.outcome = outcome;
        r.detail = detail;
        r.frame = int(command.frame);
        r.timeMs = gameLocal.time;
        r.clipTimeMs = clipTime;
        r.play = state.plays;
        if (state.frameCommands.size() < 256) state.frameCommands.push_back(r);
        gameLocal.Printf("WOLF2009_SOURCE_AI_FRAME_COMMAND actor=%s clip=%s frame=%d one_based=%d command=%s argument=%s "
                         "outcome=%s time=%d clip_time=%d play=%u detail=\"%s\"\n",
                         actor.c_str(), r.clip.c_str(), r.frame, r.frame + 1, r.command.c_str(),
                         r.argument.empty() ? "-" : r.argument.c_str(), outcome, r.timeMs, clipTime, r.play, detail.c_str());
    }
    void Fire(const wolf2009::SourceFrameCommand &command, int clipTime) {
        using Code = wolf2009::SourceFrameCommandCode;
        switch (command.code) {
        case Code::Trigger: {
            // FindEntity(name); found -> synchronous activate with the owner.
            const auto &name = command.arguments[0];
            idEntity *target = gameLocal.FindEntity(name.c_str());
            if (!target) {
                gameLocal.Warning("Framecommand 'trigger' on entity '%s': Could not find entity '%s'", actor.c_str(), name.c_str());
                Receipt(command, "missing-target", "Could not find entity '" + name + "'", clipTime);
                return;
            }
            const bool handled = target->ProcessEvent(&EV_Activate, Body());
            Receipt(command, handled ? "activated" : "no-handler",
                    std::string("synchronous activate on ") + target->GetType()->classname + " with activator " + actor, clipTime);
            return;
        }
        case Code::Speak:
        case Code::SpeakNoSubtitle:
        case Code::SpeakDeferred:
            if (command.arguments.empty() || command.arguments[0].empty()) {
                Receipt(command, "skipped", "no line", clipTime);
                return;
            }
            Receipt(command, "refused",
                    "speak event (Speak +0x3ac) belongs to SourceAISpeechV1 (stage 3), which is not implemented", clipTime);
            return;
        case Code::FxOffset:
            Receipt(command, "refused", "fx_offset body (code 0x1a) was not read", clipTime);
            return;
        case Code::InteractiveRagdoll:
            Receipt(command, "refused",
                    "interactiveRagdoll (0x20012330, interactive AF physics) is outside SourceAIScriptedAnimV1", clipTime);
            return;
        default:
            Receipt(command, "refused", "frame command outside SourceAIScriptedAnimV1 dispatch", clipTime);
            return;
        }
    }
    // GPL idAnimBlend::CallFrameCommands shape on the primary blend.
    void Service() {
        auto *entity = Body();
        if (!entity || playing < 0) return;
        const int from = servicedTo, to = gameLocal.time;
        servicedTo = to;
        const auto &clip = clips[std::size_t(playing)];
        if (clip.block.commands.empty()) return;
        const auto *blend = entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL);
        if (!blend || blend->AnimNum() != clip.animNumber) return; // replaced: its commands stop
        const int start = blend->GetStartTime(), end = blend->GetEndTime();
        if ((end > 0 && from > end) || to <= start || to <= from) return;
        const int fromTime = blend->AnimTime(from), toTime = blend->AnimTime(to);
        const auto *md5 = blend->Anim()->MD5Anim(0);
        frameBlend_t first, second;
        md5->ConvertTimeToFrame(fromTime, blend->GetCycleCount(), first);
        md5->ConvertTimeToFrame(toTime, blend->GetCycleCount(), second);
        ++state.serviceCalls;
        auto &list = *lists[std::size_t(playing)];
        auto fire = [&](const wolf2009::SourceFrameCommand &command) { Fire(command, toTime); };
        const int fromFrame = fromTime <= 0 ? -1 : first.frame1;
        if (second.cycleCount > first.cycleCount) {
            // Explicit wrap for cycles: one call never wraps.
            list.Dispatch(fromFrame, clip.frames - 1, fire);
            list.Dispatch(-1, second.frame1, fire);
        } else {
            list.Dispatch(fromFrame, second.frame1, fire);
        }
    }
};

WolfSourceAIScriptedAnimator::WolfSourceAIScriptedAnimator(const std::string &actor, idAnimatedEntity *body,
                                                           std::vector<WolfSourceAIClipProfile> clips,
                                                           std::vector<std::string> declared, wolf2009::SourceGameLcg &lcg,
                                                           const WolfSourceAIDeathKeys &keys)
    : impl_(new Impl(actor, body, std::move(clips), std::move(declared), lcg, keys)) {
    ++liveAnimators;
}
WolfSourceAIScriptedAnimator::~WolfSourceAIScriptedAnimator() { --liveAnimators; }
const WolfSourceAIScriptedAnimState &WolfSourceAIScriptedAnimator::State() const { return impl_->state; }
bool WolfSourceAIScriptedAnimator::Dead() const { return impl_->state.dead; }
bool WolfSourceAIScriptedAnimator::Alive() const { return impl_->Body() != nullptr; }

bool WolfSourceAIScriptedAnimator::ScriptedAnim(const std::string &name, int blendFrames, int numLoops, bool endWithIdle,
                                                const std::string &task, std::string &effect) {
    auto &p = *impl_;
    auto &s = p.state;
    if (s.dead) { effect = "dead AI (ai+0x954 bit 0x4): Begin returns false"; return false; }
    if (!p.Body()) { effect = "native body generation expired"; return false; }
    // Begin +0x458: scripted bit 0x20, endWithIdle in bit 0x10, movement
    // style cleared; outside a vehicle the movement stops (+0x4e0).
    s.scripted = true;
    s.endWithIdle = endWithIdle;
    ++s.scriptedAnims;
    std::string why;
    const int index = p.Lookup(name, why);
    if (index < 0) {
        ++s.scriptedFailures;
        if (why == "missing") {
            // The scripted bit set by Begin stays set (Static).
            s.lastMissing = "Missing '" + name + "' animation on '" + p.actor + "' (" + task + ")";
            gameLocal.Warning("%s", s.lastMissing.c_str());
            effect = s.lastMissing + "; scripted bit stays set; returned false";
        } else {
            effect = "refused: " + why + "; scripted bit stays set; returned false";
        }
        return false;
    }
    if (numLoops < 0) {
        ++s.scriptedFailures;
        effect = "refused: numLoops -1 maps to the unread global cycle default 0x205ecea4";
        return false;
    }
    p.scriptedIndex = index;
    // Turn toward the current yaw (0x2019da60): no native turn is pending.
    p.Play(index, blendFrames, numLoops);
    // 0x20166070, cancel action, clear the state thread, post the pair.
    p.thread = Impl::Thread::ScriptedAnim;
    s.scriptedStartMs = gameLocal.time;
    s.statePosts.push_back("State_ScriptedAnim");
    s.statePosts.push_back("State_ScriptedStop");
    effect = "ScriptedAnim " + p.clips[std::size_t(index)].logicalName + " " + (numLoops ? "cycle" : "play") +
             " blend=" + std::to_string(wolf2009::SourceFrameToMs(blendFrames)) + "ms; " + s.lastLookup +
             "; posted State_ScriptedAnim/State_ScriptedStop";
    return true;
}

bool WolfSourceAIScriptedAnimator::AnimDoneByName(const std::string &name, int blendFrames, int &remaining,
                                                  std::string &current) const {
    const auto &p = *impl_;
    remaining = -1;
    current.clear();
    auto *entity = p.Body();
    if (!entity) return true;
    const auto *blend = entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL);
    if (!blend || !blend->AnimNum() || !blend->Anim()) return true;
    current = blend->AnimFullName();
    // 0x20417430: full name or base name, case-insensitive.
    if (!FoldEqual(name, current) && !FoldEqual(name, wolf2009::SourceAnimBaseName(current))) return true;
    if (blend->GetEndTime() < 0) return false;
    remaining = blend->GetEndTime() - gameLocal.time;
    return wolf2009::SourceAnimRemainingDone(remaining, blendFrames);
}

bool WolfSourceAIScriptedAnimator::Kill(bool bypass, const std::string &deathOverride, std::string &effect) {
    auto &p = *impl_;
    auto &s = p.state;
    if (s.dead) { effect = "already dead; <kill> recorded without a second death"; return true; }
    if (!p.Body()) { effect = "native body generation expired"; return false; }
    s.dead = true;
    ++s.kills;
    s.killFlags = bypass ? 0x11 : 0x1;
    s.killTimeMs = gameLocal.time;
    s.scripted = false;
    p.thread = Impl::Thread::None;
    // Death-clip predicate +0x5e4 with the unread terms (ai+0x11b0 bit 0x4,
    // ai+0x11b3 bit 0x2, damage flag 0x400, physics state, action ai+0x2a58)
    // all clear for this profile.
    std::string predicate;
    if (!p.keys.playDeathAnim) { s.deathPredicate = false; predicate = "playDeathAnim clear"; }
    else if (!p.keys.ragdollOnDeath) { s.deathPredicate = true; predicate = "playDeathAnim set, ragdollOnDeath clear"; }
    else { s.deathPredicate = !(s.killFlags & 0x10); predicate = "ragdollOnDeath set: !(flags & 0x10)"; }
    effect = "<kill> flags=0x" + std::string(bypass ? "11" : "1") + " damage=0; predicate +0x5e4=" +
             (s.deathPredicate ? "1" : "0") + " (" + predicate + ")";
    if (!s.deathPredicate) {
        s.ragdoll = "refused: ragdoll-on-death (AF physics) is outside SourceAIScriptedAnimV1";
        s.deathPlayback = "no death clip";
        effect += "; " + s.ragdoll;
    } else {
        // Choice +0x69c: override first, without an existence check.
        if (!deathOverride.empty()) { s.deathChoice = deathOverride; s.deathChoiceSource = "override ai+0x388c (unchecked)"; }
        else if (!p.keys.deathAnimOverride.empty()) {
            s.deathChoiceSource = "spawn arg deathAnimOverride";
            std::string why;
            auto lcg = p.lcg; // existence check 0x200173a0 does not draw
            const auto found = wolf2009::LookupSourceClipV1(p.declared, p.keys.deathAnimOverride, lcg);
            if (found.status == wolf2009::SourceClipLookup::Found) s.deathChoice = p.keys.deathAnimOverride;
        }
        if (s.deathChoice.empty()) {
            s.deathChoiceSource = "computed directional choice";
            s.deathPlayback = "refused: the computed directional death choice is not recovered";
        } else {
            std::string why;
            const int index = p.Lookup(s.deathChoice, why);
            if (index < 0) {
                s.deathPlayback = "failed: unchecked death choice '" + s.deathChoice + "' " +
                                  (why == "missing" ? std::string("names an absent clip") : why);
            } else {
                // +0x31c(name, 4, 1.0): play with 4 blend frames.
                p.Play(index, 4, 0);
                s.deathClipPlayed = true;
                s.deathPlayback = "played " + s.deathChoice + " with 4 blend frames";
                if (p.clips[std::size_t(index)].block.HasFlag(wolf2009::SourceAnimFlag_NoInteractiveRagdoll)) {
                    s.ragdoll = "deferred: clip carries NO_INTERACTIVE_RAGDOLL";
                } else {
                    s.ragdoll = "refused: immediate interactive ragdoll (0x20012330) is outside SourceAIScriptedAnimV1";
                }
                s.waitingDeathAnim = true;
                s.statePosts.push_back("State_Wait_DeathAnim");
            }
        }
        effect += "; choice=" + (s.deathChoice.empty() ? std::string("-") : s.deathChoice) + " (" + s.deathChoiceSource +
                  "); " + s.deathPlayback + "; ragdoll " + s.ragdoll;
    }
    gameLocal.Printf("WOLF2009_SOURCE_AI_KILL actor=%s profile=%s flags=0x%x predicate=%d choice=%s played=%d time=%d "
                     "effect=\"%s\"\n",
                     p.actor.c_str(), Profile, s.killFlags, int(s.deathPredicate),
                     s.deathChoice.empty() ? "-" : s.deathChoice.c_str(), int(s.deathClipPlayed), s.killTimeMs, effect.c_str());
    return true;
}

void WolfSourceAIScriptedAnimator::Update(bool scriptedManual) {
    auto &p = *impl_;
    auto &s = p.state;
    auto *entity = p.Body();
    if (!entity || p.busy) return;
    struct Busy { bool &b; explicit Busy(bool &v) : b(v) { b = true; } ~Busy() { b = false; } } busy(p.busy);
    // State thread: State_ScriptedAnim waits while the scripted bit is set
    // and AnimDone(ai+0x38d4, 4) is false; State_ScriptedStop then runs
    // EndScripted(false).
    if (p.thread == Impl::Thread::ScriptedAnim) {
        int remaining = -1;
        if (!s.scripted || p.AnimDone(p.scriptedIndex, 4, remaining)) {
            p.thread = Impl::Thread::None;
            ++s.scriptedStops;
            s.scriptedStopMs = gameLocal.time;
            s.scriptedStopRemainingMs = remaining;
            if (!s.dead) {
                if (scriptedManual) s.statePosts.push_back("Wait_ScriptedManual");
                s.statePosts.push_back("initial-state(+0x76c)");
                s.scripted = false;
                ++s.idleRefusals;
            }
            gameLocal.Printf("WOLF2009_SOURCE_AI_SCRIPTED_STOP actor=%s profile=%s clip=%s time=%d remaining=%d "
                             "manual=%d idle=\"refused: the initial state's default idle name is not recovered; the clip holds\"\n",
                             p.actor.c_str(), Profile, s.currentClip.c_str(), s.scriptedStopMs, remaining, int(scriptedManual));
        }
    }
    entity->UpdateAnimation();
    p.Service();
}

int Wolf2009_LiveSourceAIScriptedAnimators() { return liveAnimators; }
