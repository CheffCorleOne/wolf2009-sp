// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_MAP_BODIES_H
#define WOLF2009_SOURCE_MAP_BODIES_H
#include "Entities.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
struct SourceMapBodiesLimits {
    std::size_t hkxsBytes=64u*1024u*1024u, objects=100000, fixups=1000000;
    std::size_t bodies=100000, nameBytes=64u*1024u*1024u;
    EntityLimits entities;
};
struct SourceMapBodyRecord {
    std::uint32_t sourceBodyOffset=0, rootShapeOffset=0, rawFilterInfo=0;
    std::string rawName, rootShapeType;
    bool hasName=false, motionStateFinite=false;
    // Exact binary32 words at body+224..351: matrix/translation and swept
    // state. They are source facts, not an ENTS/world placement or native pose.
    std::array<std::uint32_t,32> rawMotionStateWords{};
};
struct SourceMapBodyInventory {
    std::vector<unsigned char> source;
    std::uint64_t sourceFingerprint=0;
    std::size_t sourceBytes=0;
    // Increasing typed-object offset, including unnamed bodies and raw filter0.
    std::vector<SourceMapBodyRecord> bodies;
};
enum class SourceMapBodyRole {
    OtherUnbound, SelectedWorld, ExactEntityName, ResourceNameUnbound
};
struct SourceMapBodyBinding {
    SourceMapBodyRole role=SourceMapBodyRole::OtherUnbound;
    std::size_t entityIndex=static_cast<std::size_t>(-1);
};
struct SourceMapBodyPlan {
    SourceMapBodyInventory inventory;
    EntityDocument entities;
    // Populated by DecodeSourceMapBodies; data-only Bind owns the document but
    // does not invent its original ENTS byte identity.
    std::vector<unsigned char> entsSource;
    std::uint64_t entsFingerprint=0;
    std::size_t entsBytes=0;
    std::string selectedWorldName;
    std::size_t selectedWorldBody=static_cast<std::size_t>(-1);
    // Same index domain/order as inventory.bodies. No enablement is inferred.
    std::vector<SourceMapBodyBinding> bindings;
    // A source ENTS entry can have at most one exact-name instance body.
    std::vector<std::size_t> entityBodyIndices;
    std::size_t entityBodies=0, resourceNameBodies=0, otherUnboundBodies=0;
    // Duplicate names unrelated to selected world/exact entity bindings stay
    // explicit. Trying to bind an ambiguous name refuses the whole operation.
    std::vector<std::string> ambiguousUnboundBodyNames, ambiguousUnboundEntityNames;
};

// Checked read of the observed Havok32/5.6 packfile. Retains whole bytes, raw
// names/filter words and motion state. Does not traverse or admit geometry.
SourceMapBodyInventory ReadSourceMapBodyInventory(const std::vector<unsigned char>& hkxs,
    const SourceMapBodiesLimits& limits=SourceMapBodiesLimits());
// Data-only exact byte-name join. The inventory/document may be independently
// caller-authored; this overload makes no source-ENTS byte provenance claim.
// Resource-looking names are unbound resources, not proof of runtime roles.
SourceMapBodyPlan BindSourceMapBodies(const SourceMapBodyInventory&,const EntityDocument&,
    const std::string& exactWorldBodyName,const SourceMapBodiesLimits& limits=SourceMapBodiesLimits());
// Owning decode + exact-name join for engine use. Returned only after every
// selected binding is unambiguous. Geometry/filter/activation remains separate.
SourceMapBodyPlan DecodeSourceMapBodies(const std::vector<unsigned char>& hkxs,
    const std::vector<unsigned char>& ents,const std::string& exactWorldBodyName,
    const SourceMapBodiesLimits& limits=SourceMapBodiesLimits());
}
#endif
