// SPDX-License-Identifier: GPL-3.0-or-later
// Authored token lists only (no original data).
#include "SourceAnimCommands.h"
#include <iostream>
#include <stdexcept>

namespace wolf2009 {
namespace {
unsigned checks = 0;
void Need(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template <class F> bool Throws(F body) {
    try { body(); } catch (const std::exception &) { return true; }
    return false;
}
std::vector<std::string> Words(const std::string &text) {
    std::vector<std::string> out;
    std::string word;
    for (char c : text) {
        if (c == ' ') { if (!word.empty()) out.push_back(word); word.clear(); }
        else word += c;
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

void Blocks() {
    const auto kc = ParseSourceAnimBlockV1(
        Words("no_interactive_ragdoll frame 207 fx_offset fx_a head_channel 0 0 0 0 0 0 frame 207 trigger decal_a "
              "frame 230 interactive_ragdoll"), 283);
    Need(kc.HasFlag(SourceAnimFlag_NoInteractiveRagdoll) && kc.flags == (1u << 19) && kc.commands.size() == 3,
         "flag and three commands");
    Need(kc.commands[0].frame == 206 && kc.commands[0].code == SourceFrameCommandCode::FxOffset &&
         kc.commands[0].arguments.size() == 8 && kc.commands[1].name == "trigger" &&
         kc.commands[1].arguments == std::vector<std::string>{"decal_a"} && kc.commands[2].frame == 229 &&
         kc.commands[2].arguments.empty(), "one-based frames stored zero-based with arguments");
    const auto nzi = ParseSourceAnimBlockV1(Words("frame 112 speak lipsync_x frame 207 trigger gun"), 273);
    Need(!nzi.flags && nzi.commands.size() == 2 && nzi.commands[0].frame == 111 && nzi.commands[1].frame == 206,
         "speak and trigger frames");
    const auto list = ParseSourceAnimBlockV1(Words("frame 1,5 trigger a frame 3 , 4 trigger b rate 0.5 NO_TRANS_POSE"), 10);
    Need(list.commands.size() == 4 && list.commands[0].frame == 0 && list.commands[1].frame == 4 &&
         list.commands[2].frame == 2 && list.commands[3].frame == 3 && list.hasRate && list.rate == .5f &&
         list.HasFlag(20), "frame lists, rate and case-insensitive flag");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 0 trigger a"), 10); }), "frame 0 refused");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 11 trigger a"), 10); }), "frame past clip refused");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 2 Trigger a"), 10); }), "command names are case-sensitive");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 2 launch_missile a"), 10); }), "unknown command refused");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 2 speak_random a"), 10); }), "unadmitted arity refused");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 2 trigger a b"), 10); }), "extra argument refused");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("bogus_flag"), 10); }), "unknown flag refused");
    Need(Throws([] { ParseSourceAnimBlockV1(Words("frame 2"), 10); }), "truncated frame refused");
    Need(SourceAnimFlagIndex("No_Interactive_Ragdoll") == 19 && SourceAnimFlagIndex("x") < 0 &&
         std::string(SourceAnimFlagName(19)) == "NO_INTERACTIVE_RAGDOLL" && !SourceAnimFlagName(22), "flag table");
}

void Dispatch() {
    const auto block = ParseSourceAnimBlockV1(
        Words("frame 1 trigger first frame 207 fx_offset f j 0 0 0 0 0 0 frame 207 trigger t frame 230 interactive_ragdoll"), 283);
    SourceFrameCommandList commands(block.commands);
    std::vector<std::string> fired;
    auto fire = [&](const SourceFrameCommand &c) { fired.push_back(c.name + "@" + std::to_string(c.frame)); };
    Need(commands.EntryCount() == 3, "entries grouped by frame");
    Need(commands.Dispatch(-1, 0, fire) == 1 && fired.back() == "trigger@0", "explicit start fires frame 0");
    Need(commands.Dispatch(0, 205, fire) == 0, "nothing before 206");
    Need(commands.Dispatch(205, 206, fire) == 2 && fired[1] == "fx_offset@206" && fired[2] == "trigger@206",
         "frame 206 fires in textual order");
    Need(commands.Dispatch(206, 206, fire) == 0, "an empty range fires nothing");
    Need(commands.Dispatch(206, 228, fire) == 0 && commands.Dispatch(228, 229, fire) == 1, "ragdoll at 229 once");
    Need(commands.Dispatch(229, 282, fire) == 0 && commands.Dispatch(282, 3, fire) == 0, "no wrap within one call");
    const auto restarts = commands.Restarts();
    Need(commands.Dispatch(-1, 206, fire) == 3 && commands.Restarts() == restarts + 1,
         "time going back restarts the cursor and replays once");
    Need(fired.size() == 7, "total fired");
}

void Timing() {
    Need(SourceFrameToMs(2) == 83 && SourceFrameToMs(4) == 166 && SourceFrameToMs(24) == 1000, "FRAME2MS integer");
    Need(SourceAnimRemainingDone(83, 2) && !SourceAnimRemainingDone(84, 2) && SourceAnimRemainingDone(166, 4) &&
         !SourceAnimRemainingDone(167, 4), "remaining boundary");
}

void Lookup() {
    const std::vector<std::string> clips = {"walk1", "walk2", "walk", "run_one", "idle3", "Run_one"};
    SourceGameLcg lcg;
    lcg.seed = 1;
    auto exact = LookupSourceClipV1(clips, "walk2", lcg);
    Need(exact.status == SourceClipLookup::Found && exact.exact && exact.clip == "walk2" && lcg.seed == 1,
         "trailing digit is exact and does not draw");
    Need(LookupSourceClipV1(clips, "walk3", lcg).status == SourceClipLookup::Missing && lcg.seed == 1, "exact miss");
    auto random = LookupSourceClipV1(clips, "walk", lcg);
    const std::uint32_t seed = 1u * 214013u + 2531011u;
    Need(random.status == SourceClipLookup::Found && random.candidates == 3 && lcg.seed == seed &&
         random.clip == clips[std::size_t((std::uint64_t((seed >> 17) & 0x7fff) * 3) >> 15)], "base-name draw on the LCG");
    auto one = LookupSourceClipV1(clips, "run_one", lcg);
    Need(one.status == SourceClipLookup::Found && one.candidates == 1 && one.clip == "run_one" && lcg.seed != seed,
         "single base-name match still advances the LCG (case-sensitive)");
    const auto before = lcg.seed;
    Need(LookupSourceClipV1(clips, "", lcg).status == SourceClipLookup::Missing && lcg.seed == before, "empty name");
    Need(LookupSourceClipV1(clips, "absent", lcg).status == SourceClipLookup::Missing && lcg.seed == before,
         "no base-name match does not draw");
    Need(SourceAnimBaseName("kc_01") == "kc_" && SourceAnimBaseName("nzi_one") == "nzi_one", "base name");
}
} // namespace

int RunSourceAnimCommandTests() {
    Blocks();
    Dispatch();
    Timing();
    Lookup();
    std::cout << "source anim command checks: " << checks << " passed\n";
    return 0;
}
} // namespace wolf2009

#ifdef WOLF2009_SOURCE_ANIM_COMMANDS_TEST_MAIN
int main() {
    try {
        return wolf2009::RunSourceAnimCommandTests();
    } catch (const std::exception &error) {
        std::cerr << "source anim command test failed: " << error.what() << "\n";
        return 1;
    }
}
#endif
