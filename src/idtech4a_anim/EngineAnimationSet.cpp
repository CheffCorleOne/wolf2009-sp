// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineAnimationSet.h"
#include "AnimationWireV2.h"
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
const std::size_t MaximumClips=64,MaximumText=64u*1024u,MaximumSets=4096;
struct SetRecord {
    std::string text,model;
    std::vector<WolfSourceAnimationSetEntry> entries;
    bool discoverable=true;
};
std::map<std::string,SetRecord> sets;
void Need(bool condition,const char* reason) { if(!condition)throw std::runtime_error(reason); }
const idDecl* WithoutParsing(const char* name) {
    const idDecl* found=declManager->FindDeclWithoutParsing(DECL_MODELDEF,name,false);
    if(found)return found;
    // FindDeclWithoutParsing returns a null self for a catalogued declaration
    // whose native object has not been allocated yet. The generated names have
    // no extension and are already canonical. Check that namespace without
    // parsing foreign text or replacing a live declaration's animation data.
    for(int i=0;i<declManager->GetNumDecls(DECL_MODELDEF);++i){
        const idDecl* candidate=declManager->DeclByIndex(DECL_MODELDEF,i,false);
        if(candidate&&idStr::Icmp(candidate->GetName(),name)==0)return candidate;
    }
    return NULL;
}
bool TextMatches(const idDecl* definition,const std::string& expected) {
    if(!definition||definition->GetTextLength()!=int(expected.size()))return false;
    std::vector<char> text(expected.size()+1,0);definition->GetText(text.data());
    return std::string(text.data(),expected.size())==expected;
}
bool LogicalName(const std::string& name) {
    if(name.empty()||name.size()>127)return false;
    for(std::size_t i=0;i<name.size();++i){const unsigned char c=name[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||(i>0&&c>='0'&&c<='9')))return false;
    }
    return true;
}
bool QuotedPath(const std::string& name) {
    if(name.empty()||name.size()>4096)return false;
    for(unsigned char c:name)if(c<33||c>=127||c=='"'||c=='\\'||c=='{'||c=='}')return false;
    return true;
}
std::string BoundedPath(const char* name) {
    Need(name!=NULL,"animation set path is absent");
    std::size_t length=0;while(length<=4096&&name[length])++length;
    Need(length<=4096,"animation set path budget exceeded");
    const std::string result(name,length);
    Need(QuotedPath(result),"animation set path is unsafe");return result;
}
void Append(std::string& text,const std::string& value) {
    Need(value.size()<=MaximumText-text.size(),"animation set declaration text budget exceeded");
    text+=value;
}
bool FlagsAbsent(const idAnim* clip) {
    const animFlags_t& flags=clip->GetAnimFlags();
    return !flags.prevent_idle_override&&!flags.random_cycle_start&&!flags.ai_no_turn&&!flags.anim_turn;
}
std::string Alias(const std::string& text) {
    std::uint64_t value=wolf2009::animation2::Fingerprint(
        reinterpret_cast<const unsigned char*>(text.data()),text.size());
    std::string hex(16,'0');for(int i=15;i>=0;--i){hex[std::size_t(i)]="0123456789abcdef"[value&15];value>>=4;}
    return "wolf2009/animation-model-set/"+hex;
}
struct AnimationRefs {
    std::vector<const idMD5Anim*> values;
    explicit AnimationRefs(std::size_t count) { values.reserve(count); }
    void Hold(const idMD5Anim* animation) { values.push_back(animation);animation->IncreaseRefs(); }
    ~AnimationRefs() { for(const auto* value:values)value->DecreaseRefs(); }
};
bool Matches(const idDeclModelDef* definition,const SetRecord& record) {
    if(!definition||definition->GetState()==DS_DEFAULTED||!TextMatches(definition,record.text)||
       definition->NumAnims()!=int(record.entries.size()+1)||!definition->UsesSourceAnimation())return false;
    const idRenderModel* model=definition->ModelHandle();
    if(!model||!model->IsLoaded()||model->IsDefaultModel()||record.model!=model->Name()||
       model->NumJoints()<1||model->NumJoints()>256||definition->Joints().Num()!=model->NumJoints())return false;
    for(const auto& entry:record.entries){
        const int index=definition->GetSpecificAnim(entry.logicalName.c_str());
        const idAnim* clip=index>0?definition->GetAnim(index):NULL;
        if(!clip||clip->NumAnims()!=1||clip->HasFrameCommands()||!FlagsAbsent(clip)||
           entry.logicalName!=clip->FullName())return false;
        const idMD5Anim* animation=clip->MD5Anim(0);
        if(!animation||!animation->IsSourceAnimationV2()||entry.animationAlias!=animation->Name()||
           record.model!=animation->SourceModelAlias()||!animation->SourceMatchesModel(model)||
           animation->NumJoints()!=model->NumJoints()||animation->NumFrames()<1||animation->Length()<1)return false;
    }
    return true;
}
}

