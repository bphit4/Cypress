#include "pch.h"
#include "Cypress/Core/Assert.h"
#include <Cypress/Core/Logging.h>

namespace
{
	bool Cypress_ShouldHideLauncherUI()
	{
		char hideUi[8] = {};
		DWORD length = GetEnvironmentVariableA("CYPRESS_HIDE_LAUNCHER_UI", hideUi, static_cast<DWORD>(std::size(hideUi)));
		return length > 0 && hideUi[0] != '0';
	}
}

void CypressAssert(const char* condition, const char* filename, int lineNumber, const char* msg, ...)
{
	char msgBuf[1024];

	va_list argList;
	va_start(argList, msg);
	vsnprintf(msgBuf, sizeof(msgBuf), msg, argList);
	va_end(argList);

#ifdef _DEBUG
	std::string assertMsg = std::format("\n{}({})\nassertion failed: '{}'\n{}\n", filename, lineNumber, condition, msgBuf);
#else
	std::string assertMsg = std::format("\nassertion failed: '{}'\n{}\n", condition, msgBuf);
#endif

	CYPRESS_LOGMESSAGE(LogLevel::Error, "Assertion failed: {}", msgBuf);

	if (!Cypress_ShouldHideLauncherUI())
	{
		MessageBoxA(GetActiveWindow(), assertMsg.c_str(), "Assertion failed!", MB_ICONERROR);
	}

	if (!IsDebuggerPresent())
	{
		exit(0);
	}
}
