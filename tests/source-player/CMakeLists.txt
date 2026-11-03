cmake_minimum_required(VERSION 3.16)
project(wolf2009_source_player LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
set(core "${CMAKE_CURRENT_SOURCE_DIR}/../../src/wolf2009")
add_library(source_player_inputs STATIC "${core}/SourcePlayerProfile.cpp"
    "${core}/SourceDefinitions.cpp" "${core}/CompiledDeclarations.cpp"
    "${core}/CompiledDeclarationCatalog.cpp" "${core}/CompiledDeclarationNamespace.cpp")
target_include_directories(source_player_inputs PUBLIC "${core}")
add_executable(wolf2009-source-player SourcePlayerProfileTest.cpp)
target_include_directories(wolf2009-source-player PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(wolf2009-source-player PRIVATE source_player_inputs)
add_test(NAME wolf2009_source_player_synthetic COMMAND wolf2009-source-player --self-test)
set(WOLF2009_SOURCE_PLAYER_GLOBAL_DECLS "" CACHE FILEPATH "Optional owner-supplied global compiled declarations")
set(WOLF2009_SOURCE_PLAYER_GLOBAL_HKXS "" CACHE FILEPATH "Optional owner-supplied global HKXS resource")
if(WOLF2009_SOURCE_PLAYER_GLOBAL_DECLS AND WOLF2009_SOURCE_PLAYER_GLOBAL_HKXS)
    add_test(NAME wolf2009_source_player_original COMMAND wolf2009-source-player
        --source-decls "${WOLF2009_SOURCE_PLAYER_GLOBAL_DECLS}"
        --source-hkxs "${WOLF2009_SOURCE_PLAYER_GLOBAL_HKXS}")
endif()
