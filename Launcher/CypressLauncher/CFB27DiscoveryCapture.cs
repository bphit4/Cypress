#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

internal sealed record CFB27CaptureInstance(
	int Pid,
	string Game,
	bool IsServer,
	int ClientGamePort,
	int ServerGamePort,
	string StartTime,
	bool HasExited,
	string? LaunchArgs,
	string? MasterUrl,
	string? DynastyUrl,
	int SideChannelPort,
	string? DynastyProfile);

internal sealed record CFB27CaptureResult(bool Ok, string RunId, string RunDirectory, string? Error);

internal sealed class CFB27DiscoveryCapture
{
	private readonly HttpClient _httpClient;
	private readonly Func<string> _appDataRoot;

	public CFB27DiscoveryCapture(HttpClient httpClient, Func<string> appDataRoot)
	{
		_httpClient = httpClient;
		_appDataRoot = appDataRoot;
	}

	public string EvidenceRoot => Path.Combine(_appDataRoot(), "Diagnostics", "CFB27");

	public async Task<CFB27CaptureResult> CaptureAsync(
		string scenario,
		string gameDirectory,
		IReadOnlyCollection<CFB27CaptureInstance> instances,
		IReadOnlyCollection<string> recentEvents)
	{
		string runId = DateTime.Now.ToString("yyyyMMdd_HHmmss");
		string runDir = Path.Combine(EvidenceRoot, "runs", runId);
		Directory.CreateDirectory(runDir);

		try
		{
			var services = await CaptureServicesAsync();
			var endpoints = CaptureEndpoints();
			var liveProcesses = CaptureLiveCFB27Processes();
			var ownedEndpoints = CaptureProcessOwnedEndpoints();
			var knownEaEndpoints = await CaptureKnownEaEndpointsAsync();
			var knownEaMatches = MatchKnownEaEndpoints(endpoints, ownedEndpoints, knownEaEndpoints);
			var env = new JObject
			{
				["computerName"] = Environment.MachineName,
				["osVersion"] = Environment.OSVersion.ToString(),
				["userInteractive"] = Environment.UserInteractive,
				["process64Bit"] = Environment.Is64BitProcess
			};

			var evidence = new JObject
			{
				["runId"] = runId,
				["createdAt"] = DateTime.UtcNow.ToString("o"),
				["scenario"] = string.IsNullOrWhiteSpace(scenario) ? "manual snapshot" : scenario.Trim(),
				["gameDirectory"] = gameDirectory,
				["instances"] = JArray.FromObject(instances),
				["liveProcesses"] = liveProcesses,
				["launchArgs"] = JArray.FromObject(instances.Select(i => new
				{
					i.Pid,
					i.IsServer,
					i.LaunchArgs
				})),
				["environment"] = env,
				["services"] = services,
				["tcpConnections"] = endpoints["tcpConnections"],
				["udpListeners"] = endpoints["udpListeners"],
				["processOwnedEndpoints"] = ownedEndpoints,
				["knownEaEndpoints"] = knownEaEndpoints,
				["knownEaEndpointMatches"] = knownEaMatches,
				["notes"] = "Observation only. No gameplay hooks, offsets, tokens, or anti-cheat data are captured."
			};

			WriteJson(Path.Combine(runDir, "evidence.json"), evidence);
			WriteJson(Path.Combine(runDir, "services.json"), services);
			WriteJson(Path.Combine(runDir, "endpoints.json"), endpoints);
			WriteJson(Path.Combine(runDir, "process-endpoints.json"), ownedEndpoints);
			WriteJson(Path.Combine(runDir, "known-ea-endpoints.json"), knownEaEndpoints);
			WriteJson(Path.Combine(runDir, "known-ea-endpoint-matches.json"), knownEaMatches);
			File.WriteAllLines(Path.Combine(runDir, "launcher-events.log"), SanitizeEvents(recentEvents));
			File.WriteAllText(Path.Combine(runDir, "summary.md"), BuildSummary(runId, evidence, services, instances));

			return new CFB27CaptureResult(true, runId, runDir, null);
		}
		catch (Exception ex)
		{
			File.WriteAllText(Path.Combine(runDir, "capture-error.txt"), ex.ToString());
			return new CFB27CaptureResult(false, runId, runDir, ex.Message);
		}
	}

