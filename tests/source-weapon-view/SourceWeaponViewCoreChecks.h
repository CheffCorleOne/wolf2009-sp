// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_WEAPON_VIEW_CORE_CHECKS_H
#define WOLF2009_SOURCE_WEAPON_VIEW_CORE_CHECKS_H
// Test-only scalar ABI between the already loaded game module and core. No
// renderer/game objects or STL values cross this optional QA boundary.
enum WolfSourceWeaponViewQaAction {
    WolfSourceWeaponViewQa_Begin=0,
    WolfSourceWeaponViewQa_Flat=1,
    WolfSourceWeaponViewQa_Original=2,
    WolfSourceWeaponViewQa_FirePose=3,
    WolfSourceWeaponViewQa_ReloadPose=4,
    WolfSourceWeaponViewQa_End=5,
    // Returns the live native entity number, not the usual success integer.
    WolfSourceWeaponViewQa_EntityNumber=6
};
// Called after the ordinary EGL pbuffer has been initialized. Missing explicit
// fixtures/source-session.enabled means this check is not requested.
bool Wolf2009_RunSourceWeaponViewCoreChecks();
#endif
