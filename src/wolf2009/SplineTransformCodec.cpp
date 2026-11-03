// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Havok Format Library, Copyright (C) 2016-2023 Lukas Cone.
// PredatorCZ/HavokLib ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b:
// source/hka_spline_decompressor.{hpp,cpp}. Modified 2026: checked C++11 scalar
// reads, owning curves, explicit unsupported status, budgets, source clock,
// exact TRS boundaries, quaternion normalization and scale qualification.
// See tests/spline-codec/PROVENANCE.md and the repository GPL-3.0 LICENSE.
#include "SplineTransformCodec.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#ifdef __FAST_MATH__
#error Spline_transform_codec_requires_finite_IEEE_arithmetic
#endif

namespace wolf2009 {
namespace {
typedef std::array<double, 4> Point;
void Need(bool ok, const char* message, SplineCodecFailure failure = SplineCodecFailure::InvalidData) {
    if (!ok) throw SplineCodecError(failure, message);
}
struct Budget {
    const SplineTransformLimits& limits;
    std::size_t retained = 0, work = 0, controls = 0;
    explicit Budget(const SplineTransformLimits& l) : limits(l) {}
    void Store(std::size_t count, std::size_t size) {
        Need(size && retained <= limits.retainedBytes && count <= (limits.retainedBytes-retained)/size,
             "Spline retained-storage budget", SplineCodecFailure::LimitExceeded);
        retained += count*size;
    }
    void Work(std::size_t count) {
        Need(work <= limits.work && count <= limits.work-work, "Spline work budget", SplineCodecFailure::LimitExceeded);
        work += count;
    }
    void Controls(std::size_t count) {
        Need(count && count <= limits.controlsPerCurve && controls <= limits.totalControls && count <= limits.totalControls-controls,
             "Spline curve control budget", SplineCodecFailure::LimitExceeded);
        controls += count;
        Store(count, 2*sizeof(Point));
        Work(count);
    }
};
Point Unit(Point point) {
    double norm = 0;
    for (double value : point) { Need(std::isfinite(value), "Nonfinite spline quaternion"); norm += value*value; }
    Need(std::isfinite(norm) && norm > 1e-12, "Degenerate spline quaternion");
    const double length = std::sqrt(norm);
    for (double& value : point) value /= length;
    return point;
}
struct Curve {
    unsigned degree = 0;
    std::vector<unsigned char> knots;
    std::vector<Point> controls;
};
struct Track { Curve translation, rotation, scale; };
struct BlockData { SplineTransformBlockInfo info; std::vector<Track> tracks; };
class Cursor {
    const std::vector<unsigned char>& bytes;
    const SplineTransformLimits& limits;
    Budget& budget;
    std::size_t at, end;
    unsigned lastFrame;
  public:
    Cursor(const std::vector<unsigned char>& b, std::size_t begin, std::size_t e,
           unsigned last, Budget& budgetIn)
        : bytes(b), limits(budgetIn.limits), budget(budgetIn), at(begin), end(e), lastFrame(last) {}
    std::size_t Position() const { return at; }
    void Range(std::size_t size) const { Need(at <= end && size <= end-at, "Spline read exceeds supplied transform range"); }
    unsigned B() { Range(1); return bytes[at++]; }
    unsigned H() { const unsigned low = B(); return low | (B()<<8); }
    std::uint32_t U() { const std::uint32_t low = H(); return low | (std::uint32_t(H())<<16); }
    double F() {
        const std::uint32_t bits = U(); float value;
        static_assert(sizeof(float) == sizeof(bits), "IEEE32 source required");
        std::memcpy(&value, &bits, sizeof(value));
        Need(std::isfinite(value) && std::abs(double(value)) <= limits.componentMagnitude, "Invalid spline float component");
        return value;
    }
    void Align(unsigned alignment = 4) {
        while (at%alignment) Need(B() == 0, "Nonzero spline alignment padding");
    }
    Curve Header(bool dynamic) {
        Curve curve; unsigned count = 1;
        if (dynamic) {
            count = H()+1; curve.degree = B();
            Need(curve.degree >= 1 && curve.degree <= 3, "Unsupported spline degree", SplineCodecFailure::Unsupported);
            Need(count > curve.degree, "Spline degree/control count mismatch");
            budget.Controls(count);
            const std::size_t knots = std::size_t(count)+curve.degree+1;
            budget.Store(knots, 2); budget.Work(knots); Range(knots);
            curve.knots.reserve(knots);
            for (std::size_t i = 0; i < knots; ++i) {
                const unsigned value = B();
                Need(value <= lastFrame && (curve.knots.empty() || value >= curve.knots.back()), "Invalid spline knot order/domain");
                curve.knots.push_back(static_cast<unsigned char>(value));
            }
            for (unsigned i = 0; i <= curve.degree; ++i)
                Need(curve.knots[i] == 0 && curve.knots[knots-1-i] == lastFrame, "Unsupported unclamped spline domain", SplineCodecFailure::Unsupported);
            Need(lastFrame > 0, "Dynamic spline in a single-frame block");
            Need(curve.knots[curve.degree+1]>0 && curve.knots[count-1]<lastFrame,
                 "Unsupported excessive endpoint knot multiplicity",SplineCodecFailure::Unsupported);
            // Repeated knots are legal, but an interior run may not exceed the
            // degree: sampling then has a strictly positive active interval.
            for (std::size_t i = curve.degree+1; i < count; ++i)
                if (curve.knots[i] != 0 && curve.knots[i] != lastFrame) {
                    std::size_t run = 1;
                    while (i+run < count && curve.knots[i+run] == curve.knots[i]) ++run;
                    Need(run <= curve.degree, "Unsupported discontinuous spline knot multiplicity", SplineCodecFailure::Unsupported);
                    i += run-1;
                }
        } else budget.Controls(1);
        curve.controls.resize(count);
        return curve;
    }
    Curve Vector(unsigned mask, unsigned quantization, double identity) {
        Need((mask & 0x88u) == 0 && (mask & (mask>>4) & 7u) == 0,
             "Unsupported vector component mask", SplineCodecFailure::Unsupported);
        Need(quantization <= 1, "Unsupported scalar quantization", SplineCodecFailure::Unsupported);
        const bool dynamic = (mask & 0x70u) != 0;
        Curve curve = Header(dynamic); if (dynamic) Align();
        Point literal{{identity, identity, identity, 0}}, low{{0,0,0,0}}, high{{0,0,0,0}};
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (mask & (1u<<(axis+4))) { low[axis] = F(); high[axis] = F(); Need(low[axis] <= high[axis], "Inverted spline scalar range"); }
            else if (mask & (1u<<axis)) literal[axis] = F();
        }
        for (Point& point : curve.controls) {
            point = literal;
            for (unsigned axis = 0; axis < 3; ++axis) if (mask & (1u<<(axis+4))) {
                const double fraction = quantization ? H()/65535.0 : B()/255.0;
                point[axis] = low[axis]+(high[axis]-low[axis])*fraction;
                Need(std::isfinite(point[axis]) && std::abs(point[axis]) <= limits.componentMagnitude, "Spline scalar dequantization overflow");
            }
        }
        Align(); return curve;
    }
    Point Quaternion(unsigned quantization) {
        if (quantization == 0) { // Polar32, independent scalar equivalent of the upstream SIMD code.
            const std::uint32_t bits = U();
            double r = double((bits>>18)&1023u)/1023; r = 1-r*r;
            const double packed = double(bits & 0x3ffffu), ring = std::floor(std::sqrt(packed));
            const double pi = std::acos(-1.0);
            const double theta = ring > 0 ? (pi/4)*(packed-ring*ring)/ring : 0;
            const double phi = ring*(pi/2)/511;
            const double magnitude = std::sqrt((std::max)(0.0, 1-r*r));
            Point out{{std::sin(phi)*std::cos(theta)*magnitude, std::sin(phi)*std::sin(theta)*magnitude, std::cos(phi)*magnitude, r}};
            for (unsigned i=0;i<4;++i) if (bits & (1u<<(28+i))) out[i] = -out[i];
            return Unit(out);
        }
        if (quantization == 1 || quantization == 2) {
            std::uint64_t bits = 0;
            const unsigned bytesIn = quantization == 1 ? 5 : 6;
            for (unsigned i=0;i<bytesIn;++i) bits |= std::uint64_t(B())<<(8*i);
            const unsigned width = quantization == 1 ? 12 : 15;
            unsigned omitted; bool negative; std::array<unsigned,3> packed;
            if (quantization == 1) {
                Need(!(bits>>39), "Unsupported packed40 reserved bit", SplineCodecFailure::Unsupported);
                omitted = unsigned((bits>>36)&3); negative = ((bits>>38)&1)!=0;
                for (unsigned i=0;i<3;++i) packed[i] = unsigned((bits>>(12*i))&4095u);
            } else {
                const unsigned a = unsigned(bits&65535), b = unsigned((bits>>16)&65535), c = unsigned((bits>>32)&65535);
                omitted = (a>>15) | ((b>>14)&2); negative = (c>>15)!=0;
                packed = {{a&32767u,b&32767u,c&32767u}};
            }
            const unsigned center = (1u<<(width-1))-1;
            const double factor = std::sqrt(.5)/center;
            Point out{{0,0,0,0}}; unsigned component=0; double norm=0;
            for (unsigned axis=0;axis<4;++axis) if (axis!=omitted) {
                out[axis] = (double(packed[component++])-center)*factor; norm += out[axis]*out[axis];
            }
            Need(norm <= 1.000001, "Invalid smallest-three spline quaternion norm");
            out[omitted] = std::sqrt((std::max)(0.0,1-norm))*(negative?-1:1);
            return Unit(out);
        }
        Need(quantization == 5, "Unsupported quaternion quantization", SplineCodecFailure::Unsupported);
        Point out; double norm=0; for(double& value:out){value=F();norm+=value*value;}
        Need(std::isfinite(norm) && std::abs(norm-1) <= .001, "Invalid uncompressed spline quaternion norm");
        return Unit(out);
    }
    Curve Rotation(unsigned mask, unsigned quantization) {
        Need(quantization == 0 || quantization == 1 || quantization == 2 || quantization == 5,
             "Unsupported quaternion quantization", SplineCodecFailure::Unsupported);
        // HavokLib TransformMask::GetSubTrackType(ttRotation) gives the high
        // nibble precedence. Low static bits may coexist; each control still
        // stores one complete packed quaternion, not independent components.
        Curve curve = Header((mask&240u)!=0);
        if (mask&240u) { if(quantization==2) Align(2); else if(quantization==0 || quantization==5) Align(); }
        for(Point& point:curve.controls) point = mask ? Quaternion(quantization) : Point{{0,0,0,1}};
        Align(); return curve;
    }
};
Point Evaluate(const Curve& curve, double frame) {
    if (!curve.degree || frame <= curve.knots[curve.degree]) return curve.controls.front();
    if (frame >= curve.knots[curve.controls.size()]) return curve.controls.back();
    const std::size_t span = std::size_t(std::upper_bound(curve.knots.begin(),curve.knots.end(),frame)-curve.knots.begin()-1);
    Need(span >= curve.degree && span < curve.controls.size(), "Spline sampling span outside controls");
    std::array<Point,4> values;
    for(unsigned i=0;i<=curve.degree;++i) values[i] = curve.controls[span-curve.degree+i];
    for(unsigned level=1;level<=curve.degree;++level) for(unsigned i=curve.degree;i>=level;--i) {
        const std::size_t knot = span-curve.degree+i;
        const double denominator = curve.knots[knot+curve.degree-level+1]-curve.knots[knot];
        Need(denominator > 0, "Degenerate active spline knot interval");
        const double weight = (frame-curve.knots[knot])/denominator;
        for(unsigned axis=0;axis<4;++axis) values[i][axis] = values[i-1][axis]*(1-weight)+values[i][axis]*weight;
    }
    return values[curve.degree];
}
bool NearTime(double a,double b) {
    if(!std::isfinite(a)||!std::isfinite(b))return false;
    if(a==b)return true;
    const double scale=(std::max)(std::abs(a),std::abs(b));
    return scale>0 && std::abs(a-b)/scale<=2e-5;
}
void ValidateInput(const SplineTransformInput& in, Budget& budget) {
    const auto& limits = budget.limits;
    Need(std::isfinite(limits.componentMagnitude) && limits.componentMagnitude > 0 &&
         std::isfinite(limits.durationSeconds) && limits.durationSeconds > 0, "Invalid spline numeric limits");
    Need(!in.data.empty() && in.data.size() <= limits.compressedBytes && in.transformTracks && in.transformTracks <= limits.tracks &&
         in.floatTracks <= limits.floatTracks && in.numFrames && in.numFrames <= limits.frames && !in.blockOffsets.empty() &&
         in.blockOffsets.size() <= limits.blocks, "Spline source count/byte budget", SplineCodecFailure::LimitExceeded);
    Need(in.blockOffsets.size() == in.transformEndOffsets.size(), "Missing explicit per-block transform end");
    Need(in.maxFramesPerBlock >= 2 && in.maxFramesPerBlock <= 256, "Unsupported spline block frame domain", SplineCodecFailure::Unsupported);
    Need(std::isfinite(in.duration) && in.duration >= 0 && in.duration <= limits.durationSeconds &&
         std::isfinite(in.frameDuration) && in.frameDuration > 0 && std::isfinite(in.blockDuration) && in.blockDuration > 0 &&
         std::isfinite(in.blockInverseDuration) && in.blockInverseDuration > 0, "Invalid spline timing metadata");
    Need(NearTime(in.duration,(in.numFrames-1)*in.frameDuration) &&
         NearTime(in.blockDuration,(in.maxFramesPerBlock-1)*in.frameDuration) &&
         NearTime(in.blockDuration*in.blockInverseDuration,1), "Inconsistent spline source frame clock");
    const std::size_t span = in.maxFramesPerBlock-1;
    const std::size_t blocks = in.numFrames==1 ? 1 : (std::size_t(in.numFrames)-2)/span+1;
    Need(in.blockOffsets.size() == blocks, "Spline blocks do not cover the declared shared-endpoint frame domain");
    Need(4 <= (std::numeric_limits<std::size_t>::max)()/in.transformTracks, "Spline mask byte overflow");
    const std::size_t masks = std::size_t(in.transformTracks)*4+in.floatTracks;
    Need(masks >= in.floatTracks && masks <= (std::numeric_limits<std::size_t>::max)()-3, "Spline mask byte overflow");
    const std::size_t alignedMasks = (masks+3)&~std::size_t(3);
    Need(in.maskAndQuantizationSize == alignedMasks, "Unsupported transform/float mask layout", SplineCodecFailure::Unsupported);
    Need(in.blockOffsets.front() == 0, "Unsupported prefixed spline block data", SplineCodecFailure::Unsupported);
    for(std::size_t i=0;i<blocks;++i) {
        const std::size_t begin=in.blockOffsets[i], end=i+1<blocks ? in.blockOffsets[i+1] : in.data.size();
        Need(begin%4==0 && begin<end && end<=in.data.size() && alignedMasks<=end-begin &&
             in.transformEndOffsets[i]>=begin+alignedMasks && in.transformEndOffsets[i]<=end,
             "Invalid spline block/transform range");
    }
    budget.Store(1,sizeof(SplineTransformInput)); budget.Store(in.data.size(),2);
    const std::vector<std::uint32_t>* arrays[] = {&in.blockOffsets,&in.transformEndOffsets,&in.rawFloatBlockOffsets,&in.rawTransformOffsets,&in.rawFloatOffsets};
    for(const auto* array:arrays) { Need(array->size()<=limits.work,"Spline source metadata array budget",SplineCodecFailure::LimitExceeded);budget.Store(array->size(),2*sizeof(std::uint32_t)); }
    budget.Store(blocks,2*sizeof(BlockData));
    for(std::size_t i=0;i<blocks;++i) budget.Store(in.transformTracks,2*sizeof(Track));
    budget.Work(blocks); budget.Work(in.data.size()/4);
}
} // namespace
struct SplineTransformCodec::Impl {
    SplineTransformInput source;
    std::vector<BlockData> blocks;
    bool nonIdentityScale = false;
};
SplineTransformCodec SplineTransformCodec::Load(const SplineTransformInput& input, SplineScalePolicy scalePolicy,
                                              const SplineTransformLimits& limits) {
    Need(scalePolicy==SplineScalePolicy::RequireIdentityScale || scalePolicy==SplineScalePolicy::RetainFiniteScale,
         "Unknown spline scale policy", SplineCodecFailure::Unsupported);
    Budget budget(limits); ValidateInput(input,budget);
    std::shared_ptr<Impl> impl(new Impl); impl->source=input; impl->blocks.resize(input.blockOffsets.size());
    for(std::size_t i=0;i<impl->blocks.size();++i) {
        BlockData& block=impl->blocks[i]; auto& info=block.info;
        info.begin=input.blockOffsets[i]; info.end=i+1<impl->blocks.size()?input.blockOffsets[i+1]:input.data.size();
        info.transformBegin=info.begin+input.maskAndQuantizationSize; info.transformEnd=input.transformEndOffsets[i];
        info.floatMasksBegin=info.begin+std::size_t(input.transformTracks)*4; info.floatMasksEnd=info.floatMasksBegin+input.floatTracks;
        info.opaqueSuffixBegin=info.transformEnd; info.opaqueSuffixEnd=info.end;
        info.firstFrame=std::uint32_t(i*(input.maxFramesPerBlock-1));
        info.lastLocalFrame=(std::min)(input.maxFramesPerBlock-1,input.numFrames-1-info.firstFrame);
        for(std::size_t p=info.floatMasksEnd;p<info.transformBegin;++p) Need(input.data[p]==0,"Nonzero spline mask padding");
        block.tracks.resize(input.transformTracks);
        Cursor cursor(input.data,info.transformBegin,info.transformEnd,info.lastLocalFrame,budget);
        for(std::size_t t=0;t<input.transformTracks;++t) {
            budget.Work(1); const std::size_t at=info.begin+4*t;
            const unsigned quant=input.data[at]; Track& track=block.tracks[t];
            const unsigned rotationQuant=(quant>>2)&15;
            Need((quant&3)<=1 && (quant>>6)<=1 &&
                 (rotationQuant==0 || rotationQuant==1 || rotationQuant==2 || rotationQuant==5),
                 "Unsupported transform quantization",SplineCodecFailure::Unsupported);
            track.translation=cursor.Vector(input.data[at+1],quant&3,0);
            track.rotation=cursor.Rotation(input.data[at+2],rotationQuant);
            track.scale=cursor.Vector(input.data[at+3],quant>>6,1);
            for(const Point& point:track.scale.controls) for(unsigned axis=0;axis<3;++axis) if(point[axis]!=1) impl->nonIdentityScale=true;
        }
        Need(cursor.Position()==info.transformEnd,"Decoded TRS does not consume the supplied exact transform range");
    }
    Need(scalePolicy!=SplineScalePolicy::RequireIdentityScale || !impl->nonIdentityScale,
         "Nonidentity scale is not qualified for quaternion/translation palettes",SplineCodecFailure::Unsupported);
    return SplineTransformCodec(impl);
}
std::size_t SplineTransformCodec::TrackCount()const{return impl_->source.transformTracks;}
std::size_t SplineTransformCodec::FrameCount()const{return impl_->source.numFrames;}
std::size_t SplineTransformCodec::BlockCount()const{return impl_->blocks.size();}
double SplineTransformCodec::Duration()const{return impl_->source.duration;}
double SplineTransformCodec::FrameDuration()const{return impl_->source.frameDuration;}
bool SplineTransformCodec::HasNonIdentityScale()const{return impl_->nonIdentityScale;}
const SplineTransformInput& SplineTransformCodec::Source()const{return impl_->source;}
const SplineTransformBlockInfo& SplineTransformCodec::Block(std::size_t index)const{
    Need(index<impl_->blocks.size(),"Spline block index out of range");return impl_->blocks[index].info;
}
SplineLocalTransform SplineTransformCodec::SampleTrack(double seconds,std::size_t track)const{
    Need(std::isfinite(seconds) && track<TrackCount(),"Invalid spline sample time/track");
    seconds=(std::max)(0.0,(std::min)(seconds,Duration()));
    const double quotient=seconds/impl_->source.blockDuration;
    Need(std::isfinite(quotient),"Spline block time division overflow");
    const std::size_t block=seconds>=Duration()||quotient>=double(BlockCount())?BlockCount()-1:std::size_t(quotient);
    const auto& selected=impl_->blocks[block];
    double frame=(seconds-double(block)*impl_->source.blockDuration)/FrameDuration();
    if(seconds>=Duration())frame=selected.info.lastLocalFrame;
    frame=(std::max)(0.0,(std::min)(frame,double(selected.info.lastLocalFrame)));
    const Track& curves=selected.tracks[track];const Point translation=Evaluate(curves.translation,frame),scale=Evaluate(curves.scale,frame);
    SplineLocalTransform out;out.rotation=Unit(Evaluate(curves.rotation,frame));
    for(unsigned axis=0;axis<3;++axis){out.translation[axis]=translation[axis];
        // All-one scale controls define exact identity. Preserve that fact
        // instead of exposing roundoff from evaluating a constant spline.
        out.scale[axis]=impl_->nonIdentityScale?scale[axis]:1;}
    return out;
}
std::vector<SplineLocalTransform> SplineTransformCodec::Sample(double seconds)const{
    Need(std::isfinite(seconds),"Invalid spline sample time");
    std::vector<SplineLocalTransform> out;out.reserve(TrackCount());
    for(std::size_t i=0;i<TrackCount();++i)out.push_back(SampleTrack(seconds,i));return out;
}
} // namespace wolf2009
