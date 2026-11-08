# SPDX-License-Identifier: GPL-3.0-or-later
# Include after CORE/BASE game and the renderer/stream bridge targets exist.
option(WOLF2009_ANIMATION "Enable leased source HKXA playback in native animators" OFF)
option(WOLF2009_ANIMATION_QA "Include source animation wire/lifetime/native animator QA" OFF)
option(WOLF2009_ANIMATION_V2_QA "Include four-source mesh-v2/multiblock native animation QA" OFF)
option(WOLF2009_SOURCE_AUDIO_BANK "Enable whole-bank scripted-voice SNDS registration and type19 readiness" OFF)
option(WOLF2009_SOURCE_AUDIO_BANK_QA "Include native audio-bank and type19 readiness QA" OFF)
if(WOLF2009_SOURCE_AUDIO_BANK_QA AND NOT WOLF2009_SOURCE_AUDIO_BANK)
    message(FATAL_ERROR "Audio bank QA requires WOLF2009_SOURCE_AUDIO_BANK")
endif()
if(WOLF2009_ANIMATION)
    if(NOT BUILD_D3 OR NOT BASE OR NOT TARGET game OR NOT TARGET wolf2009_render_bridge OR NOT TARGET wolf2009_stream_bridge)
        message(FATAL_ERROR "WOLF2009_ANIMATION requires GPL BASE game, WOLF2009_RENDERER and STREAMS")
    endif()
    get_filename_component(WOLF2009_ANIMATION_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
    get_target_property(wolf_anim_core_defines ${DOOM3BINARY} COMPILE_DEFINITIONS)
    get_target_property(wolf_anim_game_defines game COMPILE_DEFINITIONS)
    get_target_property(wolf_anim_game_includes game INCLUDE_DIRECTORIES)
    add_library(wolf2009_animation_core_objects OBJECT
        "${WOLF2009_ANIMATION_DIR}/AnimationWire.cpp"
        "${WOLF2009_ANIMATION_DIR}/EngineAnimationBinding.cpp")
    add_library(wolf2009_animation_game_objects OBJECT
        "${WOLF2009_ANIMATION_DIR}/AnimationWire.cpp"
        "${WOLF2009_ANIMATION_DIR}/EngineAnimationGame.cpp"
        "${WOLF2009_ANIMATION_DIR}/SourceVisualEntity.cpp"
        "${WOLF2009_CORE_DIR}/GreeterAnimation.cpp")
    foreach(wolf_anim_target wolf2009_animation_core_objects wolf2009_animation_game_objects)
        target_compile_features(${wolf_anim_target} PRIVATE cxx_std_11)
        set_target_properties(${wolf_anim_target} PROPERTIES POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
        target_include_directories(${wolf_anim_target} PRIVATE "${WOLF2009_ANIMATION_DIR}" "${WOLF2009_CORE_DIR}"
            "${WOLF2009_RENDER_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/externlibs" "${CMAKE_CURRENT_SOURCE_DIR}/externlibs/miniz")
        target_compile_definitions(${wolf_anim_target} PRIVATE WOLF2009_ANIMATION)
        if(NOT MSVC)
            target_compile_options(${wolf_anim_target} PRIVATE -ffp-contract=off -fno-fast-math -Wno-format-security)
        endif()
    endforeach()
    target_compile_definitions(wolf2009_animation_core_objects PRIVATE ${wolf_anim_core_defines} WOLF2009_RENDERER)
    target_compile_definitions(wolf2009_animation_game_objects PRIVATE ${wolf_anim_game_defines})
    target_include_directories(wolf2009_animation_game_objects PRIVATE ${wolf_anim_game_includes})
    target_sources(${DOOM3BINARY} PRIVATE $<TARGET_OBJECTS:wolf2009_animation_core_objects>)
    target_sources(game PRIVATE $<TARGET_OBJECTS:wolf2009_animation_game_objects>)
    foreach(wolf_anim_target ${DOOM3BINARY} game wolf2009_stream_bridge wolf2009_render_bridge)
        target_compile_definitions(${wolf_anim_target} PRIVATE WOLF2009_ANIMATION)
        target_include_directories(${wolf_anim_target} PRIVATE "${WOLF2009_ANIMATION_DIR}")
    endforeach()
    if(WOLF2009_ANIMATION_QA)
        if(NOT WOLF2009_BOOTSTRAP)
            message(FATAL_ERROR "Source animation QA requires WOLF2009_BOOTSTRAP")
        endif()
        target_compile_definitions(wolf2009_animation_game_objects PRIVATE WOLF2009_ANIMATION_QA)
        target_sources(wolf2009_animation_core_objects PRIVATE
            "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation/EngineAnimationChecks.cpp")
        target_sources(wolf2009_animation_game_objects PRIVATE
            "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation/SourceAnimatorChecks.cpp")
        set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
            PROPERTY COMPILE_DEFINITIONS WOLF2009_ANIMATION_QA)
    endif()
    include("${WOLF2009_ANIMATION_DIR}/EngineAnimationV2.cmake")
    include("${WOLF2009_ANIMATION_DIR}/SourceAudioBank.cmake")
elseif(WOLF2009_SOURCE_AUDIO_BANK)
    message(FATAL_ERROR "WOLF2009_SOURCE_AUDIO_BANK requires WOLF2009_ANIMATION")
elseif(WOLF2009_ANIMATION_QA OR WOLF2009_ANIMATION_V2_QA)
    message(FATAL_ERROR "Source animation QA requires WOLF2009_ANIMATION")
endif()
