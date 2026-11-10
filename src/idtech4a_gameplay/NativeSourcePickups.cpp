// SPDX-License-Identifier: GPL-3.0-or-later
// SourcePickupInventoryV1: native idItem/idTrigger_Multi subset for the
// trainyard weapon pickups. Static contracts (Gamex86.dll 7cb1cbed...):
// item touch 0x20092060, activate 0x200920b0, CanPickup 0x20092b90, Pickup
// 0x20091ed0, GiveToPlayer 0x20091a30, post-pickup 0x20091c80, player
// GiveItem 0x2031bb10, inventory Give 0x20389840/0x2038f2b0, spawn inhibition
// of flamethrowerPreOrderDLC 0x2006f498, relay re-entry guard 0x20114883.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "SourceInventory.h"
#include "SourceDefinitions.h"
#include "SourceMapBodies.h"
#include "Entities.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourcePickups.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
#include "SourceDeclarationSetRuntime.h"
#include "SourceSession.h"

namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const char* const Profile="SourcePickupInventoryV1";
int liveOwners=0,liveItems=0;
std::string Fold(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
std::string Words(const float* v,unsigned n){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);for(unsigned i=0;i<n;++i){if(i)out<<' ';out<<v[i];}return out.str();}
// Measured trainyard ENTS identities (rechecked against names and classes).
struct ItemSource {const char* name;std::size_t index;const char* classname;bool inhibited;};
const ItemSource Items[]={
    {"weapon_mp_2",1738,"weapon_mp40",false},
    {"item_dynamite",1736,"item_dynamite",false},
    {"ammo_mdl24_large",1737,"ammo_mdl24_large",false},
    {"layer54_weapon_flammenwerfer_1",1747,"weapon_flammenwerfer",true}};
const char* const PickupRelay="trigger_relay_weapon_pickup";
const char* const ActivateRelay="activate_weapons";
const char* const DlcTip="layer54_target_tip_1";
const std::size_t PickupRelayIndex=1749,ActivateRelayIndex=1739,DlcTipIndex=1748;
std::size_t Unique(const wolf2009::EntityDocument& d,const std::string& name){std::size_t found=0,count=0;
    for(std::size_t i=0;i<d.entities.size();++i){const auto* p=d.entities[i].Find("name");if(p&&p->kind==wolf2009::EntityProperty::Text&&Fold(p->text)==Fold(name)){found=i;++count;}}
    Need(count==1,"Pickup source name is absent or ambiguous");return found;}
std::string EntsText(const wolf2009::EntityRecord& e,const char* key){const auto* p=e.Find(key);
    Need(p&&p->kind==wolf2009::EntityProperty::Text&&!p->text.empty()&&p->text.size()<=256,"Pickup ENTS text field is absent/unqualified");return p->text;}
// One effective literal as text: compiled/plain string, signed integer or
// float32 bits (printed with nine digits). Other encodings are unqualified.
bool LiteralText(const wolf2009::SourceResolvedLiteral& f,std::string& out){
    if((f.encoding==wolf2009::SourceLiteralEncoding::CompiledString||f.encoding==wolf2009::SourceLiteralEncoding::PlainText)&&f.value.size()==1){out=f.value[0].text;return true;}
    if(f.encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&f.numericWords.size()==1){out=std::to_string(std::int32_t(f.numericWords[0]));return true;}
    if(f.encoding==wolf2009::SourceLiteralEncoding::CompiledFloat32&&f.numericWords.size()==1){std::uint32_t w=f.numericWords[0];float v;std::memcpy(&v,&w,4);
        if(!std::isfinite(v))return false;std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(9)<<v;out=o.str();return true;}
    return false;
}
// Effective entity dictionary: class definition literals, then the ENTS
// instance keys. Keys that matter to this profile must be qualified.
std::map<std::string,std::string> Effective(const wolf2009::SourceDefinitions& defs,const wolf2009::EntityRecord& e,const std::string& classname){
    std::map<std::string,std::string> out;const auto d=defs.Resolve(wolf2009::SourceDefinitionKind::EntityDef,classname);
    for(const auto& field:d.literalFields){std::string text;
        if(LiteralText(field.second,text)){out[field.first]=text;continue;}
        Need(field.first.compare(0,4,"inv_")!=0&&field.first!="triggerfirst"&&field.first!="spawnclass"&&field.first!="respawn_sp"&&field.first!="triggersize",
            "A pickup class field used by this profile has an unqualified encoding");}
    for(const auto& p:e.properties){const auto key=Fold(p.key);
        if(p.kind==wolf2009::EntityProperty::Text)out[key]=p.text;
        else if(p.kind==wolf2009::EntityProperty::Integer)out[key]=std::to_string(p.integer);
        else if(key!="origin"&&key!="rotation")throw std::runtime_error("Pickup ENTS instance has an unadmitted float field: "+p.key);}
    return out;
}
const std::string* Get(const std::map<std::string,std::string>& d,const char* key){const auto f=d.find(Fold(key));return f==d.end()?nullptr:&f->second;}
bool Bool(const std::map<std::string,std::string>& d,const char* key){const auto* v=Get(d,key);return v&&std::atoi(v->c_str())!=0;}
float Float(const std::map<std::string,std::string>& d,const char* key,const char* fallback){const auto* v=Get(d,key);const char* text=v?v->c_str():fallback;
    char* end=nullptr;const double value=std::strtod(text,&end);Need(end&&*end==0&&std::isfinite(value),"Pickup numeric field is not a number");return float(value);}