	private async Task<JObject> CaptureServicesAsync()
	{
		var services = new JObject
		{
			["master"] = await ProbeServiceAsync("http://127.0.0.1:27900/health"),
			["relay"] = await ProbeServiceAsync("http://127.0.0.1:8080/api/relays"),
			["dynasty"] = await ProbeServiceAsync("http://127.0.0.1:27910/health")
		};
		return services;
	}

	private async Task<JObject> ProbeServiceAsync(string url)
	{
		var result = new JObject
		{
			["url"] = url,
			["ok"] = false
		};
		try
		{
			using var resp = await _httpClient.GetAsync(url);
			string body = await resp.Content.ReadAsStringAsync();
			result["status"] = (int)resp.StatusCode;
			result["ok"] = resp.IsSuccessStatusCode;
			result["body"] = TryParseJson(body) ?? Truncate(body, 4096);
		}
		catch (Exception ex)
		{
			result["error"] = ex.Message;
		}
		return result;
	}

	private static JObject CaptureEndpoints()
	{
		var props = IPGlobalProperties.GetIPGlobalProperties();
		var tcp = new JArray();
		foreach (var c in props.GetActiveTcpConnections())
		{
			tcp.Add(new JObject
			{
				["local"] = c.LocalEndPoint.ToString(),
				["remote"] = c.RemoteEndPoint.ToString(),
				["state"] = c.State.ToString()
			});
		}

		var udp = new JArray();
		foreach (var ep in props.GetActiveUdpListeners())
			udp.Add(ep.ToString());

		return new JObject
		{
			["tcpConnections"] = tcp,
			["udpListeners"] = udp
		};
	}

	private static JObject CaptureProcessOwnedEndpoints()
	{
		var result = new JObject
		{
			["tcpConnections"] = new JArray(),
			["udpListeners"] = new JArray()
		};

		CaptureOwnedEndpointCommand(
			"Get-NetTCPConnection -ErrorAction SilentlyContinue | Select-Object OwningProcess,State,LocalAddress,LocalPort,RemoteAddress,RemotePort | ConvertTo-Json -Depth 3",
			result,
			"tcpConnections");
		CaptureOwnedEndpointCommand(
			"Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Select-Object OwningProcess,LocalAddress,LocalPort | ConvertTo-Json -Depth 3",
			result,
			"udpListeners");

		var processNames = new JObject();
		foreach (var proc in Process.GetProcesses())
		{
			try { processNames[proc.Id.ToString()] = proc.ProcessName; } catch { }
		}
		result["processNames"] = processNames;

		return result;
	}

	private static void CaptureOwnedEndpointCommand(string command, JObject result, string key)
	{
		try
		{
			var psi = new ProcessStartInfo
			{
				FileName = "powershell.exe",
				UseShellExecute = false,
				CreateNoWindow = true,
				RedirectStandardOutput = true,
				RedirectStandardError = true
			};
			psi.ArgumentList.Add("-NoProfile");
			psi.ArgumentList.Add("-ExecutionPolicy");
			psi.ArgumentList.Add("Bypass");
			psi.ArgumentList.Add("-Command");
			psi.ArgumentList.Add(command);

			using var proc = Process.Start(psi);
			if (proc == null)
				return;
			string stdout = proc.StandardOutput.ReadToEnd();
			string stderr = proc.StandardError.ReadToEnd();
			if (!proc.WaitForExit(5000))
			{
				try { proc.Kill(); } catch { }
				result[$"{key}Error"] = "PowerShell endpoint capture timed out.";
				return;
			}
			if (!string.IsNullOrWhiteSpace(stderr))
				result[$"{key}Warning"] = Truncate(stderr.Trim(), 2048);

			var parsed = TryParseJson(stdout);
			if (parsed is JArray arr)
				result[key] = arr;
			else if (parsed is JObject obj)
				result[key] = new JArray(obj);
		}
		catch (Exception ex)
		{
			result[$"{key}Error"] = ex.Message;
		}
	}

