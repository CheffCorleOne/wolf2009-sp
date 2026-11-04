// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAINYARD_ACTOR_FIXTURE_H
#define WOLF2009_TRAINYARD_ACTOR_FIXTURE_H
class idStr;
class WolfSourceDeclarationSet;
struct WolfSourceScriptedActorRequest;
// Assemble the same measured actor request from one explicit ENTS/BRAI mount.
// The detail, global rig and clip mounts remain explicit; no alternate mount
// is searched. Creates no native actor, animation command or story ACK.
bool Wolf2009_PrepareTrainyardScriptedActorFixtureRequest(
    const WolfSourceDeclarationSet&,const char* exactSourceMount,
    WolfSourceScriptedActorRequest&,idStr& reason);
// QA-only glue for a dedicated ordinary source session. The caller explicitly
// mounts sp_greeter (allies_kreisau_agt02.spk), sp_global and sp_map, and keeps
// the selected namespace/material archive alive. Does not mount or substitute
// resources, acknowledge VO/story readiness, or create a campaign controller.
bool Wolf2009_RunTrainyardScriptedActorFixtureQA(idStr& reason);
#endif
