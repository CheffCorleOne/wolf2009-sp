// SPDX-License-Identifier: GPL-3.0-or-later
#include <memory>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "game/Item.h"
#include "SourceEntityRuntime.h"
#include "SourceDeclarationSetRuntime.h"
#ifndef WOLF2009_SOURCE_ACTOR_GAME
#define WOLF2009_SOURCE_ACTOR_GAME
#endif
#include "SourceActorBinding.h"
#include "NativeSourceEntityChecks.h"

class idWolfSourceQaActor : public idEntity {public:CLASS_PROTOTYPE(idWolfSourceQaActor);void Spawn(){}};
CLASS_DECLARATION(idEntity,idWolfSourceQaActor)
END_CLASS

namespace {
unsigned checks=0,frames=0;
void Need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Owned {
    idList< idEntityPtr<idEntity> > entities;
    void Add(idEntity* entity){idEntityPtr<idEntity> ref;ref=entity;entities.Append(ref);}
    ~Owned(){for(int i=entities.Num()-1;i>=0;--i){idEntity* entity=entities[i].GetEntity();if(entity)delete entity;}}
};
struct RestorePersistence {
    std::string key,value;bool existed=false;
    explicit RestorePersistence(const std::string& name):key(name){const auto* old=gameLocal.persistentLevelInfo.FindKey(key.c_str());if(old){existed=true;value=old->GetValue().c_str();}gameLocal.persistentLevelInfo.Delete(key.c_str());}
    ~RestorePersistence(){if(existed)gameLocal.persistentLevelInfo.Set(key.c_str(),value.c_str());else gameLocal.persistentLevelInfo.Delete(key.c_str());}
};
const char* ItemName="wolf2009_source_container_qa_item";
struct RestoreInventory {
    idEntityPtr<idPlayer> player;int pickupNames;
    explicit RestoreInventory(idPlayer* value):pickupNames(value->inventory.pickupItemNames.Num()){player=value;Need(!value->FindInventoryItem(ItemName),"Native source QA inventory name already exists");}
    ~RestoreInventory(){idPlayer* owner=player.GetEntity();if(!owner)return;while(idDict* added=owner->FindInventoryItem(ItemName))owner->RemoveInventoryItem(added);owner->inventory.pickupItemNames.SetNum(pickupNames);}
};
void Frame(){usercmd_t commands[MAX_CLIENTS];memset(commands,0,sizeof(commands));gameLocal.RunFrame(commands);++frames;}
WolfSourceEntityState State(const idEntity* entity){WolfSourceEntityState state;idStr reason;Need(Wolf2009_QuerySourceEntity(entity,state,reason),reason.IsEmpty()?"Native source entity state unavailable":reason.c_str());return state;}
void Placement(idEntity* entity,const wolf2009::SourceEntityPlan& facts,bool scaled){
    std::vector<float> origin,raw;Need(facts.Find("origin").Floats(origin)&&origin.size()==3&&facts.Find("rotation").Floats(raw)&&raw.size()==9,"Native placement oracle source words unavailable");
    const auto* render=entity->GetRenderEntity();const auto state=State(entity);Need(render&&state.placementApproximated==scaled&&state.placementBoundKnown&&state.placementScaleError<=0.005,"Native near-unit placement approximation was hidden/unbounded");
    double normalized[9],difference=0;const auto& rigid=entity->GetPhysics()->GetAxis();for(int i=0;i<3;++i){double length=0;for(int j=0;j<3;++j)length+=double(raw[std::size_t(3*i+j)])*raw[std::size_t(3*i+j)];length=std::sqrt(length);
        Need(std::abs(render->origin[i]-origin[std::size_t(i)])<0.001f,"Native render origin changed authored placement");
        for(int j=0;j<3;++j){normalized[3*i+j]=raw[std::size_t(3*i+j)]/length;
            Need(std::abs(render->axis[i][j]-normalized[3*i+j])<0.000002&&std::abs(render->axis[i][j]-rigid[i][j])<0.000002f,"Native renderer/physics differs from explicit near-unit basis");
            Need(std::abs(rigid[i]*rigid[j]-(i==j?1.0f:0.0f))<0.000002f,"Native physics received a nonrigid source matrix");const double delta=raw[std::size_t(3*i+j)]-rigid[i][j];difference+=delta*delta;}}
    Need(std::abs(std::sqrt(difference)-state.placementMatrixErrorNorm)<1e-8&&std::abs(state.placementDisplacementBound-state.placementModelRadius*state.placementMatrixErrorNorm)<1e-8,"Native placement displacement metric differs from retained raw matrix");
    auto* animated=static_cast<idAnimatedEntity*>(entity);int count=0;idJointMat* joints=NULL;animated->GetAnimator()->GetJoints(&count,&joints);
    Need(count>0&&joints!=NULL,"Normal native frame did not produce a raw source joint palette");
    for(int joint=0;joint<count;++joint){const idVec3 local=joints[joint].ToVec3();idVec3 actual;idMat3 axis;
        Need(animated->GetJointWorldTransform(static_cast<jointHandle_t>(joint),gameLocal.time,actual,axis),"Native source world joint unavailable");
        for(int c=0;c<3;++c){double expected=origin[std::size_t(c)],authored=expected;for(int r=0;r<3;++r){expected+=double(local[r])*normalized[3*r+c];authored+=double(local[r])*raw[std::size_t(3*r+c)];}
            const auto rounding=8*std::numeric_limits<float>::epsilon()*(1+std::abs(expected));Need(std::abs(double(actual[c])-expected)<=rounding,"Native world joint differs from explicit near-unit basis rows");
            Need(std::abs(double(actual[c])-authored)<=state.placementDisplacementBound+rounding,"Native world joint exceeds declared authored-placement error bound");}}
    const idBounds local=render->bounds;idBounds expected,authoredBounds;expected.Clear();authoredBounds.Clear();for(int corner=0;corner<8;++corner){idVec3 point,authored;
        for(int c=0;c<3;++c){double value=origin[std::size_t(c)],source=value;for(int r=0;r<3;++r){value+=double(local[(corner>>r)&1][r])*normalized[3*r+c];source+=double(local[(corner>>r)&1][r])*raw[std::size_t(3*r+c)];}point[c]=float(value);authored[c]=float(source);}expected.AddPoint(point);authoredBounds.AddPoint(authored);}
    idBounds world;world.FromTransformedBounds(local,render->origin,render->axis);for(int side=0;side<2;++side)for(int c=0;c<3;++c)
        Need(std::abs(world[side][c]-expected[side][c])<0.002f&&std::abs(world[side][c]-authoredBounds[side][c])<=state.placementDisplacementBound+0.002,"Native bounds exceed the near-unit placement profile/error bound");
    gameLocal.Printf("WOLF2009_SOURCE_ENTITY_PLACEMENT source_index=%zu profile=NativeNearUnitPlacementV1 approximated=%d scale_error=%.9g matrix_error=%.9g model_radius=%.9g displacement_bound=%.9g raw_facts_retained=1 rigid_physics_render=1 world_joints=1 world_bounds=1\n",facts.Identity().entityIndex,int(scaled),state.placementScaleError,state.placementMatrixErrorNorm,state.placementModelRadius,state.placementDisplacementBound);
}
void Endpoint(idAnimatedEntity* entity,const WolfSourceActorBinding& binding){
    const auto* clip=binding.FindClip("open");Need(clip!=NULL,"Actual source open clip missing");const auto* definition=entity->GetAnimator()->ModelDef();const auto* anim=definition->GetAnim(definition->GetSpecificAnim("open"));
    Need(anim&&anim->NumAnims()==1&&entity->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL)->GetFrameNumber(gameLocal.time)==clip->frames,"Native completion did not explicitly hold the authored final frame");
    int count=0;idJointMat* actual=NULL;entity->GetAnimator()->GetJoints(&count,&actual);Need(actual&&count>0&&count<=256,"Native completion raw palette missing");
    idJointQuat* local=(idJointQuat*)_alloca16(std::size_t(count)*sizeof(idJointQuat));int* indices=(int*)_alloca16(std::size_t(count)*sizeof(int));idJointMat* expected=(idJointMat*)_alloca16(std::size_t(count)*sizeof(idJointMat));
    for(int i=0;i<count;++i)indices[i]=i;anim->MD5Anim(0)->GetSingleFrame(clip->frames-1,local,indices,count);const auto* hierarchy=definition->ModelHandle()->GetJoints();
    for(int i=0;i<count;++i){expected[i].SetRotation(local[i].q.ToMat3());expected[i].SetTranslation(local[i].t);if(hierarchy[i].parent){const auto parent=hierarchy[i].parent-hierarchy;Need(parent>=0&&parent<i,"Native source endpoint hierarchy invalid");expected[i]*=expected[parent];}
        for(int c=0;c<12;++c)Need(std::abs(actual[i].ToFloatPtr()[c]-expected[i].ToFloatPtr()[c])<0.0002f,"Normal source completion palette differs from direct authored endpoint/FK");}
    gameLocal.Printf("WOLF2009_SOURCE_ENTITY_ENDPOINT frame=%d raw_palette=1 direct_source_endpoint=1 normal_frame=1\n",clip->frames);
}
std::string PersistenceKey(const std::string& name){std::string map=gameLocal.GetMapName();const auto slash=map.find_last_of("/\\");if(slash!=std::string::npos)map.erase(0,slash+1);const auto dot=map.find_last_of('.');if(dot!=std::string::npos)map.resize(dot);return "wolf2009.source-container:"+map+"_"+name;}
idEntity* Spawn(const char* path,std::size_t index,const WolfSourceDeclarationSet& set,std::shared_ptr<const WolfSourceActorBinding> binding,Owned& owned){idEntity* entity=NULL;idStr reason;
    Need(Wolf2009_SpawnSourceEntity(path,index,set,binding,entity,reason)&&entity,reason.IsEmpty()?"Actual source container Spawn failed":reason.c_str());owned.Add(entity);return entity;}
