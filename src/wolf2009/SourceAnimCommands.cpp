// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceAnimCommands.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
bool FoldEqual(const std::string &a, const char *b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return i == a.size() && !b[i];
}
const char *const Flags[22] = {
    "PREVENT_IDLE_OVERRIDE", "RANDOM_CYCLE_START", "AI_NO_TURN", "AI_NO_LOOK", "AI_NO_ATTACK", "AI_LOOK_MUZZLE",
    "ANIM_TURN", "ANIM_TURN_FORCE_POSITIVE", "ANIM_TURN_FORCE_NEGATIVE", "ANIM_APPROACH_GOAL", "SYNC_CYCLE",
    "AI_DRAW_WEAPON", "AI_HIDE_WEAPON", "AI_FACE_COVER", "AI_NO_PAIN", "AI_PURE_MOTION_EXTRACTION",
    "AI_ALLOW_SLIDE_TURN", "NO_DELTA_BLEND_OUT", "FRAME_CMDS_ON_PRIMARY_ONLY", "NO_INTERACTIVE_RAGDOLL",
    "NO_TRANS_POSE", "AI_NO_DELTA_PROCESSING"};
struct CommandInfo { const char *name; SourceFrameCommandCode code; int arity; };
// Arity -1: the code is known but its argument shape is not admitted here.
const CommandInfo Commands[] = {
    {"sound", SourceFrameCommandCode::Sound, 1},
    {"sound_voice", SourceFrameCommandCode::SoundVoice, 1},
    {"trigger", SourceFrameCommandCode::Trigger, 1},
    {"trigger_alias", SourceFrameCommandCode::TriggerAlias, -1},
    // fx <name> <joint> and six numbers in the trainyard data (Inferred arity).
    {"fx_offset", SourceFrameCommandCode::FxOffset, 8},
    {"ai_enablePureMotionExtraction", SourceFrameCommandCode::EnablePureMotionExtraction, -1},
    {"ai_disablePureMotionExtraction", SourceFrameCommandCode::DisablePureMotionExtraction, -1},
    {"ai_set_eyeFocus", SourceFrameCommandCode::SetEyeFocus, 1},
    {"speak", SourceFrameCommandCode::Speak, 1},
    {"speak_no_subtitle", SourceFrameCommandCode::SpeakNoSubtitle, 1},
    {"speak_random", SourceFrameCommandCode::SpeakRandom, -1},
    {"speak_deferred", SourceFrameCommandCode::SpeakDeferred, 1},
    {"aiming_enable", SourceFrameCommandCode::AimingEnable, 0},
    {"aiming_disable", SourceFrameCommandCode::AimingDisable, 0},
    {"interactive_ragdoll", SourceFrameCommandCode::InteractiveRagdoll, 0}};
bool Digits(const std::string &text) {
    if (text.empty() || text.size() > 9) return false;
    for (char c : text) if (c < '0' || c > '9') return false;
    return true;
}
bool Structural(const std::string &word) {
    return word == "frame" || word == "rate" || SourceAnimFlagIndex(word) >= 0;
}
} // namespace

const char *SourceAnimFlagName(unsigned bit) { return bit < 22 ? Flags[bit] : nullptr; }
int SourceAnimFlagIndex(const std::string &word) {
    for (int i = 0; i < 22; ++i) if (FoldEqual(word, Flags[i])) return i;
    return -1;
}

