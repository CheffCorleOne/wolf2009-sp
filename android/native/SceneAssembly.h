// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ANDROID_SCENE_ASSEMBLY_H
#define WOLF2009_ANDROID_SCENE_ASSEMBLY_H
#include "Geometry.h"
#include <limits>
#include <stdexcept>
#include <utility>
namespace wolf2009android {
// One-time bounded append. Geometry core and model names remain unchanged;
// callers use the returned first-surface marker as explicit render eligibility.
inline std::size_t AppendPlacedActor(wolf2009::GeometryScene& world,const wolf2009::GeometryScene& actor,
                                    const wolf2009::GeometryLimits& limits){
    auto need=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    need(!actor.vertices.empty()&&!actor.surfaces.empty(),"No authored actor geometry");
    need(world.vertices.size()<=limits.vertices&&actor.vertices.size()<=limits.vertices-world.vertices.size(),"Combined level vertex budget exceeded");
    need(world.surfaces.size()<=limits.surfaces&&actor.surfaces.size()<=limits.surfaces-world.surfaces.size(),"Combined level surface budget exceeded");
    need(world.vertices.size()+actor.vertices.size()<=(std::numeric_limits<std::uint32_t>::max)(),"Combined level index range exceeded");
    std::size_t triangles=0;for(const auto& surface:world.surfaces){need(surface.indices.size()%3==0,"Malformed world triangle list");const auto count=surface.indices.size()/3;need(triangles<=limits.triangles&&count<=limits.triangles-triangles,"Combined level triangle budget exceeded");triangles+=count;}
    for(const auto& surface:actor.surfaces){need(surface.indices.size()%3==0,"Malformed actor triangle list");const auto count=surface.indices.size()/3;need(triangles<=limits.triangles&&count<=limits.triangles-triangles,"Combined level triangle budget exceeded");triangles+=count;for(auto index:surface.indices)need(index<actor.vertices.size(),"Actor index exceeds actor geometry");}
    const std::size_t combinedVertices=world.vertices.size()+actor.vertices.size();
    need(combinedVertices<=limits.totalBufferBytes/sizeof(wolf2009::GeometryVertex),"Combined level CPU buffer budget exceeded");
    const std::size_t vertexBytes=combinedVertices*sizeof(wolf2009::GeometryVertex);
    need(triangles<=(limits.totalBufferBytes-vertexBytes)/(3*sizeof(std::uint32_t)),"Combined level CPU buffer budget exceeded");
    const auto firstSurface=world.surfaces.size();const auto base=std::uint32_t(world.vertices.size());
    world.vertices.insert(world.vertices.end(),actor.vertices.begin(),actor.vertices.end());
    for(const auto& surface:actor.surfaces){wolf2009::GeometrySurface placed=surface;for(auto& index:placed.indices)index+=base;world.surfaces.push_back(std::move(placed));}
    return firstSurface;
}
}
#endif
