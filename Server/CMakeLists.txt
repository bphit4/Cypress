cmake_minimum_required(VERSION 3.20)
project(Cypress LANGUAGES C CXX)
include(CTest)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Get Git commit info
execute_process(
        COMMAND git rev-parse --short HEAD
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
)

execute_process(
        COMMAND git show -s --format=%cd --date=short
        OUTPUT_VARIABLE GIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
)

if(NOT GIT_HASH)
  set(GIT_HASH "unknown")
endif()

if(NOT GIT_DATE)
  set(GIT_DATE "unknown")
endif()

include(FetchContent)

# Dependencies
### MinHook
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG 1e9ad1eb42db11bfcb65461f687c656612d1b555
)

### json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 8167d2f64122c3ef7e5a0d9cab239fdba37aa54c
)

### Monocypher
FetchContent_Declare(
        monocypher
        GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
        GIT_TAG c0e0efd25b614ef22bb72aa71203a747904c370d
)

FetchContent_MakeAvailable(
  minhook
  nlohmann_json
  monocypher
)

# Options for variants
option(CYPRESS_GW1 "Build GW1 version" OFF)
option(CYPRESS_GW2 "Build GW2 version" OFF)
option(CYPRESS_BFN "Build BFN version" OFF)
option(CYPRESS_CFB27 "Build CFB27 version" OFF)

# Default fallback
if(NOT CYPRESS_GW1 AND NOT CYPRESS_GW2 AND NOT CYPRESS_BFN AND NOT CYPRESS_CFB27)
  set(CYPRESS_GW2 ON)
endif()

# Sources
if(CYPRESS_CFB27)
  set(SOURCES
          dllmain.cpp
          pch.cpp
          Source/CFB27/BridgeBootstrap.cpp
          Source/CFB27/BridgeConfig.cpp
          Source/CFB27/BridgeLog.cpp
          Source/CFB27/MemoryDiscovery.cpp
          Source/CFB27/PatternScanner.cpp
          Source/CFB27/RedirectorHook.cpp
  )
else()
  file(GLOB_RECURSE SOURCES
          dllmain.cpp
          Source/*.cpp
          include/*.cpp
  )
endif()

if(CYPRESS_CFB27 AND BUILD_TESTING)
  add_executable(CFB27BridgeTests
          Tests/CFB27BridgeTests.cpp
          Source/CFB27/BridgeConfig.cpp
          Source/CFB27/BridgeLog.cpp
          Source/CFB27/PatternScanner.cpp
          Source/CFB27/RedirectorHook.cpp
  )
  target_include_directories(CFB27BridgeTests PRIVATE
          ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/Source
  )
  target_compile_definitions(CFB27BridgeTests PRIVATE CYPRESS_CFB27)
  target_link_libraries(CFB27BridgeTests PRIVATE minhook bcrypt.lib ws2_32.lib winhttp.lib)
  add_test(NAME CFB27BridgeTests COMMAND CFB27BridgeTests)
endif()

add_library(Cypress SHARED ${SOURCES})

# Explicitly compile monocypher
target_sources(Cypress PRIVATE
    ${monocypher_SOURCE_DIR}/src/monocypher.c
)

# Includes
target_include_directories(Cypress PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Source
    ${monocypher_SOURCE_DIR}/src
)

# Precompiled headers
target_precompile_headers(Cypress PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)

# Defines
target_compile_definitions(Cypress PUBLIC
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        GIT_HASH="${GIT_HASH}"
        GIT_DATE="${GIT_DATE}"
        GW2SERVERPROJECT_EXPORTS
        _CRT_SECURE_NO_WARNINGS
        _WINDOWS
        _USRDLL
        $<$<BOOL:${CYPRESS_CFB27}>:CYPRESS_GAME_NAME="CFB27">
)

# EA Defines
add_compile_definitions(
        EASTL_ARENA_ALLOCATOR_HEADER="eastl_arena_allocator.h"
        EASTL_USER_CONFIG_HEADER="eastl_config.h"
        EASTLAllocatorType=fb::eastl_arena_allocator
        EASTLAllocatorDefault=fb::GetDefaultAllocator
)

if(CYPRESS_GW1 OR CYPRESS_GW2 OR CYPRESS_CFB27)
  add_compile_definitions(CHAR8_T_DEFINED)
endif()

if(CYPRESS_GW1)
  set(EA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/EA_GW")
elseif(CYPRESS_GW2)
  set(EA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/EA_GW2")
elseif(CYPRESS_CFB27)
  set(EA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/EA_BFN")
else()
  set(EA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/EA_BFN")
endif()

add_subdirectory(${EA_ROOT_DIR}/EABase)
add_subdirectory(${EA_ROOT_DIR}/EASTL)

target_include_directories(EASTL PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Variant-specific defines + output name
if(CYPRESS_GW1)
  target_compile_definitions(Cypress PRIVATE CYPRESS_GW1)
  set_target_properties(Cypress PROPERTIES OUTPUT_NAME "cypress_GW1")
elseif(CYPRESS_GW2)
  target_compile_definitions(Cypress PRIVATE CYPRESS_GW2)
  set_target_properties(Cypress PROPERTIES OUTPUT_NAME "cypress_GW2")
elseif(CYPRESS_BFN)
  target_compile_definitions(Cypress PRIVATE CYPRESS_BFN)
  set_target_properties(Cypress PROPERTIES OUTPUT_NAME "cypress_BFN")
elseif(CYPRESS_CFB27)
  target_compile_definitions(Cypress PRIVATE CYPRESS_CFB27)
  set_target_properties(Cypress PROPERTIES OUTPUT_NAME "cypress_CFB27")
endif()

# MSVC specifics
if(MSVC)
  target_compile_options(Cypress PRIVATE /W3 /MP)
endif()

# Link libraries
target_link_libraries(Cypress PRIVATE
        EABase
        EASTL
        minhook
        nlohmann_json
        ws2_32.lib
        winhttp.lib
        bcrypt.lib
        iphlpapi.lib
)

# Optimization flags
target_compile_options(Cypress PRIVATE
        $<$<CONFIG:Release>:/O2>
)
