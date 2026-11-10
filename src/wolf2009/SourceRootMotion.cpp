// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceRootMotion.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace wolf2009 {
namespace rootmotion {
namespace {
float Bits(std::uint32_t word) {
    float value;
    std::memcpy(&value, &word, 4);
    return value;
}
std::uint32_t Word(float value) {
    std::uint32_t word;
    std::memcpy(&word, &value, 4);
    return word;
}
} // namespace

Motion Rotate(const Motion& m, double yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    Motion out = m;
    out.x = c * m.x - s * m.y;
    out.y = s * m.x + c * m.y;
    return out;
}

Motion Compose(const Motion& a, const Motion& b) {
    const Motion r = Rotate(b, a.yaw);
    Motion out;
    out.x = a.x + r.x;
    out.y = a.y + r.y;
    out.z = a.z + b.z;
    out.yaw = a.yaw + b.yaw;
    return out;
}

ReferenceFrame ReferenceFrame::Admit(const SplineClipData& clip) {
    ReferenceFrame out;
    out.clip_ = clip.name;
    const auto& raw = clip.referenceFrame;
    auto refuse = [&](const std::string& why) {
        out.state_ = FrameState::Refused;
        out.reason_ = "SourceReferenceFrameV1: " + why;
        out.samples_.clear();
        return out;
    };
    if (!raw.present) {
        if (clip.extractedMotion.hasDataFixup)
            return refuse("extracted-motion class " + clip.extractedMotion.targetType + " is not hkaDefaultAnimatedReferenceFrame");
        out.reason_ = "no root motion";
        return out;
    }
    const std::uint32_t up[4] = {0, 0, Word(1.0f), 0}, forward[4] = {Word(1.0f), 0, 0, 0};
    for (unsigned i = 0; i < 4; ++i)
        if (raw.up[i] != up[i] || raw.forward[i] != forward[i])
            return refuse("axes are not up (0,0,1,0) / forward (1,0,0,0)");
    if (raw.duration != Word(float(clip.input.duration)))
        return refuse("duration differs from the clip duration");
    if (raw.samples.size() != clip.input.numFrames || raw.samples.size() < 2)
        return refuse("sample count differs from the clip frame count");
    const double duration = Bits(raw.duration);
    if (!(duration > 0) || !std::isfinite(duration)) return refuse("non-positive duration");
    const double w0 = Bits(raw.samples[0][3]);
    for (const auto& s : raw.samples) {
        Motion m;
        m.x = Bits(s[0]);
        m.y = Bits(s[1]);
        m.z = Bits(s[2]);
        const double w = Bits(s[3]);
        if (!std::isfinite(m.x) || !std::isfinite(m.y) || !std::isfinite(m.z) || !std::isfinite(w))
            return refuse("non-finite sample");
        m.yaw = w - w0;
        out.samples_.push_back(m);
    }
    if (out.samples_[0].x != 0 || out.samples_[0].y != 0 || out.samples_[0].z != 0)
        return refuse("sample 0 translation is not zero");
    out.duration_ = duration;
    out.w0_ = w0;
    out.frameDuration_ = duration / double(out.samples_.size() - 1);
    if (std::abs(out.frameDuration_ - clip.input.frameDuration) > 1e-5)
        return refuse("frame spacing differs from the clip frame duration");
    out.state_ = FrameState::Admitted;
    out.reason_.clear();
    return out;
}

ReferenceFrame ReferenceFrame::FromSamples(const std::string& clip, double duration, const std::vector<Motion>& samples) {
    if (samples.size() < 2 || !(duration > 0)) throw std::runtime_error("synthetic reference frame needs two samples");
    ReferenceFrame out;
    out.clip_ = clip;
    out.state_ = FrameState::Admitted;
    out.duration_ = duration;
    out.frameDuration_ = duration / double(samples.size() - 1);
    out.samples_ = samples;
    return out;
}

Motion ReferenceFrame::Sample(double t) const {
    if (state_ != FrameState::Admitted) throw std::runtime_error("reference frame is not admitted");
    t = std::max(0.0, std::min(t, duration_));
    const double u = t / frameDuration_;
    const std::size_t last = samples_.size() - 1;
    std::size_t i = std::size_t(std::floor(u));
    if (i >= last) return samples_[last];
    const double f = u - double(i);
    const Motion& a = samples_[i];
    const Motion& b = samples_[i + 1];
    Motion m;
    m.x = a.x + (b.x - a.x) * f;
    m.y = a.y + (b.y - a.y) * f;
    m.z = a.z + (b.z - a.z) * f;
    m.yaw = a.yaw + (b.yaw - a.yaw) * f;
    return m;
}

Motion ReferenceFrame::Delta(double t0, double t1) const {
    const Motion a = Sample(t0), b = Sample(std::max(t0, t1));
    Motion d;
    d.x = b.x - a.x;
    d.y = b.y - a.y;
    d.z = b.z - a.z;
    d = Rotate(d, -a.yaw);
    d.yaw = b.yaw - a.yaw;
    return d;
}

Motion ReferenceFrame::ModelDelta(double t0, double t1) const {
    const Motion a = Sample(t0), b = Sample(std::max(t0, t1));
    Motion d;
    d.x = b.x - a.x;
    d.y = b.y - a.y;
    d.z = b.z - a.z;
    d.yaw = b.yaw - a.yaw;
    return d;
}

Motion ReferenceFrame::LoopModelDelta(double t0, double elapsed) const {
    if (state_ != FrameState::Admitted) throw std::runtime_error("reference frame is not admitted");
    if (!(elapsed >= 0) || elapsed > 3600.0) throw std::runtime_error("loop delta elapsed time out of range");
    double t = std::max(0.0, std::min(t0, duration_));
    Motion out;
    while (elapsed > 0) {
        if (t >= duration_) t = 0;
        const double step = std::min(elapsed, duration_ - t);
        const Motion d = ModelDelta(t, t + step);
        out.x += d.x;
        out.y += d.y;
        out.z += d.z;
        out.yaw += d.yaw;
        elapsed -= step;
        t += step;
    }
    return out;
}

Motion ReferenceFrame::LoopDelta(double t0, double elapsed) const {
    if (state_ != FrameState::Admitted) throw std::runtime_error("reference frame is not admitted");
    if (!(elapsed >= 0) || elapsed > 3600.0) throw std::runtime_error("loop delta elapsed time out of range");
    double t = std::max(0.0, std::min(t0, duration_));
    Motion out;
    while (elapsed > 0) {
        if (t >= duration_) t = 0;
        const double step = std::min(elapsed, duration_ - t);
        out = Compose(out, Delta(t, t + step));
        elapsed -= step;
        t += step;
    }
    return out;
}

MoveStyle ResolveMoveStyleV1(std::uint8_t styleBits, bool noWalk, bool noRun, bool globalDash, bool moveFlag11b1x4) {
    MoveStyle out;
    if (noWalk) out.forced = Gait::Run;
    else if (noRun) out.forced = Gait::Walk;
    else if (styleBits & ScriptedRun) out.forced = Gait::Run;
    else if (styleBits & ScriptedWalk) out.forced = Gait::Walk;
    out.running = out.forced == Gait::Run;
    out.dashing = globalDash || (styleBits & ScriptedDash) != 0 || moveFlag11b1x4;
    return out;
}

std::string MoveClipNameV1(const MoveStyle& style, const std::string& prefix, const std::string& runOverride,
                           const std::string& walkOverride) {
    if (style.running && !runOverride.empty()) return runOverride;
    if (!style.running && !walkOverride.empty()) return walkOverride;
    if (prefix.empty()) return std::string();
    return prefix + (style.dashing ? "dash" : style.running ? "run" : "walk");
}

std::string CivilianUnarmedPrefixV1(const std::string& modelDef) {
    return modelDef == "model_civilian_male_04" ? "std_nil_nml_" : std::string();
}

double Normalize180(double degrees) {
    double d = std::fmod(degrees, 360.0);
    if (d > 180.0) d -= 360.0;
    else if (d <= -180.0) d += 360.0;
    return d;
}

double TurnRateV1(const TurnRates& rates, const TurnInput& in) {
    if (in.rateOverride != 0) return in.rateOverride;
    if (in.dashing) return 3000.0;
    if (in.running) return rates.running;
    if (in.moving) return rates.walking;
    if (in.alert) return rates.alert;
    return rates.idle;
}

double TurnStepV1(const TurnRates& rates, const TurnInput& in) {
    const double diff = Normalize180(in.yawIdeal - in.yawCurrent);
    const double limit = TurnRateV1(rates, in) * in.elapsedMs * 0.001;
    const double step = std::max(-limit, std::min(limit, diff)) * in.turnScale;
    return Normalize180(in.yawCurrent + step);
}

bool FacingIdealV1(double yawIdeal, double& yawCurrent) {
    if (std::abs(Normalize180(yawIdeal - yawCurrent)) < 0.01) {
        yawCurrent = yawIdeal;
        return true;
    }
    return false;
}

} // namespace rootmotion
} // namespace wolf2009
