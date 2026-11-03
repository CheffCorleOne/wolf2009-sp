// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_H
#define WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_H
#include "NativeSourceWeapon.h"
// Game-private C++98 seam. The capability and resource owners never cross API8.
class WolfSourceWeaponView;
enum WolfSourceWeaponViewPolicy { WolfSourceWeaponView_Unspecified=0,
    WolfSourceWeaponView_MP40NativeBaseDiffuseV1=1 };
struct WolfSourceWeaponViewRequest {
    WolfSourceWeaponViewPolicy policy;
    const char *modelResource,*meshSkeletonResource,*meshSkeletonName;
    const char *animationResource,*animationSkeletonResource,*animationSkeletonName;
    const char *idleClip,*fireClip,*reloadClip,*materialArchive;
    const char *sleeveTexture,*handTexture,*gunTexture,*upgradeTexture,*reloadSample;
    WolfSourceWeaponViewRequest():policy(WolfSourceWeaponView_Unspecified),modelResource(0),
        meshSkeletonResource(0),meshSkeletonName(0),animationResource(0),
        animationSkeletonResource(0),animationSkeletonName(0),idleClip(0),fireClip(0),
        reloadClip(0),materialArchive(0),sleeveTexture(0),handTexture(0),gunTexture(0),
        upgradeTexture(0),reloadSample(0){}
};
// Explicit helper for the isolated source-session fixture. Other hosts supply
// their own exact source paths; the production factory does not search packs.
void Wolf2009_SourceMP40FixtureViewRequest(WolfSourceWeaponViewRequest&);
WolfSourceWeaponView* Wolf2009_CreateSourceWeaponView(const WolfSourceDeclarationSet&,
    const WolfSourceWeaponViewRequest&,idStr& reason);
void Wolf2009_RetainSourceWeaponView(const WolfSourceWeaponView*);
void Wolf2009_ReleaseSourceWeaponView(const WolfSourceWeaponView*);
bool Wolf2009_SourceWeaponViewDescriptor(const WolfSourceWeaponView*,WolfSourceWeaponVisual&,idStr&);
struct WolfSourceWeaponViewState {
    const char *modelDefinition,*nativeModel,*skin,*flatSkin,*capability;
    const char *reloadSound,*reloadSample;
    unsigned joints,surfaces,visibleSurfaces,hiddenSurfaces,vertices,triangles,textures;
    unsigned frames[3];
    double durationSeconds[3]; // idle, fire, reload, from actual V2 descriptors
    unsigned long long sourceEpochs[3][4],sourceBytes[3][4],sourceFingerprints[3][4];
    unsigned long long catalogBytes,catalogFingerprint,textureBytes[4],textureFingerprints[4];
    unsigned textureWidths[4],textureHeights[4];
    bool baseDiffuseCompatibility,retailShaderParity;
    WolfSourceWeaponViewState():modelDefinition(0),nativeModel(0),skin(0),flatSkin(0),
        capability(0),reloadSound(0),reloadSample(0),joints(0),surfaces(0),
        visibleSurfaces(0),hiddenSurfaces(0),vertices(0),triangles(0),textures(0),
        catalogBytes(0),catalogFingerprint(0),baseDiffuseCompatibility(false),retailShaderParity(false){
        for(unsigned c=0;c<3;++c){frames[c]=0;durationSeconds[c]=0;
            for(unsigned r=0;r<4;++r)sourceEpochs[c][r]=sourceBytes[c][r]=sourceFingerprints[c][r]=0;}
        for(unsigned t=0;t<4;++t)textureBytes[t]=textureFingerprints[t]=textureWidths[t]=textureHeights[t]=0;
    }
};
// Returned strings remain valid while the view owner is retained.
bool Wolf2009_QuerySourceWeaponView(const WolfSourceWeaponView*,WolfSourceWeaponViewState&,idStr&);
struct WolfSourceWeaponPresentationState;
bool Wolf2009_QuerySourceWeaponViewPresentation(const WolfSourceWeaponView*,
    WolfSourceWeaponPresentationState&,idStr&);
int Wolf2009_LiveSourceWeaponViews();
#endif
