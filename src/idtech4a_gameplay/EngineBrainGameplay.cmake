# SPDX-License-Identifier: GPL-3.0-or-later
# Include only AFTER the ordinary GPL game target has been created/configured.
if(NOT TARGET game OR NOT WOLF2009_GAMEPLAY OR NOT WOLF2009_CORE_DIR OR NOT WOLF2009_GAMEPLAY_DIR)
    message(FATAL_ERROR "EngineBrainGameplay.cmake requires configured GPL game + WOLF2009_GAMEPLAY")
endif()
add_library(wolf2009_brain_game_objects OBJECT
    "${WOLF2009_GAMEPLAY_DIR}/EngineBrainTask.cpp"
    "${WOLF2009_GAMEPLAY_DIR}/NativeBrainEntities.cpp"
    "${WOLF2009_CORE_DIR}/BrainGraph.cpp"
    "${WOLF2009_CORE_DIR}/BrainFlow.cpp"
    "${WOLF2009_CORE_DIR}/BrainTask.cpp"
    "${WOLF2009_CORE_DIR}/BrainLiteralActions.cpp")
target_compile_features(wolf2009_brain_game_objects PRIVATE cxx_std_11)
set_target_properties(wolf2009_brain_game_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
get_target_property(wolf_brain_game_includes game INCLUDE_DIRECTORIES)
get_target_property(wolf_brain_game_defines game COMPILE_DEFINITIONS)
target_include_directories(wolf2009_brain_game_objects PRIVATE
    ${wolf_brain_game_includes} "${CMAKE_CURRENT_SOURCE_DIR}"
    "${WOLF2009_CORE_DIR}" "${WOLF2009_GAMEPLAY_DIR}")
target_compile_definitions(wolf2009_brain_game_objects PRIVATE ${wolf_brain_game_defines})
option(WOLF2009_BRAIN_QA "Enable own native Brain fixture export (BOOTSTRAP only)" OFF)
if(WOLF2009_BRAIN_QA)
    if(NOT WOLF2009_BOOTSTRAP)
        message(FATAL_ERROR "WOLF2009_BRAIN_QA requires WOLF2009_BOOTSTRAP")
    endif()
    target_compile_definitions(wolf2009_brain_game_objects PRIVATE WOLF2009_BRAIN_QA)
    if(TARGET ${DOOM3BINARY})
        set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
            PROPERTY COMPILE_DEFINITIONS WOLF2009_BRAIN_QA)
    endif()
endif()
if(NOT MSVC)
    target_compile_options(wolf2009_brain_game_objects PRIVATE -Wno-format-security)
endif()
# Class registration must not disappear through archive member selection.
target_sources(game PRIVATE $<TARGET_OBJECTS:wolf2009_brain_game_objects>)