class PickupHost {
  public:
    virtual ~PickupHost(){}
    virtual void Touched(idEntity* self,idEntity* other)=0;
    virtual void Activated(idEntity* self,idEntity* activator)=0;
    virtual void Removed(idEntity* self)=0;
};
const std::shared_ptr<PickupHost>* pendingHost=nullptr;
struct HostContext {
    explicit HostContext(const std::shared_ptr<PickupHost>& value){Need(!pendingHost,"Recursive pickup spawn");pendingHost=&value;}
    ~HostContext(){pendingHost=nullptr;}
};
}

// Native idItem subset: a CONTENTS_TRIGGER cube of half-size `triggersize`
// around the authored origin. Behaviour lives in the owner.
class idWolfSourceItem : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceItem);
    idWolfSourceItem(){Need(pendingHost&&bool(*pendingHost),"Source item requires its map owner");host=*pendingHost;++liveItems;}
    ~idWolfSourceItem(){if(const auto owner=host.lock())owner->Removed(this);host.reset();--liveItems;}
    void Spawn(){fl.networkSync=false;GetPhysics()->SetContents(CONTENTS_TRIGGER);}
    void Save(idSaveGame*)const{gameLocal.Error("Source item save state is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source item restore is unimplemented");}
  private:
    void Event_Touch(idEntity* other,trace_t*){if(const auto owner=host.lock())owner->Touched(this,other);}
    void Event_Activate(idEntity* activator){if(const auto owner=host.lock())owner->Activated(this,activator);}
    std::weak_ptr<PickupHost> host;
};
CLASS_DECLARATION(idEntity,idWolfSourceItem)
    EVENT(EV_Touch,idWolfSourceItem::Event_Touch)
    EVENT(EV_Activate,idWolfSourceItem::Event_Activate)
END_CLASS

// Native idTrigger_Multi relay subset (no touch volume, wait 0, delay 0).
class idWolfSourcePickupRelay : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourcePickupRelay);
    idWolfSourcePickupRelay(){Need(pendingHost&&bool(*pendingHost),"Source relay requires its map owner");host=*pendingHost;}
    ~idWolfSourcePickupRelay(){host.reset();}
    void Spawn(){fl.networkSync=false;}
    void Save(idSaveGame*)const{gameLocal.Error("Source pickup relay save state is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source pickup relay restore is unimplemented");}
  private:
    void Event_Activate(idEntity* activator){if(const auto owner=host.lock())owner->Activated(this,activator);}
    std::weak_ptr<PickupHost> host;
};
CLASS_DECLARATION(idEntity,idWolfSourcePickupRelay)
    EVENT(EV_Activate,idWolfSourcePickupRelay::Event_Activate)
END_CLASS

