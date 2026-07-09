#include <CFB27/BridgeLog.h>

#include <Windows.h>

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace Cypress::CFB27
{
	bool BridgeLog::Open(const std::string& runDirectory)
	{
		std::lock_guard lock(m_mutex);
		try
		{
			std::filesystem::create_directories(runDirectory);
			m_path = (std::filesystem::path(runDirectory) / "cfb27-bridge.log").string();
			m_stream.open(m_path, std::ios::out | std::ios::app);
			return m_stream.is_open();
		}
		catch (...)
		{
			return false;
		}
	}

	void BridgeLog::Write(const std::string& message)
	{
		std::lock_guard lock(m_mutex);
		if (!m_stream.is_open())
			return;

		SYSTEMTIME now{};
		GetLocalTime(&now);
		m_stream
			<< std::setfill('0')
			<< std::setw(4) << now.wYear << '-'
			<< std::setw(2) << now.wMonth << '-'
			<< std::setw(2) << now.wDay << 'T'
			<< std::setw(2) << now.wHour << ':'
			<< std::setw(2) << now.wMinute << ':'
			<< std::setw(2) << now.wSecond << '.'
			<< std::setw(3) << now.wMilliseconds
			<< ' ' << message << '\n';
		m_stream.flush();
	}
}
