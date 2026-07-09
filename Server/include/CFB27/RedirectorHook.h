#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cypress::CFB27
{
	struct BridgeConfig;
	class BridgeLog;

	struct CandidateEndpoint
	{
		std::uint32_t hostOrderAddress = 0;
		std::uint16_t port = 0;
	};

	bool IsBlazeRedirectorHost(const std::string& host);
	bool IsKnownRedirectorIPv4(std::uint32_t hostOrderAddress);
	std::vector<CandidateEndpoint> ParseCandidateEndpointsJson(const std::string& json);
	bool InstallRedirectorHooks(const BridgeConfig& config, BridgeLog& log);
}
