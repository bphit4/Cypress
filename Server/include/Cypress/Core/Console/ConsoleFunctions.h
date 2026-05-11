#pragma once

#ifdef CYPRESS_BFN
#include <EASTL/vector.h>
#include <vector>

namespace Cypress
{
	typedef std::vector<std::string> ArgList;
	typedef void(__cdecl* ConsoleFunctionPtr)(ArgList);

	struct ConsoleFunction
	{
		ConsoleFunctionPtr FunctionPtr;
		const char* FunctionName;
		const char* FunctionDescription;

		ConsoleFunction(ConsoleFunctionPtr pFunc, const char* funcName, const char* funcDesc)
			: FunctionPtr(pFunc)
			, FunctionName(funcName)
			, FunctionDescription(funcDesc)
		{ }
	};

	extern std::vector<ConsoleFunction> g_consoleFunctions;

	ConsoleFunction* GetFunction(const char* name);
	std::vector<std::string> ParseCommandString(const std::string& str);
	bool HandleCommand(const std::string& command);

#define CYPRESS_CONSOLE_FUNCTION(func, name, desc) \
	{ &func, name, desc }

#define CYPRESS_REGISTER_CONSOLE_FUNCTION(func, name, desc) \
	g_consoleFunctions.push_back({&func, name, desc});
}
#else
#define OFFSET_CONSOLEOBJECTUTIL_PROPERTYFUNC CYPRESS_GW_SELECT(0x140390470, 0x1401A89E0, 0)

namespace Cypress
{
	void registerConsoleMethod(const char* groupName, const char* name, const char* description, void* pfn = (void*)OFFSET_CONSOLEOBJECTUTIL_PROPERTYFUNC, void* juiceFn = (void*)0);

#define CYPRESS_REGISTER_CONSOLE_FUNCTION(groupName, name, description, pfn) \
		Cypress::registerConsoleMethod(groupName, name, description, pfn, 0)
}
#endif