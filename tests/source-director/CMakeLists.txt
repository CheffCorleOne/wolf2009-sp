# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(wolf2009_source_relay LANGUAGES CXX)
add_executable(wolf2009-source-relay SourceRelayTest.cpp ../../src/wolf2009/Entities.cpp)
target_compile_features(wolf2009-source-relay PRIVATE cxx_std_11)
target_include_directories(wolf2009-source-relay PRIVATE ../../src/idtech4a_gameplay ../../src/wolf2009)
enable_testing()
add_test(NAME source_relay_own_key COMMAND wolf2009-source-relay)
add_executable(wolf2009-source-arrival-tail SourceArrivalTailMain.cpp
    ../../src/wolf2009/TrainArrival.cpp ../../src/wolf2009/TrainArrivalTest.cpp
    ../../src/wolf2009/BrainStartup.cpp ../../src/wolf2009/BrainGraph.cpp
    ../../src/wolf2009/LinearMover.cpp ../../src/wolf2009/Entities.cpp)
target_compile_features(wolf2009-source-arrival-tail PRIVATE cxx_std_11)
target_include_directories(wolf2009-source-arrival-tail PRIVATE ../../src/wolf2009)
add_test(NAME source_arrival_keyed_tail COMMAND wolf2009-source-arrival-tail)
set(WOLF2009_SOURCE_ENTS "" CACHE FILEPATH "Optional owner-supplied decoded trainyard ENTS")
set(WOLF2009_SOURCE_ARRIVAL_BRAIN "" CACHE FILEPATH "Optional owner-supplied original movetrain BRAI")
if(WOLF2009_SOURCE_ENTS AND WOLF2009_SOURCE_ARRIVAL_BRAIN)
    add_test(NAME source_arrival_keyed_tail_original COMMAND wolf2009-source-arrival-tail --source "${WOLF2009_SOURCE_ENTS}" "${WOLF2009_SOURCE_ARRIVAL_BRAIN}")
endif()
