// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ROUNDED_COLLISION_RUNTIME_PROBE_H
#define WOLF2009_ROUNDED_COLLISION_RUNTIME_PROBE_H
// QA only. Call AFTER Wolf2009_RunGameplayFixtureProbe succeeds on the authored
// maps/wolf2009/runtime map. No game frames run before the existing mover tests.
// Tests real CM hooks, then the existing GPL player in an isolated x=1000 area.
bool Wolf2009_RunRoundedCollisionFixtureProbe();
// Call after normal common->Shutdown, alongside the existing CM lifetime check.
bool Wolf2009_CheckRoundedCollisionFixtureReleased();
#endif
