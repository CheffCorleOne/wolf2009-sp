// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerProfile.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
namespace wolf2009 { namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string Fold(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
const SourceResolvedLiteral& Field(const ResolvedSourceDefinition& d,const char* name){const auto* f=d.Find(name);Need(f!=NULL,"Required source player field is absent");return *f;}
std::string Text(const SourceResolvedLiteral& f){
    Need((f.encoding==SourceLiteralEncoding::CompiledString||f.encoding==SourceLiteralEncoding::PlainText)&&f.value.size()==1,"Source player text has an unqualified encoding");
    Need(f.value[0].text.size()<=4096,"Source player text exceeds limit");return f.value[0].text;
}
std::string Text(const ResolvedSourceDefinition& d,const char* name){return Text(Field(d,name));}
std::int32_t Signed(std::uint32_t u){return u<=0x7fffffffu?std::int32_t(u):-1-std::int32_t(0xffffffffu-u);}
double Number(const SourceResolvedLiteral& f){
    if(f.encoding==SourceLiteralEncoding::CompiledSignedInteger){Need(f.numericWords.size()==1,"Source player integer word count differs");return Signed(f.numericWords[0]);}
    if(f.encoding==SourceLiteralEncoding::CompiledFloat32){Need(f.numericWords.size()==1,"Source player float word count differs");float v;std::memcpy(&v,&f.numericWords[0],4);Need(std::isfinite(v),"Nonfinite source player float");return v;}
    Need(f.encoding==SourceLiteralEncoding::PlainText&&f.value.size()==1,"Source player number has an unqualified encoding");
    std::istringstream in(f.value[0].text);in.imbue(std::locale::classic());double v;Need(bool(in>>v)&&std::isfinite(v),"Malformed source player number");in>>std::ws;Need(in.eof(),"Trailing source player numeric text");return v;
}
int Integer(const SourceResolvedLiteral& f,int low,int high){const double v=Number(f);Need(v>=low&&v<=high&&v==std::floor(v),"Source player integer exceeds range or has a fraction");return int(v);}
int Integer(const ResolvedSourceDefinition& d,const char* name,int low,int high){return Integer(Field(d,name),low,high);}
float Scalar(const ResolvedSourceDefinition& d,const char* name,double low,double high){const double v=Number(Field(d,name));Need(v>=low&&v<=high,"Source player scalar exceeds range");return float(v);}
bool Starts(const std::string& s,const char* p){return s.compare(0,std::char_traits<char>::length(p),p)==0;}
unsigned Slot(const std::array<std::string,12>& slots,const std::string& name){
    const auto folded=Fold(name);for(unsigned i=0;i<slots.size();++i)if(Fold(slots[i])==folded)return i;
    throw std::runtime_error("Source loadout references a weapon outside the selected player slot table");
}
std::vector<unsigned> DefaultSlots(const SourceResolvedLiteral& f){
    std::vector<unsigned> out;std::set<unsigned> seen;
    const auto add=[&](double v){Need(std::isfinite(v)&&v>=0&&v<12&&v==std::floor(v),"Source initial weapon slot is invalid");Need(seen.insert(unsigned(v)).second,"Duplicate source initial weapon slot");out.push_back(unsigned(v));};
    if(f.encoding==SourceLiteralEncoding::CompiledFloatArray||f.encoding==SourceLiteralEncoding::CompiledFloat32){Need(!f.numericWords.empty()&&f.numericWords.size()<=12,"Source initial slot count exceeds limit");for(auto u:f.numericWords){float v;std::memcpy(&v,&u,4);add(v);}}
    else if(f.encoding==SourceLiteralEncoding::CompiledIntegerArray){Need(!f.numericWords.empty()&&f.numericWords.size()<=12,"Source initial slot count exceeds limit");for(auto u:f.numericWords)add(Signed(u));}
    else {const auto text=Text(f);std::istringstream in(text);in.imbue(std::locale::classic());double v;while(in>>v){Need(out.size()<12,"Source initial slot count exceeds limit");add(v);}Need(in.eof()&&!out.empty(),"Malformed source initial weapon list");}
    return out;
}
std::uint64_t Hash(const std::vector<unsigned char>& b){std::uint64_t h=UINT64_C(14695981039346656037);for(auto c:b){h^=c;h*=UINT64_C(1099511628211);}return h;}
}
struct SourcePlayerProfile::Impl {
    SourcePlayerProfileRequest request;ResolvedSourceDefinition player,startup,library;
    ResolvedSourceModelAnimations model;SourcePlayerNativeControls controls;
    std::array<std::string,12> slots;std::vector<unsigned> defaults,starting;
    std::vector<std::string> unsupportedPlayer,unsupportedStartup;
    unsigned selected=0,defaultSelected=0;int health=0,maxHealth=0,gold=0;
    float mass=0,capsule=0,keepDistance=0,extraGravity=0;std::string stand,crouch;
};
SourcePlayerProfile::SourcePlayerProfile(std::unique_ptr<Impl> p):impl(std::move(p)){}
SourcePlayerProfile::~SourcePlayerProfile(){}
SourcePlayerPolicy SourcePlayerProfile::Policy()const{return impl->request.policy;}
const SourcePlayerProfileRequest& SourcePlayerProfile::Request()const{return impl->request;}
const ResolvedSourceDefinition& SourcePlayerProfile::Player()const{return impl->player;}
const ResolvedSourceDefinition& SourcePlayerProfile::Startup()const{return impl->startup;}
const ResolvedSourceDefinition& SourcePlayerProfile::StartupLibrary()const{return impl->library;}
const ResolvedSourceModelAnimations& SourcePlayerProfile::SourceModel()const{return impl->model;}
const SourcePlayerNativeControls& SourcePlayerProfile::Controls()const{return impl->controls;}
const std::array<std::string,12>& SourcePlayerProfile::WeaponSlots()const{return impl->slots;}
const std::vector<unsigned>& SourcePlayerProfile::DefaultWeaponSlots()const{return impl->defaults;}
const std::vector<unsigned>& SourcePlayerProfile::StartingWeaponSlots()const{return impl->starting;}
unsigned SourcePlayerProfile::StartingWeaponSlot()const{return impl->selected;}
unsigned SourcePlayerProfile::DefaultWeaponSlot()const{return impl->defaultSelected;}
int SourcePlayerProfile::Health()const{return impl->health;}
int SourcePlayerProfile::MaximumHealth()const{return impl->maxHealth;}
int SourcePlayerProfile::StartingGold()const{return impl->gold;}
float SourcePlayerProfile::Mass()const{return impl->mass;}
float SourcePlayerProfile::CapsuleFallbackRadius()const{return impl->capsule;}
float SourcePlayerProfile::KeepDistance()const{return impl->keepDistance;}
float SourcePlayerProfile::ExtraGravity()const{return impl->extraGravity;}
const std::string& SourcePlayerProfile::StandClipReference()const{return impl->stand;}
const std::string& SourcePlayerProfile::CrouchClipReference()const{return impl->crouch;}
const std::vector<std::string>& SourcePlayerProfile::UnsupportedPlayerFeatures()const{return impl->unsupportedPlayer;}
const std::vector<std::string>& SourcePlayerProfile::UnsupportedStartupFields()const{return impl->unsupportedStartup;}
std::shared_ptr<const SourcePlayerProfile> PrepareSourcePlayerProfile(const SourceDefinitions& definitions,const SourcePlayerProfileRequest& request){
    Need(request.policy==SourcePlayerPolicy::NativeIdPlayerFirstPersonV1,"Explicit source player policy required");
    for(const auto* name:{&request.playerDefinition,&request.sourceMapName,&request.startupDeclaration,&request.startupLibrary,&request.startupLibraryKey})Need(!name->empty()&&name->size()<=4096,"Explicit bounded source player/map/loadout identities required");
    std::unique_ptr<SourcePlayerProfile::Impl> p(new SourcePlayerProfile::Impl);p->request=request;
    p->player=definitions.Resolve(SourceDefinitionKind::EntityDef,request.playerDefinition);
    Need(Fold(Text(p->player,"spawnclass"))=="idplayer","Selected source class is not idPlayer");
    Need(Integer(p->player,"useClipModel",0,1)==1,"Source player requires declared clip models");
    Need(Integer(p->player,"noDefaultPhysics",0,1)==1,"Source player default-physics policy differs");
    p->health=Integer(p->player,"health",1,100000);p->maxHealth=Integer(p->player,"maxhealth",1,100000);Need(p->health<=p->maxHealth,"Source starting health exceeds maximum");
    p->mass=Scalar(p->player,"mass",.001,100000);p->capsule=Scalar(p->player,"capsuleradius",0,10000);
    p->keepDistance=Scalar(p->player,"keepdistance",0,1000);p->extraGravity=Scalar(p->player,"extraGravity",0,10000);
    p->stand=Text(p->player,"clipmodel_stand");p->crouch=Text(p->player,"clipmodel_crouch");Need(p->stand!=p->crouch,"Source stand/crouch shape names must differ");
    p->model=definitions.ResolveModelAnimations(Text(p->player,"model"));
    std::set<std::string> names;for(unsigned i=0;i<p->slots.size();++i){p->slots[i]=Text(p->player,("def_weapon"+std::to_string(i)).c_str());Need(!p->slots[i].empty()&&names.insert(Fold(p->slots[i])).second,"Source weapon slot table is empty or ambiguous");}
    p->defaults=DefaultSlots(Field(p->player,"initial_weapons"));p->defaultSelected=unsigned(Integer(p->player,"current_weapon",0,11));
    p->library=definitions.Resolve(SourceDefinitionKind::EntityDef,request.startupLibrary);
    Need(Fold(Text(p->library,request.startupLibraryKey.c_str()))==Fold(request.startupDeclaration),"Explicit source map/library/loadout selection differs");
    p->startup=definitions.Resolve(SourceDefinitionKind::EntityDef,request.startupDeclaration);
    p->selected=Slot(p->slots,Text(p->startup,"startWeapon"));p->gold=Integer(p->startup,"startGold",0,100000000);
    std::set<unsigned> starting;for(const auto& kv:p->startup.literalFields){const auto key=Fold(kv.first);
        if(Starts(key,"giveweapon")&&!Starts(key,"giveweaponupgrade")){const auto slot=Slot(p->slots,Text(kv.second));if(starting.insert(slot).second)p->starting.push_back(slot);}
        else if(key=="classname")Need(Fold(Text(kv.second))==Fold(request.startupDeclaration),"Source startup classname differs from selected declaration");
        else if(key!="startweapon"&&key!="startgold"&&key!="editor_ignore")p->unsupportedStartup.push_back(kv.first);
    }
    for(const auto& fact:p->startup.opaqueFacts)p->unsupportedStartup.push_back(fact.data.key.text);
    Need(starting.count(p->selected)!=0,"Selected source starting weapon is not granted by the map profile");
    p->unsupportedPlayer={"source body/AF/phantom and viewcomponent","retail acceleration, sprint, lean and keepDistance response","health regeneration and Veil/powers","retail inventory persistence and saves"};
    return std::shared_ptr<const SourcePlayerProfile>(new SourcePlayerProfile(std::move(p)));
}
SourcePlayerClip DecodeSourcePlayerClip(const std::vector<unsigned char>& bytes,const std::string& exactName){
    Need(!exactName.empty()&&exactName.size()<=4096,"Explicit source player body name required");
    detail::HavokPack p(bytes,1u<<20,4096,32768);SourcePlayerClip out;std::size_t matches=0;
    for(const auto& object:p.objects)if(object.second=="hkpRigidBody"){
        const auto body=object.first;p.DataRange(body,512);const auto it=p.pointers.find(body+112);
        if(it!=p.pointers.end()&&p.DataString(it->second)==exactName){out.bodyOffset=body;++matches;}
    }
    Need(matches==1,"Source player exact named body missing or ambiguous");out.selectedBodyName=exactName;
    out.shapeOffset=p.Pointer(out.bodyOffset+16);out.sourceType=p.Type(out.shapeOffset);Need(out.sourceType=="hkpConvexVerticesShape","Source player shape is not a direct convex vertices shape");
    const auto shape=out.shapeOffset;p.DataRange(shape,96);out.rawRadiusBits=p.DU(shape+16);Need(p.F(shape+16)==0,"Rounded source player hull is unsupported by the native zero-radius profile");
    out.rawFilter=p.DU(out.bodyOffset+44);out.supportBlocks=p.DU(shape+68);out.declaredVertices=p.DU(shape+76);const auto planes=p.DU(shape+84);
    Need(out.declaredVertices>=4&&out.declaredVertices<=64&&out.supportBlocks==(out.declaredVertices+3)/4,"Source player support block/count limit differs");
    Need(planes>=4&&planes<=128,"Source player plane count exceeds limit");const auto vertices=p.Pointer(shape+64),planeArray=p.Pointer(shape+80);
    p.ArrayRange(vertices,out.supportBlocks,48);p.ArrayRange(planeArray,planes,16);
    for(unsigned i=0;i<out.supportBlocks*4;++i){const auto at=vertices+(i/4)*48+(i%4)*4;std::array<std::uint32_t,3> raw{{p.DU(at),p.DU(at+16),p.DU(at+32)}};out.rawSupportWords.push_back(raw);
        const std::array<float,3> point{{float(p.F(at)/.0254),float(p.F(at+16)/.0254),float(p.F(at+32)/.0254)}};
        for(float x:point)Need(std::isfinite(x)&&std::abs(x)<=10000,"Source player vertex exceeds finite coordinate limit");
        if(i<out.declaredVertices)out.nativeLocalPoints.push_back(point);
        else Need(std::find(out.rawSupportWords.begin(),out.rawSupportWords.begin()+out.declaredVertices,raw)!=out.rawSupportWords.begin()+out.declaredVertices,"Padded source support is not a duplicate declared support");
    }
    for(unsigned i=0;i<planes;++i){const auto at=planeArray+i*16;out.rawPlaneWords.push_back({{p.DU(at),p.DU(at+4),p.DU(at+8),p.DU(at+12)}});
        const double x=p.F(at),y=p.F(at+4),z=p.F(at+8),d=p.F(at+12);Need(std::abs(x*x+y*y+z*z-1)<=.0001,"Source player plane is not unit length");
        for(const auto& point:out.nativeLocalPoints)Need(x*point[0]+y*point[1]+z*point[2]+d/.0254<=.001,"Source player support contradicts its source plane");
    }
    for(unsigned i=0;i<16;++i)out.rawBodyTransform[i]=p.DU(out.bodyOffset+224+i*4);
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)Need(std::abs(p.F(out.bodyOffset+224+row*16+col*4)-(row==col?1:0))<=.000002,"Nonidentity source player asset-body basis unsupported");
    for(unsigned i=0;i<3;++i)Need(std::abs(p.F(out.bodyOffset+272+i*4)/.0254)<=.001,"Source player asset-body origin exceeds explicit local-shape policy");
    Need(std::abs(p.F(out.bodyOffset+284)-1)<=.000002,"Source player asset-body translation padding differs");
    out.sourceBytes=bytes.size();out.sourceFingerprint=Hash(bytes);out.source=bytes;return out;
}
}
