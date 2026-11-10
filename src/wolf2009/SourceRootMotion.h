// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_ROOT_MOTION_H
#define WOLF2009_SOURCE_ROOT_MOTION_H
// SourceAIMoveV1 portable layer (docs/AI-MOVE.md §5.1).
// - SourceReferenceFrameV1: admission of an hkaDefaultAnimatedReferenceFrame
//   with up +Z, forward +X, duration equal to the clip's, one sample per frame
//   and zero translation at sample 0. Everything else is a named refusal; a
//   clip without the pointer is the distinct "no root motion" state.
// - LinearFrameSampleV1: linear interpolation between neighbouring samples
//   (Inferred Havok sampling). Yaw is kept relative to sample 0 (w - w0).
// - Deltas are local to the start time's frame (Havok getDeltaReferenceFrame
//   shape): translation rotated by -yaw(t0), yaw as a difference.
// - Move style / clip resolvers and the ordinary turn step from §3.2/§3.4.
#include "SplineClipData.h"
#include <cstdint>
#include <string>
#include <vector>
namespace wolf2009 {
namespace rootmotion {

struct Motion {
    double x = 0, y = 0, z = 0, yaw = 0; // model space units, radians (left +)
};

enum class FrameState { Absent, Admitted, Refused };

class ReferenceFrame {
  public:
    static ReferenceFrame Admit(const SplineClipData& clip);
    FrameState State() const { return state_; }
    const std::string& Reason() const { return reason_; }
    const std::string& Clip() const { return clip_; }
    double Duration() const { return duration_; }
    double FrameDuration() const { return frameDuration_; }
    // Raw w of sample 0 (2^-10 in every conforming clip). Turn clips end at an
    // absolute w of exactly +-pi/4 or +-3pi/4, so the true end yaw is yaw + w0.
    double W0() const { return w0_; }
    std::size_t Frames() const { return samples_.size(); }
    const Motion& Frame(std::size_t i) const { return samples_.at(i); }
    // Cumulative motion at t (clamped to [0, duration]) in clip-start space.
    Motion Sample(double t) const;
    // Motion from t0 to t1 (t0 <= t1, both clamped), local to the frame at t0.
    Motion Delta(double t0, double t1) const;
    // AnimatorModelDeltaV1: S(t1) - S(t0) in clip model space (the animator's
    // translation column, AI-MOVE §3.3 step 5), yaw as a difference. The
    // retail door-open fit (§2.4) applies this, rotated by the actor axis.
    Motion ModelDelta(double t0, double t1) const;
    // Looping AnimatorModelDeltaV1: from clip time t0 forward by elapsed
    // seconds, summing model-space differences across each wrap.
    Motion LoopModelDelta(double t0, double elapsed) const;
    // Looping clip: from t0 forward by elapsed seconds, wrapping at the end.
    Motion LoopDelta(double t0, double elapsed) const;
    // Synthetic construction for portable tests (already admitted samples).
    static ReferenceFrame FromSamples(const std::string& clip, double duration, const std::vector<Motion>& samples);

  private:
    FrameState state_ = FrameState::Absent;
    std::string reason_, clip_;
    double duration_ = 0, frameDuration_ = 0, w0_ = 0;
    std::vector<Motion> samples_;
};

// Composes b after a (b expressed in a's end frame).
Motion Compose(const Motion& a, const Motion& b);
// Rotates (x, y) by yaw radians.
Motion Rotate(const Motion& m, double yaw);

// §3.1 ai+0x11b5 style bits and the relevant move flags.
enum StyleBits : std::uint8_t { ScriptedRun = 0x1, ScriptedWalk = 0x2, ScriptedDash = 0x4, ScriptedEndInCrouch = 0x8,
                                ScriptedPoseCombat = 0x10, ScriptedPoseStealth = 0x20 };
enum class Gait { None, Walk, Run };
struct MoveStyle {
    Gait forced = Gait::None;
    bool running = false, dashing = false;
};
// Forced run resolver 0x20198040 and dash resolver 0x20199780 (the global
// setting is not recovered and is passed explicitly as false by callers).
MoveStyle ResolveMoveStyleV1(std::uint8_t styleBits, bool noWalk, bool noRun, bool globalDash, bool moveFlag11b1x4);
// Move clip name 0x201681c0 with the prefix supplied by a named profile.
std::string MoveClipNameV1(const MoveStyle&, const std::string& prefix, const std::string& runOverride,
                           const std::string& walkOverride);
// CivilianUnarmedPrefixV1: the only admitted prefix table entry until the
// prefix predicates (0x20167c00) are recovered. Empty means refused.
std::string CivilianUnarmedPrefixV1(const std::string& modelDef);

// §3.4 ordinary turn and FacingIdeal.
double Normalize180(double degrees);
struct TurnRates {
    double idle = 5.0, alert = 4.5, walking = 5.0, running = 5.0; // setter 0x20184160 defaults
};
struct TurnInput {
    double yawIdeal = 0, yawCurrent = 0, elapsedMs = 0, rateOverride = 0, turnScale = 1.0;
    bool dashing = false, running = false, moving = false, alert = false;
};
double TurnRateV1(const TurnRates&, const TurnInput&);
// Returns the new yaw_current (normalized to (-180, 180]).
double TurnStepV1(const TurnRates&, const TurnInput&);
// True when |normalize180(ideal - current)| < 0.01; snaps current to ideal.
bool FacingIdealV1(double yawIdeal, double& yawCurrent);

} // namespace rootmotion
} // namespace wolf2009
#endif
