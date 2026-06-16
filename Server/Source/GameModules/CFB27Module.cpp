#include "pch.h"

#ifdef CYPRESS_CFB27

#include <GameModules/CFB27Module.h>
#include <Cypress/Core/Logging.h>
#include <cstdlib>
#include <string>

static void LogCFB27Env(const char* key)
{
	const char* value = std::getenv(key);
	if (value && value[0])
		CYPRESS_LOGMESSAGE(LogLevel::Info, std::string(key) + "=" + value);
}

void Cypress::CFB27Module::InitGameHooks()
{
	CYPRESS_LOGMESSAGE(LogLevel::Info, "CFB27 module loaded without Frostbite hooks. Offset mapping is required before gameplay hooks are enabled.");
	LogCFB27Env("CYPRESS_CFB27_DISCOVERY");
	LogCFB27Env("CYPRESS_CFB27_LAUNCH_ARGS");
	LogCFB27Env("CYPRESS_CFB27_DYNASTY_URL");
	LogCFB27Env("CYPRESS_CFB27_DYNASTY_PROFILE");
	LogCFB27Env("CYPRESS_MASTER_URL");
	LogCFB27Env("CYPRESS_SIDE_CHANNEL_PORT");
}

void Cypress::CFB27Module::InitMemPatches()
{
	CYPRESS_LOGMESSAGE(LogLevel::Info, "CFB27 memory patches disabled.");
}

void Cypress::CFB27Module::InitDedicatedServerPatches(Cypress::Server*)
{
	CYPRESS_LOGMESSAGE(LogLevel::Info, "CFB27 dedicated server patches disabled.");
}

void Cypress::CFB27Module::RegisterCommands()
{
	CYPRESS_LOGMESSAGE(LogLevel::Info, "CFB27 console command registration skipped.");
}

#endif
