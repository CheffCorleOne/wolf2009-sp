// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_SOURCE_PLAYER_HULL_H
#define WOLF2009_ENGINE_SOURCE_PLAYER_HULL_H
class idTraceModel;
class idStr;
// Private game-module seam; coordinates are already rounded native-f32 local
// supports. The caller owns source shape/body provenance and radius policy.
// A failure leaves out unchanged. No asset tables or platform API are used.
bool Wolf2009_BuildSourcePlayerTraceModel(const float* xyz,int count,
    idTraceModel& out,idStr& reason);
#endif
