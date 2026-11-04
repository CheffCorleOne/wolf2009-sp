cmake_minimum_required(VERSION 3.12)
project(wolf2009_static_skin_policy_checks LANGUAGES C CXX)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/idtech4a_render" "${CMAKE_CURRENT_BINARY_DIR}/render-adapter")
add_executable(wolf2009-static-skin-policy PolicyTest.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/wolf2009/SkinCatalog.cpp")
target_compile_features(wolf2009-static-skin-policy PRIVATE cxx_std_11)
target_include_directories(wolf2009-static-skin-policy PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/idtech4a_gameplay"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/idtech4a_render")
target_link_libraries(wolf2009-static-skin-policy PRIVATE wolf2009_render_check_core)
enable_testing()
add_test(NAME wolf2009_static_skin_policy COMMAND wolf2009-static-skin-policy --self-test)
if(WOLF2009_SP_BASE)
    add_test(NAME wolf2009_static_skin_policy_wagon COMMAND wolf2009-static-skin-policy "${WOLF2009_SP_BASE}")
endif()
