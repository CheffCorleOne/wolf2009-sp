// SPDX-License-Identifier: GPL-3.0-or-later
// Hand-authored HKXA-like packfiles, no proprietary assets or SDK output.
#include "SplineClipData.h"
#include "../HavokTestFixture.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
using namespace wolf2009;
using namespace wolf2009_test;
namespace {
unsigned checks=0,groups=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void Reject(const std::function<void()>& action,SplineClipFailure failure,const char* text){bool rejected=false;
    try{action();}catch(const SplineClipError& error){rejected=true;Check(error.Failure()==failure,"Wrong typed clip-data refusal");}
    Check(rejected,text);
}
void Refuse(const HavokFixture& fixture,const char* text,SplineClipFailure failure=SplineClipFailure::InvalidData){
    Reject([&]{SplineClipData::Load(fixture.File(),"models/own/first.hkx");},failure,text);
}
struct Fixture : HavokFixture {
    Fixture():HavokFixture(4096){
        links[0]=16;U(data,4,2);
        links[16]=128;links[20]=160;links[24]=256;
        Text(data,128,"Animation Container");Text(data,160,"hkaAnimationContainer");types[256]="hkaAnimationContainer";
        links[32]=192;links[36]=208;links[40]=320;
        Text(data,192,"animNames");Text(data,208,"rvHkStringContainer");types[320]="rvHkStringContainer";
        links[264]=384;U(data,268,2);links[320]=416;U(data,324,2);
        links[384]=1024;links[388]=1280;links[416]=512;links[420]=576;
        Text(data,512,"models/own/first.hkx");Text(data,576,"models/own/second.hkx");
        Clip(1024,2,1536,1552,1600);Clip(1280,3,1808,1824,1872);
    }
    void Clip(std::uint32_t object,unsigned tracks,std::uint32_t blockArray,std::uint32_t floatArray,std::uint32_t payload){
        types[object]="hkaSplineSkeletalAnimation";U(data,object,0x12345678);U(data,object+8,5);F(data,object+12,.4f);
        U(data,object+16,tracks);U(data,object+36,5);U(data,object+40,1);U(data,object+44,5);U(data,object+48,tracks*4);
        F(data,object+52,.4f);F(data,object+56,2.5f);F(data,object+60,.1f);
        links[object+64]=blockArray;U(data,object+68,1);U(data,object+72,0x80000001);U(data,blockArray,0);
        links[object+76]=floatArray;U(data,object+80,1);U(data,object+84,0x80000001);U(data,floatArray,tracks*4);
        links[object+112]=payload;U(data,object+116,tracks*4);U(data,object+120,0x80000000|tracks*4);
        for(unsigned i=0;i<tracks;++i)data[payload+4*i]=0x45;
    }
    void Binding(){
        links[272]=2288;U(data,276,1);links[2288]=2304;types[2304]="hkaAnimationBinding";
        links[2304]=1280;links[2308]=2368;U(data,2312,3);links[2316]=2384;U(data,2320,1);data[2324]=255;data[2325]=0xaa;
        data[2368]=2;data[2370]=255;data[2371]=255;data[2372]=0;data[2384]=7;
    }
    void TwoBlocks(){
        F(data,1036,.6f);U(data,1060,7);U(data,1064,2);U(data,1068,4);F(data,1076,.3f);F(data,1080,1/.3f);
        U(data,1092,2);U(data,1096,0x80000002);U(data,1536,0);U(data,1540,8);
        U(data,1104,2);U(data,1108,0x80000002);U(data,1552,8);U(data,1556,8);
        U(data,1140,16);U(data,1144,0x80000010);data[1608]=0x45;data[1612]=0x45;
    }
};
void NamesOwnership(){++groups;Fixture fixture;auto source=fixture.File();const auto original=source;
    const auto first=SplineClipData::Load(source,"models/own/first.hkx"),second=SplineClipData::Load(source,"models/own/second.hkx");
    Check(source==original,"Reader modified its source bytes");Check(first.nameTableIndex==0&&second.nameTableIndex==1&&first.animationObjectOffset==1024&&second.animationObjectOffset==1280,"Named parallel selection failed");
    Check(first.input.transformTracks==2&&second.input.transformTracks==3,"Reader imposed one rig's track count");
    Check(first.sourceBytes==source.size()&&first.sourceFingerprint==second.sourceFingerprint&&first.rawAnimationHeader[0]==0x78,"Source identity/raw header not retained");
    Check(first.input.transformEndOffsets==std::vector<std::uint32_t>{8}&&first.blockOffsets.rawCapacityAndFlags==0x80000001,"Source block boundary/raw capacity fact changed");
    Check(first.bindingState==SplineBindingFactsState::Absent&&first.bindingCount==0&&first.bindings.empty()&&!first.bindingTable.hasDataFixup,"Absent binding silently became a mapping");
    Check(!first.extractedMotion.hasDataFixup&&first.annotationTrackCount==0,"Absent opaque metadata changed");
    source.assign(1,255);Check(first.input.data.size()==8&&SplineTransformCodec::Load(first.input).Sample(.13).size()==2,"Reader output borrowed original bytes");
    Reject([&]{SplineClipData::Load(original,"MODELS/OWN/FIRST.HKX");},SplineClipFailure::NotFound,"Reader casefolded a source clip name");
    Reject([&]{SplineClipData::Load(original,"models/own/first");},SplineClipFailure::NotFound,"Reader invented a name extension");
}
void OpaqueBindings(){++groups;Fixture fixture;fixture.Binding();auto doc=SplineClipData::Load(fixture.File(),"models/own/first.hkx");
    Check(doc.animationCount==2&&doc.bindingCount==1&&doc.bindingState==SplineBindingFactsState::Uninterpreted&&doc.bindings.size()==1,"Sparse optional binding table rejected/defaulted");
    const auto& binding=doc.bindings[0];Check(binding.hasKnownLayout&&binding.sourceType=="hkaAnimationBinding"&&binding.animation.hasDataFixup&&binding.animation.targetOffset==1280,"Raw binding target was reassigned to selected clip");
    Check(binding.rawBlendHint==255&&binding.rawHeader[21]==0xaa&&binding.rawTransformTrackToBoneIndices==std::vector<std::int16_t>({2,-1,0})&&binding.rawFloatTrackToSlotIndices==std::vector<std::int16_t>({7}),"Unknown hint/raw mapping values interpreted or discarded");
    Check(SplineTransformCodec::Load(doc.input).Sample(.11).size()==2,"Uninterpreted binding unnecessarily blocked independent curves");
    fixture.types[2304]="ownUnknownBinding";doc=SplineClipData::Load(fixture.File(),"models/own/first.hkx");
    Check(!doc.bindings[0].hasKnownLayout&&doc.bindings[0].sourceType=="ownUnknownBinding"&&doc.bindings[0].rawTransformTrackToBoneIndices.empty(),"Unknown binding layout was falsely decoded");
    Check(SplineTransformCodec::Load(doc.input).Sample(.1).size()==2,"Opaque binding layout blocked bounded independent input");
    Fixture missing;missing.Binding();missing.links.erase(272);Refuse(missing,"Populated binding table missing fixup accepted");
    Fixture truncated;truncated.Binding();truncated.links[2308]=4094;Refuse(truncated,"Raw binding mapping exceeds section");
}
void BlocksAndRawTables(){++groups;Fixture fixture;fixture.TwoBlocks();const auto doc=SplineClipData::Load(fixture.File(),"models/own/first.hkx");
    Check(doc.input.blockOffsets==std::vector<std::uint32_t>({0,8})&&doc.input.rawFloatBlockOffsets==std::vector<std::uint32_t>({8,8})&&doc.input.transformEndOffsets==std::vector<std::uint32_t>({8,16}),"Second block used absolute float offset semantics");
    const auto codec=SplineTransformCodec::Load(doc.input);Check(codec.BlockCount()==2&&codec.Sample(.599).size()==2,"Reader multiblock input did not decode");
    Fixture raw;raw.links[1112]=2400;U(raw.data,1116,2);U(raw.data,1120,0xdeadbeef);U(raw.data,2400,29);U(raw.data,2404,11);
    raw.links[1124]=2416;U(raw.data,1128,1);U(raw.data,1132,0xcafebabe);U(raw.data,2416,99);
    const auto facts=SplineClipData::Load(raw.File(),"models/own/first.hkx");Check(facts.input.rawTransformOffsets==std::vector<std::uint32_t>({29,11})&&facts.input.rawFloatOffsets==std::vector<std::uint32_t>({99})&&facts.transformOffsets.rawCapacityAndFlags==0xdeadbeef,"Individual-track tables were invented, sorted or discarded");
    // These auxiliary table contents are facts, not sequential-codec addresses.
    Check(SplineTransformCodec::Load(facts.input).Sample(.12).size()==2,"Unused raw tables changed sequential curve decoding");
}
void InvalidNamesAndTypes(){++groups;
    {Fixture f;Text(f.data,576,"models/own/first.hkx");Refuse(f,"Duplicate source clip accepted",SplineClipFailure::Ambiguous);}
    {Fixture f;U(f.data,324,1);Refuse(f,"Mismatched parallel table counts accepted");}
    {Fixture f;f.links.erase(416);Refuse(f,"Missing name fixup accepted");}
    {Fixture f;f.links.erase(384);Refuse(f,"Missing animation object fixup accepted");}
    {Fixture f;f.types[1024]="hkaInterleavedSkeletalAnimation";Refuse(f,"Selected wrong animation class accepted",SplineClipFailure::Unsupported);}
    {Fixture f;f.types[1280]="hkaInterleavedSkeletalAnimation";Check(SplineClipData::Load(f.File(),"models/own/first.hkx").input.transformTracks==2,"Unselected animation class was promoted/blocked");}
    {Fixture f;U(f.data,1032,1);Refuse(f,"Wrong selected animation type enum accepted",SplineClipFailure::Unsupported);}
    {Fixture f;U(f.data,1148,1);Refuse(f,"Unknown endian profile accepted",SplineClipFailure::Unsupported);}
    {Fixture f;f.links[32]=128;f.links[36]=160;f.links[40]=256;Refuse(f,"Duplicate required root variant accepted",SplineClipFailure::Ambiguous);}
    {Fixture f;f.types[256]="ownWrongContainer";Refuse(f,"Mismatched variant class type accepted");}
}
void InvalidRanges(){++groups;
    {Fixture f;f.links.erase(1088);Refuse(f,"Block array missing fixup accepted");}
    {Fixture f;U(f.data,1104,2);Refuse(f,"Float boundary count mismatch accepted");}
    {Fixture f;U(f.data,1552,7);Refuse(f,"Transform end before mask range accepted");}
    {Fixture f;U(f.data,1552,9);Refuse(f,"Transform end outside block accepted");}
    {Fixture f;f.TwoBlocks();U(f.data,1540,0);Refuse(f,"Duplicate block start accepted");}
    {Fixture f;f.TwoBlocks();U(f.data,1556,0xffffffffu);Refuse(f,"Wrapping relative boundary accepted");}
    {Fixture f;f.links[1136]=4094;Refuse(f,"Compressed payload exceeds section");}
    {Fixture f;F(f.data,1036,std::numeric_limits<float>::infinity());Refuse(f,"Infinite source duration accepted");}
    {Fixture f;auto bytes=f.File();U(bytes,12,6);Reject([&]{SplineClipData::Load(bytes,"models/own/first.hkx");},SplineClipFailure::InvalidData,"Noncanonical pack version accepted");}
    {Fixture f;f.data[1600]=0x4c;const auto doc=SplineClipData::Load(f.File(),"models/own/first.hkx");bool unsupported=false;try{SplineTransformCodec::Load(doc.input);}catch(const SplineCodecError& e){unsupported=e.Failure()==SplineCodecFailure::Unsupported;}Check(unsupported,"Reader/codec boundary silently decoded unknown quantization");}
}
void BudgetsAndAtomicity(){++groups;Fixture fixture;const auto source=fixture.File(),unchanged=source;const auto before=SplineClipData::Load(source,"models/own/first.hkx");
    for(unsigned test=0;test<8;++test){SplineClipDataLimits limits;if(test==0)limits.sourceBytes=1;if(test==1)limits.clips=1;if(test==2)limits.variants=1;if(test==3)limits.nameBytes=3;if(test==4)limits.totalNameBytes=1;if(test==5)limits.decodedBytes=1;if(test==6)limits.work=1;if(test==7)limits.compressedClipBytes=1;
        Reject([&]{SplineClipData::Load(source,"models/own/first.hkx",limits);},test==3?SplineClipFailure::InvalidData:SplineClipFailure::LimitExceeded,"Reader budget not enforced");}
    Fixture withBinding;withBinding.Binding();SplineClipDataLimits limits;limits.bindings=0;
    Reject([&]{SplineClipData::Load(withBinding.File(),"models/own/first.hkx",limits);},SplineClipFailure::LimitExceeded,"Binding count budget bypassed");
    Fixture arrays;arrays.TwoBlocks();limits=SplineClipDataLimits();limits.arrayEntries=1;
    Reject([&]{SplineClipData::Load(arrays.File(),"models/own/first.hkx",limits);},SplineClipFailure::LimitExceeded,"Metadata array budget bypassed");
    Fixture longName;Text(longName.data,576,std::string(100,'x'));limits=SplineClipDataLimits();limits.nameBytes=64;
    Reject([&]{SplineClipData::Load(longName.File(),"models/own/first.hkx",limits);},SplineClipFailure::LimitExceeded,"Unselected source names escaped the byte budget");
    // The canonical reader owns its typed-object/fixup accounting. Its bounded
    // failure must remain a typed refusal at this reader's public boundary.
    for(unsigned canonical=0;canonical<2;++canonical){limits=SplineClipDataLimits();if(canonical==0)limits.objects=1;else limits.fixups=1;
        Reject([&]{SplineClipData::Load(source,"models/own/first.hkx",limits);},SplineClipFailure::InvalidData,"Canonical pack budgets were not forwarded");}
    Check(source==unchanged&&before.input.data.size()==8&&before.bindingState==SplineBindingFactsState::Absent&&SplineTransformCodec::Load(before.input).TrackCount()==2,"Rejected load mutated previous owning result/input");
}
}
int main(){try{NamesOwnership();OpaqueBindings();BlocksAndRawTables();InvalidNamesAndTypes();InvalidRanges();BudgetsAndAtomicity();
    std::cout<<"WOLF2009_SPLINE_CLIP_SYNTHETIC checks="<<checks<<" groups="<<groups<<" failed=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_SPLINE_CLIP_FAIL checks="<<checks<<" groups="<<groups<<" message="<<error.what()<<'\n';return 1;}}
