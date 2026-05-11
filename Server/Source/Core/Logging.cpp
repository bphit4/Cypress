#include "pch.h"
#include <Cypress/Core/Program.h>
#include <Cypress/Core/Logging.h>
#include <Cypress/Core/Config.h>
#include <fb/Engine/Server.h>
#include <SideChannel.h>
#include <mutex>
#include <fstream>

#ifndef CYPRESS_BFN
HWND* g_listBox = (HWND*)OFFSET_LISTBOX;
#endif

static std::mutex g_stdoutMutex;
static std::mutex g_fileLogMutex;
static std::ofstream g_logFile;

LogLevel Cypress_ParseLogLevel(const char* str);

static LogLevel Cypress_GetInitialLogLevel()
{
#ifdef _DEBUG
	LogLevel level = LogLevel::Debug;
#else
	LogLevel level = LogLevel::Info;
#endif

	const char* env = std::getenv("CYPRESS_LOG_LEVEL");
	if (env && *env)
		level = Cypress_ParseLogLevel(env);

	return level;
}

LogLevel g_cypressLogLevel = Cypress_GetInitialLogLevel();

void Cypress_SetLogLevel(LogLevel level)
{
	g_cypressLogLevel = level;
}

LogLevel Cypress_ParseLogLevel(const char* str)
{
	if (!str) return LogLevel::Info;
	std::string s(str);
	for (char& c : s)
		c = (char)tolower((unsigned char)c);

	if (s == "trace") return LogLevel::Trace;
	if (s == "debug") return LogLevel::Debug;
	if (s == "info") return LogLevel::Info;
	if (s == "warning" || s == "warn") return LogLevel::Warning;
	if (s == "error") return LogLevel::Error;
	return LogLevel::Info;
}

void Cypress_InitFileLog()
{
	std::lock_guard<std::mutex> lock(g_fileLogMutex);
	if (g_logFile.is_open()) return;

	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string dir(path);
	auto slash = dir.find_last_of("\\/");
	if (slash != std::string::npos) dir = dir.substr(0, slash);
	std::string logPath = dir + "\\cypress.log";

	g_logFile.open(logPath, std::ios::out | std::ios::trunc);
}

void Cypress_WriteFileLog(const char* msg, LogLevel logLevel)
{
	std::lock_guard<std::mutex> lock(g_fileLogMutex);
	if (!g_logFile.is_open()) return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	char ts[32];
	snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	g_logFile << "[" << ts << "] [" << Cypress_LogLevelToStr(logLevel) << "] " << msg << "\n";
	g_logFile.flush();
}

void Cypress_CloseFileLog()
{
	std::lock_guard<std::mutex> lock(g_fileLogMutex);
	if (g_logFile.is_open()) g_logFile.close();
}

bool Cypress_IsEmbeddedMode()
{
	return g_program && g_program->IsEmbedded();
}

static std::string JsonEscapeString(const char* s)
{
	std::string out;
	out.reserve(strlen(s) + 16);
	for (const char* p = s; *p; ++p)
	{
		switch (*p)
		{
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(*p) < 0x20)
			{
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
				out += buf;
			}
			else
			{
				out += *p;
			}
			break;
		}
	}
	return out;
}

static long long GetTimestampMs()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER t;
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	return (long long)((t.QuadPart - 116444736000000000ULL) / 10000ULL);
}

// Helper: broadcast an event JSON to side-channel subscribers (server or client listener)
static void BroadcastToSubscribers(const nlohmann::json& msg)
{
	if (!g_program) return;
#if(HAS_DEDICATED_SERVER)
	if (g_program->IsServer() && g_program->GetServer() && g_program->GetServer()->GetSideChannel()->IsRunning())
		g_program->GetServer()->GetSideChannel()->BroadcastEvent(msg);
#endif
	if (g_program->IsClient() && g_program->GetClient() && g_program->GetClient()->GetClientListener()->IsRunning())
		g_program->GetClient()->GetClientListener()->BroadcastEvent(msg);
}

void Cypress_EmitJsonLog(const char* msg, LogLevel logLevel)
{
	std::string escaped = JsonEscapeString(msg);
	std::string line = std::format("{{\"t\":\"log\",\"lvl\":\"{}\",\"msg\":\"{}\",\"ts\":{}}}",
		Cypress_LogLevelToStr(logLevel), escaped, GetTimestampMs());
	{
		std::lock_guard<std::mutex> lock(g_stdoutMutex);
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD written;
		std::string lineNl = line + "\n";
		WriteFile(hOut, lineNl.c_str(), (DWORD)lineNl.size(), &written, NULL);
	}
	BroadcastToSubscribers(nlohmann::json::parse(line));
}

