# SPDX-License-Identifier: GPL-3.0-or-later
# Included after the normal engine stream bridge has been defined.
if(NOT TARGET wolf2009_stream_bridge OR NOT WOLF2009_STREAMS OR NOT CORE OR NOT BASE)
    message(FATAL_ERROR "WOLF2009_GAMEPLAY requires CORE, GPL Doom3 BASE and WOLF2009_STREAMS")
endif()
foreach(source GameplayMap.cpp EngineGameplay.cpp EngineGameplay.h)
    if(NOT EXISTS "${WOLF2009_GAMEPLAY_DIR}/${source}")
        message(FATAL_ERROR "Missing WOLF2009_GAMEPLAY_DIR/${source}")
    endif()
endforeach()
target_sources(wolf2009_stream_bridge PRIVATE
    "${WOLF2009_GAMEPLAY_DIR}/GameplayMap.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/EngineGameplay.cpp"
    "${WOLF2009_CORE_DIR}/Entities.cpp"
    "${WOLF2009_CORE_DIR}/PlayerStart.cpp"
    "${WOLF2009_CORE_DIR}/StaticCollision.cpp")
target_include_directories(wolf2009_stream_bridge PRIVATE "${WOLF2009_GAMEPLAY_DIR}")
target_compile_definitions(wolf2009_stream_bridge PRIVATE WOLF2009_GAMEPLAY)
target_include_directories(${DOOM3BINARY} PRIVATE "${WOLF2009_GAMEPLAY_DIR}")
target_compile_definitions(${DOOM3BINARY} PRIVATE WOLF2009_GAMEPLAY)
if(WOLF2009_BOOTSTRAP)
    target_sources(${DOOM3BINARY} PRIVATE "${WOLF2009_GAMEPLAY_DIR}/GameplayRuntimeProbe.cpp")
endif()
