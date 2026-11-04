// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAINYARD_DOOR_FIXTURE_H
#define WOLF2009_TRAINYARD_DOOR_FIXTURE_H
class idStr;
// QA-only after ordinary empty-map initialization. The source-session owner
// supplies the selected trainyard namespace/material archive and explicit
// sp_map, sp_global and sp_greeter mounts. Creates three fresh source-native
// wagon/actor fixtures; the existing door QA owns its actual animated child.
// No trainyard world, VO readiness or campaign progression is implied.
bool Wolf2009_RunTrainyardAnimatedDoorFixtureQA(idStr& reason);
#endif
