# SPDX-License-Identifier: GPL-3.0-or-later
# Additive to the existing C++11 private animation adapters; API8 stays unchanged.
get_filename_component(WOLF2009_SOURCE_ENTITY_DIR "${WOLF2009_ANIMATION_DIR}/../idtech4a_gameplay" ABSOLUTE)
target_sources(wolf2009_animation_core_objects PRIVATE
    "${WOLF2009_ANIMATION_DIR}/SourceActorBinding.cpp"
    "${WOLF2009_ANIMATION_DIR}/SourceSoundBinding.cpp"
    "${WOLF2009_ANIMATION_DIR}/AnimationWireV2.cpp"
    "${WOLF2009_ANIMATION_DIR}/EngineAnimationBindingV2.cpp"
    "${WOLF2009_CORE_DIR}/SourceRigAnimation.cpp"
    "${WOLF2009_CORE_DIR}/SplineClipData.cpp"
    "${WOLF2009_CORE_DIR}/SplineTransformCodec.cpp")
# Core SkeletonData.cpp already belongs to wolf2009_render_bridge.
target_sources(wolf2009_animation_game_objects PRIVATE
    "${WOLF2009_ANIMATION_DIR}/SourceActorBinding.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/SourceEntityRuntime.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceEntities.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/EngineSourceAF.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/SourceSession.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourcePlayer.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/EngineSourcePlayerHull.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceWeapon.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceWeaponView.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceWeaponPresentation.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceMover.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceMap.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceStaticSkin.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceRegistry.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceDirector.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceAnimatedDoor.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceScriptedActor.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceGreeterRoute.cpp"
    "${WOLF2009_SOURCE_ENTITY_DIR}/NativeSourceScriptedVoiceReadiness.cpp"
    "${WOLF2009_CORE_DIR}/SourceMapBodies.cpp"
    "${WOLF2009_CORE_DIR}/SourceCollisionBinding.cpp"
    "${WOLF2009_CORE_DIR}/BrainStartup.cpp"
    "${WOLF2009_CORE_DIR}/LinearMover.cpp"
    "${WOLF2009_CORE_DIR}/TrainArrival.cpp"
    "${WOLF2009_CORE_DIR}/TrainDoorSignal.cpp"
    "${WOLF2009_RENDER_DIR}/RenderData.cpp"
    "${WOLF2009_RENDER_DIR}/SkinnedModelData.cpp"
    "${WOLF2009_CORE_DIR}/GreeterModel.cpp"
    "${WOLF2009_CORE_DIR}/Material.cpp"
    "${WOLF2009_CORE_DIR}/Geometry.cpp"
    "${WOLF2009_CORE_DIR}/Dds.cpp"
    "${WOLF2009_CORE_DIR}/ZipArchive.cpp"
    "${WOLF2009_CORE_DIR}/SkinCatalog.cpp"
    "${WOLF2009_CORE_DIR}/SourcePlayerProfile.cpp"
    "${WOLF2009_CORE_DIR}/SourcePlayerHull.cpp"
    "${WOLF2009_CORE_DIR}/SourceWeaponProfile.cpp"
    "${WOLF2009_CORE_DIR}/SourceRagdoll.cpp"
    "${WOLF2009_CORE_DIR}/Entities.cpp"
    "${WOLF2009_ANIMATION_DIR}/SourceSoundBinding.cpp"
    "${WOLF2009_CORE_DIR}/SourceSound.cpp"
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
target_compile_definitions(wolf2009_animation_core_objects PRIVATE WOLF2009_SOURCE_SOUND_CORE WOLF2009_SOURCE_ACTOR_CORE)
target_compile_definitions(wolf2009_animation_game_objects PRIVATE WOLF2009_SOURCE_SOUND_GAME WOLF2009_SOURCE_ACTOR_GAME)
target_include_directories(wolf2009_animation_game_objects PRIVATE "${WOLF2009_SOURCE_ENTITY_DIR}")
target_include_directories(wolf2009_animation_core_objects PRIVATE "${WOLF2009_SOURCE_ENTITY_DIR}")
target_include_directories(game PRIVATE "${WOLF2009_SOURCE_ENTITY_DIR}")
# The game-side material catalog reads its explicit owner ZIP through the
# Android system zlib. Core's existing compression backend remains unchanged.
if(ANDROID)
    target_link_libraries(game z)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(game ZLIB::ZLIB)
endif()
if(WOLF2009_ANIMATION_V2_QA)
    if(NOT WOLF2009_BOOTSTRAP)
        message(FATAL_ERROR "Source animation v2 QA requires WOLF2009_BOOTSTRAP")
    endif()
    target_compile_definitions(wolf2009_animation_game_objects PRIVATE WOLF2009_ANIMATION_V2_QA)
    target_sources(wolf2009_animation_core_objects PRIVATE
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-session/SourceSessionFixture.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation-v2/EngineAnimationV2Checks.cpp")
    target_include_directories(${DOOM3BINARY} PRIVATE "${WOLF2009_ANIMATION_DIR}/../../tests/source-session")
    target_sources(wolf2009_animation_game_objects PRIVATE
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-session/SourceSessionGameChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-session/TrainyardSessionGameChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-session/TrainyardArrivalGameChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-director/NativeSourceDirectorChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-scripted-actor/NativeSourceScriptedActorChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-scripted-actor/TrainyardActorFixture.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-animated-door/NativeSourceAnimatedDoorChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-animated-door/TrainyardDoorFixture.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-greeter-route/TrainyardGreeterRouteChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-mover/NativeSourceMoverChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-player/NativeSourcePlayerChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-player-collision/SourcePlayerHullNativeChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-weapon/NativeSourceWeaponChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation-v2/SourceAnimatorV2Checks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/engine-animation-set/SourceControlledChecks.cpp")
    target_sources(wolf2009_animation_game_objects PRIVATE
        "${WOLF2009_ANIMATION_DIR}/../../tests/compiled-definitions/SourceDeclarationRuntimeChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/compiled-definitions/SourceDeclarationSetChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/compiled-definitions/SourceDeclarationStartupChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-actor-binding/SourceActorNativeChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-actor-binding/SourceActorRuntimeChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-entity-runtime/NativeSourceEntityChecks.cpp"
        "${WOLF2009_ANIMATION_DIR}/../../tests/source-entity-runtime/NativeSourceAFChecks.cpp")
    set_property(SOURCE "${WOLF2009_BOOT_DIR}/AndroidRuntime.cpp" APPEND
        PROPERTY COMPILE_DEFINITIONS WOLF2009_ANIMATION_V2_QA)
    if(WOLF2009_RUNTIME_GL_QA)
        target_sources(wolf2009_animation_core_objects PRIVATE
            "${WOLF2009_ANIMATION_DIR}/../../tests/source-weapon-view/SourceWeaponViewCoreChecks.cpp"
            "${WOLF2009_ANIMATION_DIR}/../../tests/source-trainyard-view/TrainyardViewCoreChecks.cpp")
        target_sources(wolf2009_animation_game_objects PRIVATE
            "${WOLF2009_ANIMATION_DIR}/../../tests/source-weapon-view/SourceWeaponViewGameChecks.cpp"
            "${WOLF2009_ANIMATION_DIR}/../../tests/source-trainyard-view/TrainyardViewGameChecks.cpp")
        target_include_directories(wolf2009_animation_game_objects PRIVATE
            "${WOLF2009_ANIMATION_DIR}/../../tests/source-weapon-view")
        set_property(SOURCE "${WOLF2009_RUNTIME_GL_QA_SOURCE}" APPEND
            PROPERTY COMPILE_DEFINITIONS WOLF2009_SOURCE_WEAPON_VIEW_QA WOLF2009_SOURCE_TRAINYARD_VIEW_QA)
    endif()
endif()
