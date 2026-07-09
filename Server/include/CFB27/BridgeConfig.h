#pragma once

#include <cstdint>
#include <string>

namespace Cypress::CFB27
{
	inline constexpr const char* SupportedTrialSHA256 =
		"B16F49F6E53F81C084B0E1B2F1EAFB1DA78CE51BEE3660BD5A79ED92C626817D";
	inline constexpr const char* SupportedNormalSHA256 =
		"6BEDDE67760E425DB1165E8745BC472886B73DFF60FB4F127E573D3E40DA98B7";
	inline constexpr const char* SupportedCurrentNormalSHA256 =
		"3A587DEB7E2189E53F87A2899774BB3C66EA863B3C323B602A6820E855A7D6E6";

	struct SupportedBuild
	{
		const char* name;
		const char* sha256;
		bool trial;
	};

	struct BridgeConfig
	{
		std::string blazeHost = "127.0.0.1";
		std::uint16_t blazePort = 27920;
		std::string profile = "LocalPlayer";
		std::string runDirectory = ".";
		std::string endpointsFile = "cfb27-endpoints.json";
		std::string configSource = "environment";
		bool enableBearSslBypass = false;
		bool dumpRuntimeCodeBytes = false;
		bool enableCandidateEndpointRedirects = false;

		static BridgeConfig FromEnvironment();
	};

	std::string SHA256FileHex(const std::string& path);
	const SupportedBuild* FindSupportedBuildBySHA256(const std::string& digest);
	const SupportedBuild* FindSupportedBuild(const std::string& executablePath);
	bool IsSupportedTrialBuild(const std::string& executablePath);
}
