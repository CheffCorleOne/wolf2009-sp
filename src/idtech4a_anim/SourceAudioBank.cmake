# SPDX-License-Identifier: GPL-3.0-or-later
# Opt-in whole-bank SNDS registration and type19 readiness. Included from
# EngineAnimation.cmake after the v2 adapters; API8 and the default build stay
# unchanged while WOLF2009_SOURCE_AUDIO_BANK is OFF.
if(NOT WOLF2009_SOURCE_AUDIO_BANK)
    return()
endif()
if(NOT MP3_SUPPORT)
    message(FATAL_ERROR "WOLF2009_SOURCE_AUDIO_BANK requires the native MP3 codec (MP3_SUPPORT)")
endif()
get_filename_component(WOLF2009_AUDIO_BANK_TESTS "${WOLF2009_ANIMATION_DIR}/../../tests/source-audio-bank" ABSOLUTE)
add_library(wolf2009_audio_bank_core_objects OBJECT
    "${WOLF2009_ANIMATION_DIR}/SourceAudioBankNative.cpp"
    "${WOLF2009_ANIMATION_DIR}/SourceSoundDecodeReceipt.cpp"
    "${WOLF2009_CORE_DIR}/SourceAudioBank.cpp"
    "${WOLF2009_CORE_DIR}/ScriptedVoiceBank.cpp")
add_library(wolf2009_audio_bank_game_objects OBJECT
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeScriptedVOReadiness.cpp"
    "${WOLF2009_CORE_DIR}/ScriptedVoiceBank.cpp")
foreach(wolf_audio_target wolf2009_audio_bank_core_objects wolf2009_audio_bank_game_objects)
    target_compile_features(${wolf_audio_target} PRIVATE cxx_std_11)
    set_target_properties(${wolf_audio_target} PROPERTIES POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    target_include_directories(${wolf_audio_target} PRIVATE "${WOLF2009_ANIMATION_DIR}" "${WOLF2009_CORE_DIR}"
        "${WOLF2009_SOURCE_ENTITY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/externlibs" "${CMAKE_CURRENT_SOURCE_DIR}/externlibs/miniz")
    target_compile_definitions(${wolf_audio_target} PRIVATE WOLF2009_ANIMATION WOLF2009_SOURCE_AUDIO_BANK)
    if(NOT MSVC)
        target_compile_options(${wolf_audio_target} PRIVATE -Wno-format-security)
    endif()
endforeach()
target_compile_definitions(wolf2009_audio_bank_core_objects PRIVATE ${wolf_anim_core_defines} WOLF2009_RENDERER
    WOLF2009_SOURCE_SOUND_CORE WOLF2009_SOURCE_MP3_RECEIPT)
if(USE_MINIZ)
    target_compile_definitions(wolf2009_audio_bank_core_objects PRIVATE WOLF2009_USE_MINIZ)
endif()
target_compile_definitions(wolf2009_audio_bank_game_objects PRIVATE ${wolf_anim_game_defines})
target_include_directories(wolf2009_audio_bank_game_objects PRIVATE ${wolf_anim_game_includes})
target_sources(${DOOM3BINARY} PRIVATE $<TARGET_OBJECTS:wolf2009_audio_bank_core_objects>)
target_sources(game PRIVATE $<TARGET_OBJECTS:wolf2009_audio_bank_game_objects>)
# The router lives in the C++11 stream bridge. Only it learns the new prefix.
target_compile_definitions(wolf2009_stream_bridge PRIVATE WOLF2009_SOURCE_AUDIO_BANK)
# Strict receipt hook: compile only the codec TU (which includes snd_mp3.cpp)
# as C++11 with the private accounting enabled. Ordinary DecodeMP3 callers pass
# no accounting object and keep their historical behavior.
set(wolf_audio_decoder "${CMAKE_CURRENT_SOURCE_DIR}/sound/snd_decoder.cpp")
set_property(SOURCE "${wolf_audio_decoder}" APPEND PROPERTY COMPILE_DEFINITIONS WOLF2009_SOURCE_MP3_RECEIPT)
set_property(SOURCE "${wolf_audio_decoder}" APPEND PROPERTY COMPILE_OPTIONS -std=gnu++11)
set_property(SOURCE "${wolf_audio_decoder}" APPEND PROPERTY INCLUDE_DIRECTORIES "${WOLF2009_ANIMATION_DIR}")
if(WOLF2009_SOURCE_AUDIO_BANK_QA)
    if(NOT WOLF2009_BOOTSTRAP OR NOT WOLF2009_GAMEPLAY)
        message(FATAL_ERROR "Audio bank QA requires WOLF2009_BOOTSTRAP and WOLF2009_GAMEPLAY")
    endif()
    target_sources(wolf2009_audio_bank_core_objects PRIVATE "${WOLF2009_AUDIO_BANK_TESTS}/SourceAudioBankNativeChecks.cpp")
    target_sources(wolf2009_audio_bank_game_objects PRIVATE "${WOLF2009_AUDIO_BANK_TESTS}/ScriptedVOReadinessGameChecks.cpp")
    target_compile_definitions(wolf2009_audio_bank_core_objects PRIVATE WOLF2009_SOURCE_AUDIO_BANK_QA)
    target_compile_definitions(wolf2009_audio_bank_game_objects PRIVATE WOLF2009_SOURCE_AUDIO_BANK_QA)
    set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_SOURCE_AUDIO_BANK_QA)
endif()
