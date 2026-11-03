// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SESSION_H
#define WOLF2009_SOURCE_SESSION_H
// Private C++98 hooks in the ordinary GPL game lifecycle; API8 is unchanged.
class idPlayer;class idStr;class WolfSourceDeclarationSet;
// An explicit local wolf2009/source-session.cfg selects the namespace and native
// player policy before allocation. Missing descriptor keeps ordinary Doom3.
// A present invalid descriptor refuses startup, never substitutes stock data.
bool Wolf2009_BeginSourceSession(const char* mapFileName,idStr& reason);
bool Wolf2009_AttachSourceSessionPlayer(idPlayer*,idStr& reason);
bool Wolf2009_DevEquipSourceSessionMP40(idPlayer*,idStr& reason);
void Wolf2009_EndSourceSession();
const WolfSourceDeclarationSet* Wolf2009_SourceSessionDeclarations();
bool Wolf2009_HasSourceSession();
#endif