SourceAnimBlock ParseSourceAnimBlockV1(const std::vector<std::string> &tokens, std::uint32_t frameCount) {
    SourceAnimBlock block;
    std::uint32_t order = 0;
    Need(tokens.size() <= 4096, "anim block token budget");
    for (std::size_t i = 0; i < tokens.size();) {
        const std::string &word = tokens[i++];
        Need(!word.empty() && word != "{" && word != "}", "anim block contains a nested or empty word");
        if (word == "rate") {
            Need(i < tokens.size() && !block.hasRate, "anim block rate is absent or repeated");
            std::istringstream input(tokens[i++]);
            input.imbue(std::locale::classic());
            float value = 0;
            char extra = 0;
            Need(bool(input >> value) && !(input >> extra) && std::isfinite(value) && value > 0, "anim block rate is not a positive number");
            block.hasRate = true;
            block.rate = value;
            continue;
        }
        if (word == "frame") {
            // One-based list: "207", "1,5" or separate "," tokens.
            std::vector<std::uint32_t> frames;
            std::string list;
            for (;;) {
                Need(i < tokens.size(), "anim block frame list is truncated");
                list += tokens[i++];
                if (i < tokens.size() && tokens[i] == ",") { list += tokens[i++]; continue; }
                if (!list.empty() && list.back() == ',') continue;
                break;
            }
            std::size_t at = 0;
            while (at <= list.size()) {
                const auto comma = list.find(',', at);
                const auto part = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                Need(Digits(part), "anim block frame number is not a positive decimal");
                const auto one = std::uint32_t(std::strtoul(part.c_str(), nullptr, 10));
                Need(one >= 1 && (!frameCount || one <= frameCount), "anim block frame is outside the clip");
                frames.push_back(one - 1);
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            Need(i < tokens.size(), "anim block frame command is absent");
            const std::string name = tokens[i++];
            const CommandInfo *info = nullptr;
            for (const auto &entry : Commands) if (name == entry.name) info = &entry;
            Need(info != nullptr, "anim block command '" + name + "' is outside the SourceAIScriptedAnimV1 table");
            Need(info->arity >= 0, "anim block command '" + name + "' has no admitted argument shape");
            Need(i + std::size_t(info->arity) <= tokens.size(), "anim block command '" + name + "' is truncated");
            std::vector<std::string> arguments(tokens.begin() + std::ptrdiff_t(i), tokens.begin() + std::ptrdiff_t(i + info->arity));
            i += std::size_t(info->arity);
            for (const auto &argument : arguments)
                Need(!argument.empty() && argument != "frame" && argument != "{" && argument != "}",
                     "anim block command '" + name + "' argument is structural");
            Need(i == tokens.size() || Structural(tokens[i]),
                 "anim block command '" + name + "' is followed by an unadmitted word: " + (i < tokens.size() ? tokens[i] : ""));
            for (auto frame : frames) {
                SourceFrameCommand command;
                command.frame = frame;
                command.name = name;
                command.code = info->code;
                command.arguments = arguments;
                command.order = order++;
                block.commands.push_back(command);
            }
            continue;
        }
        const int flag = SourceAnimFlagIndex(word);
        Need(flag >= 0, "anim block word '" + word + "' is neither a flag nor a command");
        block.flags |= 1u << unsigned(flag);
        block.flagWords.push_back(word);
    }
    return block;
}

SourceFrameCommandList::SourceFrameCommandList(const std::vector<SourceFrameCommand> &commands) {
    auto sorted = commands;
    std::stable_sort(sorted.begin(), sorted.end(), [](const SourceFrameCommand &a, const SourceFrameCommand &b) {
        return a.frame < b.frame;
    });
    for (const auto &command : sorted) {
        if (entries_.empty() || entries_.back().frame != command.frame) entries_.push_back(Entry{command.frame, {}});
        entries_.back().commands.push_back(command);
    }
}

std::size_t SourceFrameCommandList::Dispatch(int from, int to, const std::function<void(const SourceFrameCommand &)> &fire) {
    if (entries_.empty()) return 0;
    if (cursor_ >= entries_.size() || int(entries_[cursor_].frame) > from) {
        if (cursor_ != 0) ++restarts_;
        cursor_ = 0;
    }
    while (cursor_ < entries_.size() && int(entries_[cursor_].frame) <= from) ++cursor_;
    std::size_t fired = 0;
    while (cursor_ < entries_.size() && int(entries_[cursor_].frame) <= to) {
        for (const auto &command : entries_[cursor_].commands) {
            ++fired;
            fire(command);
        }
        ++cursor_;
    }
    return fired;
}

int SourceFrameToMs(int frames) { return frames * 1000 / 24; }
bool SourceAnimRemainingDone(int remainingMs, int blendFrames) { return remainingMs <= SourceFrameToMs(blendFrames); }

std::string SourceAnimBaseName(const std::string &name) {
    std::size_t end = name.size();
    while (end && name[end - 1] >= '0' && name[end - 1] <= '9') --end;
    return name.substr(0, end);
}

SourceClipLookup LookupSourceClipV1(const std::vector<std::string> &declared, const std::string &name, SourceGameLcg &lcg) {
    SourceClipLookup out;
    out.seedBefore = out.seedAfter = lcg.seed;
    if (name.empty()) return out;
    const char last = name.back();
    if (last >= '0' && last <= '9') {
        out.exact = true;
        for (const auto &clip : declared)
            if (clip == name) { out.status = SourceClipLookup::Found; out.clip = clip; }
        return out;
    }
    std::vector<const std::string *> matches;
    const auto base = SourceAnimBaseName(name);
    for (const auto &clip : declared) if (SourceAnimBaseName(clip) == base) matches.push_back(&clip);
    out.candidates = matches.size();
    if (matches.empty()) return out;
    const auto index = lcg.Draw(matches.size());
    out.seedAfter = lcg.seed;
    out.status = SourceClipLookup::Found;
    out.clip = *matches[index];
    return out;
}
} // namespace wolf2009
