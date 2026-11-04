# SPDX-License-Identifier: GPL-3.0-or-later
# Included after the normal engine stream bridge has been defined.
if(NOT TARGET wolf2009_stream_bridge OR NOT WOLF2009_STREAMS OR NOT CORE OR NOT BASE)
    message(FATAL_ERROR "WOLF2009_GAMEPLAY requires CORE, GPL Doom3 BASE and WOLF2009_STREAMS")
endif()
foreach(source GameplayMap.cpp NativeCollisionMesh.cpp EngineGameplay.cpp EngineGameplay.h
               EngineRoundedCollision.cpp EngineRoundedCollision.h EngineSupportSource.cpp SupportGameplayMap.cpp)
    if(NOT EXISTS "${WOLF2009_GAMEPLAY_DIR}/${source}")
        message(FATAL_ERROR "Missing WOLF2009_GAMEPLAY_DIR/${source}")
    endif()
endforeach()
target_sources(wolf2009_stream_bridge PRIVATE
    "${WOLF2009_GAMEPLAY_DIR}/GameplayMap.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/NativeCollisionMesh.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/EngineGameplay.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/EngineRoundedCollision.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/EngineSupportSource.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/SupportGameplayMap.cpp"
    "${WOLF2009_CORE_DIR}/SourceMapBodies.cpp"
    "${WOLF2009_CORE_DIR}/SourceCollisionBinding.cpp"
    "${WOLF2009_CORE_DIR}/WorldCollisionGeometry.cpp"
    "${WOLF2009_CORE_DIR}/WorldTriangleGeometry.cpp"
    "${WOLF2009_CORE_DIR}/HavokTriangleMesh.cpp"
    "${WOLF2009_CORE_DIR}/HavokTriangleQuery.cpp"
    "${WOLF2009_CORE_DIR}/HavokPrimitiveSupport.cpp"
    "${WOLF2009_CORE_DIR}/SupportCollisionWorld.cpp"
    "${WOLF2009_CORE_DIR}/Entities.cpp"
    "${WOLF2009_CORE_DIR}/PlayerStart.cpp"
    "${WOLF2009_CORE_DIR}/StaticCollision.cpp")
target_include_directories(wolf2009_stream_bridge PRIVATE "${WOLF2009_GAMEPLAY_DIR}")
target_compile_definitions(wolf2009_stream_bridge PRIVATE WOLF2009_GAMEPLAY)
if(WOLF2009_RENDERER)
    set_property(SOURCE "${WOLF2009_GAMEPLAY_DIR}/EngineSupportSource.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_SOURCE_MAP_RENDER_CATALOG)
endif()
# The geometry target keeps Bullet types/defines out of the engine boundary.
if(NOT TARGET wolf2009_bullet_narrowphase)
    add_subdirectory("${WOLF2009_CORE_DIR}/../../third_party/bullet_narrowphase"
                     "${CMAKE_CURRENT_BINARY_DIR}/wolf2009_bullet_narrowphase")
endif()
if(NOT TARGET wolf2009_rounded_convex)
    add_library(wolf2009_rounded_convex STATIC "${WOLF2009_CORE_DIR}/RoundedConvex.cpp")
    target_compile_features(wolf2009_rounded_convex PRIVATE cxx_std_11)
    target_include_directories(wolf2009_rounded_convex PUBLIC "${WOLF2009_CORE_DIR}")
    target_link_libraries(wolf2009_rounded_convex PRIVATE wolf2009_bullet_narrowphase)
    set_target_properties(wolf2009_rounded_convex PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
if(NOT TARGET wolf2009_support_collision_index)
    add_library(wolf2009_support_collision_index STATIC "${WOLF2009_CORE_DIR}/SupportCollisionIndex.cpp")
    target_compile_features(wolf2009_support_collision_index PRIVATE cxx_std_11)
    target_include_directories(wolf2009_support_collision_index PUBLIC "${WOLF2009_CORE_DIR}")
    target_link_libraries(wolf2009_support_collision_index PUBLIC wolf2009_rounded_convex)
    set_target_properties(wolf2009_support_collision_index PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_link_libraries(wolf2009_stream_bridge wolf2009_support_collision_index)
target_include_directories(${DOOM3BINARY} PRIVATE "${WOLF2009_GAMEPLAY_DIR}")
target_compile_definitions(${DOOM3BINARY} PRIVATE WOLF2009_GAMEPLAY)
if(WOLF2009_BOOTSTRAP)
    target_sources(${DOOM3BINARY} PRIVATE
        "${WOLF2009_GAMEPLAY_DIR}/GameplayRuntimeProbe.cpp"
        "${WOLF2009_GAMEPLAY_DIR}/RoundedCollisionRuntimeProbe.cpp"
        "${WOLF2009_GAMEPLAY_DIR}/SourceSupportRuntimeProbe.cpp")
endif()