struct WolfSourcePickups::Impl : PickupHost {
    struct Item {
        WolfSourceItemState state;wolf2009::SourceItemFields fields;idEntityPtr<idEntity> entity;
        std::set<std::string> refusalsSeen;bool removeOnPickup=true;
    };
    struct Relay {WolfSourcePickupRelayState state;idEntityPtr<idEntity> entity;int nextFireMs=std::numeric_limits<int>::min();std::size_t index=0;};
    std::string profile;const wolf2009::SourceMapBodyPlan* plan=nullptr;
    std::vector<Item> items;Relay activate,pickup;WolfSourcePickupsState state;std::string tipReason;
    unsigned depth=0;bool spawned=false;
    Impl(){++liveOwners;}
    ~Impl(){Clear();--liveOwners;}
    void Clear(){
        for(auto& item:items){if(auto* e=item.entity.GetEntity())delete e;item.entity=nullptr;}
        for(Relay* r:{&activate,&pickup}){if(auto* e=r->entity.GetEntity())delete e;r->entity=nullptr;}
    }
    void Receipt(const std::string& source,const std::string& target,const char* operation,const char* outcome,const std::string& reason,idEntity* actor){
        WolfSourcePickupReceipt r;r.source=source;r.target=target;r.operation=operation;r.outcome=outcome;r.reason=reason;r.actor=actor?actor->GetName():"";r.depth=depth;r.timeMs=gameLocal.time;
        if(state.receipts.size()<512)state.receipts.push_back(r);
        gameLocal.Printf("WOLF2009_SOURCE_PICKUP_RECEIPT source=%s target=%s op=%s outcome=%s actor=%s depth=%u time=%d reason=\"%s\"\n",source.c_str(),target.c_str(),operation,outcome,
            r.actor.empty()?"-":r.actor.c_str(),depth,r.timeMs,reason.c_str());
    }
    Item* FindItem(idEntity* self){for(auto& item:items)if(item.entity.GetEntity()==self&&self)return &item;return nullptr;}
    Item* FindItem(const std::string& name){for(auto& item:items)if(item.state.name==name)return &item;return nullptr;}
    Relay* FindRelay(idEntity* self){for(Relay* r:{&activate,&pickup})if(r->entity.GetEntity()==self&&self)return r;return nullptr;}
    Relay* FindRelay(const std::string& name){for(Relay* r:{&activate,&pickup})if(r->state.name==name)return r;return nullptr;}
    // Relay fire: idTrigger_Multi with wait 0 stores time+1 before firing its
    // targets synchronously, so a re-entry in the same game frame is ignored.
    void Fire(Relay& relay,idEntity* activator){
        if(gameLocal.time<relay.nextFireMs){++relay.state.reentries;Receipt(activator?activator->GetName():"-",relay.state.name,"fire","reentry-ignored","relay already fired in this frame (time+1 guard)",activator);return;}
        relay.nextFireMs=gameLocal.time+1;++relay.state.activations;relay.state.lastFireMs=gameLocal.time;
        Receipt(activator?activator->GetName():"-",relay.state.name,"fire","applied","activate every target in ENTS target-list order",activator);
        for(const auto& target:relay.state.targets){
            if(auto* item=FindItem(target)){
                if(item->state.inhibited){Receipt(relay.state.name,target,"activate","spawn-inhibited",item->state.inhibitReason,activator);continue;}
                auto* e=item->entity.GetEntity();
                if(!e||unsigned(gameLocal.GetSpawnId(e))!=item->state.spawnId||gameLocal.FindEntity(target.c_str())!=e){Receipt(relay.state.name,target,"activate","target-expired","item native generation ended",activator);continue;}
                e->ProcessEvent(&EV_Activate,activator);continue;}
            if(target==DlcTip){Receipt(relay.state.name,target,"activate","spawn-inhibited",tipReason,activator);continue;}
            Receipt(relay.state.name,target,"activate","not-modelled","ENTS target has no native implementation in this profile",activator);
        }
    }
    bool PlayerFlagsDisabled(idEntity* player){const auto* f=Wolf2009_SourcePlayerFlags(player);return f&&f->weaponsDisabled;}
    // CanPickup 0x20092b90. The player-state gates beyond the local-player
    // check (+0x76c/+0x774 handle, 0x20003010, slot +0x3ec, noGraspPickup with
    // an inventory state) have no native state in this profile.
    bool CanPickup(Item& item,idPlayer* player,std::string& why){
        if(!item.state.armed){why="item is not armed (+0x30d)";return false;}
        if(!player||player!=gameLocal.GetLocalPlayer()){why="toucher is not the local player";return false;}
        auto* inventory=Wolf2009_SourcePlayerInventory(player);if(!inventory){why="player has no source inventory";return false;}
        bool expression=true;if(!inventory->CanPickup(item.fields,PlayerFlagsDisabled(player),expression,why))return false;
        // The optional expression gate (+0x320/+0x324) is unset for these items.
        return true;
    }
    // Pickup 0x20091ed0 -> GiveToPlayer 0x20091a30 -> post-pickup 0x20091c80.
    bool Pickup(Item& item,idPlayer* player){
        struct Depth{unsigned& d;explicit Depth(unsigned& v):d(v){++d;}~Depth(){--d;}}scope(depth);
        state.maxDepth=std::max(state.maxDepth,depth);
        auto* inventory=Wolf2009_SourcePlayerInventory(player);
        if(!inventory){Receipt(item.state.name,player?player->GetName():"-","pickup","refused","player has no source inventory",player);return false;}
        auto result=inventory->GiveItem(item.fields,PlayerFlagsDisabled(player));
        const unsigned index=item.state.pickups+1;
        for(const auto& give:result.receipts){WolfSourcePickupGive g;g.item=item.state.name;g.pickup=index;g.give=give;g.timeMs=gameLocal.time;state.gives.push_back(g);
            gameLocal.Printf("WOLF2009_SOURCE_PICKUP_GIVE item=%s pickup=%u key=%s value=%s outcome=%s before=%g after=%g overflow=%g reason=\"%s\"\n",item.state.name.c_str(),index,
                give.key.c_str(),give.value.c_str(),give.outcome.c_str(),double(give.before),double(give.after),double(give.overflow),give.reason.c_str());}
        if(!result.given){++item.state.giveRefusals;
            Receipt(item.state.name,player->GetName(),"pickup","refused",result.refusedWeaponsDisabled?"GiveItem refuses inv_weapon while weapons are disabled":"no inventory give succeeded",player);return false;}
        ++item.state.pickups;item.state.lastPickupMs=gameLocal.time;if(item.state.firstPickupMs<0)item.state.firstPickupMs=gameLocal.time;
        Receipt(item.state.name,player->GetName(),"pickup","applied","GiveToPlayer succeeded; snd_acquire, player slot +0x224 notice and pickup event 7 are not modelled",player);
        // ActivateTargets(player) inside GiveToPlayer, before the item hides.
        for(const auto& target:item.state.targets){
            if(auto* relay=FindRelay(target)){if(auto* e=relay->entity.GetEntity())e->ProcessEvent(&EV_Activate,player);else Receipt(item.state.name,target,"activate","target-expired","relay native generation ended",player);}
            else Receipt(item.state.name,target,"activate","not-modelled","target is outside this profile",player);
        }
        // Post-pickup: no contents, hidden, inactive; removal 5000 ms later
        // (respawn_sp 0, not dropped, neither no_respawn nor inv_carry).
        if(auto* e=item.entity.GetEntity()){e->GetPhysics()->SetContents(0);e->Hide();e->BecomeInactive(TH_THINK);item.state.hidden=true;
            if(item.removeOnPickup){e->PostEventMS(&EV_Remove,5000);++item.state.removalPosts;item.state.removalDueMs=gameLocal.time+5000;}}
        return true;
    }
    // After the outermost dispatch: an owned MP40 becomes the source weapon,
    // with the inventory's ammo_mp40 pool split into clip and reserve.
    void Sync(idPlayer* player){
        if(depth||!player||state.equips)return;auto* inventory=Wolf2009_SourcePlayerInventory(player);
        if(!inventory||!inventory->HasWeapon("weapon_mp40"))return;
        const float pool=inventory->Ammo(std::size_t(wolf2009::SourceAmmoSlotIndex("ammo_mp40"))).amount;
        Need(std::isfinite(pool)&&pool>=0&&pool<=100000,"Inventory MP40 pool is not bounded");
        const int total=int(pool),clip=std::min(total,state.clipSize),reserve=total-clip;idStr why;
        if(!Wolf2009_EquipSourceSessionInventoryWeapon(player,"weapon_mp40",clip,reserve,why)){state.equipOutcome=why.c_str();
            Receipt("inventory",player->GetName(),"equip","refused",why.c_str(),player);return;}
        float taken=0;std::string reason;Need(inventory->TransferAmmoToController("ammo_mp40",taken,reason)&&taken==pool,"Inventory MP40 pool transfer differs");
        ++state.equips;state.equipPool=pool;state.equipClip=clip;state.equipReserve=reserve;state.equipTimeMs=gameLocal.time;state.equipOutcome="equipped";
        Receipt("inventory",player->GetName(),"equip","applied","SourceAmmoSplitV1: clip=min(ClipSize, pool), reserve=rest; the controller now holds the rounds",player);
        gameLocal.Printf("WOLF2009_SOURCE_PICKUP_EQUIP weapon=weapon_mp40 pool=%g clip=%d reserve=%d time=%d\n",double(pool),clip,reserve,gameLocal.time);
    }
    // Touch 0x20092060: player only, CanPickup, then Pickup.
    void Touched(idEntity* self,idEntity* other)override{
        auto* item=FindItem(self);if(!item||!other)return;++item->state.touches;
        if(!other->IsType(idPlayer::Type)){++item->state.refusedTouches;return;}
        auto* player=static_cast<idPlayer*>(other);std::string why;
        if(!CanPickup(*item,player,why)){++item->state.refusedTouches;item->state.lastRefusal=why;
            if(item->refusalsSeen.insert(why).second)Receipt(item->state.name,player->GetName(),"touch","refused","CanPickup: "+why,player);return;}
        ++state.touchDispatches;Receipt(item->state.name,player->GetName(),"touch","applied","CanPickup passed",player);
        Pickup(*item,player);Sync(player);
    }
    // Item activate 0x200920b0 and relay activate.
    void Activated(idEntity* self,idEntity* activator)override{
        if(auto* relay=FindRelay(self)){Fire(*relay,activator);if(activator&&activator->IsType(idPlayer::Type))Sync(static_cast<idPlayer*>(activator));return;}
        auto* item=FindItem(self);if(!item)return;++item->state.activations;auto* e=item->entity.GetEntity();
        if(e->IsHidden()){e->Show();e->UpdateVisuals();item->state.hidden=false;++item->state.shows;
            Receipt(activator?activator->GetName():"-",item->state.name,"activate","applied","hidden item shown (highlight refresh not modelled); contents and pending removal unchanged",activator);return;}
        if(!item->state.armed&&item->state.triggerFirst){item->state.armed=true;++item->state.arms;item->state.armTimeMs=gameLocal.time;
            Receipt(activator?activator->GetName():"-",item->state.name,"activate","applied","triggerFirst: armed (+0x30d), no pickup",activator);return;}
        if(activator&&activator->IsType(idPlayer::Type)){
            // The activate path does not consult CanPickup; GiveItem still gates.
            Pickup(*item,static_cast<idPlayer*>(activator));return;}
        Receipt(activator?activator->GetName():"-",item->state.name,"activate","refused","activator is not a player",activator);
    }
    void Removed(idEntity* self)override{
        for(auto& item:items)if(item.entity.GetEntity()==self){item.state.removed=true;item.state.alive=false;item.state.removedTimeMs=gameLocal.time;
            gameLocal.Printf("WOLF2009_SOURCE_PICKUP_REMOVED item=%s time=%d due=%d\n",item.state.name.c_str(),gameLocal.time,item.state.removalDueMs);}
    }
};

