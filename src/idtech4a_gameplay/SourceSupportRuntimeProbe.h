// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_SUPPORT_RUNTIME_PROBE_H
#define WOLF2009_SOURCE_SUPPORT_RUNTIME_PROBE_H
// QA only: call after common Init/devmap has loaded original empty through
// the declared Doom3 source-support policy, before the native player probe.
// No game frames, manual FreeMap, OS input or retail ray semantics are used.
bool Wolf2009_RunSourceSupportProbe(const char* canonicalMapName);
// Call only after normal common->Shutdown, alongside the other CM checks.
bool Wolf2009_CheckSourceSupportReleased();
#endif
