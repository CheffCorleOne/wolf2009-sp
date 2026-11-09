# SPDX-License-Identifier: GPL-3.0-or-later
if(NOT BUILD_D3 OR NOT TARGET ${DOOM3BINARY} OR NOT WOLF2009_GAMEPLAY OR NOT TARGET wolf2009_stream_bridge)
    message(FATAL_ERROR "WOLF2009_AAS140 requires the Doom 3 core and Wolf2009 source maps")
endif()

add_library(wolf2009_aas140_core_objects OBJECT
    "${WOLF2009_AAS_DIR}/WolfAas140Native.cpp"
    "${WOLF2009_CORE_DIR}/AasFile140.cpp")
target_compile_features(wolf2009_aas140_core_objects PRIVATE cxx_std_11)
set_target_properties(wolf2009_aas140_core_objects PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
get_target_property(wolf_aas_core_defines ${DOOM3BINARY} COMPILE_DEFINITIONS)
target_compile_definitions(wolf2009_aas140_core_objects PRIVATE ${wolf_aas_core_defines})
target_include_directories(wolf2009_aas140_core_objects PRIVATE "${WOLF2009_AAS_DIR}"
    "${WOLF2009_CORE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/externlibs" "${CMAKE_CURRENT_SOURCE_DIR}/externlibs/miniz")
target_sources(${DOOM3BINARY} PRIVATE $<TARGET_OBJECTS:wolf2009_aas140_core_objects>)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tools/compilers/aas/AASFileManager.cpp"
    APPEND PROPERTY COMPILE_DEFINITIONS "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,${DOOM3BINARY}>:WOLF2009_AAS140>")

option(WOLF2009_AAS140_QA "Run native trainyard AAS route and lifecycle QA" OFF)
if(WOLF2009_AAS140_QA)
    if(NOT WOLF2009_ANIMATION_V2_QA OR NOT TARGET wolf2009_animation_game_objects)
        message(FATAL_ERROR "AAS 1.40 QA requires native trainyard session fixtures")
    endif()
    get_filename_component(wolf_aas_tests "${WOLF2009_AAS_DIR}/../../tests/source-aas" ABSOLUTE)
    target_sources(wolf2009_aas140_core_objects PRIVATE "${wolf_aas_tests}/TrainyardAasCoreChecks.cpp")
    target_sources(wolf2009_animation_game_objects PRIVATE "${wolf_aas_tests}/TrainyardAasGameChecks.cpp")
    target_include_directories(${DOOM3BINARY} PRIVATE "${wolf_aas_tests}")
    set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_AAS140_QA)
endif()
