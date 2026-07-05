#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

public partial class MessageHandler
{
	private readonly List<Process> m_diagnosticProcesses = new();
	private readonly Queue<string> m_cfb27RecentEvents = new();
	private readonly object m_cfb27RecentEventsLock = new();
	private CFB27DiscoveryCapture? m_cfb27Capture;
	private CFB27EndpointExperiment? m_cfb27EndpointExperiment;

	private void OnCFB27Diagnostics()
	{
		_ = Task.Run(async () =>
		{
			var result = new JObject
			{
				["type"] = "cfb27DiagnosticsResult",
				["ok"] = false,
				["masterUrl"] = "http://127.0.0.1:27900",
				["relayAddress"] = "127.0.0.1:25201",
				["dynastyUrl"] = "http://127.0.0.1:27910",
				["gatewayUrl"] = "http://127.0.0.1:27920"
			};

			try
			{
				if (!IsCFB27(m_selectedGame))
				{
					result["error"] = "Select CFB27 before running diagnostics.";
					Send(result);
					return;
				}

				string root = FindCypressRoot();
				string servicesDir = Path.Combine(root, "tools", "cypress-servers");
				string buildDir = Path.Combine(servicesDir, "build");
				string masterExe = FindToolExe(servicesDir, buildDir, "master.exe");
				string relayExe = FindToolExe(servicesDir, buildDir, "relay.exe");
				string dynastyExe = FindToolExe(servicesDir, buildDir, "dynasty.exe");
				string gatewayExe = FindToolExe(servicesDir, buildDir, "cfb27gateway.exe");
				string dataDir = Path.Combine(GetAppdataDir(), "Diagnostics", "CFB27");
				Directory.CreateDirectory(dataDir);

				await EnsureDiagnosticProcessAsync("master", masterExe, new[]
				{
					"-bind", "127.0.0.1",
					"-port", "27900",
					"-db", Path.Combine(dataDir, "cypress_master.db"),
					"-secret-file", Path.Combine(dataDir, "moderator_secret.txt")
				}, dataDir, "http://127.0.0.1:27900/health");

				await EnsureDiagnosticProcessAsync("relay", relayExe, new[]
				{
					"-bind", "127.0.0.1",
					"-port", "25201",
					"-api-bind", "127.0.0.1",
					"-api-port", "8080",
					"-relay-host", "127.0.0.1",
					"-lease-file", Path.Combine(dataDir, "relay_leases.json"),
					"-log-file", Path.Combine(dataDir, "relay.log"),
					"-master-url", "http://127.0.0.1:27900",
					"-no-dashboard"
				}, dataDir, "http://127.0.0.1:8080/api/relays");

				await EnsureDiagnosticProcessAsync("dynasty", dynastyExe, new[]
				{
					"-bind", "127.0.0.1",
					"-port", "27910",
					"-schema-root", @"C:\Users\Shadow\Desktop\CFB27\Dynasty_Files",
					"-db", Path.Combine(dataDir, "cfb27_dynasty.db")
				}, dataDir, "http://127.0.0.1:27910/health");

				await EnsureDiagnosticProcessAsync("cfb27gateway", gatewayExe, new[]
				{
					"-bind", "127.0.0.1",
					"-port", "27920",
					"-log-file", Path.Combine(dataDir, "cfb27_gateway.log"),
					"-candidates-file", Path.Combine(dataDir, "cfb27-endpoints.json")
				}, dataDir, "http://127.0.0.1:27920/health");

				await Task.Delay(1200);
				var heartbeat = new JObject
				{
					["game"] = "CFB27",
					["address"] = "127.0.0.1",
					["port"] = 25201,
					["players"] = 0,
					["maxPlayers"] = 32,
					["motd"] = "CFB27 Diagnostics",
					["level"] = "CFB27_Dynasty",
					["mode"] = "OnlineDynasty",
					["dynastyMode"] = "Online Dynasty",
					["leagueName"] = "Diagnostics League",
					["currentStage"] = "diagnostics",
					["teamCount"] = 0,
					["rosterModded"] = false,
					["relayAddress"] = "127.0.0.1:25201"
				};
				using var body = new StringContent(heartbeat.ToString(Formatting.None), Encoding.UTF8, "application/json");
				using var resp = await s_httpClient.PostAsync("http://127.0.0.1:27900/heartbeat", body);
				string text = await resp.Content.ReadAsStringAsync();
				result["heartbeatStatus"] = (int)resp.StatusCode;
				result["heartbeatResponse"] = text;
				result["ok"] = resp.IsSuccessStatusCode;
				if (!resp.IsSuccessStatusCode)
					result["error"] = "Local master rejected the diagnostic heartbeat.";
				result["services"] = await GetCFB27Capture().CaptureAsync("diagnostics service probe", m_gameDirectory, GetCFB27CaptureInstances(), GetCFB27RecentEvents())
					.ContinueWith(t => new JObject { ["evidencePath"] = t.Result.RunDirectory, ["evidenceOk"] = t.Result.Ok });
			}
			catch (Exception ex)
			{
				result["error"] = ex.Message;
			}
			Send(result);
		});
	}

