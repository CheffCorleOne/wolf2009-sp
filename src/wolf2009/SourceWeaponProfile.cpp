// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceWeaponProfile.h"
#include "CompiledDeclarationCatalog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::int32_t Signed(std::uint32_t x){return x<=UINT32_C(0x7fffffff)?std::int32_t(x):-1-std::int32_t(UINT32_C(0xffffffff)-x);}
const SourceResolvedLiteral& Field(const ResolvedSourceDefinition& definition,const char* key){
    const auto* value=definition.Find(key);if(!value)throw std::runtime_error(std::string("Required source weapon field is absent: ")+key);return *value;
}
std::string Text(const ResolvedSourceDefinition& definition,const char* key){
    const auto& value=Field(definition,key);Need(value.encoding==SourceLiteralEncoding::CompiledString&&value.value.size()==1,
        "Source weapon text must be a typed compiled string");Need(value.value[0].text.size()<=4096,"Source weapon text is excessive");return value.value[0].text;
}
int Integer(const ResolvedSourceDefinition& definition,const char* key){
    const auto& value=Field(definition,key);Need(value.encoding==SourceLiteralEncoding::CompiledSignedInteger&&value.numericWords.size()==1,
        "Source weapon integer is not a typed signed integer");return Signed(value.numericWords[0]);
}
float Number(const ResolvedSourceDefinition& definition,const char* key){
    const auto& value=Field(definition,key);
    if(value.encoding==SourceLiteralEncoding::CompiledSignedInteger&&value.numericWords.size()==1){const int n=Signed(value.numericWords[0]);
        Need(n>=-16777216&&n<=16777216,"Source weapon integer is not exactly representable as a native float");return float(n);}
    Need(value.encoding==SourceLiteralEncoding::CompiledFloat32&&value.numericWords.size()==1,"Source weapon number is not a typed scalar");
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"IEEE754 binary32 is required");
    float n=0;std::memcpy(&n,&value.numericWords[0],4);Need(std::isfinite(n),"Nonfinite source weapon number");return n;
}
void Compiled(const ResolvedSourceDefinition& definition){
    Need(!definition.declarations.empty()&&definition.declarations.size()<=64,"Source weapon declaration closure is absent/excessive");
    for(const auto& origin:definition.declarations)Need(origin.member.compiledRecord&&origin.member.compiledRecord->IsUnmodified(),
        "Source weapon profile requires unchanged compiled declaration provenance");
    Need(definition.literalFields.size()<=4096,"Source weapon effective-field budget exceeded");
}
bool Identifier(const std::string& value){
    if(value.empty()||value.size()>127)return false;
    for(std::size_t i=0;i<value.size();++i){const unsigned char c=value[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i&&c>='0'&&c<='9')))return false;}return true;
}
float Decimal(const SourceDefinitionToken& token){
    Need(!token.brace&&!token.quoted&&token.text.size()<=64,"Source weapon command number is not a bounded decimal token");
    std::istringstream input(token.text);input.imbue(std::locale::classic());double value=0;char extra=0;
    Need(bool(input>>value)&&!(input>>extra)&&std::isfinite(value)&&std::fabs(value)<=100000,
        "Source weapon command decimal is invalid/excessive");
    const float result=float(value);Need(std::isfinite(result),"Source weapon command float overflows");return result;
}
unsigned Frame(const SourceDefinitionToken& token){
    Need(!token.brace&&!token.quoted&&!token.text.empty()&&token.text.size()<=5,"Source weapon command frame is invalid");
    unsigned result=0;for(char c:token.text){Need(c>='0'&&c<='9',"Source weapon frame must be an unsigned integer");result=result*10+unsigned(c-'0');}
    Need(result>0&&result<=65535,"Source weapon command frame is out of bounds");return result;
}
std::vector<SourceWeaponCommand> Commands(const ResolvedSourceModelAnimations& model,const char* logical,bool fire){
    const auto* reference=model.Find(logical);Need(reference&&reference->logicalName.text==logical&&reference->hasLiteralAssetReference,
        "Required exact source weapon animation is absent/unqualified");
    Need(reference->shape==SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock&&!reference->origins.empty(),
        "Source weapon command animation must retain its authored block");
    const auto& tokens=reference->origins[0].data.values;
    Need(tokens.size()>=4&&tokens[0].text==logical&&tokens[1].text==reference->assetReference.text&&tokens[2].brace=='{'&&tokens.back().brace=='}',
        "Source weapon command block structure is inconsistent");
    std::vector<SourceWeaponCommand> commands;
    for(std::size_t at=3;at+1<tokens.size();){
        Need(commands.size()<64&&at+2<tokens.size()-1&&tokens[at].text=="frame"&&!tokens[at].brace,
            "Unimplemented source weapon animation option/command");
        SourceWeaponCommand command;command.frame=Frame(tokens[++at]);const auto& name=tokens[++at];++at;
        Need(!name.brace&&!name.quoted,"Source weapon command name is malformed");
        if(name.text=="weapon_attack")command.kind=SourceWeaponCommandKind::WeaponAttack;
        else if(name.text=="point_of_interest"){
            command.kind=SourceWeaponCommandKind::PointOfInterest;Need(at+1<tokens.size()-1,"Truncated point_of_interest");
            Need(Identifier(tokens[at].text)&&!tokens[at].brace,"Invalid source point-of-interest joint");command.argument=tokens[at++].text;
            command.values.push_back(Decimal(tokens[at++]));Need(command.values[0]>=0&&command.values[0]<=1,"Unqualified point-of-interest weight");
        }else if(name.text=="sound_weapon"){
            command.kind=SourceWeaponCommandKind::SoundWeapon;Need(at<tokens.size()-1&&Identifier(tokens[at].text)&&!tokens[at].brace,
                "Source weapon sound key is absent/unsafe");command.argument=tokens[at++].text;Need(command.argument.compare(0,4,"snd_")==0,"Source weapon sound requires an exact snd_ key");
        }else if(name.text=="eye_angles"){
            command.kind=SourceWeaponCommandKind::EyeAngles;Need(at+4<tokens.size()-1,"Truncated eye_angles command");
            for(unsigned i=0;i<5;++i)command.values.push_back(Decimal(tokens[at++]));
            Need(std::fabs(command.values[0])<=89&&std::fabs(command.values[1])<=89&&std::fabs(command.values[2])<=89&&
                command.values[3]>=0&&command.values[3]<=10000&&command.values[4]>=0&&command.values[4]<=1,
                "Source weapon eye_angles exceeds explicit native visual policy");
        }else throw std::runtime_error("Unimplemented source weapon frame command");
        Need(!fire||command.kind==SourceWeaponCommandKind::WeaponAttack,"Primary source fire admits weapon_attack only");
        Need(fire||command.kind!=SourceWeaponCommandKind::WeaponAttack,"Reload cannot carry weapon_attack");
        command.origins=reference->origins;commands.push_back(std::move(command));
    }
    Need(!commands.empty(),"Source weapon animation command list is empty");
    if(fire)Need(commands.size()==1&&commands[0].frame==1,"Native primary source fire requires exactly one frame1 weapon_attack");
    // Dispatch order is source frame then source textual order for ties.
    std::stable_sort(commands.begin(),commands.end(),[](const SourceWeaponCommand& a,const SourceWeaponCommand& b){return a.frame<b.frame;});return commands;
}
int Interval(float seconds){
    const double value=double(seconds)*1000;Need(std::isfinite(value)&&value>=1&&value<=10000,"Source autofire interval is invalid/excessive");
    const double base=std::floor(value),fraction=value-base;
    // Explicit nearest-even conversion, matching the measured x87 FISTP
    // interval for ordinary rounding mode without changing the host's FP mode.
    const int result=int(base)+(fraction>0.5||(fraction==0.5&&std::fmod(base,2.0)!=0)?1:0);
    Need(result>0,"Source autofire interval rounded to zero");return result;
}
void Timing(const SourceWeaponProfile& profile,const SourceWeaponTiming& timing){
    Need(timing.policy==SourceWeaponClockPolicy::NativeSimulationMillisecondsV1,"Explicit native weapon clock policy required");
    if(profile.Policy()==SourceWeaponPolicy::NoneV1)return;
    Need(timing.fireFrames>=2&&timing.fireFrames<=65535&&timing.reloadFrames>=2&&timing.reloadFrames<=65535&&
         std::isfinite(timing.fireDurationSeconds)&&timing.fireDurationSeconds>0&&timing.fireDurationSeconds<=60&&
         std::isfinite(timing.reloadDurationSeconds)&&timing.reloadDurationSeconds>0&&timing.reloadDurationSeconds<=60,
         "Source weapon native timing metadata is invalid/excessive");
    for(const auto& command:profile.FireCommands())Need(command.frame<=timing.fireFrames,"Fire frame command exceeds source frame count");
    for(const auto& command:profile.ReloadCommands())Need(command.frame<=timing.reloadFrames,"Reload frame command exceeds source frame count");
}
}
struct SourceWeaponProfile::Impl {
    SourceWeaponPolicy policy=SourceWeaponPolicy::Unspecified;
    SourceWeaponConfiguration configuration;
    ResolvedSourceDefinition weapon,ammo,attack,damage;
    ResolvedSourceModelAnimations view,world;
    std::vector<ResolvedSourceDefinition> effects;
    std::vector<SourceWeaponCommand> fire,reload;
};
SourceWeaponProfile::SourceWeaponProfile(std::unique_ptr<Impl> data):impl_(std::move(data)){}
SourceWeaponProfile::~SourceWeaponProfile(){}
SourceWeaponPolicy SourceWeaponProfile::Policy()const{return impl_->policy;}
const SourceWeaponConfiguration& SourceWeaponProfile::Configuration()const{return impl_->configuration;}
const ResolvedSourceDefinition& SourceWeaponProfile::Weapon()const{return impl_->weapon;}
const ResolvedSourceDefinition& SourceWeaponProfile::Ammo()const{return impl_->ammo;}
const ResolvedSourceDefinition& SourceWeaponProfile::Attack()const{return impl_->attack;}
const ResolvedSourceDefinition& SourceWeaponProfile::Damage()const{return impl_->damage;}
const ResolvedSourceModelAnimations& SourceWeaponProfile::ViewModel()const{return impl_->view;}
const ResolvedSourceModelAnimations& SourceWeaponProfile::WorldModel()const{return impl_->world;}
const std::vector<ResolvedSourceDefinition>& SourceWeaponProfile::EffectDefinitions()const{return impl_->effects;}
const std::vector<SourceWeaponCommand>& SourceWeaponProfile::FireCommands()const{return impl_->fire;}
const std::vector<SourceWeaponCommand>& SourceWeaponProfile::ReloadCommands()const{return impl_->reload;}

