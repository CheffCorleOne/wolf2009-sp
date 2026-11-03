// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_SOURCE_ACTOR_HOOKS_H
#define WOLF2009_ENGINE_SOURCE_ACTOR_HOOKS_H
// Ordinary C++98 game TUs must not include STL after idStr's legacy macros.
class idDeclModelDef;
class idAnim;
bool Wolf2009_IsQualifiedSourceActorAnimation(const idDeclModelDef*,const idAnim*);
bool Wolf2009_IsQualifiedSourceFrameSound(const idDeclModelDef*,const idAnim*);
#endif
