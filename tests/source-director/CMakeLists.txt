# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(wolf2009_source_relay LANGUAGES CXX)
add_executable(wolf2009-source-relay SourceRelayTest.cpp ../../src/wolf2009/Entities.cpp)
target_compile_features(wolf2009-source-relay PRIVATE cxx_std_11)
target_include_directories(wolf2009-source-relay PRIVATE ../../src/idtech4a_gameplay ../../src/wolf2009)
enable_testing()
add_test(NAME source_relay_own_key COMMAND wolf2009-source-relay)