std::shared_ptr<const SourceWeaponProfile> PrepareSourceWeaponProfile(const SourceDefinitions& definitions,const std::string& name,
    SourceWeaponPolicy policy,const SourceDefinitionContext& context){
    Need(policy==SourceWeaponPolicy::NoneV1||policy==SourceWeaponPolicy::MP40PrimaryNativeV1,"Explicit supported source weapon profile required");
    Need(name==(policy==SourceWeaponPolicy::NoneV1?"weapon_none":"weapon_mp40"),"Source weapon name does not match selected class policy");
    std::unique_ptr<SourceWeaponProfile::Impl> data(new SourceWeaponProfile::Impl);data->policy=policy;
    auto resolve=[&](const std::string& key){auto result=definitions.Resolve(SourceDefinitionKind::EntityDef,key,context);Compiled(result);return result;};
    data->weapon=resolve(name);auto& c=data->configuration;c.name=name;
    if(policy==SourceWeaponPolicy::NoneV1){
        Need(Integer(data->weapon,"HasPrimaryAttack")==0&&Integer(data->weapon,"HasMeleeAttack")==0&&Integer(data->weapon,"HasGrenadeAttack")==0,
            "Source weapon_none has an enabled attack");return std::shared_ptr<const SourceWeaponProfile>(new SourceWeaponProfile(std::move(data)));
    }
    c.weaponclass=Text(data->weapon,"weaponclass");Need(c.weaponclass=="rvWeaponCommonGun","Unimplemented source equipped weaponclass");
    Need(Text(data->weapon,"spawnclass")=="idItem","Unexpected source weapon pickup class");
    c.viewModel=Text(data->weapon,"model_view");c.worldModel=Text(data->weapon,"model_world");c.detailPack=Text(data->weapon,"detailPack");
    c.ammoType=Text(data->weapon,"def_ammoType");Need(!c.ammoType.empty(),"Source MP40 ammo definition is empty");
    c.clipSize=Integer(data->weapon,"clipSize");c.reserveCapacity=Integer(data->weapon,"InvAmmoCapacity");
    c.initialAmmo=Integer(data->weapon,"InvInitialAmmo");c.pickupAmmo=Integer(data->weapon,"inv_ammo_mp40");c.ammoRequired=Integer(data->weapon,"ammoRequired");
    Need(c.clipSize>0&&c.clipSize<=255&&c.reserveCapacity>=c.clipSize&&c.reserveCapacity<=100000&&c.initialAmmo>=0&&
         c.initialAmmo<=c.reserveCapacity&&c.pickupAmmo>=0&&c.pickupAmmo<=c.reserveCapacity&&c.ammoRequired==1,
         "Source weapon ammo configuration is outside the admitted single-bullet profile");
    data->ammo=resolve(c.ammoType);Need(Text(data->ammo,"ammo_type")==c.ammoType,"Weapon/ammo named type mismatch");
    c.attackDefinition=Text(data->weapon,"def_attack0");data->attack=resolve(c.attackDefinition);
    Need(Text(data->attack,"AttackClass")=="SoundHitScanAttack"&&Text(data->attack,"TriggerClass")=="Hold",
         "Source primary attack is not the admitted SoundHitScanAttack/Hold class");
    Need(Text(data->attack,"def_ammoType")==c.ammoType&&Integer(data->attack,"num_projectiles")==1,
         "Source primary attack ammo/count differs from the admitted weapon");
    c.maxRange=Number(data->attack,"MaxRange");Need(c.maxRange>0&&c.maxRange<=100000,"Invalid/excessive source hitscan range");
    c.sourceAutoFireSeconds=Number(data->attack,"AutoFireInterval");c.autoFireIntervalMs=Interval(c.sourceAutoFireSeconds);
    c.damageDefinition=Text(data->attack,"def_damage");data->damage=resolve(c.damageDefinition);c.damage=Number(data->damage,"damage");
    Need(c.damage>0&&c.damage<=100000&&std::floor(c.damage)==c.damage,"Source damage requires a positive integral native amount");
    if(data->damage.Find("radius"))Need(Number(data->damage,"radius")==0,"Area damage is unimplemented by the native primary ray profile");
    std::set<std::string> seen;
    for(const auto& field:data->weapon.literalFields)if(field.first.compare(0,10,"def_effect")==0){
        const auto selected=Text(data->weapon,field.first.c_str());if(selected.empty()||!seen.insert(selected).second)continue;
        Need(data->effects.size()<32,"Source weapon effect closure budget exceeded");data->effects.push_back(resolve(selected));
    }
    data->view=definitions.ResolveModelAnimations(c.viewModel,context);data->world=definitions.ResolveModelAnimations(c.worldModel,context);
    Compiled(data->view.model);Compiled(data->world.model);
    const auto* idle=data->view.Find("idle");Need(idle&&idle->logicalName.text=="idle"&&idle->hasLiteralAssetReference&&
        idle->shape==SourceAnimationReferenceShape::SingleLiteralNoTail,"Source weapon idle has unimplemented commands/options");
    data->fire=Commands(data->view,"fire",true);data->reload=Commands(data->view,"reload",false);
    // Other animation references remain complete in ViewModel, including
    // alternate shots, rates, melee/grenade/aim and movement commands. They are
    // not selectable through this primary idle/fire/reload profile.
    return std::shared_ptr<const SourceWeaponProfile>(new SourceWeaponProfile(std::move(data)));
}

