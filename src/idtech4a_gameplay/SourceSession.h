// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SESSION_H
#define WOLF2009_SOURCE_SESSION_H
// Private C++98 hooks in the ordinary GPL game lifecycle; API8 is unchanged.
class idPlayer;class idStr;class WolfSourceDeclarationSet;
class WolfSourceMapRuntime;
// An explicit local wolf2009-session.cfg selects the namespace and native
// player policy before allocation. Missing descriptor keeps ordinary Doom3.
// A present invalid descriptor refuses startup, never substitutes stock data.
bool Wolf2009_BeginSourceSession(const char* mapFileName,idStr& reason);
bool Wolf2009_AttachSourceSessionPlayer(idPlayer*,idStr& reason);
// QA/development grant only: a fresh MP40 view with 32/32 rounds. It does not
// touch the source inventory, so PlayerHasWeapon stays false. The console
// command wolf2009DevEquipMP40 refuses unless wolf2009_qaDevWeaponGrant is 1.
bool Wolf2009_DevEquipSourceSessionMP40(idPlayer*,idStr& reason);
// Normal flow: equip the source weapon controller for a weapon the player's
// source inventory owns, with rounds taken from the inventory's ammo pool.
bool Wolf2009_EquipSourceSessionInventoryWeapon(idPlayer*,const char* weapon,int clip,int reserve,idStr& reason);
void Wolf2009_EndSourceSession();
const WolfSourceDeclarationSet* Wolf2009_SourceSessionDeclarations();
bool Wolf2009_HasSourceSession();
bool Wolf2009_SpawnSourceSessionMapEntities(idStr& reason);
WolfSourceMapRuntime* Wolf2009_SourceSessionMapRuntime();
#endif