	private static async Task<JObject> CaptureKnownEaEndpointsAsync()
	{
		var services = new JArray();
		var hosts = new JObject();

		foreach (var svc in GetKnownEaServices())
		{
			if (!TryParseServiceUrl(svc.Url, out string host, out int port))
				continue;

			services.Add(new JObject
			{
				["key"] = svc.Key,
				["url"] = svc.Url,
				["host"] = host,
				["port"] = port
			});

			if (hosts[host] == null)
				hosts[host] = await ResolveHostAsync(host);
		}

		return new JObject
		{
			["source"] = "https://gcs.ea.com/application_id/CFB_27_PC_CLIENT?platform=origin&sdk_version=1.31.1-fb&application_version=nil&device_type=pc&language=en&country=US",
			["capturedAt"] = DateTime.UtcNow.ToString("o"),
			["services"] = services,
			["hosts"] = hosts
		};
	}

	private static async Task<JArray> ResolveHostAsync(string host)
	{
		var result = new JArray();
		try
		{
			foreach (var addr in await Dns.GetHostAddressesAsync(host))
				result.Add(addr.ToString());
		}
		catch (Exception ex)
		{
			result.Add("resolve-error: " + ex.Message);
		}
		return result;
	}

	private static JArray MatchKnownEaEndpoints(JObject endpoints, JObject ownedEndpoints, JObject knownEaEndpoints)
	{
		var ipToServices = BuildKnownEaIpMap(knownEaEndpoints);
		var processNames = ownedEndpoints["processNames"] as JObject ?? new JObject();
		var matches = new JArray();

		foreach (var c in (endpoints["tcpConnections"] as JArray ?? new JArray()).OfType<JObject>())
		{
			string remote = ((string?)c["remote"]) ?? "";
			AddKnownMatch(matches, ipToServices, remote, "system", null, null, c["state"]);
		}

		foreach (var c in (ownedEndpoints["tcpConnections"] as JArray ?? new JArray()).OfType<JObject>())
		{
			string remoteAddress = ((string?)c["RemoteAddress"]) ?? "";
			string remotePort = c["RemotePort"]?.ToString() ?? "";
			string remote = string.IsNullOrWhiteSpace(remotePort) ? remoteAddress : $"{remoteAddress}:{remotePort}";
			string? pid = c["OwningProcess"]?.ToString();
			string? processName = pid != null ? (string?)processNames[pid] : null;
			AddKnownMatch(matches, ipToServices, remote, "process-owned", pid, processName, c["State"]);
		}

		return DeduplicateMatches(matches);
	}

	private static Dictionary<string, List<JObject>> BuildKnownEaIpMap(JObject knownEaEndpoints)
	{
		var map = new Dictionary<string, List<JObject>>(StringComparer.OrdinalIgnoreCase);
		var hosts = knownEaEndpoints["hosts"] as JObject ?? new JObject();
		var services = knownEaEndpoints["services"] as JArray ?? new JArray();
		foreach (var svc in services.OfType<JObject>())
		{
			string host = ((string?)svc["host"]) ?? "";
			int port = (int?)svc["port"] ?? 443;
			if (string.IsNullOrWhiteSpace(host))
				continue;

			foreach (var ipToken in (hosts[host] as JArray ?? new JArray()))
			{
				string ip = ((string?)ipToken) ?? "";
				if (string.IsNullOrWhiteSpace(ip) || ip.StartsWith("resolve-error:", StringComparison.OrdinalIgnoreCase))
					continue;

				string key = $"{ip}:{port}";
				if (!map.TryGetValue(key, out var entries))
					map[key] = entries = new List<JObject>();
				entries.Add(new JObject
				{
					["key"] = svc["key"],
					["host"] = host,
					["url"] = svc["url"],
					["port"] = port
				});
			}
		}
		return map;
	}

