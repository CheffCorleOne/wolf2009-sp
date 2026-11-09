// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceAIVisuals.h"
#include "SourceActorBinding.h"
#include "Entities.h"
#include <array>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"

namespace {
namespace actor = wolf2009::actor;
const char GlobalSkeletons[] = "wolf2009/sp_global/skel/maps/game/global/global";
const char TrainyardSkeletons[] = "wolf2009/main/skel/maps/game/trainyard/trainyard";
const char BodyRig[] = "models/characters/_humanoid_male/empty_male_skel.hkx";

struct SourceActor {
    const char* name;
    const char* classname;
    const char* model;
    const char* head;
    const char* pack;
    const char* mount;
    unsigned headJoints;
    const char* attachment;
    const char* hat;
};

const SourceActor TrainyardActors[] = {
    {"nazi_interragator_1", "trainyard_nazi_talker_1", "model_new_wehrmacht_infantry_v2",
     "heads_enemy_02", "wehrmacht_infantry_mp40", "sp_ai_wehrmacht", 32, "attach_wermacht_helmet_v2", ""},
    {"nazi_interragator_2", "trainyard_nazi_talker_2", "model_new_wehrmacht_infantry_v2",
     "heads_enemy_02", "wehrmacht_infantry_mp40", "sp_ai_wehrmacht", 32, "attach_wermacht_helmet_v2", ""},
    {"trainyard_hostage_1", "trainyard_allies01", "model_civilian_male_02",
     "heads_abm_agt02", "allies_kreisau_agt01", "sp_ai_agt01", 27, "attach_holster", "attach_civilian_hat_1a"},
    {"trainyard_hostage_2", "trainyard_allies02", "model_civilian_male_04",
     "heads_civilian_male_03", "allies_kreisau_agt02", "sp_greeter", 27, "attach_pouch", ""},
    {"train_greeter", "trainyard_greeter", "model_civilian_male_04",
     "heads_civilian_male_03", "allies_kreisau_agt02", "sp_greeter", 27, "attach_pouch", ""}
};

void Need(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

std::string Fold(std::string text) {
    for (char& c : text) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return text;
}

std::string Scalar(const wolf2009::ResolvedSourceDefinition& definition, const char* key,
                   bool required = true) {
    const auto* field = definition.Find(key);
    if (!field) {
        Need(!required, key);
        return std::string();
    }
    Need(field->value.size() == 1 &&
         (field->encoding == wolf2009::SourceLiteralEncoding::PlainText ||
          field->encoding == wolf2009::SourceLiteralEncoding::CompiledString ||
          field->encoding == wolf2009::SourceLiteralEncoding::CompiledSignedInteger), key);
    return field->value[0].text;
}

wolf2009::ResolvedSourceDefinition Resolve(const WolfSourceDeclarationSet& set,
        wolf2009::SourceDefinitionKind kind, const std::string& name) {
    wolf2009::SourceDefinitionContext context;
    context.mapModelDefinition = "model_trainyard_base";
    auto definition = set.Definitions().Resolve(kind, name, context);
    Need(!definition.declarations.empty(), "SourceAIShellV1 has no owning declaration");
    for (const auto& origin : definition.declarations) {
        const auto& entry = origin.member.compiledRecord;
        const auto selected = set.Namespace().Lookup(origin.declarationKind, origin.declarationName);
        Need(entry && entry->IsUnmodified() && selected.status == wolf2009::CompiledCatalogLookupStatus::Unique &&
             selected.matches.size() == 1 && selected.matches[0].IsUnmodified() &&
             selected.matches[0].document == entry->document &&
             selected.matches[0].catalogOrdinal == entry->catalogOrdinal,
             "SourceAIShellV1 declaration escaped the selected compiled namespace");
    }
    return definition;
}

std::string ModlPath(const char* mount, const std::string& mesh) {
    const auto name = Fold(mesh);
    Need(name.compare(0, 7, "models/") == 0 && name.size() > 5 &&
         name.compare(name.size() - 5, 5, ".md5r") == 0,
         "SourceAIShellV1 expects an original MD5R model reference");
    return std::string("wolf2009/") + mount + "/modl/" + name;
}

struct CloseFile {
    void operator()(idFile* file) const { if (file) fileSystem->CloseFile(file); }
};
using File = std::unique_ptr<idFile, CloseFile>;

struct NativeMesh {
    File capability;
    std::string definition;
    idRenderModel* model = nullptr;
    const idDeclSkin* skin = nullptr;
};

NativeMesh PrepareMesh(const actor::ShellMeshRequest& request) {
    NativeMesh mesh;
    const auto path = actor::ShellMeshRequestPath(request);
    mesh.capability.reset(fileSystem->OpenFileRead(path.c_str(), false));
    if (!mesh.capability) throw std::runtime_error("SourceAIShellV1 mesh refused: " + request.modelResource);
    const int size = mesh.capability->Length();
    Need(size > 0 && std::size_t(size) <= actor::MaximumCapabilityBytes, "SourceAIShellV1 capability size");
    actor::Bytes bytes(static_cast<std::size_t>(size));
    Need(mesh.capability->Read(bytes.data(), size) == size, "SourceAIShellV1 short capability read");
    const auto binding = actor::ParseShellMeshBinding(bytes, request);
    mesh.model = renderModelManager->CheckModel(binding.modelAlias.c_str());
    Need(mesh.model && mesh.model->IsLoaded() && !mesh.model->IsDefaultModel() &&
         mesh.model->NumJoints() == int(request.joints) && binding.modelAlias == mesh.model->Name(),
         "SourceAIShellV1 resident model differs from the qualified mesh");
    if (!binding.skinAlias.empty()) {
        mesh.skin = declManager->FindSkin(binding.skinAlias.c_str(), false);
        Need(mesh.skin && mesh.skin->GetState() != DS_DEFAULTED, "SourceAIShellV1 native skin missing");
    }

    // One immutable private modelDef per qualified mesh and skin. Native
    // idAnimator evaluates its exact reference pose for this shell profile.
    mesh.definition = binding.modelAlias + "/source-ai-shell";
    if (!binding.skinAlias.empty())
        mesh.definition += "/" + binding.skinAlias.substr(binding.skinAlias.rfind('/') + 1);
    const std::string text = "{\n mesh \"" + binding.modelAlias + "\"\n}\n";
    const auto* decl = declManager->FindDeclWithoutParsing(DECL_MODELDEF, mesh.definition.c_str(), false);
    if (!decl) {
        auto* created = declManager->CreateNewDecl(DECL_MODELDEF, mesh.definition.c_str(), "<wolf2009-source-ai-shell>");
        Need(created != nullptr, "SourceAIShellV1 modelDef allocation failed");
        created->SetText(text.c_str());
        created->Invalidate();
        decl = created;
    }
    Need(decl->GetTextLength() == int(text.size()), "SourceAIShellV1 private modelDef size differs");
    std::vector<char> existing(static_cast<std::size_t>(decl->GetTextLength()) + 1, 0);
    decl->GetText(existing.data());
    Need(text == existing.data(), "SourceAIShellV1 private modelDef namespace occupied");
    const auto* modelDef = static_cast<const idDeclModelDef*>(
        declManager->FindType(DECL_MODELDEF, mesh.definition.c_str(), false));
    Need(modelDef && modelDef->GetState() != DS_DEFAULTED && modelDef->ModelHandle() == mesh.model &&
         modelDef->NumAnims() == 1, "SourceAIShellV1 native reference-pose declaration differs");
    return mesh;
}

idVec3 Vector(const wolf2009::ResolvedSourceDefinition& definition, const char* key) {
    const auto* field = definition.Find(key);
    Need(field != nullptr, key);
    idVec3 value;
    if (field->encoding == wolf2009::SourceLiteralEncoding::CompiledFloatArray && field->numericWords.size() == 3) {
        for (int i = 0; i < 3; ++i) std::memcpy(&value[i], &field->numericWords[i], sizeof(float));
    } else {
        std::istringstream text(Scalar(definition, key));
        text.imbue(std::locale::classic());
        char extra;
        Need(bool(text >> value.x >> value.y >> value.z) && !(text >> extra), key);
    }
    for (int i = 0; i < 3; ++i) Need(std::isfinite(value[i]) && std::abs(value[i]) <= 360.f, key);
    return value;
}

template<class Entity>
Entity* Spawn(const std::string& name, const std::string& model, const idDeclSkin* skin = nullptr) {
    Need(!gameLocal.FindEntity(name.c_str()), "SourceAIShellV1 native name already occupied");
    idDict args;
    args.Set("name", name.c_str());
    args.Set("model", model.c_str());
    args.Set("noclipmodel", "1");
    args.Set("solid", "0");
    if (skin) args.Set("skin", skin->GetName());
    auto* entity = static_cast<Entity*>(gameLocal.SpawnEntityType(Entity::Type, &args));
    Need(entity != nullptr, "SourceAIShellV1 native entity allocation failed");
    entity->GetPhysics()->SetContents(0);
    return entity;
}

jointHandle_t Joint(idAnimatedEntity& entity, const std::string& name) {
    const auto joint = entity.GetAnimator()->GetJointHandle(name.c_str());
    Need(joint != INVALID_JOINT, "SourceAIShellV1 exact joint absent");
    return joint;
}

void Bind(idEntity& child, idAnimatedEntity& parent, jointHandle_t joint,
          const idVec3& localOrigin, const idMat3& localAxis) {
    child.BindToJoint(&parent, joint, true);
    Need(child.GetBindMaster() == &parent && child.GetBindJoint() == joint,
         "SourceAIShellV1 native joint bind refused");
    child.SetAxis(localAxis);
    child.SetOrigin(localOrigin);
}

struct AccessoryPlan {
    std::string model;
    std::string joint;
    idVec3 origin;
    idMat3 axis;
    bool head = false;
};
AccessoryPlan PrepareAccessory(const WolfSourceDeclarationSet& set, const char* name) {
    const auto definition = Resolve(set, wolf2009::SourceDefinitionKind::EntityDef, name);
    AccessoryPlan result;
    result.model = ModlPath("main", Scalar(definition, "model"));
    result.joint = Scalar(definition, "joint");
    result.origin = Vector(definition, "origin");
    const auto angles = Vector(definition, "angles");
    result.axis = idAngles(angles.x, angles.y, angles.z).ToMat3();
    result.head = result.joint == "head";
    Need(result.head || result.joint == "chest" || result.joint == "hips",
         "SourceAIShellV1 unsupported accessory joint");
    const auto oriented = Scalar(definition, "oriented", false);
    Need(oriented.empty() || oriented == "1", "SourceAIShellV1 unsupported accessory orientation");
    auto* model = renderModelManager->CheckModel(result.model.c_str());
    if (!model || !model->IsLoaded() || model->IsDefaultModel() ||
        model->IsDynamicModel() != DM_STATIC || model->NumSurfaces() <= 0)
        throw std::runtime_error("SourceAIShellV1 rigid accessory MODL refused: " + result.model);
    return result;
}
}

struct WolfSourceAIVisuals::Impl {
    NativeMesh bodyMesh, headMesh;
    idEntityPtr<idAnimatedEntity> body, head;
    std::array<jointHandle_t, 3> bodyCopy, headCopy;
    std::vector<idEntityPtr<idEntity> > accessories;
    std::vector<AccessoryPlan> accessoryPlans;
    ~Impl() {
        for (auto child = accessories.rbegin(); child != accessories.rend(); ++child)
            delete child->GetEntity();
        delete head.GetEntity();
        delete body.GetEntity();
    }
};

WolfSourceAIVisuals::WolfSourceAIVisuals(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WolfSourceAIVisuals::~WolfSourceAIVisuals() = default;
idAnimatedEntity* WolfSourceAIVisuals::Body() const { return impl_->body.GetEntity(); }
idAnimatedEntity* WolfSourceAIVisuals::Head() const { return impl_->head.GetEntity(); }
unsigned WolfSourceAIVisuals::AccessoryCount() const { return unsigned(impl_->accessories.size()); }
idEntity* WolfSourceAIVisuals::Accessory(unsigned index) const {
    return index < AccessoryCount() ? impl_->accessories[index].GetEntity() : nullptr;
}

void WolfSourceAIVisuals::SetHidden(bool hidden) {
    const auto set = [hidden](idEntity* entity) {
        if (!entity || entity->IsHidden() == hidden) return;
        if (hidden) {
            entity->Hide();
            entity->BecomeInactive(TH_THINK | TH_ANIMATE | TH_PHYSICS);
        } else {
            entity->Show();
        }
    };
    set(Body());
    set(Head());
    for (unsigned i = 0; i < AccessoryCount(); ++i) set(Accessory(i));
}

bool WolfSourceAIVisuals::UpdatePose(idStr& reason) {
    reason.Clear();
    try {
        auto* body = Body();
        auto* head = Head();
        Need(body && head && head->GetBindMaster() == body, "SourceAIShellV1 body/head lifetime changed");
        head->SetAxis(mat3_identity);
        head->SetOrigin(vec3_zero);
        const auto inverseHead = head->GetPhysics()->GetAxis().Transpose();
        auto* animator = head->GetAnimator();
        for (unsigned i = 0; i < impl_->bodyCopy.size(); ++i) {
            idVec3 position;
            idMat3 axis;
            Need(body->GetJointWorldTransform(impl_->bodyCopy[i], gameLocal.time, position, axis),
                 "SourceAIShellV1 body copy joint pose missing");
            animator->SetJointPos(impl_->headCopy[i], JOINTMOD_WORLD_OVERRIDE,
                                  (position - head->GetPhysics()->GetOrigin()) * inverseHead);
            animator->SetJointAxis(impl_->headCopy[i], JOINTMOD_WORLD_OVERRIDE, axis * inverseHead);
        }
        animator->CreateFrame(gameLocal.time, true);
        head->UpdateAnimation();
        head->UpdateVisuals();
        for (unsigned i = 0; i < AccessoryCount(); ++i) {
            const auto& plan = impl_->accessoryPlans[i];
            auto* child = Accessory(i);
            Need(child && child->GetBindMaster() == (plan.head ? head : body),
                 "SourceAIShellV1 accessory lifetime changed");
            child->SetAxis(plan.axis);
            child->SetOrigin(plan.origin);
            child->Present();
        }
        body->Present();
        head->Present();
        return true;
    } catch (const std::exception& error) {
        reason = error.what();
        return false;
    }
}

std::unique_ptr<WolfSourceAIVisuals> Wolf2009_SpawnSourceAIVisuals(
    const WolfSourceDeclarationSet& set, const wolf2009::EntityRecord& record,
    const std::string& materialArchive, idStr& reason) {
    reason.Clear();
    try {
        const auto placement = wolf2009::ReadEntityPlacement(record);
        const SourceActor* profile = nullptr;
        for (const auto& entry : TrainyardActors)
            if (placement.name == entry.name && placement.classname == entry.classname) profile = &entry;
        Need(profile && placement.hasOrigin && placement.hasAngle &&
             !record.Find("bind") && !record.Find("model") && !record.Find("skin") &&
             !record.Find("def_head"), "SourceAIShellV1 actor placement is outside the trainyard profile");
        for (float value : placement.origin)
            Need(std::isfinite(value) && std::abs(value) <= 10000000.f, "SourceAIShellV1 invalid actor origin");
        const bool greeter = placement.name == "train_greeter";
        Need((greeter && placement.angle == 0.f && placement.hasRotation) ||
             (!greeter && placement.angle == 270.f && !placement.hasRotation),
             "SourceAIShellV1 actor rotation differs from the observed ENTS");
        if (greeter) {
            for (int i = 0; i < 9; ++i)
                Need(placement.rotation[i] == (i % 4 == 0 ? 1.f : 0.f), "SourceAIShellV1 greeter matrix differs");
        }
        const auto actorDef = Resolve(set, wolf2009::SourceDefinitionKind::EntityDef, placement.classname);
        Need(Scalar(actorDef, "model") == profile->model && Scalar(actorDef, "def_head") == profile->head &&
             Scalar(actorDef, "joint_head") == "chest" &&
             Fold(Scalar(actorDef, "detailPack")) == std::string("streampacks/") + profile->pack + ".spk",
             "SourceAIShellV1 body, head or character pack differs");
        Need(Scalar(actorDef, "def_attach1") == profile->attachment &&
             Scalar(actorDef, "def_attach_hat1", false) == profile->hat &&
             Scalar(actorDef, "def_attach2", false).empty() &&
             Scalar(actorDef, "def_attach_hat2", false).empty() && !actorDef.Find("attach_hat_chance"),
             "SourceAIShellV1 attachment selection differs from the qualified source");
        const auto bodyModel = Resolve(set, wolf2009::SourceDefinitionKind::Model, profile->model);
        const auto headDef = Resolve(set, wolf2009::SourceDefinitionKind::EntityDef, profile->head);
        const auto headModel = Resolve(set, wolf2009::SourceDefinitionKind::Model, Scalar(headDef, "model"));
        actor::ShellMeshRequest bodyRequest, headRequest;
        bodyRequest.modelResource = ModlPath(profile->mount, Scalar(bodyModel, "mesh"));
        bodyRequest.skeletonResource = GlobalSkeletons;
        bodyRequest.skeletonName = Scalar(bodyModel, "skeleton");
        Need(Fold(bodyRequest.skeletonName) == BodyRig, "SourceAIShellV1 body skeleton differs");
        bodyRequest.joints = 52;
        bodyRequest.wehrmachtIntact = std::string(profile->mount) == "sp_ai_wehrmacht";
        if (bodyRequest.wehrmachtIntact) {
            const char* keys[] = {"hidesurface", "hidesurface2", "hidesurface3", "hidesurface4", "hidesurface5"};
            const char* caps[] = {"kneecap_l", "uplegcap_r", "elbowcap_r", "shouldercap_l", "neck"};
            for (unsigned i = 0; i < 5; ++i)
                Need(Scalar(actorDef, keys[i]) == std::string("models/characters/enemies/infantry_two_gore_") + caps[i],
                     "SourceAIShellV1 intact Wehrmacht body hide-surface list differs");
        }
        bodyRequest.skin = Scalar(actorDef, "skin", false);
        if (!bodyRequest.skin.empty()) bodyRequest.catalog = materialArchive;
        headRequest.modelResource = ModlPath(profile->mount, Scalar(headModel, "mesh"));
        headRequest.skeletonResource = TrainyardSkeletons;
        headRequest.skeletonName = Scalar(headModel, "skeleton");
        headRequest.joints = profile->headJoints;
        headRequest.skin = Scalar(headDef, "skin", false);
        if (!headRequest.skin.empty()) headRequest.catalog = materialArchive;

        std::unique_ptr<WolfSourceAIVisuals::Impl> owner(new WolfSourceAIVisuals::Impl);
        owner->bodyMesh = PrepareMesh(bodyRequest);
        owner->headMesh = PrepareMesh(headRequest);
        owner->accessoryPlans.push_back(PrepareAccessory(set, profile->attachment));
        if (*profile->hat) owner->accessoryPlans.push_back(PrepareAccessory(set, profile->hat));
        owner->accessories.reserve(owner->accessoryPlans.size());
        auto* body = Spawn<idAnimatedEntity>(placement.name, owner->bodyMesh.definition, owner->bodyMesh.skin);
        owner->body = body;
        body->SetOrigin(idVec3(placement.origin[0], placement.origin[1], placement.origin[2]));
        body->SetAxis(idAngles(0.f, placement.angle, 0.f).ToMat3());
        auto* head = Spawn<idAnimatedEntity>(placement.name + "_source_head", owner->headMesh.definition, owner->headMesh.skin);
        owner->head = head;
        Need(body->GetAnimator()->ModelHandle() == owner->bodyMesh.model &&
             head->GetAnimator()->ModelHandle() == owner->headMesh.model,
             "SourceAIShellV1 exact native animator models differ");
        Bind(*head, *body, Joint(*body, "chest"), vec3_zero, mat3_identity);

        const char* expectedCopy[] = {"neck", "shoulders", "head"};
        const char* copyKeys[] = {"copy_joint1", "copy_joint2", "copy_joint3"};
        for (unsigned i = 0; i < owner->bodyCopy.size(); ++i) {
            const auto name = Scalar(headDef, copyKeys[i]);
            Need(name == expectedCopy[i], "SourceAIShellV1 head copy profile differs");
            const auto key = "copy_joint " + name;
            const auto bodyJoint = Scalar(actorDef, key.c_str(), false);
            owner->bodyCopy[i] = Joint(*body, bodyJoint.empty() ? name : bodyJoint);
            owner->headCopy[i] = Joint(*head, name);
        }
        for (unsigned i = 0; i < owner->accessoryPlans.size(); ++i) {
            const auto& plan = owner->accessoryPlans[i];
            auto* child = Spawn<idStaticEntity>(placement.name + "_source_accessory_" + std::to_string(i), plan.model);
            idEntityPtr<idEntity> pointer;
            pointer = child;
            owner->accessories.push_back(pointer);
            auto& master = plan.head ? *head : *body;
            Bind(*child, master, Joint(master, plan.joint), plan.origin, plan.axis);
        }
        std::unique_ptr<WolfSourceAIVisuals> result(new WolfSourceAIVisuals(std::move(owner)));
        if (!result->UpdatePose(reason)) return nullptr;
        result->SetHidden(!greeter);
        gameLocal.Printf("WOLF2009_SOURCE_AI_VISUAL actor=%s body=%s head=%s accessories=%u reference_pose=1\n",
                         profile->name, bodyRequest.modelResource.c_str(), headRequest.modelResource.c_str(),
                         result->AccessoryCount());
        return result;
    } catch (const std::exception& error) {
        reason = error.what();
        return nullptr;
    }
}
