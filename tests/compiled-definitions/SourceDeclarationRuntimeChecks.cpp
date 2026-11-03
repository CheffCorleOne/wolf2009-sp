// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in checks inside the real GPL game module. Only two source path strings
// crosses the QA export; all portable C++ objects stay in this game DSO.
#include "CompiledDeclarationCatalog.h"
#include "SourceDefinitions.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationRuntime.h"
std::vector<wolf2009::SourceResolvedLiteral> Wolf2009_CheckSourceDeclarationSetQA(const WolfSourceDeclarationPacket&,const WolfSourceDeclarationPacket&);
std::vector<wolf2009::SourceResolvedLiteral> Wolf2009_CheckSourceDeclarationStartupQA(const char*,const char*,const char*,const char*);
namespace {
struct Stats {std::size_t checks=0,refusals=0,opaqueFields=0,globalOpaqueFields=0,numericFields=0;};
void Need(Stats& s,bool condition,const char* reason){++s.checks;if(!condition)throw std::runtime_error(reason);}
std::uint64_t Hash(const void* data,std::size_t bytes){const auto* p=static_cast<const unsigned char*>(data);std::uint64_t hash=UINT64_C(14695981039346656037);
    for(std::size_t i=0;i<bytes;++i)hash=(hash^p[i])*UINT64_C(1099511628211);return hash;}
struct NativeState {
    int time,previousTime,frame,msec,entityCount,spawned,firstFree,declTypes;
    std::uint64_t entityPointers,spawnIds;
    std::vector<std::pair<int,int>> declarations;
    NativeState():time(gameLocal.time),previousTime(gameLocal.previousTime),frame(gameLocal.framenum),msec(gameLocal.msec),
        entityCount(gameLocal.num_entities),spawned(gameLocal.spawnedEntities.Num()),firstFree(gameLocal.firstFreeIndex),declTypes(declManager->GetNumDeclTypes()),
        entityPointers(Hash(gameLocal.entities,sizeof(gameLocal.entities))),spawnIds(Hash(gameLocal.spawnIds,sizeof(gameLocal.spawnIds))){
        // GetNumDecls on an unregistered enum slot is fatal. Resolve known native
        // type names first instead of assuming all slots below declTypes exist.
        for(const auto* name:{"table","material","skin","sound","entityDef","model","fx","particle","af","pda","video","audio","email","export","mapDef"}){
            const auto type=declManager->GetDeclTypeFromName(name);if(type!=DECL_MAX_TYPES)declarations.push_back({int(type),declManager->GetNumDecls(type)});}
    }
    bool operator==(const NativeState& other)const{return time==other.time&&previousTime==other.previousTime&&frame==other.frame&&msec==other.msec&&
        entityCount==other.entityCount&&spawned==other.spawned&&firstFree==other.firstFree&&declTypes==other.declTypes&&
        entityPointers==other.entityPointers&&spawnIds==other.spawnIds&&declarations==other.declarations;}
};
const wolf2009::SourceResolvedLiteral& Field(Stats& s,const wolf2009::ResolvedSourceDefinition& definition,const char* key){
    const auto* value=definition.Find(key);Need(s,value!=NULL,"native selected compiled field is missing");return *value;
}
std::string Scalar(Stats& s,const wolf2009::ResolvedSourceDefinition& definition,const char* key,wolf2009::SourceLiteralEncoding encoding){
    const auto& field=Field(s,definition,key);Need(s,field.encoding==encoding&&field.value.size()==1&&!field.gameplayImplemented,
        "native compiled field type/value cardinality was guessed or promoted to gameplay");return field.value[0].text;
}
std::string Path(Stats& s,const char* input){Need(s,input!=NULL,"native declarations QA requires a resource path");std::size_t size=0;
    while(size<=4096&&input[size])++size;Need(s,size>0&&size<=4096,"native declarations QA source path byte budget");return std::string(input,size);
}
}
#if defined(_WIN32)
#define WOLF2009_DECLARATIONS_QA_EXPORT __declspec(dllexport)
#else
#define WOLF2009_DECLARATIONS_QA_EXPORT __attribute__((visibility("default")))
#endif
extern "C" WOLF2009_DECLARATIONS_QA_EXPORT int Wolf2009_RunSourceDeclarationsQA(const char* declResourcePath,const char* globalDeclPath,
    const char* mapLocaleDeclPath,const char* globalLocaleDeclPath){
    Stats s;
    try{
        const std::string path=Path(s,declResourcePath),globalPath=Path(s,globalDeclPath),identity="native-selected-trainyard-declarations",
            globalIdentity="native-selected-global-declarations";
        const NativeState initial;
        std::unique_ptr<WolfSourceDeclarationPacket> packet,globalPacket;
        idStr reason;
        {WolfSourceDeclarationRequest request;request.resourcePath=path.c_str();request.loadIdentity=identity.c_str();
            request.policy=WolfSourceDeclaration_SelectedTypedPacketV1;request.role=WolfSourceDeclaration_SelectedMap;
            packet=Wolf2009_LoadSourceDeclarations(request,reason);Need(s,packet.get()!=NULL&&reason.IsEmpty(),"native API8 selected declaration packet load failed");
            request.resourcePath="changed-after-load";request.loadIdentity="changed-after-load";}
        {WolfSourceDeclarationRequest request;request.resourcePath=globalPath.c_str();request.loadIdentity=globalIdentity.c_str();
            request.policy=WolfSourceDeclaration_SelectedTypedPacketV1;request.role=WolfSourceDeclaration_SelectedGlobal;
            globalPacket=Wolf2009_LoadSourceDeclarations(request,reason);
            if(!globalPacket)throw std::runtime_error(std::string("native selected global load failed: ")+reason.c_str());
            Need(s,reason.IsEmpty(),"successful native global packet load retained a failure reason");
            request.resourcePath="changed-global-after-load";request.loadIdentity.Clear();}
        Need(s,NativeState()==initial,"data-only source loader changed native declarations/entities/game clock");
        const auto counts=packet->Counts();const auto& catalog=packet->Catalog();const auto& definitions=packet->Definitions();
        Need(s,counts.packetBytes==2112750&&counts.packetFingerprint==UINT64_C(0xc8d044282ad8a99e)&&counts.entries==3269&&counts.entityDefinitions==974&&
             counts.modelDefinitions==31&&counts.otherDeclarations==2264&&catalog.LoadUnitCount()==1&&catalog.EntryCount()==3269&&
             definitions.DefinitionCount()==1005&&definitions.MemberCount()==1005,"native compiled packet/catalog/frontend counts or source identity differ");
        auto source=catalog.Source(0);auto document=catalog.Document(0);
        Need(s,source->path==path&&source->loadIdentity==identity&&source->role==wolf2009::CompiledCatalogRole::Map&&source->epoch==0&&
             source->byteIdentityKnown&&source->bytes==counts.packetBytes&&source->fingerprint==counts.packetFingerprint&&
             document->pools[0].words.empty()&&document->pools[1].words.size()==1467,
             "native owning catalog lost explicit source/path/pools or invented a mount epoch");
        const auto globalCounts=globalPacket->Counts();const auto& globalCatalog=globalPacket->Catalog();const auto& globalDefinitions=globalPacket->Definitions();
        auto globalSource=globalCatalog.Source(0);auto globalDocument=globalCatalog.Document(0);
        Need(s,globalCounts.packetBytes==2175123&&globalCounts.packetFingerprint==UINT64_C(0xd79bab8fa3531c10)&&globalCounts.entries==3246&&
             globalCounts.entityDefinitions==1191&&globalCounts.modelDefinitions==21&&globalCounts.otherDeclarations==2034&&
             globalCatalog.LoadUnitCount()==1&&globalCatalog.EntryCount()==3246&&globalDefinitions.DefinitionCount()==1212&&globalDefinitions.MemberCount()==1212,
             "native selected global packet/catalog/frontend counts differ");
        Need(s,globalSource->path==globalPath&&globalSource->loadIdentity==globalIdentity&&globalSource->role==wolf2009::CompiledCatalogRole::Global&&
             globalSource->epoch==0&&globalSource->byteIdentityKnown&&globalSource->bytes==globalCounts.packetBytes&&globalSource->fingerprint==globalCounts.packetFingerprint&&
             globalDocument->pools[0].words.empty()&&globalDocument->pools[1].words.size()==1016&&globalSource!=source&&globalDocument!=document&&
             document->pools[1].words.size()==1467&&globalCounts.packetFingerprint!=counts.packetFingerprint,
             "independent global/map documents, roles or prefix pools were merged/aliased");
        for(std::size_t i=0;i<globalCatalog.EntryCount();++i){const auto entry=globalCatalog.Entry(i);
            Need(s,entry.IsUnmodified()&&entry.catalogOrdinal==i&&entry.documentOrdinal==i&&entry.loadOrdinal==0&&entry.document==globalDocument&&entry.source==globalSource,
                 "native selected global catalog changed immutable header/source order");}
        wolf2009::SourceDefinitionItem globalFloat;
        std::shared_ptr<const wolf2009::CompiledCatalogEntry> globalFloatOwner;
        for(std::size_t i=0;i<globalDefinitions.DefinitionCount();++i){const auto& definition=globalDefinitions.Definition(i);
            Need(s,definition.member.compiledRecord&&definition.member.compiledRecord->IsUnmodified(),"native global frontend lost its owning catalog entry");
            for(const auto& item:definition.items){
                if(item.encoding==wolf2009::SourceLiteralEncoding::CompiledOpaque){++s.globalOpaqueFields;
                    Need(s,item.kind==wolf2009::SourceDefinitionItemKind::Literal&&item.values.size()==3&&!item.rawText.empty(),"native global frontend dropped an opaque raw tuple/barrier");}
                if(definition.nameToken.text=="frozen_bullet"&&item.key.text=="forwardOffset"){
                    globalFloat=item;globalFloatOwner=definition.member.compiledRecord;
                }
            }
        }
        const auto globalClass=globalDefinitions.Resolve(wolf2009::SourceDefinitionKind::EntityDef,"ammo_default");
        Need(s,!globalClass.gameplaySupported&&Scalar(s,globalClass,"spawnclass",wolf2009::SourceLiteralEncoding::CompiledString)=="idItem"&&
             Scalar(s,globalClass,"classname",wolf2009::SourceLiteralEncoding::CompiledString)=="ammo_default"&&
             Scalar(s,globalClass,"respawn",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="40"&&
             Scalar(s,globalClass,"density",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="1500",
             "selected actual global class/scalar fields changed");
        const auto globalOwner=globalClass.selected.member.compiledRecord;
        Need(s,globalOwner&&globalOwner->IsUnmodified()&&globalOwner->source==globalSource&&globalOwner->document==globalDocument&&
             globalOwner->Group().sourceFile=="def/ammo.def"&&globalOwner->Record().rawId==33&&globalOwner->Record().textSpan.begin==1614627&&
             globalOwner->Record().textSpan.end==1614843,"global class packet/header provenance changed");
        Need(s,Field(s,globalClass,"respawn").numericWords==std::vector<std::uint32_t>{40}&&
             Field(s,globalClass,"density").numericWords==std::vector<std::uint32_t>{1500},"global signed numeric words differ from known typed literals");s.numericFields+=2;
        Need(s,globalFloatOwner&&globalFloatOwner->IsUnmodified()&&globalFloatOwner->source==globalSource&&globalFloatOwner->document==globalDocument&&
             globalFloat.encoding==wolf2009::SourceLiteralEncoding::CompiledFloat32&&globalFloat.rawValueType==3&&globalFloat.rawValueAux==1&&
             globalFloat.numericWords==std::vector<std::uint32_t>{UINT32_C(0x41a00000)}&&globalFloat.values.size()==3&&globalFloat.values[2].text=="1101004800",
             "actual global frozen_bullet direct float word was decimalized/dropped or assigned another pool");++s.numericFields;
        for(std::size_t i=0;i<catalog.EntryCount();++i){const auto entry=catalog.Entry(i);
            Need(s,entry.IsUnmodified()&&entry.catalogOrdinal==i&&entry.documentOrdinal==i&&entry.loadOrdinal==0&&entry.document==document&&entry.source==source,
                 "native API8 copied catalog record is not immutable or changed source order");}
        for(std::size_t i=0;i<definitions.DefinitionCount();++i){const auto& definition=definitions.Definition(i);
            Need(s,definition.member.compiledRecord&&definition.member.compiledRecord->IsUnmodified(),"native frontend dropped its owning compiled record");
            for(const auto& item:definition.items)if(item.encoding==wolf2009::SourceLiteralEncoding::CompiledOpaque){++s.opaqueFields;
                Need(s,item.kind==wolf2009::SourceDefinitionItemKind::Literal&&item.values.size()==3&&!item.rawText.empty(),"native frontend dropped an unsupported compiled raw tuple/barrier");}}
        const auto container=definitions.Resolve(wolf2009::SourceDefinitionKind::EntityDef,"container_locker_foot_01");
        Need(s,!container.gameplaySupported,"native compiled data frontend claimed container gameplay");
        Need(s,Scalar(s,container,"spawnclass",wolf2009::SourceLiteralEncoding::CompiledString)=="rvAnimatedContainer"&&
             Scalar(s,container,"model",wolf2009::SourceLiteralEncoding::CompiledString)=="model_env_locker_foot_01_anim",
             "native selected container class/model fields differ");
        for(const auto* key:{"noclipmodel","usable","persist","applyHilightTemplate"})
            Need(s,Scalar(s,container,key,wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="1","native selected container signed flags differ");
        Need(s,Scalar(s,container,"hide_finish",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="0"&&
             Scalar(s,container,"giveDelay",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="1200"&&
             Scalar(s,container,"health",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="10000","native selected container signed delay/health differ");
        const auto& delay=Field(s,container,"giveDelay");Need(s,!delay.origins.empty(),"native compiled delay has no provenance");
        const auto& delayOrigin=delay.origins[0];const auto owner=delayOrigin.origin.member.compiledRecord;
        Need(s,owner&&owner->IsUnmodified()&&owner->source==source&&owner->Record().rawId==1682&&owner->Record().textSpan.begin==1710675&&
             owner->Record().textSpan.end==1711184&&delayOrigin.data.values.size()==3&&delayOrigin.data.values[0].text=="4"&&delayOrigin.data.values[1].text=="1"&&
             delayOrigin.origin.begin<=delayOrigin.origin.end&&delayOrigin.origin.end<=owner->Record().text.size(),"native compiled tuple/source packet provenance differs");
        Need(s,std::string(owner->Record().text.begin()+delayOrigin.origin.begin,owner->Record().text.begin()+delayOrigin.origin.end)==delayOrigin.data.rawText,
             "native frontend confused record-local and packet-global spans");
        const auto references=definitions.ResolveModelAnimations("model_env_locker_foot_01_anim");std::size_t pure=0,blocks=0;
        for(const auto& entry:references.references){const auto& reference=entry.second;
            pure+=reference.shape==wolf2009::SourceAnimationReferenceShape::SingleLiteralNoTail;
            blocks+=reference.shape==wolf2009::SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock;
            Need(s,!reference.runtimeImplemented&&!reference.origins.empty()&&reference.origins[0].origin.member.compiledRecord&&
                 reference.origins[0].origin.member.compiledRecord->IsUnmodified(),"native model animation reference lost source ownership or claimed execution");}
        const auto* open=references.Find("open");
        Need(s,references.references.size()==6&&pure==4&&blocks==2&&!references.gameplaySupported&&open&&open->hasLiteralAssetReference&&
             open->assetReference.text=="models/props/locker_foot_01_anim_open.md5anim","native model references changed or inferred an HKXA extension rewrite");
        auto locker=catalog.Lookup("model","model_env_locker_foot_01_anim");
        Need(s,locker.status==wolf2009::CompiledCatalogLookupStatus::Unique&&locker.matches[0].Group().defaultTypeId==3&&locker.matches[0].Record().rawId==334,
             "native catalog used group defaultType/rawId as actual payload type");
        for(const auto* suffix:{"idle","open","idle_open","close"}){
            Need(s,catalog.Lookup("animBlendTree",std::string("models/props/locker_foot_01_anim_")+suffix+".md5anim",wolf2009::CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).status==wolf2009::CompiledCatalogLookupStatus::Missing,
                 "selected packet contains an explicit queried locker tree header");
            Need(s,globalCatalog.Lookup("animBlendTree",std::string("models/props/locker_foot_01_anim_")+suffix+".md5anim",wolf2009::CompiledCatalogNamePolicy::AsciiCaseSlashLastDotV1).status==wolf2009::CompiledCatalogLookupStatus::Missing,
                 "selected global packet contains an explicit queried locker tree header");}
        const auto effectiveFacts=Wolf2009_CheckSourceDeclarationSetQA(*packet,*globalPacket);
        Need(s,effectiveFacts.size()==3&&NativeState()==initial,"Effective declaration set changed native registry/entities/clock");
        const auto startupFacts=Wolf2009_CheckSourceDeclarationStartupQA(path.c_str(),globalPath.c_str(),
            Path(s,mapLocaleDeclPath).c_str(),Path(s,globalLocaleDeclPath).c_str());
        Need(s,startupFacts.size()==2&&NativeState()==initial,"Compiled startup changed native registry/entities/clock");
        const auto rejected=[&](const WolfSourceDeclarationRequest& request){idStr failure="must-be-replaced";
            auto result=Wolf2009_LoadSourceDeclarations(request,failure);Need(s,!result&&!failure.IsEmpty()&&failure!="must-be-replaced","invalid source declaration request published a result or omitted its reason");
            Need(s,NativeState()==initial&&packet->Catalog().EntryCount()==3269&&packet->Definitions().DefinitionCount()==1005&&
                 globalPacket->Catalog().EntryCount()==3246&&globalPacket->Definitions().DefinitionCount()==1212,
                 "refused source declaration load changed existing owning data/native state");++s.refusals;};
        WolfSourceDeclarationRequest bad;bad.resourcePath=path.c_str();bad.loadIdentity="native-declarations-refusal";bad.role=WolfSourceDeclaration_SelectedMap;
        rejected(bad);bad.policy=static_cast<WolfSourceDeclarationPolicy>(99);rejected(bad);
        bad.policy=WolfSourceDeclaration_SelectedTypedPacketV1;bad.role=WolfSourceDeclaration_RoleUnspecified;rejected(bad);
        bad.role=WolfSourceDeclaration_SelectedMap;bad.resourcePath=(path+"/__source_declaration_qa_missing__").c_str();rejected(bad);
        bad.resourcePath="fixtures/not-a-qualified-source-packet";rejected(bad);bad.resourcePath=path.c_str();bad.loadIdentity.Clear();rejected(bad);
        bad.loadIdentity="native-global-refusal";bad.role=WolfSourceDeclaration_SelectedGlobal;
        for(const auto* invalid:{"../global.decls","/global.decls","C:/global.decls","fixtures/global.txt","wolf2009/main/DECL/global.decls"}){
            bad.resourcePath=invalid;rejected(bad);}
        bad.resourcePath=(globalPath+".missing.decls").c_str();rejected(bad);
        bad.role=WolfSourceDeclaration_SelectedMap;bad.resourcePath=globalPath.c_str();rejected(bad);
        // Every result is independently owned after the request and API8 file
        // are gone; copied resolved facts also outlive the whole runtime handle.
        locker.matches.clear();source.reset();document.reset();packet.reset();
        Need(s,globalPacket->Counts().entries==3246&&globalPacket->Catalog().Document(0)==globalDocument&&globalPacket->Definitions().DefinitionCount()==1212,
             "destroying the selected map handle invalidated the independent global handle");
        globalSource.reset();globalDocument.reset();globalPacket.reset();
        Need(s,startupFacts[0].numericWords==std::vector<std::uint32_t>({140})&&startupFacts[1].numericWords==std::vector<std::uint32_t>({1200})&&
             startupFacts[0].origins[0].origin.member.compiledRecord->IsUnmodified()&&startupFacts[1].origins[0].origin.member.compiledRecord->IsUnmodified(),
             "Startup facts lost source ownership after all callback packet handles died");
        Need(s,effectiveFacts[0].origins[0].origin.member.compiledRecord->IsUnmodified()&&
             effectiveFacts[0].origins[0].origin.member.compiledRecord->document->pools[1].words.size()==1016&&
             effectiveFacts[1].origins[0].origin.member.compiledRecord->IsUnmodified()&&
             effectiveFacts[1].origins[0].origin.member.compiledRecord->document->pools[1].words.size()==1467&&
             effectiveFacts[2].numericWords==std::vector<std::uint32_t>({1200}),"Effective set facts lost owning pools after all input handles died");
        Need(s,owner->IsUnmodified()&&owner->source->path==path&&owner->source->epoch==0&&owner->document->pools[1].words.size()==1467&&
             Scalar(s,container,"giveDelay",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="1200"&&references.Find("open")!=NULL,
             "native source declaration ownership died with the loader handle");
        Need(s,globalOwner->IsUnmodified()&&globalOwner->source->path==globalPath&&globalOwner->source->role==wolf2009::CompiledCatalogRole::Global&&
             globalOwner->source->epoch==0&&globalOwner->document->pools[1].words.size()==1016&&
             Scalar(s,globalClass,"respawn",wolf2009::SourceLiteralEncoding::CompiledSignedInteger)=="40"&&
             globalFloatOwner->IsUnmodified()&&globalFloat.numericWords[0]==UINT32_C(0x41a00000),
             "global owning class/numeric provenance died with its independent loader handle");
        Need(s,NativeState()==initial,"source declaration destruction changed native registry/entities/clock");
        printf("WOLF2009_NATIVE_SOURCE_DECLARATIONS_QA_OK checks=%zu refusals=%zu entries=%zu entitydefs=%zu models=%zu imports=1005 opaque_fields=%zu model_refs=6 pure=4 blocks=2 selected_missing_trees=4 bytes=%zu fingerprint=%llx global_entries=%zu global_entitydefs=%zu global_models=%zu global_imports=1212 global_opaque_fields=%zu global_bytes=%zu global_fingerprint=%llx numeric_fields=%zu selected_global_missing_trees=4 independent_namespaces=1 owning_after_file_and_handle=1 epoch=0 mount_pin_claim=0 native_registry_import=0 gameplay=0 runtime_namespace_absence=0\n",
            s.checks,s.refusals,counts.entries,counts.entityDefinitions,counts.modelDefinitions,s.opaqueFields,counts.packetBytes,static_cast<unsigned long long>(counts.packetFingerprint),
            globalCounts.entries,globalCounts.entityDefinitions,globalCounts.modelDefinitions,s.globalOpaqueFields,globalCounts.packetBytes,static_cast<unsigned long long>(globalCounts.packetFingerprint),s.numericFields);
        return 1;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_DECLARATIONS_FAIL checks=%zu message=%s\n",s.checks,error.what());}
    catch(...){fprintf(stderr,"WOLF2009_NATIVE_SOURCE_DECLARATIONS_FAIL checks=%zu message=native-exception\n",s.checks);}return 0;
}
