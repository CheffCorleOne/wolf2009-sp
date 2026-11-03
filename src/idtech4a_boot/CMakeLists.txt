# SPDX-License-Identifier: GPL-3.0-or-later
# Included by the opt-in patch to the pinned upstream engine.
if(NOT ANDROID OR NOT CORE OR NOT BASE)
    message(FATAL_ERROR "WOLF2009_BOOTSTRAP requires Android CORE=ON and GPL Doom3 BASE=ON")
endif()
option(WOLF2009_RUNTIME_GL_QA "Include isolated EGL pbuffer rendering checks" OFF)
set(WOLF2009_RUNTIME_GL_QA_SOURCE "" CACHE FILEPATH "Independent offscreen integration check source")
if(WOLF2009_RUNTIME_GL_QA)
    if(NOT WOLF2009_RENDERER OR NOT EXISTS "${WOLF2009_RUNTIME_GL_QA_SOURCE}")
        message(FATAL_ERROR "Offscreen QA requires WOLF2009_RENDERER and its explicit check source")
    endif()
    target_sources(${DOOM3BINARY} PRIVATE "${WOLF2009_RUNTIME_GL_QA_SOURCE}")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/AndroidRuntime.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/glimp.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_rendertools.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_render.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_RUNTIME_GL_QA)
endif()
target_sources(${DOOM3BINARY} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/AndroidRuntime.cpp")
target_include_directories(${DOOM3BINARY} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
option(WOLF2009_RUNTIME_RENDER_QA "Include actual native render world/model/image integration checks" OFF)
set(WOLF2009_RUNTIME_RENDER_QA_SOURCE "" CACHE FILEPATH "Independent render integration check source")
if(WOLF2009_RUNTIME_RENDER_QA)
    if(NOT WOLF2009_RENDERER OR NOT EXISTS "${WOLF2009_RUNTIME_RENDER_QA_SOURCE}")
        message(FATAL_ERROR "Runtime render QA requires WOLF2009_RENDERER and its explicit check source")
    endif()
    target_sources(${DOOM3BINARY} PRIVATE "${WOLF2009_RUNTIME_RENDER_QA_SOURCE}")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/AndroidRuntime.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_RUNTIME_RENDER_QA)
endif()