	private static void AddKnownMatch(JArray matches, Dictionary<string, List<JObject>> ipToServices, string remote, string source, string? pid, string? processName, JToken? state)
	{
		if (!TrySplitEndpoint(remote, out string address, out int port))
			return;
		if (!ipToServices.TryGetValue($"{address}:{port}", out var services))
			return;

		matches.Add(new JObject
		{
			["source"] = source,
			["remote"] = remote,
			["pid"] = pid,
			["processName"] = processName,
			["state"] = state,
			["services"] = JArray.FromObject(services)
		});
	}

	private static JArray DeduplicateMatches(JArray matches)
	{
		var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		var result = new JArray();
		foreach (var match in matches.OfType<JObject>())
		{
			string key = $"{match["source"]}|{match["remote"]}|{match["pid"]}|{match["processName"]}|{match["state"]}|{match["services"]}";
			if (seen.Add(key))
				result.Add(match);
		}
		return result;
	}

	private static bool TrySplitEndpoint(string endpoint, out string address, out int port)
	{
		address = "";
		port = 0;
		if (string.IsNullOrWhiteSpace(endpoint))
			return false;

		string value = endpoint.Trim();
		if (value.StartsWith("[", StringComparison.Ordinal))
		{
			int endBracket = value.IndexOf(']');
			if (endBracket < 0 || endBracket + 2 > value.Length || value[endBracket + 1] != ':')
				return false;
			address = value.Substring(1, endBracket - 1);
			return int.TryParse(value[(endBracket + 2)..], out port);
		}

		int colon = value.LastIndexOf(':');
		if (colon <= 0)
			return false;
		address = value[..colon];
		return int.TryParse(value[(colon + 1)..], out port);
	}

	private static bool TryParseServiceUrl(string url, out string host, out int port)
	{
		host = "";
		port = 443;
		if (!Uri.TryCreate(url, UriKind.Absolute, out var uri))
			return false;
		host = uri.Host;
		port = uri.IsDefaultPort ? (uri.Scheme.Equals("https", StringComparison.OrdinalIgnoreCase) ? 443 : uri.Port) : uri.Port;
		return !string.IsNullOrWhiteSpace(host);
	}

