// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_ANIM_COMMANDS_H
#define WOLF2009_SOURCE_ANIM_COMMANDS_H
// Portable model anim-block data for SourceAIScriptedAnimV1 (docs/AI-PRESENTATION.md
// §2.3, §3). Nothing here touches an entity or a native animator.
//
// Static contracts (hash-identified Gamex86.dll 7cb1cbed..., this build only):
// - anim block parser 0x203fda20: `frame <list> <command...>`, `rate <float>`,
//   any other word is an anim flag (case-insensitive, bit = 1 << table index);
// - frame lists are one-based and stored zero-based (0x204046e0);
// - command names compare case-sensitively (parser 0x204003c0);
// - the frame-command list (vftable 0x20563134, vslot +0x10 = 0x20403980)
//   dispatches (from, to] on zero-based frames, never wraps within one call,
//   and restarts its cursor when the cursor entry lies beyond `from`;
// - clip lookup 0x20017300: a name ending in a digit is an exact, case-sensitive
//   match; otherwise one clip with that base name is drawn with the game LCG
//   at 0x2063376c (seed = seed * 214013 + 2531011; index = ((seed >> 17) * n) >> 15).
// Command arities are this profile's bounded data reading of the trainyard
// clips: a command outside the arity table is refused, not guessed.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wolf2009 {
// Anim flag bit indices (table read by 0x203fda20).
enum SourceAnimFlagBit : std::uint32_t {
    SourceAnimFlag_NoInteractiveRagdoll = 19
};
const char *SourceAnimFlagName(unsigned bit); // null outside 0..21
// Returns the bit index or -1 (ASCII case-insensitive).
int SourceAnimFlagIndex(const std::string &word);

// Recovered command codes (parser 0x204003c0) used on the trainyard path.
enum class SourceFrameCommandCode : std::uint32_t {
    Sound = 0x01, SoundVoice = 0x02, Trigger = 0x10, TriggerAlias = 0x11, FxOffset = 0x1a,
    EnablePureMotionExtraction = 0x3f, DisablePureMotionExtraction = 0x40, SetEyeFocus = 0x42,
    Speak = 0x45, SpeakNoSubtitle = 0x46, SpeakRandom = 0x47, SpeakDeferred = 0x48,
    AimingEnable = 0x5d, AimingDisable = 0x5e, InteractiveRagdoll = 0x70
};
struct SourceFrameCommand {
    std::uint32_t frame = 0; // zero-based (authored one-based frame - 1)
    std::string name;
    SourceFrameCommandCode code = SourceFrameCommandCode::Trigger;
    std::vector<std::string> arguments;
    std::uint32_t order = 0; // textual order inside the block
};
struct SourceAnimBlock {
    std::uint32_t flags = 0;
    std::vector<std::string> flagWords; // verbatim, block order
    bool hasRate = false;
    float rate = 1.f;
    std::vector<SourceFrameCommand> commands; // textual order
    bool HasFlag(unsigned bit) const { return bit < 32 && (flags & (1u << bit)) != 0; }
};
// Parses the tokens strictly inside `{ ... }`. frameCount bounds one-based
// frames (1..frameCount); 0 disables that check. Throws std::runtime_error
// with a named reason for an unknown or unadmitted word, arity or frame.
SourceAnimBlock ParseSourceAnimBlockV1(const std::vector<std::string> &tokens, std::uint32_t frameCount);

// Per-clip command list with the recovered cursor rule. Entries are grouped
// by frame in ascending order; commands of one frame keep textual order
// (the insert routine 0x20404f60 was not read: Inferred).
class SourceFrameCommandList {
  public:
    explicit SourceFrameCommandList(const std::vector<SourceFrameCommand> &commands);
    // Dispatches every command with from < frame <= to. from = -1 selects
    // frame 0. Never wraps: to < from dispatches nothing. Returns the count.
    std::size_t Dispatch(int from, int to, const std::function<void(const SourceFrameCommand &)> &fire);
    std::size_t Cursor() const { return cursor_; }
    std::size_t Restarts() const { return restarts_; }
    std::size_t EntryCount() const { return entries_.size(); }
  private:
    struct Entry { std::uint32_t frame; std::vector<SourceFrameCommand> commands; };
    std::vector<Entry> entries_;
    std::size_t cursor_ = 0, restarts_ = 0;
};

// FRAME2MS: blendFrames * 1000 / 24 (integer), as AnimPlay/AnimCycle and AnimDone use.
int SourceFrameToMs(int frames);
// Remaining-time rule of AnimDone (+0x338) and AnimFinished (+0x334):
// done when remaining <= FRAME2MS(blendFrames).
bool SourceAnimRemainingDone(int remainingMs, int blendFrames);

// Shared game LCG at 0x2063376c. The initial seed of a session is Unknown;
// hosts own one generator per game and report its seed.
struct SourceGameLcg {
    std::uint32_t seed = 0;
    std::uint32_t Next15() { seed = seed * 214013u + 2531011u; return (seed >> 17) & 0x7fffu; }
    std::size_t Draw(std::size_t count) { return std::size_t((std::uint64_t(Next15()) * count) >> 15); }
};
struct SourceClipLookup {
    enum Status { Found, Missing } status = Missing;
    bool exact = false;          // trailing digit
    std::size_t candidates = 0;  // base-name matches (random path)
    std::string clip;            // exact chosen clip name
    std::uint32_t seedBefore = 0, seedAfter = 0;
};
std::string SourceAnimBaseName(const std::string &name); // strips trailing digits
SourceClipLookup LookupSourceClipV1(const std::vector<std::string> &declaredClips, const std::string &name,
                                    SourceGameLcg &lcg);
int RunSourceAnimCommandTests();
} // namespace wolf2009
#endif
