// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored fixtures; no commercial payloads.
#include "SplineTransformCodec.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
using namespace wolf2009;
namespace {
unsigned checks=0,groups=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void Near(double a,double b,double tolerance,const char* message){Check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance,message);}
void Reject(const std::function<void()>& action,SplineCodecFailure expected,const char* message){
    bool caught=false;try{action();}catch(const SplineCodecError& error){caught=true;++checks;if(error.Failure()!=expected)throw std::runtime_error(std::string(message)+": wrong typed failure: "+error.what());}
    Check(caught,message);
}
typedef std::vector<unsigned char> Bytes;
struct Writer {
    Bytes bytes;
    void B(unsigned value){bytes.push_back(static_cast<unsigned char>(value));}
    void H(unsigned value){B(value);B(value>>8);}
    void U(std::uint32_t value){H(value);H(value>>16);}
    void F(float value){std::uint32_t bits;std::memcpy(&bits,&value,4);U(bits);}
    void Align(unsigned alignment=4){while(bytes.size()%alignment)B(0);}
    void Mask(unsigned quant,unsigned position,unsigned rotation,unsigned scale){B(quant);B(position);B(rotation);B(scale);}
    void Header(unsigned controls,unsigned degree,unsigned last){H(controls-1);B(degree);for(unsigned i=0;i<=degree;++i)B(0);for(unsigned i=0;i<=degree;++i)B(last);}
    void Q40(unsigned omitted=3,bool negative=false){std::uint64_t bits=2047ull|(2047ull<<12)|(2047ull<<24)|(std::uint64_t(omitted)<<36)|(std::uint64_t(negative)<<38);for(unsigned i=0;i<5;++i)B(unsigned(bits>>(8*i)));}
    void Q48(unsigned omitted=3,bool negative=false){H(16383|((omitted&1)<<15));H(16383|((omitted&2)<<14));H(16383|(unsigned(negative)<<15));}
};
SplineTransformInput Input(const Bytes& data,unsigned frames=5,unsigned maxFrames=5){
    SplineTransformInput input;input.data=data;input.transformTracks=1;input.numFrames=frames;input.maxFramesPerBlock=maxFrames;
    input.frameDuration=.1;input.duration=(frames-1)*input.frameDuration;input.blockDuration=(maxFrames-1)*input.frameDuration;
    input.blockInverseDuration=1/input.blockDuration;input.maskAndQuantizationSize=4;input.blockOffsets.push_back(0);input.transformEndOffsets.push_back(std::uint32_t(data.size()));return input;
}
Writer Linear(unsigned quant=0x45,unsigned mask=0x10,unsigned last=4){
    Writer w;w.Mask(quant,mask,0,0);w.Header(2,1,last);w.Align();w.F(0);w.F(10);if(mask&2)w.F(7);
    if((quant&3)==0){w.B(0);w.B(255);}else{w.H(0);w.H(65535);}w.Align();return w;
}
void IdentityAndOwnership(){++groups;
    Writer w;w.Mask(0x45,0,0,0);auto input=Input(w.bytes);input.data.insert(input.data.end(),{0x12,0x56,0x78,0x9a});
    input.rawFloatBlockOffsets={17,42};input.rawTransformOffsets={91};input.rawFloatOffsets={13};
    const auto codec=SplineTransformCodec::Load(input);input.data[0]=0xff;input.rawFloatBlockOffsets[0]=999;
    Check(codec.TrackCount()==1&&codec.FrameCount()==5&&codec.BlockCount()==1&&!codec.HasNonIdentityScale(),"Identity metadata");
    const auto pose=codec.Sample(-12);Check(pose.size()==1&&pose[0].translation==std::array<double,3>{{0,0,0}}&&pose[0].rotation==std::array<double,4>{{0,0,0,1}}&&pose[0].scale==std::array<double,3>{{1,1,1}},"Identity pose");
    Check(codec.Source().data[0]==0x45&&codec.Source().rawFloatBlockOffsets[0]==17,"Codec did not own its source facts");
    const auto& info=codec.Block(0);Check(info.transformBegin==4&&info.transformEnd==4&&info.opaqueSuffixBegin==4&&info.opaqueSuffixEnd==8,"Opaque suffix range");
    Reject([&]{codec.Sample(std::numeric_limits<double>::infinity());},SplineCodecFailure::InvalidData,"Nonfinite sample accepted");
    Reject([&]{codec.SampleTrack(0,1);},SplineCodecFailure::InvalidData,"Invalid track accepted");
    Reject([&]{codec.Block(1);},SplineCodecFailure::InvalidData,"Invalid block accepted");
    Writer one;one.Mask(0x45,0,0,0);auto oneInput=Input(one.bytes,1);const auto constant=SplineTransformCodec::Load(oneInput);
    Check(constant.Duration()==0&&constant.Sample(25)[0].rotation[3]==1,"Single-frame constant profile");
}
void ScalarsAndClock(){++groups;
    for(unsigned quant:{0x44u,0x45u}){const auto codec=SplineTransformCodec::Load(Input(Linear(quant,0x12).bytes));
        for(double time:{-.1,0.0,.017,.13,.27,.4,.8}){const auto pose=codec.SampleTrack(time,0);const double clamped=(std::max)(0.0,(std::min)(time,.4));
            Near(pose.translation[0],25*clamped,1e-12,"Source frame-duration linear clock");Near(pose.translation[1],7,1e-12,"Mixed static scalar");Near(pose.translation[2],0,0,"Identity scalar");}}
    Writer w;w.Mask(0x45,7,0,0);w.F(-2);w.F(4);w.F(9);const auto codec=SplineTransformCodec::Load(Input(w.bytes));
    Check(codec.Sample(.21)[0].translation==std::array<double,3>{{-2,4,9}},"Static translation");
}
void PolynomialCurves(){++groups;
    for(unsigned degree:{2u,3u}){
        Writer w;w.Mask(0x44,0x10,0,0);w.Header(degree+1,degree,4);w.Align();w.F(0);w.F(10);
        if(degree==2){w.B(0);w.B(51);w.B(255);}else{w.B(0);w.B(0);w.B(255);w.B(255);}w.Align();
        const auto codec=SplineTransformCodec::Load(Input(w.bytes));
        for(double t:{0.0,.13,.5,.87,1.0}){const double expected=degree==2?4*t*(1-t)+10*t*t:30*t*t-20*t*t*t;
            Near(codec.SampleTrack(t*.4,0).translation[0],expected,1e-11,"Quadratic/cubic B-spline curve");}
    }
    for(unsigned rotationMask:{0xf0u,0x99u}){Writer w;w.Mask(0x54,0,rotationMask,0);w.Header(4,3,4);w.Align();
        const float a[4][4]={{0,0,0,1},{0,0,.6f,.8f},{0,0,.6f,.8f},{0,0,0,1}};
        for(const auto& q:a)for(float v:q)w.F(v);w.Align();const auto codec=SplineTransformCodec::Load(Input(w.bytes));
        const auto pose=codec.SampleTrack(.2,0);const double z=.75*double(.6f),r=.25+.75*double(.8f),norm=std::sqrt(z*z+r*r);
        Near(pose.rotation[2],z/norm,2e-8,"Quaternion component spline was replaced by SLERP");Near(pose.rotation[3],r/norm,2e-8,"Quaternion spline normalization");}
}
void Quaternions(){++groups;
    for(unsigned qtype:{0u,1u,2u,5u})for(bool negative:{false,true}){
        Writer w;w.Mask(0x40|(qtype<<2),0,15,0);
        if(qtype==0)w.U(negative?0x80000000u:0u);else if(qtype==1)w.Q40(3,negative);else if(qtype==2)w.Q48(3,negative);else{w.F(0);w.F(0);w.F(0);w.F(negative?-1.f:1.f);}w.Align();
        const auto codec=SplineTransformCodec::Load(Input(w.bytes));const auto q=codec.Sample(.17)[0].rotation;
        Near(q[0],0,1e-12,"Packed quaternion X");Near(q[1],0,1e-12,"Packed quaternion Y");Near(q[2],0,1e-12,"Packed quaternion Z");Near(q[3],negative?-1:1,1e-12,"Packed quaternion W/sign");
    }
    for(unsigned omitted=0;omitted<4;++omitted){Writer w;w.Mask(0x44,0,9,0);w.Q40(omitted);w.Align();const auto q=SplineTransformCodec::Load(Input(w.bytes)).Sample(0)[0].rotation;
        for(unsigned axis=0;axis<4;++axis)Near(q[axis],axis==omitted?1:0,1e-12,"Smallest-three omitted index");}
}
void ScalePolicy(){++groups;
    Writer w;w.Mask(0x45,0,0,7);w.F(1);w.F(2);w.F(1);const auto input=Input(w.bytes);
    Reject([&]{SplineTransformCodec::Load(input);},SplineCodecFailure::Unsupported,"Unqualified scale accepted");
    const auto codec=SplineTransformCodec::Load(input,SplineScalePolicy::RetainFiniteScale);
    Check(codec.HasNonIdentityScale()&&codec.Sample(.2)[0].scale==std::array<double,3>{{1,2,1}},"Retained explicit scale");
    Writer dynamic;dynamic.Mask(0x05,0,0,0x10);dynamic.Header(2,1,4);dynamic.Align();dynamic.F(1);dynamic.F(2);dynamic.B(0);dynamic.B(255);dynamic.Align();
    Reject([&]{SplineTransformCodec::Load(Input(dynamic.bytes));},SplineCodecFailure::Unsupported,"Dynamic nonidentity scale admitted");
    const auto scaled=SplineTransformCodec::Load(Input(dynamic.bytes),SplineScalePolicy::RetainFiniteScale);
    Near(scaled.Sample(.2)[0].scale[0],1.5,1e-12,"Scale spline");
    Near(scaled.SampleFrame(2)[0].scale[0],1.5,1e-12,"Frame scale spline");
    Writer unit;unit.Mask(0x05,0,0,0x10);unit.Header(2,1,4);unit.Align();unit.F(1);unit.F(1);unit.B(0);unit.B(255);unit.Align();
    const auto qualified=SplineTransformCodec::Load(Input(unit.bytes));
    Check(!qualified.HasNonIdentityScale()&&qualified.Sample(.017)[0].scale==std::array<double,3>{{1,1,1}},"Constant scale spline lost exact identity qualification");
    Check(qualified.SampleFrame(.17)[0].scale==std::array<double,3>{{1,1,1}},"Frame scale spline lost identity qualification");
}
void BlocksAndFloats(){++groups;
    auto input=Input(Bytes(),7,4);input.floatTracks=2;input.maskAndQuantizationSize=8;input.blockOffsets.clear();input.transformEndOffsets.clear();
    for(unsigned block=0;block<2;++block){Writer w;w.Mask(0x45,0x10,0,0);w.B(0x12);w.B(0x34);w.Align();w.Header(2,1,3);w.Align();w.F(float(block*3));w.F(float(block*3+3));w.H(0);w.H(65535);w.Align();
        input.blockOffsets.push_back(std::uint32_t(input.data.size()));input.transformEndOffsets.push_back(std::uint32_t(input.data.size()+w.bytes.size()));
        input.data.insert(input.data.end(),w.bytes.begin(),w.bytes.end());input.data.insert(input.data.end(),{0xde,0xad,0xbe,0xef});}
    input.rawFloatBlockOffsets={28,28};const auto codec=SplineTransformCodec::Load(input);
    Check(codec.BlockCount()==2&&codec.Block(1).firstFrame==3&&codec.Block(1).lastLocalFrame==3,"Multiblock frame domain");
    for(double time:{0.0,.13,.299999,.3,.300001,.47,.6,.8})Near(codec.Sample(time)[0].translation[0],10*(std::min)(time,.6),1e-10,"Multiblock clock/boundary");
    for(std::size_t block=0;block<2;++block){const auto& info=codec.Block(block);Check(info.floatMasksEnd-info.floatMasksBegin==2&&codec.Source().data[info.floatMasksBegin]==0x12&&codec.Source().data[info.floatMasksBegin+1]==0x34&&
        info.opaqueSuffixEnd-info.opaqueSuffixBegin==4&&codec.Source().data[info.opaqueSuffixBegin]==0xde,"Float masks/suffix not retained opaque");}
}
SplineTransformInput FrameJump(){
    auto input=Input(Bytes(),7,4);input.blockOffsets.clear();input.transformEndOffsets.clear();
    input.frameDuration=double(.1f);input.blockDuration=double(.3f);input.blockInverseDuration=double(1/.3f);input.duration=double(.6f);
    for(unsigned block=0;block<2;++block){Writer w;w.Mask(0x45,0x10,0,0);w.Header(2,1,3);w.Align();w.F(block?100.f:0.f);w.F(block?103.f:3.f);w.H(0);w.H(65535);w.Align();
        input.blockOffsets.push_back(std::uint32_t(input.data.size()));input.transformEndOffsets.push_back(std::uint32_t(input.data.size()+w.bytes.size()));input.data.insert(input.data.end(),w.bytes.begin(),w.bytes.end());}
    return input;
}
void FrameCoordinates(){++groups;const auto input=FrameJump();const auto codec=SplineTransformCodec::Load(input);
    const double before=std::nextafter(3.0,0.0),after=std::nextafter(3.0,4.0);
    Check(input.blockDuration!=3*input.frameDuration,"Authored frame fixture lacks float32 block-clock drift");
    for(double frame:{-1.0,0.0,.37,1.73,before,3.0,after,4.5,6.0,100.0}){
        const double f=(std::max)(0.0,(std::min)(frame,6.0));const double expected=f<3?f:100+(f-3);
        Near(codec.SampleTrackFrame(frame,0).translation[0],expected,1e-12,"Direct frame sampler crossed an analytical block boundary");
        Near(codec.SampleFrame(frame)[0].translation[0],expected,1e-12,"Full frame palette disagrees with analytical source curve");
        const double timestamp=codec.SourceTimeAtFrame(frame);Check(std::isfinite(timestamp)&&timestamp>=0&&timestamp<=codec.Duration(),"Frame metadata timestamp left source domain");
    }
    Near(codec.SampleFrame(3)[0].translation[0],100,0,"Shared frame did not select next block");
    Near(codec.Sample(3*input.frameDuration)[0].translation[0],3,1e-12,"Existing source-time sampler changed its drifted boundary behavior");
    Near(codec.FrameCoordinateAtTime(input.blockDuration),3,0,"Source block boundary inverse coordinate");
    Near(codec.FrameCoordinateAtTime(3*input.frameDuration),3,1e-12,"Clamped previous-block endpoint coordinate");
    Check(codec.SampleFrame(codec.FrameCoordinateAtTime(3*input.frameDuration))[0].translation[0]!=codec.Sample(3*input.frameDuration)[0].translation[0],
          "Authored source clock no longer exercises the documented nonbijective shared endpoint");
    for(double seconds:{-1.0,0.0,.13,input.blockDuration,.47,input.duration,input.duration+2}){
        const double t=(std::max)(0.0,(std::min)(seconds,input.duration));const double local=t<input.blockDuration?t/input.frameDuration:(t-input.blockDuration)/input.frameDuration;
        const double expected=t>=input.duration?6:(t<input.blockDuration?(std::min)(local,3.0):3+(std::min)(local,3.0));
        Near(codec.FrameCoordinateAtTime(seconds),expected,1e-12,"Source seconds to frame coordinate used an inferred global clock");
    }
    Near(codec.SourceTimeAtFrame(3),input.blockDuration,0,"Frame boundary metadata did not use next source block start");
    Near(codec.SourceTimeAtFrame(6),input.duration,0,"Final frame metadata did not use exact source duration");
    auto shorter=input;shorter.blockDuration=double(std::nextafter(.3f,0.f));shorter.blockInverseDuration=1/shorter.blockDuration;
    const auto shortClock=SplineTransformCodec::Load(shorter);
    Near(shortClock.SampleFrame(before)[0].translation[0],before,1e-12,"Direct frame depended on shorter source block clock");
    Check(shortClock.Sample(shortClock.SourceTimeAtFrame(before))[0].translation[0]>99,
          "Metadata roundtrip fixture failed to distinguish the previous frame block");
    auto shortDuration=input;shortDuration.duration=double(std::nextafter(.6f,0.f));const auto clipped=SplineTransformCodec::Load(shortDuration);
    Near(clipped.SourceTimeAtFrame(std::nextafter(6.0,0.0)),shortDuration.duration,0,"Frame metadata overflowed a drifted source end");
    for(double invalid:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        Reject([&]{codec.SampleFrame(invalid);},SplineCodecFailure::InvalidData,"Nonfinite frame accepted");
        Reject([&]{codec.SampleTrackFrame(invalid,0);},SplineCodecFailure::InvalidData,"Nonfinite track frame accepted");
        Reject([&]{codec.FrameCoordinateAtTime(invalid);},SplineCodecFailure::InvalidData,"Nonfinite inverse time accepted");
        Reject([&]{codec.SourceTimeAtFrame(invalid);},SplineCodecFailure::InvalidData,"Nonfinite frame metadata accepted");
    }
    Reject([&]{codec.SampleTrackFrame(0,1);},SplineCodecFailure::InvalidData,"Invalid frame track accepted");
    Writer one;one.Mask(0x45,0,0,0);const auto constant=SplineTransformCodec::Load(Input(one.bytes,1));
    Check(constant.SampleFrame(1)[0].rotation[3]==1&&constant.FrameCoordinateAtTime(5)==0&&constant.SourceTimeAtFrame(5)==0,"Single-frame coordinate profile");
}
void InvalidInput(){++groups;
    const auto valid=Input(Linear().bytes);
    {auto in=valid;in.transformEndOffsets.clear();Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"Missing transform end accepted");}
    for(int delta:{-1,1}){auto in=valid;in.data.push_back(0);in.transformEndOffsets[0]+=delta;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"Inexact transform end accepted");}
    {auto in=valid;in.blockOffsets[0]=4;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::Unsupported,"Prefixed block accepted");}
    {auto in=valid;in.frameDuration=.09;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"Inconsistent frame clock accepted");}
    {auto in=valid;in.blockDuration=1e-308;in.blockInverseDuration=1e308;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"Tiny false clock admitted an overflowing block index");}
    {auto in=valid;in.duration=std::numeric_limits<double>::quiet_NaN();Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"NaN duration accepted");}
    for(unsigned quant:{0x46u,0x4cu,0x50u,0x85u}){auto in=valid;in.data[0]=static_cast<unsigned char>(quant);Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::Unsupported,"Unknown quantization silently accepted");}
    for(unsigned mask:{0x88u,0x11u}){auto in=valid;in.data[1]=static_cast<unsigned char>(mask);Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::Unsupported,"Unknown vector mask accepted");}
    {auto in=valid;in.data[6]=4;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::Unsupported,"Unsupported degree accepted");}
    {auto in=valid;in.data[9]=3;in.data[10]=2;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"Descending knots accepted");}
    {auto in=valid;in.data[11]=1;Reject([&]{SplineTransformCodec::Load(in);},SplineCodecFailure::InvalidData,"Nonzero padding accepted");}
    {Writer w;w.Mask(0x44,0x10,0,0);w.H(3);w.B(1);for(unsigned knot:{0u,0u,0u,2u,4u,4u})w.B(knot);w.Align();w.F(0);w.F(1);for(int i=0;i<4;++i)w.B(unsigned(i));w.Align();
        Reject([&]{SplineTransformCodec::Load(Input(w.bytes));},SplineCodecFailure::Unsupported,"Excess initial knot multiplicity accepted");}
    {Writer w;w.Mask(0x45,7,0,0);w.F(std::numeric_limits<float>::infinity());w.F(0);w.F(0);Reject([&]{SplineTransformCodec::Load(Input(w.bytes));},SplineCodecFailure::InvalidData,"Infinite scalar accepted");}
    {Writer w;w.Mask(0x54,0,15,0);for(int i=0;i<4;++i)w.F(0);Reject([&]{SplineTransformCodec::Load(Input(w.bytes));},SplineCodecFailure::InvalidData,"Degenerate quaternion accepted");}
    {Writer w;w.Mask(0x44,0,15,0);w.Q40();w.bytes.back()|=0x80;w.Align();Reject([&]{SplineTransformCodec::Load(Input(w.bytes));},SplineCodecFailure::Unsupported,"Reserved quaternion flag accepted");}
}
void Budgets(){++groups;const auto valid=Input(Linear().bytes);
    for(unsigned test=0;test<5;++test){SplineTransformLimits limits;if(test==0)limits.compressedBytes=1;if(test==1)limits.retainedBytes=1;if(test==2)limits.totalControls=1;if(test==3)limits.work=1;if(test==4)limits.controlsPerCurve=1;
        Reject([&]{SplineTransformCodec::Load(valid,SplineScalePolicy::RequireIdentityScale,limits);},SplineCodecFailure::LimitExceeded,"Decode budget bypassed");}
    const auto stillValid=SplineTransformCodec::Load(valid);Near(stillValid.Sample(.2)[0].translation[0],5,1e-12,"Failed load poisoned later independent input");
}
}
int main(){try{IdentityAndOwnership();ScalarsAndClock();PolynomialCurves();Quaternions();ScalePolicy();BlocksAndFloats();FrameCoordinates();InvalidInput();Budgets();
    std::cout<<"WOLF2009_SPLINE_CODEC_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_SPLINE_CODEC_FAIL checks="<<checks<<" groups="<<groups<<" message="<<error.what()<<'\n';return 1;}}
