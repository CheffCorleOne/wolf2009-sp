// SPDX-License-Identifier: GPL-3.0-or-later
#include "idlib/precompiled.h"
#include "game/Game_local.h"

// idEntity is abstract in Doom3's registration table, while idStaticEntity's
// Spawn overwrites exact clipmodel contents with0 or1. A concrete registration
// of the ordinary entity lifecycle retains native idPhysics_Static and each
// brush's contents. No custom collision/movement implementation is needed.
class idWolf2009Static : public idEntity {
  public:
    CLASS_PROTOTYPE(idWolf2009Static);
};
CLASS_DECLARATION(idEntity,idWolf2009Static)
END_CLASS