	private void OnCFB27CaptureSnapshot(JObject msg)
	{
		_ = Task.Run(async () =>
		{
			string scenario = ((string?)msg["scenario"]) ?? "manual snapshot";
			var result = await GetCFB27Capture().CaptureAsync(
				scenario,
				m_gameDirectory,
				GetCFB27CaptureInstances(),
				GetCFB27RecentEvents());
			Send(new JObject
			{
				["type"] = "cfb27CaptureResult",
				["ok"] = result.Ok,
				["runId"] = result.RunId,
				["path"] = result.RunDirectory,
				["error"] = result.Error ?? ""
			});
		});
	}

	private void OnCFB27TraceEndpoints(JObject msg)
	{
		_ = Task.Run(async () =>
		{
			int seconds = (int?)msg["seconds"] ?? 30;
			var result = await GetCFB27Capture().TraceLiveEndpointsAsync(seconds);
			Send(new JObject
			{
				["type"] = "cfb27TraceResult",
				["ok"] = result.Ok,
				["runId"] = result.RunId,
				["path"] = result.RunDirectory,
				["eventCount"] = result.EventCount,
				["error"] = result.Error ?? ""
			});
		});
	}

	private void OnCFB27NetworkTrace(JObject msg)
	{
		_ = Task.Run(async () =>
		{
			int seconds = (int?)msg["seconds"] ?? 30;
			var result = await GetCFB27EndpointExperiment().CaptureWindowsTraceAsync(seconds);
			Send(new JObject
			{
				["type"] = "cfb27ExperimentResult",
				["action"] = "networkTrace",
				["ok"] = result.Ok,
				["runId"] = result.RunId,
				["path"] = result.Path,
				["message"] = result.Message,
				["error"] = result.Error ?? ""
			});
		});
	}

	private void OnCFB27BlockCandidates()
	{
		_ = Task.Run(async () =>
		{
			var result = await GetCFB27EndpointExperiment().BlockCandidatesAsync();
			Send(new JObject
			{
				["type"] = "cfb27ExperimentResult",
				["action"] = "blockCandidates",
				["ok"] = result.Ok,
				["runId"] = result.RunId,
				["path"] = result.Path,
				["message"] = result.Message,
				["error"] = result.Error ?? ""
			});
		});
	}

	private void OnCFB27UnblockCandidates()
	{
		_ = Task.Run(async () =>
		{
			var result = await GetCFB27EndpointExperiment().UnblockCandidatesAsync();
			Send(new JObject
			{
				["type"] = "cfb27ExperimentResult",
				["action"] = "unblockCandidates",
				["ok"] = result.Ok,
				["runId"] = result.RunId,
				["path"] = result.Path,
				["message"] = result.Message,
				["error"] = result.Error ?? ""
			});
		});
	}