SourceWeaponController::SourceWeaponController(std::shared_ptr<const SourceWeaponProfile> profile,const SourceWeaponTiming& timing,int clip,int reserve):
    profile_(std::move(profile)),timing_(timing){
    Need(bool(profile_),"Source weapon controller has no owning profile");Timing(*profile_,timing_);
    const auto& c=profile_->Configuration();Need(clip>=0&&clip<=c.clipSize&&reserve>=0&&reserve<=c.reserveCapacity,"Native weapon ammo counts are out of bounds");
    state_.clip=clip;state_.reserve=reserve;state_.phase=profile_->Policy()==SourceWeaponPolicy::NoneV1?SourceWeaponPhase::None:SourceWeaponPhase::Ready;
}
const SourceWeaponRuntimeState& SourceWeaponController::State()const{return state_;}
const SourceWeaponProfile& SourceWeaponController::Profile()const{return *profile_;}
std::vector<SourceWeaponEvent> SourceWeaponController::Step(int now,bool held,bool reload,bool enabled){
    Need(now>=0&&now>=state_.lastTimeMs,"Native source weapon clock moved backwards");
    auto state=state_;int animationStart=animationStartMs_;bool isReload=reloadAnimation_,latched=emptyLatched_;std::size_t next=nextCommand_;
    std::vector<SourceWeaponEvent> out;out.reserve(16);
    auto emit=[&](SourceWeaponEventKind kind){SourceWeaponEvent event;event.kind=kind;event.timeMs=now;out.push_back(event);};
    auto commands=[&](){
        if(animationStart<0)return;
        const auto& list=isReload?profile_->ReloadCommands():profile_->FireCommands();
        const unsigned frames=isReload?timing_.reloadFrames:timing_.fireFrames;
        const double duration=(isReload?timing_.reloadDurationSeconds:timing_.fireDurationSeconds)*1000;
        const double elapsed=double(now)-animationStart;
        while(next<list.size()){
            const double when=double(list[next].frame-1)*duration/double(frames-1);if(elapsed<when)break;
            Need(state.frameCommands!=std::numeric_limits<unsigned>::max(),"Source weapon command counter exhausted");
            SourceWeaponEvent event;event.kind=SourceWeaponEventKind::FrameCommand;event.command=next++;event.reloadCommand=isReload;event.timeMs=now;
            out.push_back(event);++state.frameCommands;
        }
    };
    auto increment=[](unsigned& counter){Need(counter!=std::numeric_limits<unsigned>::max(),"Source weapon event counter exhausted");++counter;};
    if(profile_->Policy()==SourceWeaponPolicy::NoneV1){state.lastTimeMs=now;state.attackHeld=held;state_=state;return out;}
    if(!enabled){
        if(state.phase!=SourceWeaponPhase::Disabled)emit(SourceWeaponEventKind::Disabled);
        state.phase=SourceWeaponPhase::Disabled;state.reloadEndTimeMs=-1;animationStart=-1;next=0;latched=false;
    }else{
        if(state.phase==SourceWeaponPhase::Disabled)state.phase=SourceWeaponPhase::Ready;
        commands();
        if(state.phase==SourceWeaponPhase::Reloading&&now>=state.reloadEndTimeMs){
            const int amount=std::min(profile_->Configuration().clipSize-state.clip,state.reserve);
            state.clip+=amount;state.reserve-=amount;state.phase=SourceWeaponPhase::Ready;state.reloadEndTimeMs=-1;animationStart=-1;
            emit(SourceWeaponEventKind::ReloadComplete);
        }
        if(reload&&state.phase==SourceWeaponPhase::Ready&&state.reserve>0&&state.clip<profile_->Configuration().clipSize){
            const double length=std::ceil(timing_.reloadDurationSeconds*1000);Need(length<=double(std::numeric_limits<int>::max()-now),"Native source reload exceeds clock horizon");
            state.phase=SourceWeaponPhase::Reloading;state.reloadEndTimeMs=now+int(length);animationStart=now;isReload=true;next=0;
            increment(state.reloads);emit(SourceWeaponEventKind::StartReload);commands();
        }
        if(!held)latched=false;
        const bool elapsed=state.lastShotTimeMs<0||std::int64_t(now)>std::int64_t(state.lastShotTimeMs)+profile_->Configuration().autoFireIntervalMs;
        if(held&&state.phase==SourceWeaponPhase::Ready&&elapsed){
            if(state.clip>0){
                --state.clip;state.lastShotTimeMs=now;increment(state.shots);animationStart=now;isReload=false;next=0;
                emit(SourceWeaponEventKind::StartFire);commands();
            }else if(!latched){increment(state.emptyClicks);latched=true;emit(SourceWeaponEventKind::EmptyClick);}
        }
    }
    state.lastTimeMs=now;state.attackHeld=held;state_=state;animationStartMs_=animationStart;reloadAnimation_=isReload;emptyLatched_=latched;nextCommand_=next;return out;
}
}
