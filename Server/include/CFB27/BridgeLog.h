#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace Cypress::CFB27
{
	class BridgeLog
	{
	public:
		bool Open(const std::string& runDirectory);
		void Write(const std::string& message);
		const std::string& Path() const { return m_path; }

	private:
		std::mutex m_mutex;
		std::ofstream m_stream;
		std::string m_path;
	};
}
