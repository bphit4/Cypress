#pragma once
#include <string>
#include <format>

#define CYPRESS_VERSION_MAJOR 2
#define CYPRESS_VERSION_MINOR 0
#define CYPRESS_VERSION_PATCH 0

#ifdef _DEBUG
#define CYPRESS_BUILD_CONFIG "Debug"
#else
#define CYPRESS_BUILD_CONFIG "Release"
#endif

#ifdef CYPRESS_GW1
#define CYPRESS_GAME_NAME "GW1"
#elif CYPRESS_GW2
#define CYPRESS_GAME_NAME "GW2"
#elif CYPRESS_BFN
#define CYPRESS_GAME_NAME "BFN"
#endif

// defined in VersionInfo.cpp, force-rebuilt every build so timestamp is accurate
std::string GetCypressVersion();