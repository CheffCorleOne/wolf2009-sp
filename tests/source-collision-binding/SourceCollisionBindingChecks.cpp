// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored wire/geometry fixtures; original resources are optional.
#include "SourceCollisionBinding.h"
#include "SupportGameplayMap.h"
#include "SupportCollisionIndex.h"
#include "Archive.h"
#include "../HavokTestFixture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace wolf2009;
using namespace wolf2009_test;
using Policy=SupportGameplayContentsPolicy;
unsigned checks=0,refusals=0;
void Check(bool ok,const char* why){++checks;Need(ok,why);}
template<class F>void Refuse(F operation,const char* why){bool caught=false;
    try{operation();}catch(const std::runtime_error&){caught=true;}++refusals;Check(caught,why);}
std::string Hex(const std::string& text){const char digits[]="0123456789abcdef";std::string out;
    for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string ChangeLine(const std::string& text,unsigned index,const std::string& value){
    std::size_t begin=0;for(unsigned i=0;i<index;++i){begin=text.find('\n',begin);Need(begin!=std::string::npos,"Own wire fixture line");++begin;}
    const auto end=text.find('\n',begin);Need(end!=std::string::npos,"Own wire fixture final line");
    return text.substr(0,begin)+value+text.substr(end);
}
struct Hash {
    std::uint64_t value=UINT64_C(14695981039346656037);
    void Word(std::uint64_t word){for(unsigned i=0;i<8;++i){value^=static_cast<unsigned char>(word>>(i*8));value*=UINT64_C(1099511628211);}}
    void Text(const std::string& text){Word(text.size());for(unsigned char c:text){value^=c;value*=UINT64_C(1099511628211);}}
    void Real(double d){std::uint64_t bits;static_assert(sizeof(bits)==sizeof(d),"binary64 audit");std::memcpy(&bits,&d,8);Word(bits);}
    void Vec(CollisionVec3 p){Real(p.x);Real(p.y);Real(p.z);}
    void Pose(const CollisionRigidTransform& p){for(auto v:p.axis)Vec(v);Vec(p.translation);}
};
void CylinderHash(Hash& h,const HavokCylinderGeometryMetadata& c){
    h.Word(unsigned(c.profile));h.Word(c.rawCylinderRadiusBits);h.Word(c.rawBaseRadiusFactorBits);h.Word(c.quantizationKBits);
    h.Word(c.virtualVertexStepBits);h.Word(c.observedRoundThresholdBits);h.Word(c.hasRuntimeThresholdObservation);h.Word(c.matchesNativeSupportSelection);
}
std::uint64_t Audit(const SupportCollisionWorld& world){
    Hash h;h.Word(world.sourceFingerprint);h.Word(world.sourceBytes);h.Text(world.selectedBodyName);h.Word(world.sourceBodyOffset);h.Word(world.rootShapeOffset);
    h.Word(world.rootRawFilterInfo);h.Word(world.rootRawFilterKnown);h.Word(world.totalCorePoints);h.Word(world.expectedTriangles);h.Word(world.importedTriangles);
    h.Word(world.otherSourceRigidBodies);h.Word(world.selectedGeometryComplete);h.Word(world.selectedHierarchyComplete);h.Word(world.selectedContentsComplete);
    h.Word(world.selectedChildFiltersComplete);h.Word(world.selectedContactMetadataComplete);h.Word(world.selectedCoverageComplete);
    h.Word(world.allSourceRigidBodiesSelected);h.Word(world.entityBindingsResolved);h.Word(world.nativeRunnable);h.Word(world.shapes.size());
    for(const auto& s:world.shapes){h.Word(unsigned(s.kind));h.Word(s.source.sourceShapeOffset);h.Word(s.source.sourceRecordOffset);h.Word(s.source.sourceRegion);
        h.Word(s.source.occurrence);h.Word(s.source.sourceSubpartIndex);h.Word(s.source.triangleIndex);h.Word(s.corePoints.size());h.Word(s.declaredCorePoints);
        for(auto p:s.corePoints)h.Vec(p);h.Real(s.radius);h.Pose(s.placement);h.Vec(s.worldBounds.minimum);h.Vec(s.worldBounds.maximum);
        h.Word(s.filter.inheritedRawContents);h.Word(s.filter.rawChildFilterInfo);h.Word(s.filter.inheritedContentsKnown);h.Word(s.filter.childFilterKnown);
        h.Word(unsigned(s.filter.contentsProvenance));h.Word(unsigned(s.filter.childFilterStatus));h.Word(s.triangle.rawIndexType);h.Word(s.triangle.rawFlipByte);
        h.Word(s.triangle.weldingType);h.Word(s.triangle.rawTriangleOffset);h.Word(s.triangle.rawRadiusBits);h.Word(std::uint64_t(s.triangle.weldingIndex));
        h.Word(s.triangle.weldingArrayCount);h.Word(s.triangle.weldingInfo);h.Word(unsigned(s.triangle.weldingStatus));for(auto b:s.triangle.rawExtrusionBits)h.Word(b);
        h.Word(s.triangle.contactMetadataQualified);CylinderHash(h,s.cylinder);
    }
    h.Word(world.sourceOmissions.size());for(const auto& o:world.sourceOmissions){h.Text(o.source.type);h.Word(o.source.sourceShapeOffset);h.Word(o.source.sourceSubpartIndex);
        h.Word(o.source.triangles);h.Word(o.source.occurrence);h.Pose(o.source.placement);h.Word(o.source.rawContents);h.Word(o.source.contentsKnown);
        h.Word(o.source.hasWrapperRadiusMetadata);h.Word(o.source.wrapperRadiusCount);h.Real(o.source.minimumWrapperRadius);h.Real(o.source.maximumWrapperRadius);
        h.Word(unsigned(o.resolution));h.Word(o.firstShape);h.Word(o.shapeCount);}
    for(const auto& p:world.sourceUnsupported){h.Text(p.first);h.Word(p.second);}h.Word(0x1234);
    for(const auto& p:world.unresolved){h.Text(p.first);h.Word(p.second);}return h.value;
}
SourceCollisionBodyRequest Request(){SourceCollisionBodyRequest r;r.policy=SourceCollisionBodyPolicy::NativeCommonContentsCylinderEnvelopeV1;
    r.resourcePath="wolf2009/source_map/hkxs/maps/game/authored";r.bodyName="models/Authored\\Exact Name.hkx";
    r.expectedEpoch=7;r.expectedBytes=4096;r.expectedFingerprint=UINT64_C(0xfedcba9876543210);return r;}
SourceCollisionBodyBinding Binding(){SourceCollisionBodyBinding b;b.request=Request();b.epoch=7;b.bytes=4096;b.fingerprint=b.request.expectedFingerprint;
    b.shapes=16;b.corePoints=128;b.nativeContents=3;return b;}
void Wire(){
    const auto request=Request();const auto path=SourceCollisionBodyRequestPath(request);
    Check(path.compare(0,std::strlen(SourceCollisionBodyRequestPrefix()),SourceCollisionBodyRequestPrefix())==0,"Request prefix");
    Check(SameSourceCollisionBodyRequest(request,ParseSourceCollisionBodyRequestPath(path)),"Exact request roundtrip");
    Check(ParseSourceCollisionBodyRequestPath(path).bodyName=="models/Authored\\Exact Name.hkx","Body case/slash/space retained exactly");
    const auto original=Binding();const auto text=SerializeSourceCollisionBodyBinding(original);const auto restored=ParseSourceCollisionBodyBinding(text);
    Check(SameSourceCollisionBodyRequest(original.request,restored.request)&&restored.epoch==7&&restored.bytes==4096&&restored.fingerprint==original.fingerprint&&
        restored.shapes==16&&restored.corePoints==128&&restored.nativeContents==3,"Binding roundtrip retains all ownership/count/contents facts");
    const auto model=SourceCollisionBodyModelName(original);Check(SerializeSourceCollisionBodyBinding(ParseSourceCollisionBodyModelName(model))==text,"Model alias roundtrip");
    auto other=original;other.request.expectedEpoch=0;++other.epoch;Check(SourceCollisionBodyModelName(other)!=model,"Model alias owns actual generation");
    other=original;other.request.expectedFingerprint=0;++other.fingerprint;Check(SourceCollisionBodyModelName(other)!=model,"Model alias owns source fingerprint");
    other=original;other.request.expectedBytes=0;++other.bytes;Check(SourceCollisionBodyModelName(other)!=model,"Model alias owns byte identity");
    for(auto policy:{SourceCollisionBodyPolicy::DeclaredDoom3BitsV1,SourceCollisionBodyPolicy::NativeCommonContentsCylinderEnvelopeV1,SourceCollisionBodyPolicy::NativeSolidEntityCylinderEnvelopeV1}){
        auto r=request;r.policy=policy;Check(SameSourceCollisionBodyRequest(r,ParseSourceCollisionBodyRequestPath(SourceCollisionBodyRequestPath(r))),"Explicit policy roundtrip");}
    auto wildcard=request;wildcard.expectedEpoch=wildcard.expectedBytes=wildcard.expectedFingerprint=0;
    Check(SameSourceCollisionBodyRequest(wildcard,ParseSourceCollisionBodyRequestPath(SourceCollisionBodyRequestPath(wildcard))),"Explicit unpinned request retained");
    auto maximum=original;maximum.request.expectedEpoch=maximum.epoch=(std::numeric_limits<std::uint64_t>::max)();
    maximum.request.expectedFingerprint=maximum.fingerprint=(std::numeric_limits<std::uint64_t>::max)();maximum.request.expectedBytes=maximum.bytes=64u<<20;
    maximum.request.bodyName=std::string(512,'x');maximum.shapes=300000;maximum.corePoints=8000000;maximum.nativeContents=0xffffffffu;
    Check(SerializeSourceCollisionBodyBinding(ParseSourceCollisionBodyModelName(SourceCollisionBodyModelName(maximum)))==SerializeSourceCollisionBodyBinding(maximum),"Boundary wire budgets and integer widths");
    for(const std::string& resource:{"wolf2009/a/HKXS/map","wolf2009/a/hkxs/../map","wolf2009//hkxs/map","wolf2009/a/hkxs/./map","wolf2009/a/hkxs/map/","wolf2009/a/hkxs/space name","other/a/hkxs/map","wolf2009/a/modl/map"}){
        auto r=request;r.resourcePath=resource;Refuse([&]{SourceCollisionBodyRequestPath(r);},"Invalid typed resource accepted");}
    for(const std::string& name:{std::string(),std::string(513,'x'),std::string("a\nb"),std::string("a\0b",3),std::string("\xc3\xa9")}){
        auto r=request;r.bodyName=name;Refuse([&]{SourceCollisionBodyRequestPath(r);},"Invalid exact body name accepted");}
    auto r=request;r.policy=SourceCollisionBodyPolicy::Unspecified;Refuse([&]{SourceCollisionBodyRequestPath(r);},"Unspecified policy accepted");
    r=request;r.expectedBytes=(64u<<20)+1u;Refuse([&]{SourceCollisionBodyRequestPath(r);},"Source byte expectation overflow accepted");
    r=request;r.resourcePath="wolf2009/a/hkxs/"+std::string(1025,'x');Refuse([&]{SourceCollisionBodyRequestPath(r);},"Source path byte budget ignored");
    const auto requestText=text.substr(0,text.find("Wolf2009CollisionBodyBinding1\n"));
    for(const std::string& number:{"01","-1","+1"," 1","1 ","1e3","18446744073709551616"}){
        const auto malformed=ChangeLine(requestText,4,number);
        Refuse([&]{ParseSourceCollisionBodyRequestPath(std::string(SourceCollisionBodyRequestPrefix())+Hex(malformed));},"Noncanonical/overflow integer accepted");}
    for(const std::string& policy:{"0","4","18446744073709551615"}){
        const auto malformed=ChangeLine(requestText,1,policy);
        Refuse([&]{ParseSourceCollisionBodyRequestPath(std::string(SourceCollisionBodyRequestPrefix())+Hex(malformed));},"Unknown policy wire value accepted");}
    for(const std::string& malformed:{path+"0",path+"gg",path+Hex("extra\n"),std::string(SourceCollisionBodyRequestPrefix())+"FF",std::string(8193,'a'),"x"+path})
        Refuse([&]{ParseSourceCollisionBodyRequestPath(malformed);},"Bad request framing/encoding accepted");
    for(unsigned line:{0u,7u}){const auto malformed=ChangeLine(text,line,"wrong");Refuse([&]{ParseSourceCollisionBodyBinding(malformed);},"Bad record signature accepted");}
    for(const std::string& malformed:{text.substr(0,text.size()-1),text+"\n",text+"extra\n",ChangeLine(text,8,"0"),ChangeLine(text,9,"0"),
        ChangeLine(text,11,"0"),ChangeLine(text,11,"300001"),ChangeLine(text,12,"15"),ChangeLine(text,12,"8000001"),ChangeLine(text,13,"0"),ChangeLine(text,13,"4294967296")})
        Refuse([&]{ParseSourceCollisionBodyBinding(malformed);},"Bad binding framing/metadata accepted");
    for(unsigned line:{8u,9u,10u}){const auto malformed=ChangeLine(text,line,"999");Refuse([&]{ParseSourceCollisionBodyBinding(malformed);},"Requested source identity mismatch accepted");}
    Refuse([&]{ParseSourceCollisionBodyModelName(model+"0");},"Malformed model alias accepted");
    Refuse([&]{ParseSourceCollisionBodyModelName(path);},"Request path accepted as model alias");
    Check(SerializeSourceCollisionBodyBinding(original)==text&&SourceCollisionBodyRequestPath(request)==path,"Wire refusals mutated earlier values");
}
SupportCollisionWorld BoxWorld(){
    SupportCollisionWorld w;w.sourceBytes=4096;w.sourceFingerprint=123;w.selectedBodyName="maps/authored";w.rootRawFilterKnown=true;w.rootRawFilterInfo=3;
    w.selectedCoverageComplete=w.allSourceRigidBodiesSelected=true;SupportCollisionShape s;s.source.occurrence=1;s.source.sourceShapeOffset=128;s.source.sourceRegion=99;
    s.filter.inheritedRawContents=3;s.filter.inheritedContentsKnown=true;s.declaredCorePoints=8;
    for(unsigned i=0;i<8;++i)s.corePoints.push_back({i&1?1.:-1.,i&2?1.:-1.,i&4?1.:-1.});w.totalCorePoints=8;w.shapes.push_back(s);return w;
}
struct CylinderPack : HavokFixture {
    CylinderPack():HavokFixture(4096){types[64]="hkpRigidBody";types[576]="hkpCylinderShape";links[64+16]=576;links[64+112]=800;
        Text(data,800,"models/authored/cylinder");for(unsigned i=0;i<3;++i)F(data,64+224+i*20,1);F(data,64+284,1);F(data,64+332,1);F(data,64+348,1);
        F(data,576+16,.0254f);F(data,576+20,.0508f);U(data,576+24,0x7fc00123u);F(data,576+40,-.127f);F(data,576+56,.127f);
        F(data,576+64,1);F(data,576+84,1);}
};
class IncludeAll : public SupportCollisionQueryFilter {public:SupportFilterDecision Select(const SupportCollisionIdentity&,const SupportCollisionFilter&)const override{return SupportFilterDecision::Include;}};
void ResponseProfiles(){
    auto world=BoxWorld();const auto before=Audit(world);const auto legacy=AdmitSelectedSupportGameplayBody(world,Policy::DeclaredDoom3BitsV1);
    const auto native=AdmitSelectedSupportGameplayBody(world,Policy::NativeSourceWordsCylinderEnvelopeV1);
    Check(legacy.size()==1&&legacy[0].contents==3&&native.size()==1&&native[0].contents==3&&native[0].material=="wolf2009/collision/source-word-v1/00000003","Explicit response material/domain differs from raw geometry");
    Check(Audit(world)==before,"Response admission changed source metadata");
    world=BoxWorld();world.selectedContentsComplete=world.selectedCoverageComplete=false;world.shapes[0].kind=SupportCollisionKind::Triangle;
    world.shapes[0].corePoints={{-1,-1,0},{1,-1,0},{0,1,0}};world.shapes[0].declaredCorePoints=3;world.totalCorePoints=3;
    world.shapes[0].triangle.contactMetadataQualified=true;world.shapes[0].filter.childFilterKnown=true;world.shapes[0].filter.childFilterStatus=HavokTriangleQueryStatus::Resolved;
    world.shapes[0].source.sourceSubpartIndex=0;world.shapes[0].source.triangleIndex=0;world.shapes[0].source.sourceRecordOffset=256;
    world.shapes[0].filter.contentsProvenance=SupportCollisionContentsProvenance::TriangleOccurrenceValueUnmapped;world.shapes[0].filter.inheritedContentsKnown=false;
    const auto triangleBefore=Audit(world);Check(AdmitSelectedSupportGameplayBody(world,Policy::NativeSourceWordsCylinderEnvelopeV1)[0].contents==3,"Explicit zero triangle child falls back to retained root");
    world.shapes[0].filter.rawChildFilterInfo=0x02000003u;
    Check(AdmitSelectedSupportGameplayBody(world,Policy::NativeSourceWordsCylinderEnvelopeV1)[0].contents==0x02000003u,"Nonzero raw triangle word was flattened");
    world.shapes[0].filter.rawChildFilterInfo=0;Check(Audit(world)==triangleBefore,"Triangle source provenance was rewritten");
    // A source occurrence identifies the storage/subpart placement. Triangles
    // in that part share it and retain their distinct per-triangle indices.
    auto siblings=world;siblings.shapes.push_back(siblings.shapes.front());
    siblings.shapes.back().source.triangleIndex=1;siblings.shapes.back().source.sourceRecordOffset=262;
    Check(AdmitSelectedSupportGameplayBody(siblings,Policy::NativeSourceWordsCylinderEnvelopeV1).size()==2,
        "Distinct source triangles in one retained occurrence were rejected as duplicates");
    siblings.shapes.back()=siblings.shapes.front();
    Refuse([&]{AdmitSelectedSupportGameplayBody(siblings,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Exact duplicate source triangle admitted");
    Refuse([&]{AdmitSelectedSupportGameplayBody(world,Policy::DeclaredDoom3BitsV1);},"Default strict admission accepted unmapped triangle source");
    for(unsigned failure=0;failure<4;++failure){auto bad=world;
        if(failure==0)bad.shapes[0].triangle.contactMetadataQualified=false;if(failure==1)bad.shapes[0].filter.childFilterKnown=false;
        if(failure==2)bad.shapes[0].filter.childFilterStatus=HavokTriangleQueryStatus::Unavailable;if(failure==3)bad.rootRawFilterKnown=false;
        Refuse([&]{AdmitSelectedSupportGameplayBody(bad,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Unknown triangle response metadata admitted");}
    for(std::uint32_t word:{0u,4096u,0x04000000u}){auto bad=BoxWorld();bad.shapes[0].filter.inheritedRawContents=word;
        Refuse([&]{AdmitSelectedSupportGameplayBody(bad,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Unknown/trigger contents admitted");}
    for(unsigned failure=0;failure<7;++failure){auto bad=BoxWorld();
        if(failure==0)bad.selectedGeometryComplete=false;if(failure==1)bad.selectedHierarchyComplete=false;if(failure==2)bad.selectedChildFiltersComplete=false;
        if(failure==3)bad.selectedContactMetadataComplete=false;if(failure==4)bad.shapes[0].filter.inheritedContentsKnown=false;
        if(failure==5)bad.shapes.push_back(bad.shapes.front());if(failure==6)bad.shapes.clear();
        Refuse([&]{AdmitSelectedSupportGameplayBody(bad,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Incomplete/duplicate/empty source admitted");}
    auto omitted=BoxWorld();omitted.sourceOmissions.resize(1);Refuse([&]{AdmitSelectedSupportGameplayBody(omitted,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Unresolved source omission admitted");
    Refuse([&]{AdmitSelectedSupportGameplayBody(BoxWorld(),static_cast<Policy>(99));},"Unknown response policy admitted");
    const auto bytes=CylinderPack().File();const auto cylinder=DecodeSelectedSupportGameplayBody(bytes,"models/authored/cylinder",Policy::NativeSolidEntityCylinderEnvelopeV1,true);
    const auto cylinderBefore=Audit(cylinder);Check(cylinder.shapes.size()==1&&cylinder.shapes[0].kind==SupportCollisionKind::Cylinder&&cylinder.shapes[0].corePoints.size()==32,"Original-style authored virtual cylinder decoded");
    const auto solid=AdmitSelectedSupportGameplayBody(cylinder,Policy::NativeSolidEntityCylinderEnvelopeV1);
    {auto invalid=cylinder;invalid.sourceUnsupported["unverified-storage-contents"]=1;invalid.unresolved["unverified-storage-contents"]=1;
        Refuse([&]{AdmitSelectedSupportGameplayBody(invalid,Policy::NativeSolidEntityCylinderEnvelopeV1);},"Unknown storage material layout enabled by zero-template override");}
    {auto invalid=cylinder;invalid.unresolved["unrecognized-contact-policy"]=1;
        Refuse([&]{AdmitSelectedSupportGameplayBody(invalid,Policy::NativeSolidEntityCylinderEnvelopeV1);},"Unknown source diagnostic enabled by native entity override");}
    Check(solid.size()==1&&solid[0].contents==3&&cylinder.rootRawFilterInfo==0&&cylinder.shapes[0].filter.inheritedRawContents==0&&
        !cylinder.shapes[0].filter.inheritedContentsKnown,"Explicit solid response retains raw zero-source facts");
    Check(cylinder.shapes[0].cylinder.rawBaseRadiusFactorBits==0x7fc00123u&&!cylinder.shapes[0].cylinder.matchesNativeSupportSelection,"Opaque cylinder payload and retail selector distinction retained");
    Refuse([&]{AdmitSelectedSupportGameplayBody(cylinder,Policy::DeclaredDoom3BitsV1);},"Default strict cylinder admission relaxed");
    Refuse([&]{AdmitSelectedSupportGameplayBody(cylinder,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Raw-zero resource enabled without solid-entity policy");
    auto authored=cylinder;authored.rootRawFilterInfo=3;Refuse([&]{AdmitSelectedSupportGameplayBody(authored,Policy::NativeSolidEntityCylinderEnvelopeV1);},"Solid entity overrode nonzero root source word");
    authored=cylinder;authored.shapes[0].filter.inheritedRawContents=3;Refuse([&]{AdmitSelectedSupportGameplayBody(authored,Policy::NativeSolidEntityCylinderEnvelopeV1);},"Solid entity overrode nonzero child source word");
    authored=cylinder;authored.selectedBodyName="maps/authored/cylinder";Refuse([&]{AdmitSelectedSupportGameplayBody(authored,Policy::NativeSolidEntityCylinderEnvelopeV1);},"Solid entity policy applied to world");
    authored=cylinder;authored.shapes[0].cylinder.profile=HavokCylinderGeometryProfile::Unresolved;Refuse([&]{AdmitSelectedSupportGameplayBody(authored,Policy::NativeSolidEntityCylinderEnvelopeV1);},"Unqualified cylinder envelope admitted");
    const auto defaultDecoded=DecodeSelectedSupportGameplayBody(bytes,"models/authored/cylinder",Policy::DeclaredDoom3BitsV1,true);
    Check(!defaultDecoded.selectedGeometryComplete&&defaultDecoded.shapes.empty(),"Default decoder silently enabled virtual envelope");
    IncludeAll filter;SupportCollisionIndex ordinary(cylinder);SupportCollisionIndexLimits opt;opt.nativeCylinderEnvelopeResponse=true;SupportCollisionIndex explicitResponse(cylinder,opt);
    RoundedConvexShape actor;actor.corePoints={{0,0,0}};actor.radius=.25;const CollisionRigidTransform pose;
    const auto strictHit=ordinary.Overlap(actor,pose,filter),explicitHit=explicitResponse.Overlap(actor,pose,filter);
    Check(strictHit.status==SupportCollisionQueryStatus::InitialOverlap&&explicitHit.status==strictHit.status&&strictHit.hasCandidate&&explicitHit.hasCandidate,"Authored cylinder physical overlap must be certified");
    Check(!strictHit.responseMetadataQualified&&explicitHit.responseMetadataQualified&&explicitHit.sourceKind==SupportCollisionKind::Cylinder&&
        !explicitHit.cylinder.matchesNativeSupportSelection&&!explicitHit.sceneClearanceVerified,"Index envelope response requires explicit policy and retains source distinction");
    Check(Audit(cylinder)==cylinderBefore,"Response/index construction mutated cylinder source data");
}
struct OriginalData {Bytes hkxs,ents;std::string world;};
OriginalData ReadOriginal(const std::string& path){std::ifstream input(path.c_str(),std::ios::binary);Need(bool(input),"Owner-supplied original MPK missing");
    const auto archive=Archive::Scan(input);OriginalData out;unsigned h=0,e=0;
    for(std::size_t c=0;c<archive.Chunks().size();++c){const auto& chunk=archive.Chunks()[c];for(std::size_t n=0;n<chunk.entries.size();++n){
        if(chunk.type=="HKXS"){++h;out.hkxs=archive.ReadEntry(input,c,n);out.world=chunk.entries[n].name;}
        if(chunk.type=="ENTS"){++e;out.ents=archive.ReadEntry(input,c,n);}}}
    Need(h==1&&e==1,"Original collision test expects exact single HKXS/ENTS");return out;
}
void Original(const std::string& path){
    const auto input=ReadOriginal(path);Need(input.world=="maps/game/trainyard/trainyard","Original collision-binding case expects trainyard");
    const auto defaults=DecodeSelectedSupportGameplayBody(input.hkxs,input.world,Policy::DeclaredDoom3BitsV1);
    Check(defaults.shapes.size()==29650&&!defaults.selectedGeometryComplete,"Default world retains162 unresolved cylinders");
    Refuse([&]{AdmitSelectedSupportGameplayBody(defaults,Policy::DeclaredDoom3BitsV1);},"Default strict trainyard admission relaxed");
    const auto world=DecodeSelectedSupportGameplayBody(input.hkxs,input.world,Policy::NativeSourceWordsCylinderEnvelopeV1);
    Check(world.shapes.size()==29812&&world.selectedGeometryComplete&&world.selectedHierarchyComplete&&world.selectedChildFiltersComplete&&world.selectedContactMetadataComplete,"Explicit trainyard geometry/metadata count or completeness differs");
    Check(world.rootRawFilterKnown&&world.rootRawFilterInfo==34029631u&&world.otherSourceRigidBodies==534&&!world.selectedContentsComplete&&
        !world.selectedCoverageComplete&&!world.allSourceRigidBodiesSelected&&!world.nativeRunnable&&!world.entityBindingsResolved,"Opt-in loader rewrote source coverage/role/filter qualification");
    std::map<SupportCollisionKind,std::size_t> kinds;unsigned unmappedTriangles=0,cylinders=0;
    for(const auto& shape:world.shapes){++kinds[shape.kind];if(shape.kind==SupportCollisionKind::Triangle){++unmappedTriangles;
            Check(shape.filter.contentsProvenance==SupportCollisionContentsProvenance::TriangleOccurrenceValueUnmapped&&shape.filter.childFilterKnown&&
                shape.filter.childFilterStatus==HavokTriangleQueryStatus::Resolved&&shape.triangle.contactMetadataQualified,"Original triangle raw child/contact metadata lost");}
        if(shape.kind==SupportCollisionKind::Cylinder){++cylinders;Check(shape.corePoints.size()==32&&shape.cylinder.profile==HavokCylinderGeometryProfile::RetailDefault16VirtualEnvelope&&
            !shape.cylinder.matchesNativeSupportSelection,"Original cylinder source/profile distinction lost");}}
    Check(kinds[SupportCollisionKind::Box]==9386&&kinds[SupportCollisionKind::ConvexVertices]==5960&&unmappedTriangles==14238&&
        kinds[SupportCollisionKind::Sphere]==34&&kinds[SupportCollisionKind::Capsule]==32&&cylinders==162,"Original trainyard primitive partition differs");
    const auto before=Audit(world);const auto responses=AdmitSelectedSupportGameplayBody(world,Policy::NativeSourceWordsCylinderEnvelopeV1);
    Check(responses.size()==world.shapes.size()&&Audit(world)==before,"World response admission changed raw source facts");
    Refuse([&]{AdmitSelectedSupportGameplayBody(world,Policy::DeclaredDoom3BitsV1);},"Explicit envelope accidentally made strict world runnable");
    const auto wagon=DecodeSelectedSupportGameplayBody(input.hkxs,"models/vehicles/box_car_a",Policy::NativeSolidEntityCylinderEnvelopeV1,true);
    Check(wagon.shapes.size()==86&&wagon.rootRawFilterKnown&&wagon.rootRawFilterInfo==0&&wagon.sourceBodyOffset==4958752&&wagon.rootShapeOffset==4959296,
        "Original wagon source geometry/identity differs");
    std::map<SupportCollisionKind,std::size_t> wagonKinds;for(const auto& shape:wagon.shapes){++wagonKinds[shape.kind];Check(shape.filter.inheritedRawContents==0&&!shape.filter.inheritedContentsKnown,"Wagon raw zero filter was changed");}
    Check(wagonKinds[SupportCollisionKind::Box]==66&&wagonKinds[SupportCollisionKind::ConvexVertices]==8&&wagonKinds[SupportCollisionKind::Cylinder]==12,"Original wagon primitive partition differs");
    const auto wagonBefore=Audit(wagon);const auto wagonResponse=AdmitSelectedSupportGameplayBody(wagon,Policy::NativeSolidEntityCylinderEnvelopeV1);
    Check(wagonResponse.size()==86&&std::all_of(wagonResponse.begin(),wagonResponse.end(),[](const SupportGameplayResponse& r){return r.contents==3;}),"Explicit native solid wagon response differs");
    Check(Audit(wagon)==wagonBefore&&!wagon.selectedCoverageComplete&&!wagon.nativeRunnable&&!wagon.entityBindingsResolved,"Solid entity policy rewrote raw source/runnable facts");
    Refuse([&]{AdmitSelectedSupportGameplayBody(wagon,Policy::DeclaredDoom3BitsV1);},"Strict admission accepted explicit solid wagon");
    Refuse([&]{AdmitSelectedSupportGameplayBody(wagon,Policy::NativeSourceWordsCylinderEnvelopeV1);},"Wagon template enabled without entity override");
    const auto map=PrepareSupportGameplayMap(input.hkxs,input.ents,"wolf2009/source_map/PROC/maps/game/trainyard/trainyard",41,"",Policy::NativeSourceWordsCylinderEnvelopeV1);
    Check(map.bodies&&map.bodies->inventory.bodies.size()==535&&map.bodies->entityBodies==227&&map.bodies->resourceNameBodies==307&&map.responses.size()==29812,
        "Owning world/body map plan did not retain original partition");
    Check(map.sourceEntities==2238&&map.unimplementedEntities>0&&map.nativeCylinderEnvelopeResponse&&!map.world.nativeRunnable&&
        map.mapText.find("brushDef")==std::string::npos&&map.cmText.find("vertices { 0 }")!=std::string::npos,"Map plan hid missing entities or created native brush geometry");
    Refuse([&]{PrepareSupportGameplayMap(input.hkxs,input.ents,"wolf2009/source_map/PROC/maps/game/trainyard/trainyard",41);},"Default map path silently opted into source words/envelopes");
    SourceCollisionBodyBinding binding;binding.request.policy=SourceCollisionBodyPolicy::NativeSolidEntityCylinderEnvelopeV1;
    binding.request.resourcePath="wolf2009/source_map/hkxs/maps/game/trainyard/trainyard";binding.request.bodyName=wagon.selectedBodyName;
    binding.epoch=41;binding.bytes=wagon.sourceBytes;binding.fingerprint=wagon.sourceFingerprint;binding.shapes=wagon.shapes.size();binding.corePoints=wagon.totalCorePoints;binding.nativeContents=3;
    const auto sourceModel=SourceCollisionBodyModelName(binding);const auto bound=ParseSourceCollisionBodyModelName(sourceModel);
    Check(bound.request.bodyName==wagon.selectedBodyName&&bound.bytes==input.hkxs.size()&&bound.fingerprint==world.sourceFingerprint&&bound.shapes==86&&bound.corePoints==wagon.totalCorePoints,
        "Original body wire binding lost immutable source identity");
    std::cout<<"SOURCE_COLLISION_BINDING_ORIGINAL_OK checks="<<checks<<" refusals="<<refusals<<" world_shapes="<<world.shapes.size()<<" world_cylinders="<<cylinders
        <<" world_triangles="<<unmappedTriangles<<" wagon_shapes="<<wagon.shapes.size()<<" wagon_cylinders="<<wagonKinds[SupportCollisionKind::Cylinder]
        <<" raw_source_unchanged=1 native_runtime_tested=0 retail_filter_equivalence=0\n";
}
}
int main(int argc,char** argv){try{
    if(argc==1||(argc==2&&std::string(argv[1])=="--self-test")){Wire();ResponseProfiles();std::cout<<"SOURCE_COLLISION_BINDING_SYNTHETIC_OK checks="<<checks<<" refusals="<<refusals<<'\n';return 0;}
    if(argc==3&&std::string(argv[1])=="--source-map"){Original(argv[2]);return 0;}
    throw std::runtime_error("usage: wolf2009-source-collision-binding [--self-test | --source-map OWNER_TRAINYARD_MPK]");
}catch(const std::exception& e){std::cerr<<"SOURCE_COLLISION_BINDING_FAIL checks="<<checks<<" refusals="<<refusals<<" reason="<<e.what()<<'\n';return 1;}}
