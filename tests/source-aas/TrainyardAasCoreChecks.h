// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAINYARD_AAS_CORE_CHECKS_H
#define WOLF2009_TRAINYARD_AAS_CORE_CHECKS_H

bool Wolf2009_TrainyardAasCheckRequested();
bool Wolf2009_CheckTrainyardAasQA(const char* gameLibrary, unsigned expectedLoads);
bool Wolf2009_FinishTrainyardAasQA();

#endif
