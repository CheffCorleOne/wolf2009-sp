// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_GAMEPLAY_H
#define WOLF2009_ENGINE_GAMEPLAY_H
class idFile;
// C++98-compatible boundary for the opt-in engine filesystem hook.
bool Wolf2009_IsGameplayMapPath(const char *path);
idFile *Wolf2009_OpenGameplayMap(const char *path);
// QA-only named .cm resource, enabled explicitly on the engine thread around
// ordinary collisionModelManager::LoadModel. No source world is substituted.
void Wolf2009_EnableNativeCollisionFixture(bool enabled);
#endif
