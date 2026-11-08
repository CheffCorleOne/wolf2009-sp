// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAINYARD_VIEW_CORE_CHECKS_H
#define WOLF2009_TRAINYARD_VIEW_CORE_CHECKS_H
// Optional test-only scalar seam between already-loaded core/game modules.
// Neither engine/game objects nor STL values cross this boundary.
enum WolfTrainyardViewQaAction {
    WolfTrainyardViewQa_Begin=0,
    WolfTrainyardViewQa_HideWagon=1,
    WolfTrainyardViewQa_RestoreWagon=2,
    WolfTrainyardViewQa_End=3,
    // Returns the actual live native entity number, not success=1.
    WolfTrainyardViewQa_WagonEntityNumber=4,
    WolfTrainyardViewQa_CheckUnchanged=5,
    // Greeter-route sessions only: the bound animated door. Returns -1 when the
    // session has no greeter route; Hide/Restore follow the wagon stages.
    WolfTrainyardViewQa_DoorEntityNumber=6,
    WolfTrainyardViewQa_HideDoor=7,
    WolfTrainyardViewQa_RestoreDoor=8
};
// Called after the ordinary native EGL pbuffer/renderer setup. Missing explicit
// fixtures/trainyard-session.enabled means this check was not requested.
bool Wolf2009_RunTrainyardViewCoreChecks();
#endif