idItem* ItemAt(idEntity* container,Owned& owned,const char* name="wolf2009_source_native_loot"){
    idAnimatedEntity* animated=static_cast<idAnimatedEntity*>(container);idBounds local,world;Need(animated->GetAnimator()->GetBounds(gameLocal.time,local),"Actual source container bounds unavailable");
    const auto* render=container->GetRenderEntity();world.FromTransformedBounds(local,render->origin,render->axis);
    idDict args;args.Set("classname","wolf2009_native_item_qa");args.Set("name",name);args.Set("origin",world.GetCenter().ToString(9));
    args.Set("mins","-1 -1 -1");args.Set("maxs","1 1 1");args.SetBool("inv_carry",true);args.Set("inv_name",ItemName);args.SetBool("dropToFloor",false);args.SetBool("networkSync",false);
    idItem* item=static_cast<idItem*>(gameLocal.SpawnEntityType(idItem::Type,&args));Need(item&&item->GetPhysics()->GetClipModel(),"GPL idItem native Spawn failed");owned.Add(item);return item;
}
}
bool Wolf2009_CheckSourceEntityQA(const WolfSourceDeclarationSet& set,std::shared_ptr<const WolfSourceActorBinding> binding,const char* path){
    idStr reason;checks=frames=0;try{
        Need(path&&binding&&gameLocal.GameState()==GAMESTATE_ACTIVE,"Native source entity QA requires active game/binding");
        Need(Wolf2009_LiveSourceEntities()==0,"Native source entities already exist before QA");
        const auto& source=binding->SourcePlan();Need(source.ResourceRequest().entityDefinition=="container_locker_foot_01","Native source entity QA requires the actual compiled container class");
        idPlayer* player=gameLocal.GetLocalPlayer();Need(player!=NULL,"Native source entity QA needs the real local GPL player");RestoreInventory inventory(player);
        std::vector<unsigned char> ents;{idFile* file=fileSystem->OpenFileRead(path,false);Need(file!=NULL,"Actual ENTS unavailable for native QA");const int length=file->Length();
            if(length<=0||length>64*1024*1024){fileSystem->CloseFile(file);throw std::runtime_error("Native QA ENTS byte budget");}
            try{ents.resize(static_cast<std::size_t>(length));Need(file->Read(ents.data(),length)==length,"Native QA ENTS short read");}catch(...){fileSystem->CloseFile(file);throw;}fileSystem->CloseFile(file);}
        const auto document=wolf2009::DecodeEntities4(ents);std::size_t index=document.entities.size(),secondIndex=index;
        for(std::size_t i=0;i<document.entities.size();++i){const auto* c=document.entities[i].Find("classname");if(c&&c->kind==wolf2009::EntityProperty::Text&&c->text=="container_locker_foot_01"){if(index==document.entities.size())index=i;else{secondIndex=i;break;}}}
        Need(index==917&&secondIndex==921,"Actual selected container indices differ from qualified source");
        const auto facts=wolf2009::PrepareSourceEntity(ents,path,index,set.Definitions(),wolf2009::SourceEntityPolicy::AnimatedContainerV1,source.Context());
        const auto otherFacts=wolf2009::PrepareSourceEntity(ents,path,secondIndex,set.Definitions(),wolf2009::SourceEntityPolicy::AnimatedContainerV1,source.Context());
        RestorePersistence persistent(PersistenceKey(facts->Name())),otherPersistent(PersistenceKey(otherFacts->Name()));Owned owned;idEntity* entity=Spawn(path,index,set,binding,owned);
        Need(std::string(entity->GetName())==facts->Name()&&std::string(entity->spawnArgs.GetString("classname"))==facts->Classname(),"Native factory replaced source name/class spelling");
        idEntity* duplicate=reinterpret_cast<idEntity*>(1);idStr failure;Need(!Wolf2009_SpawnSourceEntity(path,index,set,binding,duplicate,failure)&&duplicate==NULL&&!failure.IsEmpty()&&Wolf2009_LiveSourceEntities()==1,"Native duplicate admission changed entity registry");
        idEntity* second=Spawn(path,secondIndex,set,binding,owned);Need(std::string(second->GetName())==otherFacts->Name()&&Wolf2009_LiveSourceEntities()==2,"Second actual ENTS entity was renamed or coalesced");
        auto* animated=static_cast<idAnimatedEntity*>(entity);for(int i=0;i<5;++i)Frame();auto state=State(entity);
        Placement(entity,*facts,false);Placement(second,*otherFacts,true);
        Need(state.phase==WolfSourceEntity_Idle&&state.activations==0&&state.usable&&animated->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL)->GetCycleCount()==1073741824&&
            !animated->GetAnimator()->CurrentAnim(ANIMCHANNEL_ALL)->IsDone(gameLocal.time),"Actual source start_anim did not remain a native source-duration cycle");
        Need(entity->GetSoundEmitter()==NULL&&second->GetSoundEmitter()==NULL,"Source idle allocated a sound emitter before its first frame command");
        const int opened=gameLocal.time;Need(Wolf2009_UseSourceEntity(entity,player,"source_key_is_ignored",reason),reason.IsEmpty()?"Actual source Use/Activate failed":reason.c_str());state=State(entity);
        Need(state.sequence==1&&state.animation=="open"&&state.phase==WolfSourceEntity_Playing&&!state.usable&&state.pendingGiveEvents==1&&state.persisted&&
            gameLocal.persistentLevelInfo.GetBool(state.persistenceKey.c_str()),"Actual Activate did not play anim1/clear usable/persist/schedule source-ms giving");
        Need(State(second).phase==WolfSourceEntity_Idle&&State(second).activations==0&&State(second).usable,"First actual entity's activation changed the second instance");
        Frame();Need(entity->GetSoundEmitter()!=NULL&&entity->GetSoundEmitter()->CurrentlyPlaying()&&second->GetSoundEmitter()==NULL,
            "First native frame did not dispatch the authored frame1 sound on its own entity");
        gameLocal.Printf("WOLF2009_SOURCE_ENTITY_FRAME_SOUND first_native_frame=1 emitter=1 playing=1 isolated=1 custom_sound_timer=0\n");
        while(gameLocal.time<opened+1100){Frame();Need(!player->FindInventoryItem(ItemName),"Source loot appeared before an item existed");}
        Endpoint(animated,*binding);
        state=State(entity);Need(state.animationCompletions==1&&(state.phase==WolfSourceEntity_UnsupportedAFHandoff||state.phase==WolfSourceEntity_Completed)&&state.pendingGiveEvents==1,"Completion canceled an independent source give event or missed its AF boundary");
        idItem* item=ItemAt(entity,owned);const int deadline=opened+1200;
        while(gameLocal.time<deadline){Frame();if(gameLocal.time<deadline)Need(!item->IsHidden()&&!player->FindInventoryItem(ItemName),"Source giveDelay fired before1200 native milliseconds");}
        for(int i=0;i<3&&State(entity).giveEvents==0;++i)Frame();state=State(entity);
        Need(state.giveEvents==1&&state.pendingGiveEvents==0&&state.activatedItems==1&&item->IsHidden()&&player->FindInventoryItem(ItemName),"Source delayed give did not call the real GPL idItem pickup route");
        gameLocal.Printf("WOLF2009_SOURCE_ENTITY_NATIVE_LOOT index=%zu delay=1200 delivered=%d native_item=1 inventory=1 source_mask=0x9000 native_query=NativeIdItemBoundsQueryV1 af_blocked=%d\n",index,gameLocal.time-opened,int(state.afHandoffUnsupported));
        idEntityPtr<idEntity> old;old=entity;delete entity;Need(!old.GetEntity()&&Wolf2009_LiveSourceEntities()==1,"Source entity destruction retained its native owner");
        entity=Spawn(path,index,set,binding,owned);Frame();state=State(entity);Need(state.activations==1&&state.persisted&&state.pendingGiveEvents==0&&!state.usable,"Persist respawn did not self-activate without automatic giving");
        idDict actorArgs;actorArgs.Set("classname","wolf2009_source_actor_qa");actorArgs.Set("name","wolf2009_source_opener_qa");actorArgs.SetBool("noclipmodel",true);actorArgs.SetBool("networkSync",false);
        idEntity* actor=gameLocal.SpawnEntityType(idWolfSourceQaActor::Type,&actorArgs);Need(actor!=NULL,"Native generation-control actor failed");const int actorNumber=actor->entityNumber;idEntityPtr<idEntity> actorRef;actorRef=actor;
        const int staleDeadline=gameLocal.time+1200;Need(Wolf2009_ActivateSourceEntity(entity,actor,reason),"Source repeat direct Activate failed");delete actor;
        actorArgs.SetInt("spawn_entnum",actorNumber);idEntity* replacement=gameLocal.SpawnEntityType(idWolfSourceQaActor::Type,&actorArgs);Need(replacement&&replacement->entityNumber==actorNumber&&!actorRef.GetEntity(),"Native entity generation replacement control failed");owned.Add(replacement);
        while(gameLocal.time<=staleDeadline)Frame();state=State(entity);Need(state.giveEvents==1&&state.expiredOpeners==1&&state.activatedItems==0,"Delayed source giving rebound a reused opener slot");
        Need(Wolf2009_ActivateSourceEntity(entity,player,reason)&&State(entity).pendingGiveEvents==1,"Deletion cancellation control did not schedule a pending event");
        idItem* cancelItem=ItemAt(entity,owned,"wolf2009_source_cancel_loot");const int inventoryBeforeCancel=player->inventory.items.Num();
        delete entity;Need(Wolf2009_LiveSourceEntities()==1,"Source entity remained after pending-event deletion");const int cancelDeadline=gameLocal.time+1240;while(gameLocal.time<cancelDeadline)Frame();
        Need(item->IsHidden()&&!cancelItem->IsHidden()&&player->FindInventoryItem(ItemName)&&player->inventory.items.Num()==inventoryBeforeCancel,"Deleted source owner's event activated the control item or changed inventory");
        Need(State(second).phase==WolfSourceEntity_Idle&&State(second).activations==0,"Independent source instance stopped cycling");delete second;Need(Wolf2009_LiveSourceEntities()==0,"Native source entity cleanup retained an instance");
        gameLocal.Printf("WOLF2009_SOURCE_ENTITY_NATIVE_QA_OK checks=%u frames=%u source_index=%zu second_index=%zu idle_cycle=1073741824 activate_anim1=1 give_ms=1200 native_item=1 persist_respawn=1 expired_opener_generation=1 delete_pending=1 original_loot_coverage=0\n",checks,frames,index,secondIndex);
        return true;
    }catch(const std::exception& error){gameLocal.Printf("WOLF2009_SOURCE_ENTITY_NATIVE_QA_FAIL reason=%s\n",error.what());return false;}
}