void Cypress_EmitJsonServerLog(const char* msg, LogLevel logLevel)
{
	std::string escaped = JsonEscapeString(msg);
	std::string line = std::format("{{\"t\":\"srv\",\"lvl\":\"{}\",\"msg\":\"{}\",\"ts\":{}}}",
		Cypress_LogLevelToStr(logLevel), escaped, GetTimestampMs());
	{
		std::lock_guard<std::mutex> lock(g_stdoutMutex);
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD written;
		std::string lineNl = line + "\n";
		WriteFile(hOut, lineNl.c_str(), (DWORD)lineNl.size(), &written, NULL);
	}
	BroadcastToSubscribers(nlohmann::json::parse(line));
}

void Cypress_EmitJsonStatus(const char* map, const char* mode, const char* col1, const char* col2)
{
	std::string line = std::format("{{\"t\":\"status\",\"col1\":\"{}\",\"col2\":\"{}\",\"ts\":{}}}",
		JsonEscapeString(col1), JsonEscapeString(col2), GetTimestampMs());
	{
		std::lock_guard<std::mutex> lock(g_stdoutMutex);
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD written;
		std::string lineNl = line + "\n";
		WriteFile(hOut, lineNl.c_str(), (DWORD)lineNl.size(), &written, NULL);
	}
	BroadcastToSubscribers(nlohmann::json::parse(line));
}

void Cypress_EmitJsonPlayerEvent(const char* event, int playerId, const char* name, const char* extra)
{
	std::string extraField = extra ? std::format(",\"extra\":\"{}\"", JsonEscapeString(extra)) : "";
	std::string line = std::format("{{\"t\":\"{}\",\"id\":{},\"name\":\"{}\",\"ts\":{}{}}}",
		event, playerId, JsonEscapeString(name), GetTimestampMs(), extraField);
	{
		std::lock_guard<std::mutex> lock(g_stdoutMutex);
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD written;
		std::string lineNl = line + "\n";
		WriteFile(hOut, lineNl.c_str(), (DWORD)lineNl.size(), &written, NULL);
	}
	BroadcastToSubscribers(nlohmann::json::parse(line));
}

void Cypress_WriteRawStdout(const std::string& data)
{
	std::lock_guard<std::mutex> lock(g_stdoutMutex);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD written;
	WriteFile(hOut, data.c_str(), (DWORD)data.size(), &written, NULL);
}

void Cypress_LogToServer(const char* msg, const char* fileName, int lineNumber, LogLevel logLevel)
{
#if(HAS_DEDICATED_SERVER)
    // In embedded mode, emit JSON to stdout instead of listbox
    if (g_program && g_program->IsEmbedded())
    {
        Cypress_EmitJsonServerLog(msg, logLevel);
        return;
    }

    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);

    char timePrefix[64];
    strftime(timePrefix, sizeof(timePrefix), "[%d.%m.%Y %H:%M:%S]", tm_info);

#ifdef CYPRESS_BFN
#if _DEBUG
    const char* filePath = strrchr(fileName, '\\');
    if (filePath == nullptr)
        filePath = fileName;
    else
        ++filePath;

    std::string formattedLog = std::format("{} [{}] [{}:{}] {}", timePrefix, Cypress_LogLevelToStr(logLevel), filePath, lineNumber, msg);
#else
    std::string formattedLog = std::format("{} [{}] [Server] {}", timePrefix, Cypress_LogLevelToStr(logLevel), msg);
#endif

    Cypress::Server* pServer = g_program->GetServer();
    HWND listBox = pServer->GetListBox();

    if (listBox == NULL)
        std::cout << "\x1B[36m[SrvLog]" << formattedLog.c_str() << "\x1B[0m";
    else
    {
        if (pServer->GetServerLogEnabled())
        {
            int pos = (int)SendMessageA(listBox, LB_ADDSTRING, 0, (LPARAM)formattedLog.c_str());
            if (pos >= 1000)
            {
                SendMessage(listBox, LB_DELETESTRING, 0, 0);
                pos--;
            }

            SendMessage(listBox, LB_SETCURSEL, pos, 1);
        }
    }
#else // GW1 / GW2
    const char* filePath = strrchr(fileName, '\\');
    if (filePath == nullptr)
        filePath = fileName;
    else
        ++filePath;

    std::string formattedLog = std::format("{} [{}] [{}:{}] {}", timePrefix, Cypress_LogLevelToStr(logLevel), filePath, lineNumber, msg);

    if (*g_listBox == NULL)
        std::cout << "\x1B[36m[SrvLog]" << formattedLog.c_str() << "\x1B[0m";
    else
    {
        if(g_program->GetServer()->GetServerLogEnabled())
        {
            int pos = (int)SendMessageA(*g_listBox, LB_ADDSTRING, 0, (LPARAM)formattedLog.c_str());
            if (pos >= 1000)
            {
                SendMessage(*g_listBox, LB_DELETESTRING, 0, 0);
                pos--;
            }

            SendMessage(*g_listBox, LB_SETCURSEL, pos, 1);
        }
    }
#endif // CYPRESS_BFN
#endif // HAS_DEDICATED_SERVER
}