bool Wolf2009_CreateSourceAnimationSet(const std::vector<WolfSourceAnimationSetEntry>& entries,
    const char* appearanceAlias,idStr& out,idStr& reason) {
    out.Clear();reason.Clear();
    try {
        Need(!entries.empty()&&entries.size()<=MaximumClips,"animation set clip count must be 1..64");
        std::set<std::string> logicalNames;
        for(const auto& entry:entries){
            Need(LogicalName(entry.logicalName),"animation set logical name is unsafe");
            Need(logicalNames.insert(entry.logicalName).second,"animation set repeats an exact logical name");
            Need(QuotedPath(entry.animationAlias),"animation set source alias is unsafe");
        }
        SetRecord record;record.entries=entries;
        AnimationRefs references(entries.size());
        const idRenderModel* model=NULL;
        for(const auto& entry:entries){
            idMD5Anim* animation=animationLib.GetAnim(entry.animationAlias.c_str());
            Need(animation&&animation->IsSourceAnimationV2(),"animation set requires qualified source-v2 clips");
            references.Hold(animation);
            const std::string actual=BoundedPath(animation->SourceModelAlias());
            if(!model){record.model=actual;model=renderModelManager->CheckModel(actual.c_str());}
            Need(model&&model->IsLoaded()&&!model->IsDefaultModel()&&actual==record.model&&
                 record.model==model->Name()&&animation->SourceMatchesModel(model),
                 "animation set clips must match one actual source model and full hierarchy");
            Need(model->NumJoints()>=1&&model->NumJoints()<=256&&animation->NumJoints()==model->NumJoints()&&
                 animation->NumFrames()>=1&&animation->Length()>=1,"animation set clip count/time is invalid");
        }
        Append(record.text,"{\n mesh \"");Append(record.text,record.model);Append(record.text,"\"\n");
        if(appearanceAlias&&*appearanceAlias){
            const std::string skinName=BoundedPath(appearanceAlias);
            const idDeclSkin* skin=declManager->FindSkin(skinName.c_str(),false);
            Need(skin&&skin->GetState()!=DS_DEFAULTED,"animation set appearance is not a qualified native skin");
            Append(record.text," skin \"");Append(record.text,skinName);Append(record.text,"\"\n");
        }
        for(const auto& entry:entries){
            Append(record.text," anim ");Append(record.text,entry.logicalName);Append(record.text," \"");
            Append(record.text,entry.animationAlias);Append(record.text,"\"\n");
        }
        Append(record.text,"}\n");
        const std::string name=Alias(record.text);
        auto prior=sets.find(name);
        Need(prior==sets.end()||prior->second.text==record.text,
             "animation set fingerprint collision with different full declaration");
        Need(prior!=sets.end()||sets.size()<MaximumSets,"animation set registry budget exceeded");
        if(prior==sets.end()){
            Need(!WithoutParsing(name.c_str()),"animation set native namespace is already occupied");
            idDecl* created=declManager->CreateNewDecl(DECL_MODELDEF,name.c_str(),"<wolf2009-source-animation-set>");
            Need(created!=NULL,"animation set native declaration allocation failed");
            created->SetText(record.text.c_str());created->Invalidate();
        }else{
            Need(TextMatches(WithoutParsing(name.c_str()),record.text),"animation set native declaration text changed");
        }
        const auto* definition=static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name.c_str(),false));
        Need(Matches(definition,record),"animation set native parse/identity validation failed");
        if(prior==sets.end())sets.insert(std::make_pair(name,std::move(record)));
        else prior->second.discoverable=true;
        out=name.c_str();return true;
    }catch(const std::exception& error){reason=error.what();return false;}
}

const idDeclModelDef* Wolf2009_FindRegisteredSourceAnimationSet(const char* name) {
    if(!name)return NULL;
    const auto found=sets.find(name);if(found==sets.end()||!found->second.discoverable)return NULL;
    // FindType can FreeData/reparse. Compare immutable text first, while a live
    // animator can still hold this declaration and its previous channels.
    if(!TextMatches(WithoutParsing(name),found->second.text))return NULL;
    const auto* definition=static_cast<const idDeclModelDef*>(declManager->FindType(DECL_MODELDEF,name,false));
    return Matches(definition,found->second)?definition:NULL;
}
bool Wolf2009_ForgetSourceAnimationSet(const char* name) {
    if(!name)return false;
    const auto found=sets.find(name);if(found==sets.end()||!found->second.discoverable)return false;
    found->second.discoverable=false;return true;
}
void Wolf2009_ClearAnimationSetRegistry() { sets.clear(); }
