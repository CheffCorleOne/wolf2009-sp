# SPDX-License-Identifier: GPL-3.0-or-later
# Included after the existing wolf2009_render_bridge target, by the owning
# engine integration. Animation manager/game code deliberately stays unchanged.
target_compile_definitions(wolf2009_render_bridge PRIVATE WOLF2009_RENDERER)
target_sources(wolf2009_render_bridge PRIVATE
    "${WOLF2009_RENDER_DIR}/EngineSkinnedModel.cpp"
    "${WOLF2009_RENDER_DIR}/SkinnedModelData.cpp"
    "${WOLF2009_CORE_DIR}/GreeterModel.cpp"
    "${WOLF2009_CORE_DIR}/GreeterAnimation.cpp"
    "${WOLF2009_CORE_DIR}/SkinCatalog.cpp")

option(WOLF2009_SKINNED_QA "Include own source-backed native MD5 integration checks" OFF)
if(WOLF2009_SKINNED_QA)
    if(NOT WOLF2009_BOOTSTRAP OR NOT WOLF2009_RUNTIME_RENDER_QA)
        message(FATAL_ERROR "Skinned model QA requires BOOTSTRAP and RUNTIME_RENDER_QA")
    endif()
    # Keep the legacy renderer sources at C++98. Only this portable-type QA
    # consumer needs C++11, and its object registration must link directly.
    add_library(wolf2009_skinned_qa_objects OBJECT
        "${WOLF2009_RENDER_DIR}/../../tests/engine-render/EngineSkinnedChecks.cpp")
    target_compile_features(wolf2009_skinned_qa_objects PRIVATE cxx_std_11)
    set_target_properties(wolf2009_skinned_qa_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    get_target_property(wolf_skinned_qa_defines ${DOOM3BINARY} COMPILE_DEFINITIONS)
    target_compile_definitions(wolf2009_skinned_qa_objects PRIVATE ${wolf_skinned_qa_defines} WOLF2009_RENDERER)
    target_include_directories(wolf2009_skinned_qa_objects PRIVATE
        "${WOLF2009_CORE_DIR}" "${WOLF2009_RENDER_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/externlibs"
        "${CMAKE_CURRENT_SOURCE_DIR}/externlibs/miniz")
    target_sources(${DOOM3BINARY} PRIVATE $<TARGET_OBJECTS:wolf2009_skinned_qa_objects>)
    set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_SKINNED_QA)
    if(WOLF2009_RUNTIME_GL_QA)
        target_sources(wolf2009_skinned_qa_objects PRIVATE
            "${WOLF2009_RENDER_DIR}/../../tests/engine-render/EngineSkinnedOffscreenChecks.cpp")
        set_property(SOURCE "${WOLF2009_RUNTIME_GL_QA_SOURCE}" APPEND
            PROPERTY COMPILE_DEFINITIONS WOLF2009_SKINNED_QA)
    endif()
endif()
