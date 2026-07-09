#include <CFB27/BridgeConfig.h>
#include <CFB27/BridgeLog.h>
#include <CFB27/MemoryDiscovery.h>
#include <MinHook.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	std::string s_localRedirectorHost;

	struct ImageView
	{
		std::uint8_t* base = nullptr;
		std::size_t size = 0;
		IMAGE_NT_HEADERS64* headers = nullptr;
	};

	ImageView GetImage()
	{
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
		if (!base)
			return {};
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return {};
		auto* headers = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		if (headers->Signature != IMAGE_NT_SIGNATURE)
			return {};
		return {base, headers->OptionalHeader.SizeOfImage, headers};
	}

	template <typename TCallback>
	void ForEachReadableSection(const ImageView& image, TCallback callback)
	{
		if (!image.base || !image.headers)
			return;
		auto* section = IMAGE_FIRST_SECTION(image.headers);
		for (WORD index = 0; index < image.headers->FileHeader.NumberOfSections; ++index, ++section)
		{
			if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0)
				continue;
			const std::size_t offset = section->VirtualAddress;
			if (offset >= image.size)
				continue;
			std::size_t size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
			size = (std::min)(size, image.size - offset);
			if (size == 0)
				continue;
			callback(image.base + offset, size);
		}
	}

	std::vector<std::uint8_t*> FindBytes(
		const ImageView& image,
		const void* needle,
		const std::size_t needleSize)
	{
		std::vector<std::uint8_t*> matches;
		if (!image.base || !needle || needleSize == 0 || needleSize > image.size)
			return matches;
		const auto* expected = static_cast<const std::uint8_t*>(needle);
		ForEachReadableSection(image, [&](std::uint8_t* range, const std::size_t rangeSize)
		{
			if (needleSize > rangeSize)
				return;
			for (std::size_t offset = 0; offset <= rangeSize - needleSize; ++offset)
			{
				if (std::memcmp(range + offset, expected, needleSize) == 0)
					matches.push_back(range + offset);
			}
		});
		return matches;
	}

	std::vector<std::uint8_t*> FindPointer(const ImageView& image, const void* value)
	{
		const auto raw = reinterpret_cast<std::uintptr_t>(value);
		return FindBytes(image, &raw, sizeof(raw));
	}

	std::string FormatRva(const ImageView& image, const void* address)
	{
		if (!image.base || !address)
			return "0";
		const auto rva = reinterpret_cast<const std::uint8_t*>(address) - image.base;
		char buffer[32] = {};
		sprintf_s(buffer, "%llX", static_cast<unsigned long long>(rva));
		return buffer;
	}

	std::string FormatImageSummary(const ImageView& image)
	{
		if (!image.base || !image.headers)
			return "image unavailable";
		char buffer[160] = {};
		sprintf_s(
			buffer,
			"image base=%p size=0x%llX sections=%u entryRVA=0x%X",
			image.base,
			static_cast<unsigned long long>(image.size),
			static_cast<unsigned int>(image.headers->FileHeader.NumberOfSections),
			image.headers->OptionalHeader.AddressOfEntryPoint);
		return buffer;
	}

	bool IsExecutablePointer(const ImageView& image, const void* pointer)
	{
		if (!image.base || !image.headers || !pointer)
			return false;
		const auto* address = reinterpret_cast<const std::uint8_t*>(pointer);
		auto* section = IMAGE_FIRST_SECTION(image.headers);
		for (WORD index = 0; index < image.headers->FileHeader.NumberOfSections; ++index, ++section)
		{
			if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
				continue;
			const std::size_t offset = section->VirtualAddress;
			if (offset >= image.size)
				continue;
			std::size_t size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
			size = (std::min)(size, image.size - offset);
			const auto* begin = image.base + offset;
			const auto* end = begin + size;
			if (address >= begin && address < end)
				return true;
		}
		return false;
	}

	bool IsWritableAddress(const ImageView& image, const void* pointer)
	{
		if (!image.base || !image.headers || !pointer)
			return false;
		const auto* address = reinterpret_cast<const std::uint8_t*>(pointer);
		auto* section = IMAGE_FIRST_SECTION(image.headers);
		for (WORD index = 0; index < image.headers->FileHeader.NumberOfSections; ++index, ++section)
		{
			if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0)
				continue;
			const std::size_t offset = section->VirtualAddress;
			if (offset >= image.size)
				continue;
			std::size_t size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
			size = (std::min)(size, image.size - offset);
			const auto* begin = image.base + offset;
			const auto* end = begin + size;
			if (address >= begin && address < end)
				return true;
		}
		return false;
	}

	struct BearSslX509Class
	{
		std::size_t contextSize;
		void* startChain;
		void* startCert;
		void* append;
		void* endCert;
		void* endChain;
		void* getPkey;
	};

	constexpr std::size_t BearSslMinimalContextSize = 3168;
	constexpr std::size_t BearSslMinimalErrOffset = 320;
	constexpr std::size_t BearSslMinimalKeyUsagesOffset = 336;
	constexpr int BearSslX509Ok = 32;
	constexpr std::uint8_t BearSslKeyUsageBoth = 0x30;
	constexpr std::uintptr_t TrialBearSslEndChainRva = 0x09609DB0;
	constexpr std::uintptr_t CurrentNormalProductionRedirectorHostSlotRva = 0x0AFCAE18;
	constexpr std::uintptr_t CurrentNormalSecureServiceSlotRva = 0x0CB5C9B0;

	using BearSslStartChainFunction = void(__cdecl*)(const void* const*, const char*);
	using BearSslStartCertFunction = void(__cdecl*)(const void* const*, std::uint32_t);
	using BearSslAppendFunction = void(__cdecl*)(const void* const*, const unsigned char*, std::size_t);
	using BearSslEndCertFunction = void(__cdecl*)(const void* const*);
	using BearSslEndChainFunction = unsigned(__cdecl*)(const void* const*);
	using BearSslGetPkeyFunction = const void*(__cdecl*)(const void* const*, unsigned*);

	BearSslStartChainFunction s_originalBearSslStartChain = nullptr;
	BearSslStartCertFunction s_originalBearSslStartCert = nullptr;
	BearSslAppendFunction s_originalBearSslAppend = nullptr;
	BearSslEndCertFunction s_originalBearSslEndCert = nullptr;
	BearSslEndChainFunction s_originalBearSslEndChain = nullptr;
	BearSslGetPkeyFunction s_originalBearSslGetPkey = nullptr;
	const void* s_bearSslX509VTable = nullptr;
	Cypress::CFB27::BridgeLog* s_bearSslLog = nullptr;
	std::atomic_uint s_bearSslBypassLogCount = 0;
	std::atomic_uint s_bearSslCallbackLogCount = 0;

	bool SafeReadContextTable(const void* const* context, const void** table)
	{
		if (!context || !table)
			return false;
		__try
		{
			*table = context[0];
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool SafeForceBearSslOk(const void* const* context, int& previousError)
	{
		__try
		{
			auto* bytes = reinterpret_cast<std::uint8_t*>(const_cast<void*>(reinterpret_cast<const void*>(context)));
			std::memcpy(&previousError, bytes + BearSslMinimalErrOffset, sizeof(previousError));
			const int ok = BearSslX509Ok;
			std::memcpy(bytes + BearSslMinimalErrOffset, &ok, sizeof(ok));
			bytes[BearSslMinimalKeyUsagesOffset] =
				static_cast<std::uint8_t>(bytes[BearSslMinimalKeyUsagesOffset] | BearSslKeyUsageBoth);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void LogBearSslCallback(const char* name, const void* const* context, const std::string& detail = {})
	{
		const unsigned count = s_bearSslCallbackLogCount.fetch_add(1);
		if (!s_bearSslLog || count >= 96)
			return;

		const ImageView image = GetImage();
		const void* table = nullptr;
		const bool haveTable = context && SafeReadContextTable(context, &table);
		const bool tableMatches = !s_bearSslX509VTable || (haveTable && table == s_bearSslX509VTable);
		s_bearSslLog->Write(
			"BearSSL X509 callback[" + std::to_string(count + 1) +
			"] name=" + name +
			" table=" + (haveTable ? ("0x" + FormatRva(image, table)) : std::string("<unreadable>")) +
			" expected=" + (s_bearSslX509VTable ? ("0x" + FormatRva(image, s_bearSslX509VTable)) : std::string("<any>")) +
			" tableMatch=" + (tableMatches ? "true" : "false") +
			(detail.empty() ? std::string() : " " + detail));
	}

	bool LooksLikeBearSslX509Class(const ImageView& image, const BearSslX509Class& table)
	{
		if (table.contextSize != BearSslMinimalContextSize)
			return false;
		const std::array<void*, 6> callbacks{
			table.startChain,
			table.startCert,
			table.append,
			table.endCert,
			table.endChain,
			table.getPkey,
		};
		for (const void* callback : callbacks)
		{
			if (!IsExecutablePointer(image, callback))
				return false;
		}

		std::uintptr_t minimum = reinterpret_cast<std::uintptr_t>(callbacks.front());
		std::uintptr_t maximum = minimum;
		for (const void* callback : callbacks)
		{
			const auto address = reinterpret_cast<std::uintptr_t>(callback);
			minimum = (std::min)(minimum, address);
			maximum = (std::max)(maximum, address);
		}
		return maximum - minimum <= 0x1000;
	}

	std::vector<BearSslX509Class*> FindBearSslX509Classes(const ImageView& image)
	{
		std::vector<BearSslX509Class*> matches;
		ForEachReadableSection(image, [&](std::uint8_t* range, const std::size_t rangeSize)
		{
			if (rangeSize < sizeof(BearSslX509Class))
				return;
			for (std::size_t offset = 0; offset <= rangeSize - sizeof(BearSslX509Class); offset += sizeof(void*))
			{
				auto* candidate = reinterpret_cast<BearSslX509Class*>(range + offset);
				BearSslX509Class snapshot{};
				std::memcpy(&snapshot, candidate, sizeof(snapshot));
				if (LooksLikeBearSslX509Class(image, snapshot))
					matches.push_back(candidate);
			}
		});
		return matches;
	}

	std::uint8_t* RvaToAddress(const ImageView& image, const std::uintptr_t rva, const std::size_t length)
	{
		if (!image.base || rva >= image.size || length > image.size - rva)
			return nullptr;
		return image.base + rva;
	}

	bool SafeReadPointer(const void* address, std::uintptr_t& value)
	{
		__try
		{
			std::memcpy(&value, address, sizeof(value));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	std::string FormatHex(const std::uintptr_t value)
	{
		char buffer[32] = {};
		sprintf_s(buffer, "%llX", static_cast<unsigned long long>(value));
		return buffer;
	}

	bool SafeReadBytes(const void* address, void* output, const std::size_t length)
	{
		if (!address || !output)
			return false;
		__try
		{
			std::memcpy(output, address, length);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	std::string FormatBytes(const std::uint8_t* bytes, const std::size_t length)
	{
		std::string text;
		text.reserve(length * 2);
		char buffer[3] = {};
		for (std::size_t index = 0; index < length; ++index)
		{
			sprintf_s(buffer, "%02X", static_cast<unsigned int>(bytes[index]));
			text += buffer;
		}
		return text;
	}

	bool SafeCStringEquals(const void* address, const char* expected)
	{
		if (!address || !expected)
			return false;
		__try
		{
			return std::strcmp(static_cast<const char*>(address), expected) == 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool PatchPointerSlot(
		const ImageView& image,
		void* slot,
		const std::uintptr_t replacement,
		const char* logPrefix,
		Cypress::CFB27::BridgeLog& log)
	{
		DWORD oldProtection = 0;
		if (!VirtualProtect(slot, sizeof(replacement), PAGE_READWRITE, &oldProtection))
			return false;
		std::memcpy(slot, &replacement, sizeof(replacement));
		DWORD ignored = 0;
		VirtualProtect(slot, sizeof(replacement), oldProtection, &ignored);
		FlushInstructionCache(GetCurrentProcess(), slot, sizeof(replacement));
		log.Write(std::string(logPrefix) + " at RVA 0x" + FormatRva(image, slot));
		return true;
	}

	void __cdecl HookBearSslStartChain(const void* const* context, const char* serverName)
	{
		LogBearSslCallback(
			"start_chain",
			context,
			std::string("server=") + (serverName ? serverName : "<null>"));
		if (s_originalBearSslStartChain)
			s_originalBearSslStartChain(context, serverName);
	}

	void __cdecl HookBearSslStartCert(const void* const* context, const std::uint32_t length)
	{
		LogBearSslCallback("start_cert", context, "length=" + std::to_string(length));
		if (s_originalBearSslStartCert)
			s_originalBearSslStartCert(context, length);
	}

	void __cdecl HookBearSslAppend(const void* const* context, const unsigned char* buffer, const std::size_t length)
	{
		LogBearSslCallback("append", context, "length=" + std::to_string(length));
		if (s_originalBearSslAppend)
			s_originalBearSslAppend(context, buffer, length);
	}

	void __cdecl HookBearSslEndCert(const void* const* context)
	{
		LogBearSslCallback("end_cert", context);
		if (s_originalBearSslEndCert)
			s_originalBearSslEndCert(context);

		int previousError = 0;
		if (context && SafeForceBearSslOk(context, previousError))
			LogBearSslCallback("end_cert_forced_ok", context, "ctxErr=" + std::to_string(previousError));
	}

	unsigned __cdecl HookBearSslEndChain(const void* const* context)
	{
		LogBearSslCallback("end_chain_enter", context);
		const unsigned result = s_originalBearSslEndChain ? s_originalBearSslEndChain(context) : 0;
		const void* table = nullptr;
		const bool haveTable = context && SafeReadContextTable(context, &table);
		const bool tableMatches = !s_bearSslX509VTable || (haveTable && table == s_bearSslX509VTable);

		int previousError = 0;
		const bool patched = context && SafeForceBearSslOk(context, previousError);

		const unsigned count = s_bearSslBypassLogCount.fetch_add(1);
		if (s_bearSslLog && count < 32)
		{
			const ImageView image = GetImage();
			s_bearSslLog->Write(
				"BearSSL X509 end_chain result=" + std::to_string(result) +
				" ctxErr=" + std::to_string(previousError) +
				" table=" + (haveTable ? ("0x" + FormatRva(image, table)) : std::string("<unreadable>")) +
				" expected=" + (s_bearSslX509VTable ? ("0x" + FormatRva(image, s_bearSslX509VTable)) : std::string("<any>")) +
				" tableMatch=" + (tableMatches ? "true" : "false") +
				(patched ? " forced-ok" : " force-ok-failed"));
		}
		return patched ? 0 : result;
	}

	const void* __cdecl HookBearSslGetPkey(const void* const* context, unsigned* usages)
	{
		LogBearSslCallback("get_pkey", context);
		const void* result = s_originalBearSslGetPkey ? s_originalBearSslGetPkey(context, usages) : nullptr;
		if (usages)
			*usages = static_cast<unsigned>(*usages | BearSslKeyUsageBoth);
		return result;
	}

	bool CreateAndEnableBearSslHook(
		void* target,
		void* detour,
		void** original,
		const char* name,
		Cypress::CFB27::BridgeLog& log,
		const bool required)
	{
		if (!target)
		{
			log.Write(std::string("BearSSL X509 ") + name + " hook target missing");
			return !required;
		}

		const auto createStatus = MH_CreateHook(target, detour, original);
		if (createStatus != MH_OK)
		{
			log.Write(
				std::string("BearSSL X509 ") + name +
				" hook creation failed status=" +
				std::to_string(static_cast<int>(createStatus)));
			return !required;
		}

		const auto enableStatus = MH_EnableHook(target);
		if (enableStatus != MH_OK)
		{
			log.Write(
				std::string("BearSSL X509 ") + name +
				" hook enable failed status=" +
				std::to_string(static_cast<int>(enableStatus)));
			return !required;
		}

		log.Write(std::string("BearSSL X509 ") + name + " hook installed");
		return true;
	}

}

namespace Cypress::CFB27
{
	void LogImageDiscoverySummary(BridgeLog& log)
	{
		const ImageView image = GetImage();
		log.Write(FormatImageSummary(image));
		if (!image.base)
			return;

		constexpr char productionHost[] = "spring25.client.blazeredirector.ea.com";
		const auto redirectorStrings = FindBytes(image, productionHost, sizeof(productionHost));
		log.Write("discovery redirector hostname string matches=" + std::to_string(redirectorStrings.size()));
		if (redirectorStrings.size() == 1)
		{
			const auto pointers = FindPointer(image, redirectorStrings.front());
			log.Write("discovery redirector hostname pointer matches=" + std::to_string(pointers.size()));
		}

		const auto bearSslTables = FindBearSslX509Classes(image);
		log.Write("discovery BearSSL X509 vtable candidates=" + std::to_string(bearSslTables.size()));
		for (std::size_t index = 0; index < bearSslTables.size() && index < 8; ++index)
		{
			BearSslX509Class snapshot{};
			std::memcpy(&snapshot, bearSslTables[index], sizeof(snapshot));
			log.Write(
				"discovery BearSSL candidate[" + std::to_string(index) +
				"] tableRVA=0x" + FormatRva(image, bearSslTables[index]) +
				" end_chainRVA=0x" + FormatRva(image, snapshot.endChain));
		}
	}

	bool PatchRedirectorHostnameTable(const BridgeConfig& config, BridgeLog& log)
	{
		const ImageView image = GetImage();
		if (!image.base)
			return false;

		constexpr char productionHost[] = "spring25.client.blazeredirector.ea.com";
		auto* knownSlot = RvaToAddress(image, CurrentNormalProductionRedirectorHostSlotRva, sizeof(std::uintptr_t));
		std::uintptr_t currentHost = 0;
		if (knownSlot && SafeReadPointer(knownSlot, currentHost) && SafeCStringEquals(reinterpret_cast<const void*>(currentHost), productionHost))
		{
			s_localRedirectorHost = config.blazeHost;
			const auto replacement = reinterpret_cast<std::uintptr_t>(s_localRedirectorHost.c_str());
			return PatchPointerSlot(image, knownSlot, replacement, "patched production redirector hostname table fast-path", log);
		}

		const auto strings = FindBytes(image, productionHost, sizeof(productionHost));
		if (strings.size() != 1)
		{
			log.Write("redirector hostname string matches=" + std::to_string(strings.size()));
			return false;
		}

		const auto pointers = FindPointer(image, strings.front());
		std::vector<std::uint8_t*> productionEntries;
		for (auto* pointer : pointers)
		{
			if (pointer + sizeof(std::uintptr_t) * 2 > image.base + image.size)
				continue;
			std::uintptr_t environment = 0;
			std::memcpy(&environment, pointer + sizeof(std::uintptr_t), sizeof(environment));
			if (environment == 0)
				productionEntries.push_back(pointer);
		}
		if (productionEntries.size() != 1)
		{
			log.Write("production redirector table entries=" + std::to_string(productionEntries.size()));
			return false;
		}

		s_localRedirectorHost = config.blazeHost;
		const auto replacement = reinterpret_cast<std::uintptr_t>(s_localRedirectorHost.c_str());
		return PatchPointerSlot(image, productionEntries.front(), replacement, "patched production redirector hostname table", log);
	}

	bool PatchRedirectorServiceNameTable(BridgeLog& log)
	{
		const ImageView image = GetImage();
		if (!image.base)
			return false;

		constexpr char insecureService[] = "standardInsecure_v4";
		constexpr char secureService[] = "standardSecure_v4";
		auto* knownSecureSlot = RvaToAddress(image, CurrentNormalSecureServiceSlotRva, sizeof(std::uintptr_t));
		auto* knownInsecureSlot = RvaToAddress(
			image,
			CurrentNormalSecureServiceSlotRva - sizeof(std::uintptr_t),
			sizeof(std::uintptr_t));
		std::uintptr_t secureAddress = 0;
		std::uintptr_t insecureAddress = 0;
		if (knownSecureSlot && knownInsecureSlot &&
			SafeReadPointer(knownSecureSlot, secureAddress) &&
			SafeReadPointer(knownInsecureSlot, insecureAddress) &&
			SafeCStringEquals(reinterpret_cast<const void*>(secureAddress), secureService) &&
			SafeCStringEquals(reinterpret_cast<const void*>(insecureAddress), insecureService))
		{
			return PatchPointerSlot(
				image,
				knownSecureSlot,
				insecureAddress,
				"patched redirector service-name table fast-path standardSecure_v4->standardInsecure_v4",
				log);
		}

		const auto insecureStrings = FindBytes(image, insecureService, sizeof(insecureService));
		const auto secureStrings = FindBytes(image, secureService, sizeof(secureService));
		if (insecureStrings.size() != 1 || secureStrings.size() != 1)
		{
			log.Write(
				"redirector service-name string matches insecure=" +
				std::to_string(insecureStrings.size()) +
				" secure=" +
				std::to_string(secureStrings.size()));
			return false;
		}

		insecureAddress = reinterpret_cast<std::uintptr_t>(insecureStrings.front());
		secureAddress = reinterpret_cast<std::uintptr_t>(secureStrings.front());
		const auto securePointers = FindPointer(image, secureStrings.front());
		std::vector<std::uint8_t*> serviceSlots;
		for (auto* pointer : securePointers)
		{
			if (!IsWritableAddress(image, pointer))
				continue;
			if (pointer < image.base + sizeof(std::uintptr_t))
				continue;
			std::uintptr_t previous = 0;
			std::memcpy(&previous, pointer - sizeof(std::uintptr_t), sizeof(previous));
			if (previous == insecureAddress)
				serviceSlots.push_back(pointer);
		}

		if (serviceSlots.size() != 1)
		{
			log.Write(
				"redirector service-name secure pointer slots=" +
				std::to_string(serviceSlots.size()) +
				" rawSecurePointers=" +
				std::to_string(securePointers.size()));
			return false;
		}

		auto* slot = serviceSlots.front();
		return PatchPointerSlot(
			image,
			slot,
			insecureAddress,
			"patched redirector service-name table standardSecure_v4->standardInsecure_v4",
			log);
	}

	bool InstallBearSslCertificateBypass(BridgeLog& log)
	{
		if (s_originalBearSslEndChain)
			return true;

		const ImageView image = GetImage();
		if (!image.base)
			return false;

		auto matches = FindBearSslX509Classes(image);
		void* endChain = nullptr;
		if (matches.size() == 1)
		{
			auto* table = matches.front();
			s_bearSslX509VTable = table;
			endChain = table->endChain;
			log.Write(
				"BearSSL X509 structural candidate selected table RVA 0x" +
				FormatRva(image, table) +
				" end_chain RVA 0x" +
				FormatRva(image, endChain));
		}
		else
		{
			auto* fallback = image.base + TrialBearSslEndChainRva;
			if (!IsExecutablePointer(image, fallback))
			{
				log.Write("BearSSL X509 vtable candidates=" + std::to_string(matches.size()) + "; known RVA target was not executable");
				return false;
			}
			s_bearSslX509VTable = nullptr;
			endChain = fallback;
			log.Write("BearSSL X509 vtable candidates=" + std::to_string(matches.size()) + "; using known Trial RVA fallback");
		}

		s_bearSslLog = &log;

		if (s_bearSslX509VTable)
		{
			BearSslX509Class snapshot{};
			std::memcpy(&snapshot, s_bearSslX509VTable, sizeof(snapshot));
			CreateAndEnableBearSslHook(
				snapshot.startChain,
				reinterpret_cast<void*>(&HookBearSslStartChain),
				reinterpret_cast<void**>(&s_originalBearSslStartChain),
				"start_chain",
				log,
				false);
			CreateAndEnableBearSslHook(
				snapshot.startCert,
				reinterpret_cast<void*>(&HookBearSslStartCert),
				reinterpret_cast<void**>(&s_originalBearSslStartCert),
				"start_cert",
				log,
				false);
			CreateAndEnableBearSslHook(
				snapshot.append,
				reinterpret_cast<void*>(&HookBearSslAppend),
				reinterpret_cast<void**>(&s_originalBearSslAppend),
				"append",
				log,
				false);
			CreateAndEnableBearSslHook(
				snapshot.endCert,
				reinterpret_cast<void*>(&HookBearSslEndCert),
				reinterpret_cast<void**>(&s_originalBearSslEndCert),
				"end_cert",
				log,
				false);
			CreateAndEnableBearSslHook(
				snapshot.getPkey,
				reinterpret_cast<void*>(&HookBearSslGetPkey),
				reinterpret_cast<void**>(&s_originalBearSslGetPkey),
				"get_pkey",
				log,
				false);
		}

		if (!CreateAndEnableBearSslHook(
			endChain,
			reinterpret_cast<void*>(&HookBearSslEndChain),
			reinterpret_cast<void**>(&s_originalBearSslEndChain),
			"end_chain",
			log,
			true))
			return false;

		log.Write(
			"BearSSL X509 validation bypass installed at vtable RVA 0x" +
			(s_bearSslX509VTable ? FormatRva(image, s_bearSslX509VTable) : std::string("unchecked")) +
			" end_chain RVA 0x" +
			FormatRva(image, endChain));
		return true;
	}

	void LogProtoSslRuntimeCodeBytes(BridgeLog& log)
	{
		const ImageView image = GetImage();
		if (!image.base)
			return;

		struct RuntimeFunctionProbe
		{
			const char* name;
			std::uintptr_t rva;
			std::size_t length;
		};

		constexpr RuntimeFunctionProbe probes[] = {
			{"close-leaf-a", 0x016DAD7C, 0x100},
			{"close-leaf-b", 0x016D1470, 0x100},
			{"redirector-close-path", 0x016D4934, 0x220},
			{"close-leaf-c", 0x016C37B1, 0x380},
			{"proto-lower-a", 0x016BA6A2, 0x180},
			{"proto-lower-b", 0x016BB400, 0x100},
			{"proto-lower-c", 0x016C2350, 0x80},
			{"higher-blaze-a", 0x022E9580, 0x180},
			{"higher-blaze-b", 0x022E9480, 0x80},
			{"close-stack-a", 0x016D2F00, 0x100},
			{"close-stack-b", 0x0182C2EA, 0x80},
			{"close-stack-c", 0x018353DC, 0x100},
			{"close-stack-d", 0x016D731C, 0x100},
			{"close-stack-e", 0x01844E25, 0x100},
			{"close-stack-f", 0x0181725E, 0x100},
			{"close-stack-g", 0x0183D640, 0x100},
			{"close-stack-h", 0x01822B20, 0x100},
		};

		for (const auto& probe : probes)
		{
			std::vector<std::uint8_t> bytes(probe.length);
			const auto* address = RvaToAddress(image, probe.rva, bytes.size());
			const bool executable = IsExecutablePointer(image, address);
			const bool read = address && !bytes.empty() && SafeReadBytes(address, bytes.data(), bytes.size());
			log.Write(
				std::string("runtime-code ") +
				probe.name +
				" rva=0x" + FormatHex(probe.rva) +
				" length=0x" + FormatHex(bytes.size()) +
				" executable=" + (executable ? "true" : "false") +
				" read=" + (read ? "true" : "false") +
				(read ? (" bytes=" + FormatBytes(bytes.data(), bytes.size())) : std::string()));
		}
	}

	void LogRuntimeRedirectorReferences(BridgeLog& log)
	{
		const ImageView image = GetImage();
		if (!image.base)
			return;

		constexpr char marker[] = "redirector-getServerInstance";
		const auto markers = FindBytes(image, marker, sizeof(marker));
		log.Write("runtime redirector marker matches=" + std::to_string(markers.size()));
		if (markers.size() == 1)
		{
			const auto pointers = FindPointer(image, markers.front());
			log.Write("runtime redirector marker pointer matches=" + std::to_string(pointers.size()));
		}
	}
}
