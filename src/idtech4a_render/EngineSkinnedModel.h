// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_SKINNED_MODEL_H
#define WOLF2009_ENGINE_SKINNED_MODEL_H
// C++98-compatible engine entry points. Call on the model-manager owner thread.
class idRenderModelMD5;
class idStr;
namespace wolf2009 { namespace idtech4a { struct SkinnedModelRequest; struct SkinnedMeshRequestV2; } }
void Wolf2009_InitSkinnedModelCommands();
void Wolf2009_ShutdownSkinnedModelCommands();
// Immutable registration; paths are exact original typed resource names.
// Returns an MD5R alias for ordinary renderModelManager->FindModel(). No read,
// implicit skeleton selection, pose baking, or entity placement occurs here.
bool Wolf2009_RegisterSkinnedModel(const char* modlPath, const char* hkxaPath,
                                 const char* skelPath, int levelOfDetail, idStr& nativeName);
// Additive geometry-only source. Profile 1=weighted64, 2=weighted/rigid64.
bool Wolf2009_RegisterSkinnedMeshV2(const char* modlPath,const char* skelContainer,const char* exactSkeletonName,
                                  int levelOfDetail,int geometryProfile,idStr& nativeName);
// LOD policy 1=authored records, 2=explicit single resource with no LOD records
// anywhere, 3=one authored root distance with zero/absent mesh markers. Policies
// 2/3 use explicit compatibility slot0. The old signature always uses policy1.
bool Wolf2009_RegisterSkinnedMeshV2(const char* modlPath,const char* skelContainer,const char* exactSkeletonName,
                                  int levelOfDetail,int geometryProfile,int lodPolicy,idStr& nativeName);
bool Wolf2009_GetSkinnedMeshRequestV2(const char* nativeName,wolf2009::idtech4a::SkinnedMeshRequestV2& request);
bool Wolf2009_IsSkinnedModelPath(const char* path);
bool Wolf2009_LoadSkinnedModel(idRenderModelMD5* model, const char* nativeName);
bool Wolf2009_GetSkinnedModelRequest(const char* nativeName,
                                    wolf2009::idtech4a::SkinnedModelRequest& request);
// Resident model only. An explicit authored material-remap skin is qualified
// against the supplied original catalog bytes and the current diffuse profile.
// Returns an immutable native idDeclSkin name for renderEntity.customSkin.
// Does not mutate shared mesh attributes or select an appearance for an entity.
bool Wolf2009_RegisterSkinnedAppearance(const char* modelAlias, const char* sourceSkinName,
                                       const char* catalogPath, idStr& skinAlias);
void Wolf2009_ReleaseSkinnedModelSources(idRenderModelMD5* model);
#endif
