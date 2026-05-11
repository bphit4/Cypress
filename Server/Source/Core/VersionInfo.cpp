#include "pch.h"
#include "Cypress/Core/VersionInfo.h"

std::string GetCypressVersion()
{
	return std::format("{}.{}.{} ({}.{})", CYPRESS_VERSION_MAJOR, CYPRESS_VERSION_MINOR, CYPRESS_VERSION_PATCH, GIT_HASH, GIT_DATE);
}