	private static IReadOnlyList<(string Key, string Url)> GetKnownEaServices()
	{
		return new[]
		{
			("eadp.candi.offer.service", "https://gateway.grpc.ea.com:443"),
			("eadp.social.presence.v1", "https://api.k.social.ea.com"),
			("eadp.friends.v1.InviteListNotifications", "https://api.k.social.ea.com"),
			("eadp.friends.notifications", "https://api.k.social.ea.com"),
			("eadp.mcr.query.noncached.service", "https://q-origin-internal.mcr.ea.com"),
			("eadp.pushnotification", "https://pn.tnt-ea.com"),
			("eadp.friends.v1", "https://api.k.social.ea.com"),
			("eadp.identity.v2", "https://accounts.ea.com"),
			("shadowbroker.grpc.service.subscription", "https://gateway.grpc.ea.com:443"),
			("eadp.crossplaycontentmart.v1", "https://api.k.social.ea.com"),
			("eadp.candi.catalog.service", "https://gateway.grpc.ea.com:443"),
			("eadp.instrumentation.service", "https://freeform-river.data.ea.com"),
			("eadp.candi.drm.service", "https://gateway.grpc.ea.com:443"),
			("eadp.auth.account", "https://signin.ea.com/"),
			("eadp.friends.v1.FriendListNotifications", "https://api.k.social.ea.com"),
			("eadp.social.groups.v1.GroupNotifications", "https://api.k.social.ea.com"),
			("eadp.eaid.grpc.model", "https://gateway.grpc.ea.com"),
			("eadp.leaderboards", "https://leaderboards.gameservices.ea.com:11000"),
			("eadp.experimentation.tracking.v2", "https://experimentation-tracking-grpc.data.ea.com"),
			("eadp.experimentation.tracking.v1", "https://experimentation-tracking-grpc.data.ea.com"),
			("eadp.mcr", "https://api.mcr.ea.com:4430"),
			("eadp.friendrecommendations.v1", "https://friend-recommendations.k.social.ea.com"),
			("eadp.chat.tcp", "https://rtm.tnt-ea.com:9000"),
			("eadp.social.privacy.v1.MuteListNotifications", "https://api.k.social.ea.com"),
			("eadp.pin", "https://pin-river-grpc.data.ea.com:443"),
			("eadp.candi.entitlement.v2.service", "https://gateway.grpc.ea.com:443"),
			("eadp.social.privacy.v1.BlockListNotifications", "https://api.k.social.ea.com"),
			("eadp.candi.valuetransfer.service", "https://gateway.grpc.ea.com:443"),
			("eadp.leaderboards.v2", "https://leaderboards-api-ext.leaderboards.ea.com:443"),
			("eadp.chat", "https://rtm.tnt-ea.com"),
			("eadp.candi.catalog.v2.service", "https://gateway.grpc.ea.com:443"),
			("eadp.candi.valuetransfer.v2.service", "https://gateway.grpc.ea.com:443"),
			("eadp.nexus.connect.grpc.v1", "https://accounts.grpc.ea.com"),
			("eadp.eaid.grpc.model.v1", "https://gateway.grpc.ea.com"),
			("eadp.candi.offer.v2.service", "https://gateway.grpc.ea.com:443"),
			("eadp.realtimemessaging", "https://rtm.tnt-ea.com:9000"),
			("eadp.identity", "https://accounts.ea.com"),
			("eadp.identity.proxy", "https://gateway.ea.com/proxy"),
			("eadp.um.v1", "https://pin-em-grpc.data.ea.com:443"),
			("eadp.mcr.query.service", "https://q.mcr.ea.com"),
			("eadp.playersearch.grpc.search.v1.service", "https://gateway.grpc.ea.com"),
			("eadp.playercard.v1", "https://gateway.grpc.ea.com"),
			("eadp.stats", "https://stats.gameservices.ea.com:11000"),
			("eadp.candi.gamecontentpromocodes.service", "https://gateway.grpc.ea.com:443"),
			("eadp.experimentation.grouping.v1", "https://experimentation-grpc.data.ea.com"),
			("eadp.social.privacy.v1", "https://api.k.social.ea.com"),
			("eadp.experimentation.grouping.v2", "https://experimentation-grpc.data.ea.com"),
			("eadp.social.gameinvite.v1", "https://api.k.social.ea.com"),
			("eadp.candi.firstpartycheckout", "https://gateway.grpc.ea.com:443"),
			("eadp.social.groups.v1", "https://groups.social.ea.com"),
			("cfb27.liveConfig.basePrivacyUrl", "https://tos.ea.com/legalapp/webprivacy/"),
			("cfb27.liveConfig.baseTosUrl", "https://tos.ea.com/legalapp/webterms/"),
			("cfb27.liveConfig.eaSportsRedirect", "https://www.easports.com"),
			("cfb27.liveConfig.originStore", "https://www.origin.com/store/"),
			("cfb27.liveConfig.mcrApiHost", "https://api.mcr.ea.com"),
			("cfb27.liveConfig.mcrQueryHost", "https://q.mcr.ea.com"),
			("cfb27.liveConfig.eadpGsCdnBase", "https://eaassets-a.akamaihd.net/gameplayservices/prod/Madden/2026/"),
			("cfb27.liveConfig.datapatchBase", "https://eaassets-a.akamaihd.net/gameplayservices/prod/Madden/2026/datapatch/"),
			("cfb27.liveConfig.datapatchG5", "https://eaassets-a.akamaihd.net/gameplayservices/prod/Madden/2026/datapatch/G5/a99637879ed58a75f6898593a376a476/"),
			("cfb27.liveConfig.netResourceImages", "https://eaassets-a.akamaihd.net/gameplayservices/prod/Madden/2026/resource/pc/"),
			("cfb27.liveConfig.eadpUmCdnBase", "https://eaassets-a.akamaihd.net/prm/em/um/"),
			("cfb27.liveConfig.socialUpsProfile", "https://info.social.ea.com")
		};
	}

