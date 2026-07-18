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
	inline constexpr const char* SupportedJuly16NormalSHA256 =
		"A048578530F7ED5967DF38803B63AD9B9F04FC71287F1E151C901A94AB240BFD";

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
		// Experimental, off by default. When enabled, arms a guard-page execution probe over
		// the known ProtoSSL runtime-code regions so a single handshake reveals which of those
		// functions actually run during the live certificate decision (i.e. the real verify
		// path that the BearSSL end_chain hook is missing). Diagnostic only; can slow the game.
		bool enableProtoSslVerifyProbe = false;
		// Experimental, off by default. Hooks the ProtoSSL function at RVA 0x16D1750 (a call
		// target on the handshake verify path) and logs its return value. When certVerifyForce
		// is also set, it overrides that return with 0 to force certificate acceptance.
		bool enableCertVerifyHook = false;
		bool certVerifyForce = false;
		// Experimental, off by default. Hooks _ProtoSSLUpdate (RVA 0x16E1A40) to grab the
		// per-connection state pointer, then arms a hardware (debug-register) write watch on
		// the iState field state[0x370] and logs each write with a backtrace. The write of 3
		// (fail) during Certificate processing is the certificate-reject verdict we want to
		// patch. Requires enableProtoSslVerifyProbe OFF (both use single-step VEH).
		bool enableFailStateWatch = false;

		static BridgeConfig FromEnvironment();
	};

	std::string SHA256FileHex(const std::string& path);
	const SupportedBuild* FindSupportedBuildBySHA256(const std::string& digest);
	const SupportedBuild* FindSupportedBuild(const std::string& executablePath);
	bool IsSupportedTrialBuild(const std::string& executablePath);
}
