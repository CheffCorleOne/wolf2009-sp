// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only selected owner data checks, not a general rig/retail-playback claim.
#include "SplineTransformCodec.h"
#include "GreeterAnimation.h"
#include "HavokPack.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
using namespace wolf2009;
namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::uint64_t Hash(const std::vector<unsigned char>& bytes){std::uint64_t hash=14695981039346656037ull;for(unsigned char b:bytes)hash=(hash^b)*1099511628211ull;return hash;}
struct Selected { std::string name;std::uint32_t object=0;SplineTransformInput input; };
std::uint32_t Variant(const detail::HavokPack& pack,const char* wanted,const char* type){
    const auto root=pack.U(28),count=pack.DU(root+4),array=pack.Pointer(root);Check(count&&count<=4096,"Source root variant budget");pack.ArrayRange(array,count,16);
    std::uint32_t result=0;for(std::uint32_t i=0;i<count;++i){const auto at=array+16*i;if(pack.DataString(pack.Pointer(at))!=wanted)continue;
        Check(!result&&pack.DataString(pack.Pointer(at+4))==type,"Ambiguous/mistyped source named variant");result=pack.Pointer(at+8);Check(pack.Type(result)==type,"Source variant class mismatch");}
    Check(result!=0,"Missing selected source named variant");return result;
}
std::vector<std::uint32_t> Array(const detail::HavokPack& pack,std::uint32_t slot,std::size_t limit){
    const auto count=pack.DU(slot+4);Check(count<=limit,"Source spline metadata array budget");std::vector<std::uint32_t> out;if(!count)return out;
    const auto pointer=pack.Pointer(slot);pack.ArrayRange(pointer,count,4);out.reserve(count);for(std::uint32_t i=0;i<count;++i)out.push_back(pack.DU(pointer+4*i));return out;
}
Selected Read(const detail::HavokPack& pack,const char* selectedName){
    const auto container=Variant(pack,"Animation Container","hkaAnimationContainer"),names=Variant(pack,"animNames","rvHkStringContainer");
    const auto count=pack.DU(container+12),objects=pack.Pointer(container+8),strings=pack.Pointer(names);
    Check(count&&count<=4096&&count==pack.DU(names+4),"Source animation object/name count mismatch");pack.ArrayRange(objects,count,4);pack.ArrayRange(strings,count,4);
    Selected selected;selected.name=selectedName;
    for(std::uint32_t i=0;i<count;++i)if(pack.DataString(pack.Pointer(strings+4*i))==selectedName){Check(!selected.object,"Duplicate selected source animation name");selected.object=pack.Pointer(objects+4*i);}
    const auto object=selected.object;Check(object&&pack.Type(object)=="hkaSplineSkeletalAnimation"&&pack.DU(object+8)==5,"Source clip is not the selected spline class");
    pack.DataRange(object,128);Check(pack.DU(object+124)==0,"Source spline byte order profile changed");
    auto& input=selected.input;input.transformTracks=pack.DU(object+16);input.floatTracks=pack.DU(object+20);input.numFrames=pack.DU(object+36);
    const auto blocks=pack.DU(object+40);input.maxFramesPerBlock=pack.DU(object+44);input.maskAndQuantizationSize=pack.DU(object+48);
    input.duration=pack.F(object+12);input.blockDuration=pack.F(object+52);input.blockInverseDuration=pack.F(object+56);input.frameDuration=pack.F(object+60);
    input.blockOffsets=Array(pack,object+64,4096);input.rawFloatBlockOffsets=Array(pack,object+76,4096);
    input.rawTransformOffsets=Array(pack,object+88,1000000);input.rawFloatOffsets=Array(pack,object+100,1000000);
    Check(blocks&&blocks==input.blockOffsets.size()&&blocks==input.rawFloatBlockOffsets.size(),"Source block/float boundary count mismatch");
    const auto size=pack.DU(object+116);Check(size&&size<=64u*1024u*1024u,"Source compressed clip byte budget");const auto data=pack.Pointer(object+112);pack.DataRange(data,size);
    const std::size_t base=pack.sections[2].absolute+std::size_t(data);input.data.assign(pack.bytes.begin()+base,pack.bytes.begin()+base+size);
    for(std::size_t i=0;i<blocks;++i){
        // Wolf2.exe 10374466..73 and 1037452d..48 independently establish
        // data + blockOffsets[b] + floatBlockOffsets[b], not an absolute end.
        const std::uint64_t end=std::uint64_t(input.blockOffsets[i])+input.rawFloatBlockOffsets[i];
        Check(end<=input.data.size(),"Source relative float boundary outside compressed data");input.transformEndOffsets.push_back(std::uint32_t(end));
    }
    return selected;
}
void Report(const Selected& selected,const SplineTransformCodec& codec){
    const auto& input=selected.input;
    std::cout<<"SOURCE_SPLINE name="<<selected.name<<" object="<<selected.object<<" tracks="<<codec.TrackCount()<<" frames="<<codec.FrameCount()<<" blocks="<<codec.BlockCount()
        <<" compressed_bytes="<<input.data.size()<<" duration="<<codec.Duration()<<" frame_duration="<<codec.FrameDuration()<<" nonidentity_scale="<<codec.HasNonIdentityScale()
        <<" data_fingerprint="<<std::hex<<Hash(input.data)<<std::dec<<" raw_transform_offsets="<<input.rawTransformOffsets.size()<<" raw_float_offsets="<<input.rawFloatOffsets.size()<<'\n';
    for(std::size_t i=0;i<codec.BlockCount();++i){const auto& block=codec.Block(i);std::cout<<"SOURCE_BLOCK i="<<i<<" start="<<block.begin<<" trs_begin="<<block.transformBegin<<" trs_end="<<block.transformEnd
        <<" raw_float_relative="<<input.rawFloatBlockOffsets[i]<<" opaque_end="<<block.end<<" local_last_frame="<<block.lastLocalFrame<<'\n';}
}
void CompareGreeter(const Selected& selected,const std::vector<unsigned char>& hkxa,const std::vector<unsigned char>& skel){
    const auto codec=SplineTransformCodec::Load(selected.input);const auto legacy=GreeterAnimation::Load(hkxa,skel);
    Check(codec.TrackCount()==legacy.Joints().size()&&codec.FrameCount()==legacy.FrameCount()&&codec.Duration()==legacy.Duration()&&codec.FrameDuration()==legacy.FrameDuration(),"Selected greeter codec metadata differs");
    double maxTranslation=0,maxRotation=0;unsigned samples=0;
    std::vector<double> times{-1,0,.017,.021,.042,.043,1.234,codec.Duration(),std::ceil(codec.Duration()*1000)*.001};
    for(std::size_t frame=0;frame<codec.FrameCount();++frame){times.push_back(frame*codec.FrameDuration());times.push_back((frame+.37)*codec.FrameDuration());}
    for(double time:times){const auto actual=codec.Sample(time);const auto expected=legacy.SamplePose(time);++samples;Check(actual.size()==expected.local.size(),"Selected greeter palette size differs");
        for(std::size_t i=0;i<actual.size();++i){for(unsigned axis=0;axis<3;++axis){maxTranslation=(std::max)(maxTranslation,std::abs(actual[i].translation[axis]-expected.local[i].translation[axis]));Check(actual[i].scale[axis]==1,"Selected greeter scale differs");}
            for(unsigned axis=0;axis<4;++axis)maxRotation=(std::max)(maxRotation,std::abs(actual[i].rotation[axis]-expected.local[i].rotation[axis]));}}
    Check(maxTranslation<1e-9&&maxRotation<1e-9,"Generic codec disagrees with previously verified greeter decoder");Report(selected,codec);
    std::cout<<"SOURCE_GREETER_DIFFERENTIAL samples="<<samples<<" max_translation_error="<<maxTranslation<<" max_quaternion_error="<<maxRotation<<'\n';
    double frameError=0;unsigned frameSamples=0;
    for(std::size_t frame=0;frame<codec.FrameCount();++frame)for(double fraction:{0.0,.37}){
        const double coordinate=double(frame)+fraction;const auto actual=codec.SampleFrame(coordinate);const auto expected=legacy.SamplePose(coordinate*legacy.FrameDuration());++frameSamples;
        for(std::size_t t=0;t<actual.size();++t){for(unsigned axis=0;axis<3;++axis)frameError=(std::max)(frameError,std::abs(actual[t].translation[axis]-expected.local[t].translation[axis]));
            for(unsigned axis=0;axis<4;++axis)frameError=(std::max)(frameError,std::abs(actual[t].rotation[axis]-expected.local[t].rotation[axis]));}}
    Check(frameError<1e-9,"Single-block direct frame curves differ from the verified greeter source");
    std::cout<<"SOURCE_GREETER_FRAME samples="<<frameSamples<<" max_component_error="<<frameError<<'\n';
}
void MultiBlock(const Selected& selected){
    const auto codec=SplineTransformCodec::Load(selected.input);Check(codec.BlockCount()==2&&codec.FrameCount()==335&&codec.TrackCount()==52,"Selected original multiblock profile changed");
    Check(selected.input.rawFloatBlockOffsets[1]<selected.input.blockOffsets[1],"Source second boundary no longer distinguishes relative/absolute offsets");
    double maxSelectionError=0,maxTranslationJump=0,maxQuaternionJump=0;unsigned samples=0;
    for(std::size_t block=0;block<codec.BlockCount();++block){const auto& info=codec.Block(block);auto isolated=selected.input;
        isolated.data.assign(selected.input.data.begin()+info.begin,selected.input.data.begin()+info.end);isolated.blockOffsets={0};isolated.transformEndOffsets={std::uint32_t(info.transformEnd-info.begin)};
        isolated.numFrames=info.lastLocalFrame+1;isolated.duration=info.lastLocalFrame*isolated.frameDuration;const auto one=SplineTransformCodec::Load(isolated);
        for(double fraction:{0.0,.013,.37,.83}){const double local=isolated.duration*fraction,global=block*isolated.blockDuration+local;const auto combined=codec.Sample(global),separate=one.Sample(local);++samples;
            for(std::size_t t=0;t<combined.size();++t){for(unsigned axis=0;axis<3;++axis){Check(std::isfinite(combined[t].translation[axis])&&combined[t].scale[axis]==1,"Source multiblock nonfinite/unqualified scale");maxSelectionError=(std::max)(maxSelectionError,std::abs(combined[t].translation[axis]-separate[t].translation[axis]));}
                for(unsigned axis=0;axis<4;++axis)maxSelectionError=(std::max)(maxSelectionError,std::abs(combined[t].rotation[axis]-separate[t].rotation[axis]));}}
    }
    Check(maxSelectionError<1e-9,"Multiblock sampling differs from separately checked local source blocks");
    const auto left=codec.Sample(std::nextafter(selected.input.blockDuration,0.0)),right=codec.Sample(selected.input.blockDuration);
    for(std::size_t t=0;t<left.size();++t){for(unsigned axis=0;axis<3;++axis)maxTranslationJump=(std::max)(maxTranslationJump,std::abs(left[t].translation[axis]-right[t].translation[axis]));
        double dot=0;for(unsigned axis=0;axis<4;++axis)dot+=left[t].rotation[axis]*right[t].rotation[axis];maxQuaternionJump=(std::max)(maxQuaternionJump,1-std::abs(dot));}
    const auto endpoint=codec.Sample(codec.Duration()),clamped=codec.Sample(codec.Duration()+100);Check(endpoint.size()==52,"Missing source multiblock endpoint");
    for(std::size_t t=0;t<endpoint.size();++t)Check(endpoint[t].translation==clamped[t].translation&&endpoint[t].rotation==clamped[t].rotation,"Source multiblock endpoint did not clamp exactly");
    Report(selected,codec);std::cout<<"SOURCE_MULTIBLOCK_COMPATIBILITY samples="<<samples<<" max_local_selection_error="<<maxSelectionError
        <<" boundary_translation_jump="<<maxTranslationJump<<" boundary_one_minus_abs_quaternion_dot="<<maxQuaternionJump
        <<" boundary_jump_is_diagnostic=1 independent_retail_pose_oracle=0 rig_binding=0\n";
    double frameError=0;unsigned frameSamples=0;
    for(std::size_t block=0;block<codec.BlockCount();++block){const auto& info=codec.Block(block);auto isolated=selected.input;
        isolated.data.assign(selected.input.data.begin()+info.begin,selected.input.data.begin()+info.end);isolated.blockOffsets={0};isolated.transformEndOffsets={std::uint32_t(info.transformEnd-info.begin)};
        isolated.numFrames=info.lastLocalFrame+1;isolated.duration=info.lastLocalFrame*isolated.frameDuration;const auto one=SplineTransformCodec::Load(isolated);
        std::vector<double> frames{double(info.firstFrame),double(info.firstFrame)+.37,double(info.firstFrame)+info.lastLocalFrame*.83,
            std::nextafter(double(info.firstFrame+info.lastLocalFrame),double(info.firstFrame))};
        if(block+1==codec.BlockCount())frames.push_back(info.firstFrame+info.lastLocalFrame);
        for(double coordinate:frames){const auto actual=codec.SampleFrame(coordinate);const auto expected=one.Sample((coordinate-info.firstFrame)*one.FrameDuration());++frameSamples;
            for(std::size_t t=0;t<actual.size();++t){for(unsigned axis=0;axis<3;++axis)frameError=(std::max)(frameError,std::abs(actual[t].translation[axis]-expected[t].translation[axis]));
                for(unsigned axis=0;axis<4;++axis)frameError=(std::max)(frameError,std::abs(actual[t].rotation[axis]-expected[t].rotation[axis]));}}
    }
    Check(frameError<1e-9,"Direct global frames selected the wrong actual source block/local curve");
    const double split=codec.Block(1).firstFrame,prior=std::nextafter(split,0.0);
    const auto direct=codec.SampleFrame(prior),naive=codec.Sample(prior*codec.FrameDuration());double naiveError=0;
    for(std::size_t t=0;t<direct.size();++t){for(unsigned axis=0;axis<3;++axis)naiveError=(std::max)(naiveError,std::abs(direct[t].translation[axis]-naive[t].translation[axis]));
        for(unsigned axis=0;axis<4;++axis)naiveError=(std::max)(naiveError,std::abs(direct[t].rotation[axis]-naive[t].rotation[axis]));}
    Check(naiveError>1e-6,"Original multiblock input did not exercise the global-frame*time drift failure");
    Check(codec.FrameCoordinateAtTime(selected.input.blockDuration)==split&&codec.SourceTimeAtFrame(split)==selected.input.blockDuration&&
        codec.FrameCoordinateAtTime(codec.Duration())==double(codec.FrameCount()-1)&&codec.SourceTimeAtFrame(double(codec.FrameCount()-1))==codec.Duration(),
        "Actual source frame/time endpoint metadata mismatch");
    std::cout<<"SOURCE_MULTIBLOCK_FRAME samples="<<frameSamples<<" shared_frame="<<split<<" fractional_both_sides=1 max_isolated_curve_error="<<frameError
        <<" naive_frame_times_duration_error="<<naiveError<<" direct_block_selection=1 metadata_not_bijective=1\n";
}
void Run(const std::string& base){
    const std::map<std::string,std::string> paths{{"selected_map",base+"/maps/game/trainyard/trainyard.mpk"},{"selected_skeleton",base+"/maps/game/global/global.spk"}};
    StreamMounts mounts([&](const std::string& key)->std::unique_ptr<std::istream>{const auto found=paths.find(key);if(found==paths.end())return std::unique_ptr<std::istream>();return std::unique_ptr<std::istream>(new std::ifstream(found->second,std::ios::binary));});
    mounts.Mount("selected_map");mounts.Mount("selected_skeleton");
    const auto read=[&](const std::string& mount,const std::string& type,const std::string& resource){std::vector<StreamResource> found;for(const auto& entry:mounts.Find(type,resource).matches)if(entry.mountId==mount)found.push_back(entry);
        Check(found.size()==1,"Source resource missing/ambiguous in exact selected archive");return mounts.Read(found.front());};
    const auto hkxa=read("selected_map","hkxa","maps/game/trainyard/trainyard"),skel=read("selected_skeleton","skel","maps/game/global/global");
    const detail::HavokPack pack(hkxa,64u*1024u*1024u,100000,1000000);
    CompareGreeter(Read(pack,"models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx"),hkxa,skel);
    MultiBlock(Read(pack,"models/characters/_humanoid_male/srp_tyd_mg1_order_pickup.hkx"));
    const auto idle=Read(pack,"models/characters/_humanoid_male/st1_hg1_nml_idle.hkx");const auto idleCodec=SplineTransformCodec::Load(idle.input);Check(idleCodec.FrameCount()==41&&idleCodec.Sample(.137).size()==52,"Additional non-greeter source profile failed");Report(idle,idleCodec);
    std::cout<<"WOLF2009_SPLINE_CODEC_SOURCE_OK checks="<<checks<<" selected_clips=3 multiblock_clips=1 hkxa_fingerprint="<<std::hex<<Hash(hkxa)<<" skel_fingerprint="<<Hash(skel)<<std::dec<<" native_runtime=0 generic_rig_binding=0 float_evaluation=0 retail_clock=0\n";
}
}
int main(int argc,char** argv){try{if(argc!=3||std::string(argv[1])!="--source-base")throw std::runtime_error("Usage: --source-base owner-SP/base");std::cout<<std::setprecision(17);Run(argv[2]);return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_SPLINE_CODEC_SOURCE_FAIL checks="<<checks<<" message="<<error.what()<<'\n';return 1;}}