	private static JArray CaptureLiveCFB27Processes()
	{
		var processes = new JArray();
		foreach (var p in Process.GetProcessesByName("CollegeFB27").OrderBy(p => p.Id))
		{
			var item = new JObject
			{
				["pid"] = p.Id,
				["processName"] = p.ProcessName
			};
			try { item["startTime"] = p.StartTime.ToString("o"); } catch { }
			try { item["hasExited"] = p.HasExited; } catch { }
			try { item["responding"] = p.Responding; } catch { }
			try { item["mainWindowTitle"] = p.MainWindowTitle; } catch { }
			try { item["path"] = p.MainModule?.FileName ?? ""; } catch { }
			processes.Add(item);
		}
		return processes;
	}

	private static string BuildSummary(string runId, JObject evidence, JObject services, IReadOnlyCollection<CFB27CaptureInstance> instances)
	{
		string ServiceLine(string key)
		{
			var svc = services[key] as JObject;
			return $"- {key}: ok={svc?["ok"] ?? false}, status={svc?["status"] ?? "n/a"}, url={svc?["url"] ?? ""}";
		}

		var liveProcesses = evidence["liveProcesses"] as JArray ?? new JArray();
		string LiveProcessLines()
		{
			if (liveProcesses.Count == 0)
				return "- No live CollegeFB27.exe process was visible to the launcher.";
			return string.Join(Environment.NewLine, liveProcesses
				.OfType<JObject>()
				.Select(p => $"- PID {p["pid"]}, responding={p["responding"] ?? "n/a"}, title=`{p["mainWindowTitle"] ?? ""}`"));
		}

		return string.Join(Environment.NewLine, new[]
		{
			"# CFB27 Discovery Evidence",
			"",
			$"- Run: `{runId}`",
			$"- Created: `{evidence["createdAt"]}`",
			$"- Scenario: `{evidence["scenario"]}`",
			$"- Game directory: `{evidence["gameDirectory"]}`",
			$"- Instances: `{instances.Count}`",
			"",
			"## Services",
			ServiceLine("master"),
			ServiceLine("relay"),
			ServiceLine("dynasty"),
			"",
			"## Launch Args",
			instances.Count == 0
				? "- No CFB27 instances were tracked by the launcher during this capture."
				: string.Join(Environment.NewLine, instances.Select(i => $"- PID {i.Pid}, server={i.IsServer}: `{i.LaunchArgs ?? ""}`")),
			"",
			"## Live Processes",
			LiveProcessLines(),
			"",
			"## Notes",
			"- This evidence is observational only.",
			"- No gameplay hooks, offsets, account tokens, or anti-cheat data are captured.",
			""
		});
	}

	private static void WriteJson(string path, JToken token)
	{
		File.WriteAllText(path, token.ToString(Formatting.Indented));
	}

	private static JToken? TryParseJson(string body)
	{
		try { return JToken.Parse(body); }
		catch { return null; }
	}

	private static IEnumerable<string> SanitizeEvents(IEnumerable<string> events)
	{
		foreach (string e in events)
			yield return e.Replace("CYPRESS_IDENTITY_JWT", "REDACTED", StringComparison.OrdinalIgnoreCase)
				.Replace("CYPRESS_IDENTITY_KEY", "REDACTED", StringComparison.OrdinalIgnoreCase)
				.Replace("token", "redacted", StringComparison.OrdinalIgnoreCase);
	}

	private static string Truncate(string value, int max)
	{
		if (value.Length <= max)
			return value;
		return value[..max] + "...";
	}
}
