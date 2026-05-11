#include "pch.h"
#include "Cypress/Core/Console/ConsoleFunctions.h"

#ifdef CYPRESS_BFN
#include <sstream>
#include <string>

namespace Cypress
{
    ConsoleFunction* GetFunction(const char* name)
    {
        for (auto& function : g_consoleFunctions)
        {
            if (_stricmp(name, function.FunctionName) == 0)
            {
                return &function;
            }
        }
        return nullptr;
    }

    std::vector<std::string> ParseCommandString(const std::string& str)
    {
        std::vector<std::string> words;
        std::string word;
        char ch;
        bool inQuotes = false;

        for (auto&& ch : str) {
            if (ch == '"') {
                inQuotes = !inQuotes;
            }
            else if (ch == ' ' && !inQuotes) {
                if (!word.empty()) {
                    words.push_back(std::move(word));
                    word.clear();
                }
            }
            else {
                word += ch;
            }
        }

        if (!word.empty()) {
            words.push_back(std::move(word));
        }

        return words;
    }

    bool HandleCommand(const std::string& command)
    {
        std::vector<std::string> commandAndArgs = ParseCommandString(command);
        if (commandAndArgs.size() != 0)
        {
            std::string& commandName = commandAndArgs[0];
            ConsoleFunction* func = GetFunction(commandName.c_str());

            if (func)
            {
                commandAndArgs.erase(commandAndArgs.begin());
                func->FunctionPtr(commandAndArgs);

                return true;
            }
        }
        return false;
    }
}
#else
#include <fb/Engine/ConsoleRegistry.h>

namespace Cypress
{
    void registerConsoleMethod(const char* groupName, const char* name, const char* description, void* pfn, void* juiceFn) 
    {
		fb::ConsoleMethod* newMethod = new fb::ConsoleMethod();
		newMethod->pfn = pfn;
		newMethod->name = name;
		newMethod->groupName = groupName;
		newMethod->description = description;
		newMethod->juiceFn = juiceFn;

		fb::ConsoleRegistry::registerConsoleMethods(groupName, newMethod);
    }
}
#endif