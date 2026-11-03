// SPDX-License-Identifier: GPL-3.0-or-later
// Independent named-data reader using canonical checked HavokPack access.
#include "SplineClipData.h"
#include "HavokPack.h"
#include <algorithm>
#include <limits>
#include <set>
#ifdef __FAST_MATH__
#error Spline_clip_data_requires_finite_IEEE_arithmetic
#endif
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool value,const char* text,SplineClipFailure failure=SplineClipFailure::InvalidData) {
    if(!value)throw SplineClipError(failure,text);
}
struct Budget {
    const SplineClipDataLimits& limits;
    std::size_t bytes=0,work=0,names=0;
    explicit Budget(const SplineClipDataLimits& l):limits(l){}
    void Store(std::size_t count,std::size_t size){
        Need(size&&bytes<=limits.decodedBytes&&count<=(limits.decodedBytes-bytes)/size,"Spline clip decoded-byte budget",SplineClipFailure::LimitExceeded);
        bytes+=count*size;
    }
    void Work(std::size_t count){Need(work<=limits.work&&count<=limits.work-work,"Spline clip work budget",SplineClipFailure::LimitExceeded);work+=count;}
    std::string Text(const detail::HavokPack& pack,std::uint32_t offset){
        const auto text=pack.DataString(offset);
        Need(text.size()<=limits.nameBytes&&names<=limits.totalNameBytes&&text.size()<=limits.totalNameBytes-names,
             "Spline clip name-byte budget",SplineClipFailure::LimitExceeded);
        names+=text.size();Store(text.size()+1,2);Work(text.size()+1);return text;
    }
};
SplinePointerFact Pointer(const detail::HavokPack& pack,std::uint32_t slot,Budget& budget){
    SplinePointerFact fact;fact.sourceSlot=slot;fact.rawWord=pack.DU(slot);
    const auto found=pack.pointers.find(slot);
    if(found!=pack.pointers.end()){
        fact.hasDataFixup=true;fact.targetOffset=found->second;const auto type=pack.Type(fact.targetOffset);
        budget.Store(type.size()+1,2);fact.targetType=type;
    }
    return fact;
}
template<std::size_t N> void Raw(const detail::HavokPack& pack,std::uint32_t offset,std::array<unsigned char,N>& out){
    pack.DataRange(offset,N);const std::size_t at=pack.sections[2].absolute+std::size_t(offset);
    std::copy(pack.bytes.begin()+at,pack.bytes.begin()+at+N,out.begin());
}
std::uint32_t Variant(const detail::HavokPack& pack,const char* name,const char* type,Budget& budget){
    const auto root=pack.U(28);pack.DataRange(root,8);const auto count=pack.DU(root+4);
    Need(count&&count<=budget.limits.variants,"Spline clip root variant budget",SplineClipFailure::LimitExceeded);
    const auto array=pack.Pointer(root);pack.ArrayRange(array,count,16);std::uint32_t selected=0;bool present=false;
    for(std::uint32_t i=0;i<count;++i){budget.Work(1);const auto slot=array+16*i;
        if(budget.Text(pack,pack.Pointer(slot))!=name)continue;
        Need(!present,"Duplicate selected HKXA named variant",SplineClipFailure::Ambiguous);present=true;
        Need(budget.Text(pack,pack.Pointer(slot+4))==type,"HKXA selected variant type-name mismatch");
        selected=pack.Pointer(slot+8);Need(pack.Type(selected)==type,"HKXA selected variant object-type mismatch");
    }
    Need(present,"Required HKXA named variant absent",SplineClipFailure::NotFound);return selected;
}
SplineArrayFact ArrayFact(const detail::HavokPack& pack,std::uint32_t slot,Budget& budget){
    pack.DataRange(slot,12);SplineArrayFact out;out.pointer=Pointer(pack,slot,budget);out.count=pack.DU(slot+4);out.rawCapacityAndFlags=pack.DU(slot+8);return out;
}
std::vector<std::uint32_t> Values(const detail::HavokPack& pack,const SplineArrayFact& fact,Budget& budget){
    Need(fact.count<=budget.limits.arrayEntries,"Spline clip U32 array budget",SplineClipFailure::LimitExceeded);
    std::vector<std::uint32_t> out;if(!fact.count)return out;
    Need(fact.pointer.hasDataFixup,"Spline clip populated array has no data fixup");pack.ArrayRange(fact.pointer.targetOffset,fact.count,4);
    budget.Store(fact.count,2*sizeof(std::uint32_t));budget.Work(fact.count);out.reserve(fact.count);
    for(std::uint32_t i=0;i<fact.count;++i)out.push_back(pack.DU(fact.pointer.targetOffset+4*i));return out;
}
std::vector<std::int16_t> SignedValues(const detail::HavokPack& pack,std::uint32_t slot,Budget& budget){
    pack.DataRange(slot,8);const auto count=pack.DU(slot+4);Need(count<=budget.limits.arrayEntries,"Spline binding raw index-array budget",SplineClipFailure::LimitExceeded);
    std::vector<std::int16_t> out;if(!count)return out;
    const auto at=pack.Pointer(slot);pack.ArrayRange(at,count,2);budget.Store(count,2*sizeof(std::int16_t));budget.Work(count);out.reserve(count);
    const std::size_t base=pack.sections[2].absolute+std::size_t(at);
    for(std::uint32_t i=0;i<count;++i){const unsigned raw=unsigned(pack.bytes[base+2*i])|(unsigned(pack.bytes[base+2*i+1])<<8);
        out.push_back(static_cast<std::int16_t>(raw<32768?int(raw):int(raw)-65536));}return out;
}
void Bindings(const detail::HavokPack& pack,SplineClipData& out,Budget& budget){
    out.bindingTable=Pointer(pack,out.animationContainerOffset+16,budget);
    out.bindingCount=pack.DU(out.animationContainerOffset+20);
    Need(out.bindingCount<=budget.limits.bindings,"Spline clip binding table budget",SplineClipFailure::LimitExceeded);
    out.bindingState=out.bindingCount?SplineBindingFactsState::Uninterpreted:SplineBindingFactsState::Absent;
    if(!out.bindingCount)return;
    Need(out.bindingTable.hasDataFixup,"Populated HKXA binding table lacks data fixup");pack.ArrayRange(out.bindingTable.targetOffset,out.bindingCount,4);
    budget.Store(out.bindingCount,2*sizeof(SplineBindingFact));budget.Work(out.bindingCount);out.bindings.reserve(out.bindingCount);
    for(std::uint32_t i=0;i<out.bindingCount;++i){SplineBindingFact fact;fact.tableIndex=i;fact.objectOffset=pack.Pointer(out.bindingTable.targetOffset+4*i);
        const auto type=pack.Type(fact.objectOffset);budget.Store(type.size()+1,2);fact.sourceType=type;
        fact.rawObjectWord=pack.DU(fact.objectOffset);fact.hasKnownLayout=type=="hkaAnimationBinding";
        if(!fact.hasKnownLayout){out.bindings.push_back(std::move(fact));continue;}
        Raw(pack,fact.objectOffset,fact.rawHeader);fact.animation=Pointer(pack,fact.objectOffset,budget);
        fact.rawTransformTrackToBoneIndices=SignedValues(pack,fact.objectOffset+4,budget);
        fact.rawFloatTrackToSlotIndices=SignedValues(pack,fact.objectOffset+12,budget);fact.rawBlendHint=fact.rawHeader[20];
        out.bindings.push_back(std::move(fact));
    }
}
SplineClipData Read(const std::vector<unsigned char>& bytes,const std::string& exactName,const SplineClipDataLimits& limits){
    Need(!exactName.empty()&&exactName.size()<=limits.nameBytes,"Invalid selected HKXA clip name");
    for(unsigned char c:exactName)Need(c>=32&&c<=126,"Selected HKXA name is not exact printable source text");
    Need(bytes.size()<=limits.sourceBytes,"Spline clip source-byte budget",SplineClipFailure::LimitExceeded);
    const detail::HavokPack pack(bytes,limits.sourceBytes,limits.objects,limits.fixups);Budget budget(limits);budget.Store(1,2*sizeof(SplineClipData));
    SplineClipData out;budget.Store(exactName.size()+1,2);out.name=exactName;out.sourceBytes=bytes.size();out.sourceFingerprint=14695981039346656037ull;
    budget.Work(bytes.size());
    for(unsigned char b:bytes)out.sourceFingerprint=(out.sourceFingerprint^b)*1099511628211ull;
    out.rootOffset=pack.U(28);out.animationContainerOffset=Variant(pack,"Animation Container","hkaAnimationContainer",budget);
    out.nameContainerOffset=Variant(pack,"animNames","rvHkStringContainer",budget);
    Raw(pack,out.animationContainerOffset,out.rawAnimationContainerHeader);Raw(pack,out.nameContainerOffset,out.rawNameContainerHeader);
    out.skeletonCount=pack.DU(out.animationContainerOffset+4);out.animationCount=pack.DU(out.animationContainerOffset+12);
    out.attachmentCount=pack.DU(out.animationContainerOffset+28);out.skinCount=pack.DU(out.animationContainerOffset+36);
    const auto count=out.animationCount;Need(count&&count<=limits.clips,"HKXA animation table count budget",SplineClipFailure::LimitExceeded);
    Need(count==pack.DU(out.nameContainerOffset+4),"HKXA animation/name table count mismatch");
    const auto objects=pack.Pointer(out.animationContainerOffset+8),names=pack.Pointer(out.nameContainerOffset);
    pack.ArrayRange(objects,count,4);pack.ArrayRange(names,count,4);budget.Store(count,128);budget.Work(count);
    std::set<std::string> seen;bool selected=false;
    for(std::uint32_t i=0;i<count;++i){const auto name=budget.Text(pack,pack.Pointer(names+4*i));
        Need(seen.insert(name).second,"Duplicate HKXA clip name",SplineClipFailure::Ambiguous);
        // Validate every consumed parallel object pointer. Only the selected
        // object's class/body is admitted; other animation kinds remain opaque.
        const auto object=pack.Pointer(objects+4*i);pack.DataRange(object,4);
        if(name==exactName){selected=true;out.nameTableIndex=i;out.animationObjectOffset=object;}
    }
    Need(selected,"Exact selected HKXA clip is absent",SplineClipFailure::NotFound);
    const auto object=out.animationObjectOffset;
    Need(pack.Type(object)=="hkaSplineSkeletalAnimation","Selected HKXA clip is not the spline class",SplineClipFailure::Unsupported);
    Raw(pack,object,out.rawAnimationHeader);out.rawAnimationType=pack.DU(object+8);out.rawEndian=pack.DU(object+124);
    Need(out.rawAnimationType==5&&out.rawEndian==0,"Unsupported selected spline type/endian profile",SplineClipFailure::Unsupported);
    out.extractedMotion=Pointer(pack,object+24,budget);out.annotationTracks=Pointer(pack,object+28,budget);out.annotationTrackCount=pack.DU(object+32);
    auto& input=out.input;input.transformTracks=pack.DU(object+16);input.floatTracks=pack.DU(object+20);input.numFrames=pack.DU(object+36);
    const auto blocks=pack.DU(object+40);input.maxFramesPerBlock=pack.DU(object+44);input.maskAndQuantizationSize=pack.DU(object+48);
    input.duration=pack.F(object+12);input.blockDuration=pack.F(object+52);input.blockInverseDuration=pack.F(object+56);input.frameDuration=pack.F(object+60);
    out.blockOffsets=ArrayFact(pack,object+64,budget);out.floatBlockOffsets=ArrayFact(pack,object+76,budget);
    out.transformOffsets=ArrayFact(pack,object+88,budget);out.floatOffsets=ArrayFact(pack,object+100,budget);out.compressedData=ArrayFact(pack,object+112,budget);
    input.blockOffsets=Values(pack,out.blockOffsets,budget);input.rawFloatBlockOffsets=Values(pack,out.floatBlockOffsets,budget);
    input.rawTransformOffsets=Values(pack,out.transformOffsets,budget);input.rawFloatOffsets=Values(pack,out.floatOffsets,budget);
    Need(blocks&&blocks==input.blockOffsets.size()&&blocks==input.rawFloatBlockOffsets.size(),"HKXA per-block transform-boundary count mismatch");
    Need(out.compressedData.count&&out.compressedData.count<=limits.compressedClipBytes,"Spline compressed payload byte budget",SplineClipFailure::LimitExceeded);
    Need(out.compressedData.pointer.hasDataFixup,"Spline compressed data lacks data fixup");const auto data=out.compressedData.pointer.targetOffset;
    pack.DataRange(data,out.compressedData.count);budget.Store(out.compressedData.count,2);budget.Work(out.compressedData.count/4);
    const std::size_t base=pack.sections[2].absolute+std::size_t(data);
    // Validate before copying payload/retaining an unbounded vector. Source
    // floatBlockOffsets are block-relative, not absolute-from-data indices.
    budget.Store(blocks,2*sizeof(std::uint32_t));input.transformEndOffsets.reserve(blocks);
    for(std::size_t i=0;i<blocks;++i){const std::uint64_t begin=input.blockOffsets[i],end=i+1<blocks?input.blockOffsets[i+1]:out.compressedData.count;
        const std::uint64_t transformEnd=begin+input.rawFloatBlockOffsets[i];
        Need(begin<end&&end<=out.compressedData.count&&input.maskAndQuantizationSize<=end-begin&&
             transformEnd>=begin+input.maskAndQuantizationSize&&transformEnd<=end&&transformEnd<=std::numeric_limits<std::uint32_t>::max(),
             "HKXA block-relative float/transform range is invalid");input.transformEndOffsets.push_back(std::uint32_t(transformEnd));}
    input.data.assign(bytes.begin()+base,bytes.begin()+base+out.compressedData.count);Bindings(pack,out,budget);return out;
}
} // namespace
SplineClipData SplineClipData::Load(const std::vector<unsigned char>& bytes,const std::string& name,const SplineClipDataLimits& limits){
    try{return Read(bytes,name,limits);}catch(const SplineClipError&){throw;}catch(const std::runtime_error& error){throw SplineClipError(SplineClipFailure::InvalidData,error.what());}
}
} // namespace wolf2009
