// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GAMEPLAY_FIXTURE_H
#define WOLF2009_GAMEPLAY_FIXTURE_H
#include "GameplayMap.h"
namespace wolf2009 {
// Entirely authored test room. The off-axis platform and trigger exercise the
// same generic entity types the importer exposes; no commercial data is used.
GameplayMap MakeGameplayFixture();
}
#endif
