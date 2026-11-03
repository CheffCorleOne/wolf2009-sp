// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <limits>
#include <memory>
#include <iomanip>
#include <locale>
#include <sstream>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "EngineAnimationGame.h"
#include "SourceVisualEntity.h"

namespace {
int liveVisuals=0;
const int MaximumVisuals=128;
bool SafeName(const idStr& name) {
    if (name.Length()<1 || name.Length()>127 || name=="NULL" || name=="null_entity") return false;
    for (int i=0;i<name.Length();++i) {
        const unsigned char c=name[i];
        if (!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || c=='_' || (i>0&&c>='0'&&c<='9'))) return false;
    }
    return true;
}
bool Placement(const WolfSourceVisualRequest& request) {
    for (int i=0;i<3;++i) {
        if (!std::isfinite(request.origin[i]) || std::abs(request.origin[i])>10000000.0f) return false;
        for (int j=0;j<3;++j) if (!std::isfinite(request.axis[i][j])) return false;
    }
    for (int i=0;i<3;++i) for (int j=0;j<3;++j)
        if (std::abs(request.axis[i]*request.axis[j]-(i==j?1.0f:0.0f))>0.0001f) return false;
    return std::abs(request.axis.Determinant()-1.0f)<=0.0001f;
}
const idAnim* Clip(const idDeclModelDef* definition) {
    if (!definition || definition->NumAnims()!=2 || !definition->ModelHandle() || definition->ModelHandle()->IsDefaultModel()) return NULL;
    const int number=definition->GetSpecificAnim("source");
    const idAnim* clip=number>0 ? definition->GetAnim(number) : NULL;
    if (!clip || clip->NumAnims()!=1 || clip->HasFrameCommands() || !clip->UsesSourceAnimation()) return NULL;
    const idMD5Anim* source=clip->MD5Anim(0);
    if (!source || !source->IsSourceAnimation() || !source->SourceMatchesModel(definition->ModelHandle()) || clip->Length()<1) return NULL;
    return clip;
}
bool Refuse(idStr& reason,const char* text) { reason=text; return false; }
std::string PlacementWords(const float* values,int count) {
    // idDict::SetVector/SetMatrix use native ToString's two decimal places.
    // Preserve the supplied float placement through the spawn dictionary.
    std::ostringstream text;text.imbue(std::locale::classic());
    text<<std::setprecision(std::numeric_limits<float>::max_digits10);
    for(int i=0;i<count;++i) { if(i) text<<' ';text<<values[i]; }
    return text.str();
}
}

class idWolfSourceVisual : public idAnimatedEntity {
  public:
    CLASS_PROTOTYPE(idWolfSourceVisual);
    idWolfSourceVisual() : bound(false) { ++liveVisuals; animator.RemoveOriginOffset(false); }
    ~idWolfSourceVisual() { --liveVisuals; }
    void SetModel(const char* name) override {
        // Native idAnimatedEntity::SetModel starts by freeing its previous model.
        // Refuse before that call, including later script/editor model changes.
        const idDeclModelDef* definition=Wolf2009_FindRegisteredSourceModelDef(name);
        if (bound || !Clip(definition)) {
            gameLocal.Warning("Source visual SetModel refused before mutation"); return;
        }
        idAnimatedEntity::SetModel(name);
    }
    bool Begin(int start) {
        if (bound || !Clip(animator.ModelDef()) || animator.RemoveOrigin()) return false;
        const int number=animator.GetAnim("source");
        animator.PlayAnim(ANIMCHANNEL_ALL,number,start,0);
        const idAnimBlend* channel=animator.CurrentAnim(ANIMCHANNEL_ALL);
        if (!channel || channel->GetCycleCount()!=1 || channel->GetEndTime()!=start+channel->Anim()->Length()) return false;
        bound=true;
        BecomeActive(TH_ANIMATE);
        UpdateVisuals();
        return true;
    }
    // Source model/clip/clock/owner serialization needs its own versioned policy.
    void Save(idSaveGame*) const { gameLocal.Error("Source visual save-state policy is unsupported"); }
    void Restore(idRestoreGame*) { gameLocal.Error("Source visual restore policy is unsupported"); }
  private:
    bool bound;
};
CLASS_DECLARATION(idAnimatedEntity,idWolfSourceVisual)
END_CLASS

bool Wolf2009_SpawnSourceVisual(const WolfSourceVisualRequest& request,idAnimatedEntity*& out,idStr& reason) {
    out=NULL; reason.Clear();
    if (gameLocal.GameState()!=GAMESTATE_ACTIVE || gameLocal.isClient || gameLocal.isMultiplayer)
        return Refuse(reason,"source visual requires an active local single-player game");
    if (request.policy!=WolfSourceVisual_OneShotNativeMillisecondsV1)
        return Refuse(reason,"source visual requires an explicit one-shot native-ms policy");
    if (!SafeName(request.name) || gameLocal.FindEntity(request.name))
        return Refuse(reason,"source visual name is invalid, reserved or already occupied");
    if (!Placement(request)) return Refuse(reason,"source visual placement must be finite and rigid");
    if (liveVisuals>=MaximumVisuals) return Refuse(reason,"source visual live-entity budget exceeded");
    bool freeSlot=false;
    for (int i=MAX_CLIENTS;i<ENTITYNUM_MAX_NORMAL;++i) if (!gameLocal.entities[i]) { freeSlot=true; break; }
    if (!freeSlot) return Refuse(reason,"native entity slots exhausted");
    if (request.modelDefinition.Length()>4096) return Refuse(reason,"source visual modelDef name budget exceeded");
    const idDeclModelDef* definition=Wolf2009_FindRegisteredSourceModelDef(request.modelDefinition);
    const idAnim* clip=Clip(definition);
    if (!clip) return Refuse(reason,"source visual requires an unchanged registered source modelDef/clip");
    if (request.startTimeMs<0 || request.startTimeMs>std::numeric_limits<int>::max()-clip->Length())
        return Refuse(reason,"source visual one-shot native-ms range overflow");

    idDict args;
    args.Set("classname","wolf2009_source_visual");
    args.Set("name",request.name);
    args.Set("model",request.modelDefinition);
    args.Set("origin",PlacementWords(request.origin.ToFloatPtr(),3).c_str());
    args.Set("rotation",PlacementWords(request.axis.ToFloatPtr(),9).c_str());
    args.SetBool("noclipmodel",true);
    args.SetBool("neverDormant",true);
    args.SetBool("cinematic",true);
    args.SetBool("networkSync",false);
    std::unique_ptr<idWolfSourceVisual> entity(static_cast<idWolfSourceVisual*>(gameLocal.SpawnEntityType(idWolfSourceVisual::Type,&args)));
    if (!entity || entity->GetAnimator()->ModelDef()!=definition || !entity->Begin(request.startTimeMs))
        return Refuse(reason,"source visual native spawn/model/one-shot initialization failed");
    out=entity.release(); return true;
}
bool Wolf2009_IsSourceVisualEntity(const idEntity* entity) { return entity && entity->GetType()==&idWolfSourceVisual::Type; }
int Wolf2009_LiveSourceVisualEntities() { return liveVisuals; }
