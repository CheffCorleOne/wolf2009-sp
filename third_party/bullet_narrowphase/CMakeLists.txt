# Reviewed Bullet subset; see UPSTREAM.json, LICENSE.txt and README.md.
cmake_minimum_required(VERSION 3.12)
add_library(wolf2009_bullet_narrowphase STATIC
    src/LinearMath/btAlignedAllocator.cpp
    src/BulletCollision/CollisionShapes/btCollisionShape.cpp
    src/BulletCollision/CollisionShapes/btConvexShape.cpp
    src/BulletCollision/CollisionShapes/btConvexInternalShape.cpp
    src/BulletCollision/CollisionShapes/btSphereShape.cpp
    src/BulletCollision/NarrowPhaseCollision/btVoronoiSimplexSolver.cpp
    src/BulletCollision/NarrowPhaseCollision/btGjkPairDetector.cpp
    src/BulletCollision/NarrowPhaseCollision/btGjkEpa2.cpp
    src/BulletCollision/NarrowPhaseCollision/btGjkEpaPenetrationDepthSolver.cpp
    src/BulletCollision/NarrowPhaseCollision/btConvexCast.cpp
    src/BulletCollision/NarrowPhaseCollision/btContinuousConvexCollision.cpp)
target_include_directories(wolf2009_bullet_narrowphase PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(wolf2009_bullet_narrowphase PUBLIC BT_USE_DOUBLE_PRECISION)
target_compile_features(wolf2009_bullet_narrowphase PUBLIC cxx_std_11)
set_target_properties(wolf2009_bullet_narrowphase PROPERTIES POSITION_INDEPENDENT_CODE ON)