WolfSourcePickups::WolfSourcePickups(std::shared_ptr<Impl> value):impl(std::move(value)){}
WolfSourcePickups::~WolfSourcePickups(){impl->Clear();}
idEntity* WolfSourcePickups::Item(const std::string& name)const{auto* item=impl->FindItem(name);return item?item->entity.GetEntity():nullptr;}
idEntity* WolfSourcePickups::Relay(const std::string& name)const{auto* relay=impl->FindRelay(name);return relay?relay->entity.GetEntity():nullptr;}

std::unique_ptr<WolfSourcePickups> Wolf2009_PrepareSourcePickups(const WolfSourceDeclarationSet& set,const wolf2009::SourceMapBodyPlan& plan,
    const std::string& profile,idStr& reason){reason.Clear();try{
    Need(profile==Profile,"Explicit SourcePickupInventoryV1 profile required");
    const auto& d=plan.entities;Need(d.entities.size()==2238,"Measured trainyard ENTS entity count differs");
    const auto& defs=set.Definitions();
    std::shared_ptr<WolfSourcePickups::Impl> out(new WolfSourcePickups::Impl);out->profile=profile;out->plan=&plan;
    for(const auto& source:Items){Need(Unique(d,source.name)==source.index,"Pickup item ENTS index differs");const auto& e=d.entities[source.index];
        Need(EntsText(e,"classname")==source.classname,"Pickup item class differs");
        const auto fields=Effective(defs,e,source.classname);
        Need(Get(fields,"spawnclass")&&*Get(fields,"spawnclass")=="idItem","Pickup item spawnclass is not idItem");
        WolfSourcePickups::Impl::Item item;auto& s=item.state;s.name=source.name;s.classname=source.classname;s.entityIndex=unsigned(source.index);
        s.triggerFirst=Bool(fields,"triggerFirst");Need(s.triggerFirst,"Pickup item lacks the measured triggerFirst");Need(!Bool(fields,"Hide"),"Pickup item has a Hide key");
        // respawn_sp 0, not dropped, no no_respawn/inv_carry: removal after 5000 ms.
        Need(Float(fields,"respawn_sp","0.0")==0&&!Bool(fields,"dropped")&&!Bool(fields,"no_respawn")&&!Bool(fields,"inv_carry")&&!Bool(fields,"unique")&&
             !Get(fields,"def_collectibleSet")&&!Get(fields,"def_intel_data")&&!Get(fields,"gold"),"Pickup item uses a respawn/collectible branch outside this profile");
        // Static idEntity spawn default "32" (0x2003a65b, ent+0x4b0); its touch consumer is Inferred.
        s.triggerSize=Float(fields,"triggersize","32");Need(s.triggerSize>0&&s.triggerSize<=256,"Pickup triggersize is outside the admitted range");
        const auto place=wolf2009::ReadEntityPlacement(e);Need(place.hasOrigin,"Pickup item has no origin");for(unsigned k=0;k<3;++k)s.origin[k]=place.origin[k];
        s.targets.push_back(EntsText(e,"target"));Need(s.targets[0]==PickupRelay,"Pickup item target differs");
        if(source.inhibited){Need(Bool(fields,"flamethrowerPreOrderDLC"),"Inhibited pickup lacks its DLC key");s.inhibited=true;
            s.inhibitReason="flamethrowerPreOrderDLC: spawn inhibited without the pre-order entitlement (0x2006f498); NoPreOrderDLCEntitlementV1";}
        else Need(!Get(fields,"flamethrowerPreOrderDLC"),"Pickup item unexpectedly carries the DLC key");
        for(const auto& f:fields)item.fields.push_back(f);
        out->items.push_back(item);
    }
    {const auto& e=d.entities[DlcTipIndex];Need(Unique(d,DlcTip)==DlcTipIndex&&EntsText(e,"classname")=="target_tip"&&e.Find("flamethrowerPreOrderDLC"),"DLC tip differs");
        out->tipReason="target_tip with flamethrowerPreOrderDLC: spawn inhibited without the pre-order entitlement (0x2006f498)";}
    for(auto* relay:{&out->activate,&out->pickup}){const bool isPickup=relay==&out->pickup;const char* name=isPickup?PickupRelay:ActivateRelay;
        relay->index=isPickup?PickupRelayIndex:ActivateRelayIndex;Need(Unique(d,name)==relay->index,"Pickup relay ENTS index differs");const auto& e=d.entities[relay->index];
        Need(EntsText(e,"classname")=="trigger_relay","Pickup relay class differs");const auto fields=Effective(defs,e,"trigger_relay");
        Need(Get(fields,"spawnclass")&&*Get(fields,"spawnclass")=="idTrigger_Multi"&&Float(fields,"wait","0")==0&&Float(fields,"delay","0")==0&&
             Float(fields,"random","0")==0,"trigger_relay is not the measured idTrigger_Multi wait 0 delay 0 profile");
        relay->state.name=name;
        // Target-list order follows the ENTS dictionary order (target, target1, ...).
        for(const auto& p:e.properties){const auto k=Fold(p.key);if(k.compare(0,6,"target")==0)relay->state.targets.push_back(EntsText(e,p.key.c_str()));}
    }
    const std::vector<std::string> expected={"weapon_mp_2","item_dynamite","ammo_mdl24_large","layer54_weapon_flammenwerfer_1"};
    Need(out->activate.state.targets==expected,"activate_weapons target list differs");
    auto withTip=expected;withTip.push_back(DlcTip);Need(out->pickup.state.targets==withTip,"trigger_relay_weapon_pickup target list differs");
    {const auto weapon=defs.Resolve(wolf2009::SourceDefinitionKind::EntityDef,"weapon_mp40");const auto* clip=weapon.Find("ClipSize");std::string text;
        Need(clip&&LiteralText(*clip,text),"weapon_mp40 ClipSize is unqualified");out->state.clipSize=std::atoi(text.c_str());Need(out->state.clipSize>0&&out->state.clipSize<=1000,"weapon_mp40 ClipSize differs");}
    return std::unique_ptr<WolfSourcePickups>(new WolfSourcePickups(std::move(out)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourcePickups>();}}

bool WolfSourcePickups::Spawn(idStr& reason){reason.Clear();auto& p=*impl;try{
    Need(!p.spawned,"Source pickups already spawned");
    Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Source pickups require an active local single-player game");
    for(const auto& item:p.items)Need(!gameLocal.FindEntity(item.state.name.c_str()),"Pickup item native name is occupied");
    Need(!gameLocal.FindEntity(PickupRelay)&&!gameLocal.FindEntity(ActivateRelay)&&!gameLocal.FindEntity(DlcTip),"Pickup relay native name is occupied");
    const std::shared_ptr<PickupHost> host=impl;HostContext context(host);
    for(auto& item:p.items){auto& s=item.state;
        if(s.inhibited){p.Receipt(Profile,s.name,"spawn","spawn-inhibited",s.inhibitReason,nullptr);continue;}
        idDict args;args.Set("classname","wolf2009_source_item");args.Set("name",s.name.c_str());args.Set("origin",Words(s.origin,3).c_str());
        const float mins[3]={-s.triggerSize,-s.triggerSize,-s.triggerSize},maxs[3]={s.triggerSize,s.triggerSize,s.triggerSize};
        args.Set("mins",Words(mins,3).c_str());args.Set("maxs",Words(maxs,3).c_str());args.Set("networkSync","0");
        std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceItem::Type,&args));
        Need(entity&&entity->GetPhysics()->GetClipModel()&&entity->GetPhysics()->GetContents()==CONTENTS_TRIGGER,"Native source item allocation failed");
        s.spawnId=unsigned(gameLocal.GetSpawnId(entity.get()));s.alive=true;item.entity=entity.release();
        p.Receipt(Profile,s.name,"spawn","applied","idItem touch cube half-size "+std::to_string(int(s.triggerSize))+" (triggersize default 32, consumer Inferred); world model not presented",nullptr);
    }
    p.Receipt(Profile,DlcTip,"spawn","spawn-inhibited",p.tipReason,nullptr);
    for(auto* relay:{&p.activate,&p.pickup}){const auto place=wolf2009::ReadEntityPlacement(p.plan->entities.entities[relay->index]);
        idDict args;args.Set("classname","wolf2009_source_pickup_relay");args.Set("name",relay->state.name.c_str());args.Set("origin",Words(place.origin,3).c_str());
        args.Set("noclipmodel","1");args.Set("networkSync","0");
        std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourcePickupRelay::Type,&args));Need(bool(entity),"Native pickup relay allocation failed");
        relay->entity=entity.release();}
    p.spawned=true;p.state.generation=std::uint64_t(gameLocal.GetSpawnId(p.pickup.entity.GetEntity()));
    gameLocal.Printf("WOLF2009_SOURCE_PICKUPS_ASSEMBLED profile=%s items=3 inhibited=2 relays=2 clip_size=%d relay_targets=%u activate_targets=%u\n",
        Profile,p.state.clipSize,unsigned(p.pickup.state.targets.size()),unsigned(p.activate.state.targets.size()));
    return true;
}catch(const std::exception& error){reason=error.what();p.Clear();return false;}}

bool WolfSourcePickups::Query(WolfSourcePickupsState& out,idStr& reason)const{reason.Clear();try{const auto& p=*impl;
    WolfSourcePickupsState s=p.state;s.items.clear();
    for(const auto& item:p.items){auto st=item.state;auto* e=item.entity.GetEntity();st.alive=e!=nullptr;if(e){st.hidden=e->IsHidden();}s.items.push_back(st);}
    s.activateRelay=p.activate.state;s.pickupRelay=p.pickup.state;out=s;return true;
}catch(const std::exception& error){reason=error.what();return false;}}

int Wolf2009_LiveSourcePickupOwners(){return liveOwners;}
int Wolf2009_LiveSourceItems(){return liveItems;}
