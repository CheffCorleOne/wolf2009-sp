// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GAMEPLAY_RUNTIME_PROBE_H
#define WOLF2009_GAMEPLAY_RUNTIME_PROBE_H
// QA only: run after normal SP InitFromNewMap/ClientBegin has loaded the own
// maps/wolf2009/runtime fixture. Uses GPL game API/usercmd, never OS input.
bool Wolf2009_RunGameplayFixtureProbe();
#endif
