// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MAP_PHYSICS_H
#define WOLF2009_MAP_PHYSICS_H
#include "Archive.h"
#include "PlayerStart.h"
#include "StaticCollision.h"

namespace wolf2009 {
// Match both resource name and FourCC in this archive before decompressing.
std::vector<unsigned char> ReadMapResource(std::istream& input, const Archive& archive,
    const std::string& resourceName, const std::string& type, std::size_t maxBytes = static_cast<std::size_t>(-1));
// The name is the map's virtual PROC name, e.g. "maps/game/example/example".
// Only same-name resources in the supplied archive are eligible. Neither
// operation retains the stream, so Android may release its import after load.
EntityDocument ReadMapEntities(std::istream& input, const Archive& archive,
    const std::string& procResourceName);
std::vector<PlayerStartCandidate> ReadMapPlayerStarts(std::istream& input,
    const Archive& archive, const std::string& procResourceName);
StaticCollisionScene ReadMapStaticCollision(std::istream& input,
    const Archive& archive, const std::string& procResourceName);
}
#endif