	private void OnCFB27OpenEvidenceFolder()
	{
		try
		{
			string path = GetCFB27Capture().EvidenceRoot;
			Directory.CreateDirectory(path);
			Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true });
		}
		catch (Exception ex)
		{
			SendStatus("Failed to open evidence folder: " + ex.Message, "error");
		}
	}

	private CFB27DiscoveryCapture GetCFB27Capture()
	{
		return m_cfb27Capture ??= new CFB27DiscoveryCapture(s_httpClient, GetAppdataDir);
	}

	private CFB27EndpointExperiment GetCFB27EndpointExperiment()
	{
		return m_cfb27EndpointExperiment ??= new CFB27EndpointExperiment(GetAppdataDir);
	}

	private List<CFB27CaptureInstance> GetCFB27CaptureInstances()
	{
		lock (m_instanceLock)
		{
			return m_instances.Values
				.Where(i => i.Game == PVZGame.CFB27.ToString())
				.Select(i => new CFB27CaptureInstance(
					i.Pid,
					i.Game,
					i.IsServer,
					i.ClientGamePort,
					i.ServerGamePort,
					i.StartTime.ToString("o"),
					SafeHasExited(i.Process),
					i.Process.StartInfo.Arguments,
					i.Process.StartInfo.Environment.TryGetValue("CYPRESS_MASTER_URL", out var masterUrl) ? masterUrl : "",
					i.Process.StartInfo.Environment.TryGetValue("CYPRESS_CFB27_DYNASTY_URL", out var dynastyUrl) ? dynastyUrl : "",
					i.Process.StartInfo.Environment.TryGetValue("CYPRESS_SIDE_CHANNEL_PORT", out var sideChannel) && int.TryParse(sideChannel, out var sidePort) ? sidePort : 0,
					i.Process.StartInfo.Environment.TryGetValue("CYPRESS_CFB27_DYNASTY_PROFILE", out var profile) ? profile : "default"))
				.ToList();
		}
	}

	private static bool SafeHasExited(Process p)
	{
		try { return p.HasExited; } catch { return true; }
	}

	private void RecordCFB27Event(string line)
	{
		lock (m_cfb27RecentEventsLock)
		{
			m_cfb27RecentEvents.Enqueue($"{DateTime.Now:O} {line}");
			while (m_cfb27RecentEvents.Count > 500)
				m_cfb27RecentEvents.Dequeue();
		}
	}

	private List<string> GetCFB27RecentEvents()
	{
		lock (m_cfb27RecentEventsLock)
			return m_cfb27RecentEvents.ToList();
	}

	private string FindCypressRoot()
	{
		var dir = new DirectoryInfo(AppContext.BaseDirectory);
		while (dir != null)
		{
			if (Directory.Exists(Path.Combine(dir.FullName, "tools", "cypress-servers")) &&
				Directory.Exists(Path.Combine(dir.FullName, "Launcher")))
				return dir.FullName;
			dir = dir.Parent;
		}
		return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", ".."));
	}

	private static string FindToolExe(string servicesDir, string buildDir, string exeName)
	{
		string built = Path.Combine(buildDir, exeName);
		if (File.Exists(built))
			return built;
		string local = Path.Combine(servicesDir, exeName);
		if (File.Exists(local))
			return local;
		throw new FileNotFoundException($"Could not find {exeName}. Run tools/cypress-servers/build.ps1 first.", exeName);
	}

	private async Task EnsureDiagnosticProcessAsync(string name, string exe, IEnumerable<string> args, string workingDir, string healthUrl)
	{
		if (await IsDiagnosticServiceHealthyAsync(healthUrl))
		{
			RecordCFB27Event($"diagnostics: reused existing {name} service at {healthUrl}");
			return;
		}

		var proc = StartDiagnosticProcess(exe, args, workingDir);
		RecordCFB27Event($"diagnostics: started {name} pid={proc.Id}");

		DateTime deadline = DateTime.UtcNow.AddSeconds(5);
		while (DateTime.UtcNow < deadline)
		{
			if (await IsDiagnosticServiceHealthyAsync(healthUrl))
				return;

			if (proc.HasExited)
			{
				string stdout = await ReadProcessStreamSafeAsync(proc.StandardOutput);
				string stderr = await ReadProcessStreamSafeAsync(proc.StandardError);
				throw new InvalidOperationException($"{name} diagnostics service exited early with code {proc.ExitCode}. {stderr} {stdout}".Trim());
			}

			await Task.Delay(250);
		}

		throw new TimeoutException($"{name} diagnostics service did not become healthy at {healthUrl}.");
	}

	private static async Task<bool> IsDiagnosticServiceHealthyAsync(string url)
	{
		try
		{
			using var resp = await s_httpClient.GetAsync(url);
			return resp.IsSuccessStatusCode;
		}
		catch
		{
			return false;
		}
	}

	private static async Task<string> ReadProcessStreamSafeAsync(StreamReader reader)
	{
		try
		{
			if (reader.EndOfStream)
				return "";
			return await reader.ReadToEndAsync();
		}
		catch
		{
			return "";
		}
	}

	private Process StartDiagnosticProcess(string exe, IEnumerable<string> args, string workingDir)
	{
		var psi = new ProcessStartInfo
		{
			FileName = exe,
			WorkingDirectory = workingDir,
			UseShellExecute = false,
			CreateNoWindow = true,
			RedirectStandardOutput = true,
			RedirectStandardError = true
		};
		foreach (string arg in args)
			psi.ArgumentList.Add(arg);
		var proc = Process.Start(psi);
		if (proc == null)
			throw new InvalidOperationException("Failed to start " + exe);
		lock (m_diagnosticProcesses)
			m_diagnosticProcesses.Add(proc);
		return proc;
	}
}
