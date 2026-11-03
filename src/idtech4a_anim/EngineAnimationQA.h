// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ANIMATION_QA_H
#define WOLF2009_ENGINE_ANIMATION_QA_H
// QA-only plain C-function boundary. No STL, game private layouts, or API8
// import/export changes cross the core/game DSO boundary.
typedef int (*Wolf2009AnimatorQaEntry)(const char* animationAlias, const char* otherModelAlias);
typedef int (*Wolf2009AnimationSetQaEntry)(const char* firstAnimationAlias,
    const char* secondAnimationAlias, const char* wrongModelAnimationAlias);
typedef int (*Wolf2009DeclarationsQaEntry)(const char* mapDeclarationResourcePath,const char* globalDeclarationPath,
    const char* mapLocaleDeclarationResourcePath,const char* globalLocaleDeclarationPath);
bool Wolf2009_RunAnimationChecks(Wolf2009AnimatorQaEntry gameProbe);
#endif
