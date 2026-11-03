// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_GAMEPLAY_H
#define WOLF2009_ENGINE_GAMEPLAY_H
class idFile;
// C++98-compatible boundary for the opt-in engine filesystem hook.
bool Wolf2009_IsGameplayMapPath(const char *path);
idFile *Wolf2009_OpenGameplayMap(const char *path);
#endif
