// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SESSION_CHECKS_H
#define WOLF2009_SOURCE_SESSION_CHECKS_H
bool Wolf2009_PrepareSourceSessionFixture();
bool Wolf2009_SourceSessionFixtureRequested();
bool Wolf2009_RunSourceSessionFixture(const char* gameLibrary);
bool Wolf2009_FinishSourceSessionFixture(const char* gameLibrary);
#endif
