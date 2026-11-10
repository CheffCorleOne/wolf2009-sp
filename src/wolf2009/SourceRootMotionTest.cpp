// SPDX-License-Identifier: GPL-3.0-or-later
// SourceAIMoveV1 portable checks. Synthetic always; with --source-base the
// owner's read-only SP/base clips from docs/AI-MOVE.md §2.3/§5.1.
#include "SourceRootMotion.h"
#include "StreamMounts.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
using namespace wolf2009;
using namespace wolf2009::rootmotion;
const double kPi = 3.14159265358979323846;
namespace {
unsigned checks = 0;
void Need(bool value, const std::string& text) {
    ++checks;
    if (!value) throw std::runtime_error(text);
}
bool Near(double a, double b, double eps) { return std::abs(a - b) <= eps; }

void Synthetic() {
    // A 5-frame clip, 1/6 s spacing: walks +X 10 per frame, then turns left.
    std::vector<Motion> s(5);
    for (int i = 0; i < 5; ++i) s[i].x = 10.0 * i;
    s[4].yaw = kPi / 2;
    const auto f = ReferenceFrame::FromSamples("synthetic", 4.0 / 6.0, s);
    Need(Near(f.Sample(1.0 / 12).x, 5.0, 1e-12), "linear sample between frames");
    Need(Near(f.Sample(99).x, 40.0, 1e-12) && Near(f.Sample(-1).x, 0, 1e-12), "sample clamps");
    const auto d = f.Delta(0.5, 4.0 / 6.0);
    Need(Near(d.x, 10, 1e-12) && Near(d.yaw, kPi / 2, 1e-12), "delta translation and yaw difference");
    // Local-to-t0: after the turn, a straight delta is still +X locally.
    std::vector<Motion> t(3);
    t[1].yaw = kPi / 2;
    t[2].yaw = kPi / 2;
    t[2].y = 10; // world +Y after a left quarter turn is local +X
    const auto g = ReferenceFrame::FromSamples("turned", 2.0 / 24, t);
    const auto l = g.Delta(1.0 / 24, 2.0 / 24);
    Need(Near(l.x, 10, 1e-9) && Near(l.y, 0, 1e-9), "delta is local to the t0 frame");
    // Loop: straight run, 18 intervals, 126.2 per cycle.
    std::vector<Motion> r(19);
    for (int i = 0; i < 19; ++i) r[i].x = 126.2 * i / 18.0;
    const auto run = ReferenceFrame::FromSamples("run", 0.75, r);
    const auto wrap = run.LoopDelta(0.7, 0.1);
    Need(Near(wrap.x, run.Delta(0.7, 0.75).x + run.Delta(0, 0.05).x, 1e-9), "wrap delta is the sum of its parts");
    Need(Near(run.LoopDelta(0, 7.5).x, 1262.0, 1e-6), "ten cycles accumulate 1262");
    Need(Near(run.LoopModelDelta(0.7, 0.1).x, run.ModelDelta(0.7, 0.75).x + run.ModelDelta(0, 0.05).x, 1e-9) &&
         Near(run.LoopModelDelta(0, 7.5).x, 1262.0, 1e-6), "looping model delta sums each wrap");
    // A turning loop composes in the turned frame.
    std::vector<Motion> q(2);
    q[1].x = 10;
    q[1].yaw = kPi / 2;
    const auto quarter = ReferenceFrame::FromSamples("quarter", 1.0, q);
    const auto two = quarter.LoopDelta(0, 2.0);
    Need(Near(two.x, 10, 1e-9) && Near(two.y, 10, 1e-9) && Near(two.yaw, kPi, 1e-12), "looping yaw rotates the next cycle");

    // Style resolver (§3.2).
    auto st = ResolveMoveStyleV1(0x11, false, false, false, false);
    Need(st.forced == Gait::Run && st.running && !st.dashing, "0x11 -> running");
    st = ResolveMoveStyleV1(0x12, false, false, false, false);
    Need(st.forced == Gait::Walk && !st.running, "0x12 -> walking");
    st = ResolveMoveStyleV1(0x05, false, false, false, false);
    Need(st.running && st.dashing, "0x05 -> running and dashing");
    Need(ResolveMoveStyleV1(0x2, true, false, false, false).forced == Gait::Run, "noWalk first");
    Need(ResolveMoveStyleV1(0x1, false, true, false, false).forced == Gait::Walk, "noRun before scripted_run");
    Need(ResolveMoveStyleV1(0, false, false, false, false).forced == Gait::None, "no forced choice");
    Need(MoveClipNameV1(ResolveMoveStyleV1(0x11, false, false, false, false), "std_nil_nml_", "", "") == "std_nil_nml_run",
         "greeter run clip name");
    Need(MoveClipNameV1(ResolveMoveStyleV1(0x11, false, false, false, false), "std_nil_nml_", "my_run", "my_walk") == "my_run",
         "run override first");
    Need(MoveClipNameV1(ResolveMoveStyleV1(0x2, false, false, false, false), "std_nil_nml_", "my_run", "my_walk") == "my_walk",
         "walk override first");
    Need(MoveClipNameV1(ResolveMoveStyleV1(0x5, false, false, false, false), "std_nil_nml_", "", "") == "std_nil_nml_dash",
         "dash suffix");
    Need(CivilianUnarmedPrefixV1("model_civilian_male_04") == "std_nil_nml_" && CivilianUnarmedPrefixV1("model_wehrmacht").empty(),
         "prefix table admits only the greeter model");

    // Turning (§3.4).
    TurnRates rates;
    rates.idle = 180;
    rates.running = 420;
    TurnInput in;
    in.yawIdeal = -97.7;
    in.yawCurrent = 0;
    in.elapsedMs = 16;
    Need(Near(TurnStepV1(rates, in), -2.88, 1e-9), "idle rate clamp 180 deg/s");
    in.running = true;
    Need(Near(TurnStepV1(rates, in), -6.72, 1e-9), "running rate");
    in.dashing = true;
    Need(Near(TurnStepV1(rates, in), -48.0, 1e-9), "dash rate 3000");
    in = TurnInput();
    in.yawIdeal = 170;
    in.yawCurrent = -170;
    in.elapsedMs = 1000;
    Need(Near(TurnStepV1(rates, in), 170, 1e-9),
         "shortest way across the seam");
    Need(Near(Normalize180(-180), 180, 0) && Near(Normalize180(540), 180, 0), "normalize to (-180, 180]");
    double cur = 10.005;
    Need(FacingIdealV1(10, cur) && cur == 10, "facing snap below 0.01");
    cur = 10.02;
    Need(!FacingIdealV1(10, cur) && cur == 10.02, "no snap at 0.02");
}

void Source(const std::string& base) {
    const std::map<std::string, std::string> paths{{"global", base + "/maps/game/global/global.spk"},
                                                   {"trainyard", base + "/maps/game/trainyard/trainyard.mpk"}};
    StreamMounts mounts([&](const std::string& key) -> std::unique_ptr<std::istream> {
        const auto found = paths.find(key);
        if (found == paths.end()) return std::unique_ptr<std::istream>();
        return std::unique_ptr<std::istream>(new std::ifstream(found->second, std::ios::binary));
    });
    mounts.Mount("global");
    mounts.Mount("trainyard");
    const auto read = [&](const std::string& mount, const std::string& name) {
        std::vector<StreamResource> found;
        for (const auto& resource : mounts.Find("hkxa", name).matches)
            if (resource.mountId == mount) found.push_back(resource);
        Need(found.size() == 1, "HKXA member missing/ambiguous: " + name);
        return mounts.Read(found.front());
    };
    const auto global = read("global", "maps/game/global/global");
    const auto trainyard = read("trainyard", "maps/game/trainyard/trainyard");
    const std::string hm = "models/characters/_humanoid_male/";

    const auto run = ReferenceFrame::Admit(SplineClipData::Load(global, hm + "std_nil_nml_run.hkx"));
    Need(run.State() == FrameState::Admitted, "std_nil_nml_run admitted: " + run.Reason());
    Need(run.Frames() == 19 && Near(run.Frame(18).x, 126.20, 0.01) && Near(run.Frame(18).y, 0, 0.01), "run end translation");
    for (std::size_t i = 1; i < run.Frames(); ++i)
        Need(Near(run.Frame(i).x - run.Frame(i - 1).x, 7.011, 1e-3), "run per-frame step");
    // The clip ends 0.0004 rad above w0, so ten composed cycles curve by ~2 units.
    Need(Near(run.LoopDelta(0, 7.5).x, 10 * run.Frame(18).x, 0.05) && Near(run.LoopDelta(0, 7.5).x, 1262.0, 0.1),
         "ten run cycles");
    std::cout << "ROOT_MOTION clip=std_nil_nml_run frames=" << run.Frames() << " end_x=" << run.Frame(18).x
              << " speed=" << run.Frame(18).x / run.Duration() << "\n";

    for (const char* turn : {"lt_45", "rt_45", "lt_135", "rt_135"}) {
        const std::string name = hm + "st1_nil_nml_to_run_" + turn + ".hkx";
        const auto f = ReferenceFrame::Admit(SplineClipData::Load(global, name));
        Need(f.State() == FrameState::Admitted, name + ": " + f.Reason());
        const double expected = (turn[0] == 'l' ? 1 : -1) * (turn[3] == '4' ? kPi / 4 : 3 * kPi / 4);
        Need(f.W0() == 0.0009765625 && Near(f.Frame(f.Frames() - 1).yaw + f.W0(), expected, 1e-5),
             name + " absolute end yaw " + std::to_string(f.Frame(f.Frames() - 1).yaw + f.W0()));
    }
    const auto stop = ReferenceFrame::Admit(SplineClipData::Load(global, hm + "std_nil_nml_run_to_st1.hkx"));
    Need(stop.State() == FrameState::Admitted && Near(stop.Frame(stop.Frames() - 1).x, 126.22, 0.01), "run_to_st1 forward 126.22");

    const auto door = ReferenceFrame::Admit(SplineClipData::Load(trainyard, hm + "srp_tyd_nml_train_door_open.hkx"));
    Need(door.State() == FrameState::Admitted && door.Frames() == 182, "door-open clip admitted: " + door.Reason());
    for (std::size_t i = 0; i <= 147; ++i)
        Need(door.Frame(i).x == 0 && door.Frame(i).y == 0 && Near(door.Frame(i).yaw, 0, 1e-6), "door-open still through frame 147");
    Need(Near(door.Frame(181).x, -76.45, 0.01) && Near(door.Frame(181).y, 82.03, 0.01) && Near(door.Frame(181).yaw, 2.356, 1e-3),
         "door-open end");

    const auto train = ReferenceFrame::Admit(SplineClipData::Load(trainyard, "models/vehicles/train_engine/train_move.hkx"));
    Need(train.State() == FrameState::Refused && train.Reason().find("axes") != std::string::npos, "train_move refuses by axes");
    std::cout << "ROOT_MOTION train_move refused=\"" << train.Reason() << "\"\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        Synthetic();
        std::string base;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--source-base") base = argv[i + 1];
        if (!base.empty()) Source(base);
        std::cout << "SOURCE_ROOT_MOTION_OK checks=" << checks << " source=" << (base.empty() ? 0 : 1) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SOURCE_ROOT_MOTION_FAIL checks=" << checks << " reason=" << e.what() << "\n";
        return 1;
    }
}
