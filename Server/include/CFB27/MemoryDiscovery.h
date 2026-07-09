#pragma once

namespace Cypress::CFB27
{
	struct BridgeConfig;
	class BridgeLog;

	void LogImageDiscoverySummary(BridgeLog& log);
	bool PatchRedirectorHostnameTable(const BridgeConfig& config, BridgeLog& log);
	bool PatchRedirectorServiceNameTable(BridgeLog& log);
	bool InstallBearSslCertificateBypass(BridgeLog& log);
	void LogProtoSslRuntimeCodeBytes(BridgeLog& log);
	void LogRuntimeRedirectorReferences(BridgeLog& log);
}
