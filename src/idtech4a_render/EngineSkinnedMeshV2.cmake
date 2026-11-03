# SPDX-License-Identifier: GPL-3.0-or-later
# Add after EngineSkinnedModel.cmake; existing v1 and animation targets stay intact.
target_sources(wolf2009_render_bridge PRIVATE "${WOLF2009_CORE_DIR}/SkeletonData.cpp")
option(WOLF2009_MESH_V2_QA "Include geometry-only named skeleton/native MD5 checks" OFF)
if(WOLF2009_MESH_V2_QA)
    if(NOT WOLF2009_BOOTSTRAP OR NOT WOLF2009_RUNTIME_RENDER_QA)
        message(FATAL_ERROR "Mesh v2 QA requires BOOTSTRAP and RUNTIME_RENDER_QA")
    endif()
    add_library(wolf2009_mesh_v2_qa_objects OBJECT
        "${WOLF2009_RENDER_DIR}/../../tests/engine-render/EngineSkinnedMeshV2Checks.cpp")
    target_compile_features(wolf2009_mesh_v2_qa_objects PRIVATE cxx_std_11)
    set_target_properties(wolf2009_mesh_v2_qa_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    get_target_property(wolf_mesh_v2_defines ${DOOM3BINARY} COMPILE_DEFINITIONS)
    target_compile_definitions(wolf2009_mesh_v2_qa_objects PRIVATE ${wolf_mesh_v2_defines} WOLF2009_RENDERER)
    target_include_directories(wolf2009_mesh_v2_qa_objects PRIVATE "${WOLF2009_RENDER_DIR}" "${WOLF2009_CORE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/externlibs"
        "${CMAKE_CURRENT_SOURCE_DIR}/externlibs/miniz")
    target_sources(${DOOM3BINARY} PRIVATE $<TARGET_OBJECTS:wolf2009_mesh_v2_qa_objects>)
    set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND PROPERTY COMPILE_DEFINITIONS WOLF2009_MESH_V2_QA)
endif()
