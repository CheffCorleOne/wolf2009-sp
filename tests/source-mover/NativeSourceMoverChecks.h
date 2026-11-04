// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_MOVER_CHECKS_H
#define WOLF2009_NATIVE_SOURCE_MOVER_CHECKS_H
class idPlayer;
// The caller preloads its own normal native CM fixture before entering this
// game-module test. No original source body is replaced by this geometry.
bool Wolf2009_CheckNativeSourceMoverQA(idPlayer*,const char* ownNativeCollisionAlias);
#endif
