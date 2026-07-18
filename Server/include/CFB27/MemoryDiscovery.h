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

	// Experimental execution-coverage probe (off by default; gated by
	// BridgeConfig::enableProtoSslVerifyProbe). Arms PAGE_GUARD over the known ProtoSSL
	// runtime-code regions and logs, on first execution, which of those functions run during
	// a live handshake. Purpose: pin the certificate-verify path the game actually uses when
	// the BearSSL end_chain hook never fires. Returns true if the handler and guards installed.
	bool InstallProtoSslVerifyProbe(BridgeLog& log);

	// Hooks the ProtoSSL function at RVA 0x16D1750 (a confirmed call target on the handshake
	// verify path). Logs its return value each call; when force is true, overrides the return
	// with 0 to force certificate acceptance. Returns true if the hook installed.
	bool InstallCertVerifyHook(BridgeLog& log, bool force);

	// Hooks _ProtoSSLUpdate (RVA 0x16E1A40) to capture the connection state pointer, then
	// arms a hardware write breakpoint on state[0x370] (iState) and logs each write with a
	// backtrace. The write of value 3 identifies the certificate-reject verdict. Requires the
	// guard-page probe to be OFF (shared single-step VEH). Returns true if installed.
	bool InstallFailStateWatch(BridgeLog& log);
}
