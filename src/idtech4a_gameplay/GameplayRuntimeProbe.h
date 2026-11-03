// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GAMEPLAY_RUNTIME_PROBE_H
#define WOLF2009_GAMEPLAY_RUNTIME_PROBE_H
// QA only: run after normal SP InitFromNewMap/ClientBegin has loaded the own
// maps/wolf2009/runtime fixture. Uses GPL game API/usercmd, never OS input.
bool Wolf2009_RunGameplayFixtureProbe();
// QA-only standard CM LoadModel/point/trace-model/contents/contact checks.
bool Wolf2009_RunNativeCollisionFixtureProbe();
// Call after common->Shutdown while the manager still exists. Checks handle
// invalidation by the ordinary FreeMap lifecycle; no-op if QA never loaded it.
bool Wolf2009_CheckNativeCollisionFixtureReleased();
#endif
