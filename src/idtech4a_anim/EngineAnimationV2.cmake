# SPDX-License-Identifier: GPL-3.0-or-later
# Additive to the existing C++11 private animation adapters; API8 stays unchanged.
target_sources(wolf2009_animation_core_objects PRIVATE
    "${WOLF2009_ANIMATION_DIR}/AnimationWireV2.cpp"
    "${WOLF2009_ANIMATION_DIR}/EngineAnimationBindingV2.cpp"
    "${WOLF2009_CORE_DIR}/SourceRigAnimation.cpp"
    "${WOLF2009_CORE_DIR}/SplineClipData.cpp"
    "${WOLF2009_CORE_DIR}/SplineTransformCodec.cpp")
# Core SkeletonData.cpp already belongs to wolf2009_render_bridge.
target_sources(wolf2009_animation_game_objects PRIVATE
    "${WOLF2009_ANIMATION_DIR}/AnimationWireV2.cpp"
    "${WOLF2009_ANIMATION_DIR}/EngineAnimationSet.cpp"
    "${WOLF2009_ANIMATION_DIR}/SourceControlledEntity.cpp"
    "${WOLF2009_ANIMATION_DIR}/SourceDeclarationRuntime.cpp"
    "${WOLF2009_ANIMATION_DIR}/SourceDeclarationSetRuntime.cpp"
    "${WOLF2009_CORE_DIR}/CompiledDeclarations.cpp"
    "${WOLF2009_CORE_DIR}/CompiledDeclarationCatalog.cpp"
    "${WOLF2009_CORE_DIR}/CompiledDeclarationNamespace.cpp"
    "${WOLF2009_CORE_DIR}/SourceDefinitions.cpp"
    "${WOLF2009_CORE_DIR}/SourceRigAnimation.cpp"
    "${WOLF2009_CORE_DIR}/SplineClipData.cpp"
    "${WOLF2009_CORE_DIR}/SplineTransformCodec.cpp"
    "${WOLF2009_CORE_DIR}/SkeletonData.cpp")
if(WOLF2009_ANIMATION_V2_QA)
    if(NOT WOLF2009_BOOTSTRAP)
        message(FATAL_ERROR "Source animation v2 QA requires WOLF2009_BOOTSTRAP")
    endif()
    target_compile_definitions(wolf2009_animation_game_objects PRIVATE WOLF2009_ANIMATION_V2_QA)
    target_sources(wolf2009_animation_core_objects PRIVATE
        "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation-v2/EngineAnimationV2Checks.cpp")
    target_sources(wolf2009_animation_game_objects PRIVATE
        "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation-v2/SourceAnimatorV2Checks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation-set/SourceControlledChecks.cpp")
    target_sources(wolf2009_animation_game_objects PRIVATE
        "${WOLF2009_ANIMATION_DIR}/../../tests/compiled-definitions/SourceDeclarationRuntimeChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/compiled-definitions/SourceDeclarationSetChecks.cpp")
    set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_ANIMATION_V2_QA)
endif()
