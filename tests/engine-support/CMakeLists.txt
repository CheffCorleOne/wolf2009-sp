# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.12)
project(wolf2009_support_gameplay_checks LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/wolf2009"
                 "${CMAKE_CURRENT_BINARY_DIR}/core" EXCLUDE_FROM_ALL)
if(NOT TARGET wolf2009-support-gameplay-map)
add_executable(wolf2009-support-gameplay-map
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/idtech4a_gameplay/SupportGameplayMapTest.cpp")
target_link_libraries(wolf2009-support-gameplay-map PRIVATE wolf2009_support_gameplay_map)
endif()
add_test(NAME wolf2009_support_gameplay_synthetic COMMAND wolf2009-support-gameplay-map --self-test)
set(WOLF2009_SUPPORT_SOURCE_MAP "" CACHE FILEPATH "Optional owner-supplied MPK for a read-only same-resource HKXS/ENTS test")
if(WOLF2009_SUPPORT_SOURCE_MAP)
    add_test(NAME wolf2009_support_gameplay_source COMMAND wolf2009-support-gameplay-map
             --source-map "${WOLF2009_SUPPORT_SOURCE_MAP}")
endif()
