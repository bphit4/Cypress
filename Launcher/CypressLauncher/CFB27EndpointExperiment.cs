#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

internal sealed record CFB27ExperimentResult(bool Ok, string RunId, string Path, string Message, string? Error);

internal sealed class CFB27EndpointExperiment
{
	private const string RulePrefix = "Cypress CFB27 Candidate Block";
	private readonly Func<string> _appDataRoot;

	// A Windows ETL trace is a global, machine-wide session: if the launcher exits or
	// crashes while one is running, it keeps capturing and degrades networking for the
	// whole PC until "netsh trace stop" is run. Track any active session and guarantee
	// it is stopped on process exit as a backstop for the try/finally below.
	private static int s_traceActive = 0;
	private static int s_exitHookInstalled = 0;

	public CFB27EndpointExperiment(Func<string> appDataRoot)
	{
		_appDataRoot = appDataRoot;
		if (System.Threading.Interlocked.Exchange(ref s_exitHookInstalled, 1) == 0)
			AppDomain.CurrentDomain.ProcessExit += static (_, _) => StopTraceIfActiveOnExit();
	}

	private static void StopTraceIfActiveOnExit()
	{
		if (System.Threading.Interlocked.Exchange(ref s_traceActive, 0) == 0)
			return;
		try
		{
			// Synchronous, best-effort stop during shutdown; the async path is unavailable here.
			using var proc = Process.Start(new ProcessStartInfo
			{
				FileName = "netsh.exe",
				UseShellExecute = false,
				CreateNoWindow = true,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				ArgumentList = { "trace", "stop" }
			});
			proc?.WaitForExit(15000);
		}
		catch { }
	}

	public string Root => Path.Combine(_appDataRoot(), "Diagnostics", "CFB27");

	public async Task<CFB27ExperimentResult> CaptureWindowsTraceAsync(int seconds)
	{
		seconds = Math.Clamp(seconds <= 0 ? 30 : seconds, 10, 120);
		string runId = DateTime.Now.ToString("yyyyMMdd_HHmmss");
		string runDir = Path.Combine(Root, "network-traces", runId);
		Directory.CreateDirectory(runDir);
		string etlPath = Path.Combine(runDir, "cfb27-netsh.etl");
		string logPath = Path.Combine(runDir, "trace.log");

		bool started = false;
		try
		{
			var start = await RunProcessAsync("netsh.exe", new[]
			{
				"trace", "start",
				"capture=yes",
				"report=no",
				"persistent=no",
				$"tracefile={etlPath}",
				"maxsize=256"
			});
			await File.AppendAllTextAsync(logPath, "START\r\n" + start.ToText() + "\r\n");
			if (start.ExitCode != 0)
				return new CFB27ExperimentResult(false, runId, runDir, "Failed to start Windows network trace.", start.StdErr + start.StdOut);

			started = true;
			System.Threading.Interlocked.Exchange(ref s_traceActive, 1);

			await Task.Delay(TimeSpan.FromSeconds(seconds));

			var stop = await RunProcessAsync("netsh.exe", new[] { "trace", "stop" });
			started = false;
			System.Threading.Interlocked.Exchange(ref s_traceActive, 0);
			await File.AppendAllTextAsync(logPath, "STOP\r\n" + stop.ToText() + "\r\n");
			WriteJson(Path.Combine(runDir, "summary.json"), new JObject
			{
				["runId"] = runId,
				["seconds"] = seconds,
				["etlPath"] = etlPath,
				["startExitCode"] = start.ExitCode,
				["stopExitCode"] = stop.ExitCode,
				["note"] = "Windows ETL capture. Use for timing/protocol investigation; TLS payloads are not decrypted."
			});

			bool ok = stop.ExitCode == 0 && File.Exists(etlPath);
			return new CFB27ExperimentResult(ok, runId, runDir, ok ? "Windows network trace captured." : "Trace stopped, but ETL was not found.", ok ? null : stop.StdErr + stop.StdOut);
		}
		catch (Exception ex)
		{
			await File.WriteAllTextAsync(Path.Combine(runDir, "trace-error.txt"), ex.ToString());
			return new CFB27ExperimentResult(false, runId, runDir, "Windows network trace failed.", ex.Message);
		}
		finally
		{
			if (started)
			{
				try { await RunProcessAsync("netsh.exe", new[] { "trace", "stop" }); } catch { }
				System.Threading.Interlocked.Exchange(ref s_traceActive, 0);
			}
		}
	}

	public async Task<CFB27ExperimentResult> BlockCandidatesAsync()
	{
		string runId = DateTime.Now.ToString("yyyyMMdd_HHmmss");
		string runDir = Path.Combine(Root, "endpoint-experiments", runId);
		Directory.CreateDirectory(runDir);
		var candidates = LoadCandidateAddresses();
		if (candidates.Count == 0)
			return new CFB27ExperimentResult(false, runId, runDir, "No endpoint candidates found.", "Run diagnostics/trace first.");

		var results = new JArray();
		foreach (string address in candidates)
		{
			string ruleName = $"{RulePrefix} {address}";
			await RunProcessAsync("netsh.exe", new[] { "advfirewall", "firewall", "delete", "rule", $"name={ruleName}" });
			var add = await RunProcessAsync("netsh.exe", new[]
			{
				"advfirewall", "firewall", "add", "rule",
				$"name={ruleName}",
				"dir=out",
				"action=block",
				"enable=yes",
				$"remoteip={address}",
				"profile=any"
			});
			results.Add(new JObject
			{
				["address"] = address,
				["rule"] = ruleName,
				["exitCode"] = add.ExitCode,
				["stdout"] = Truncate(add.StdOut, 2048),
				["stderr"] = Truncate(add.StdErr, 2048)
			});
		}

		WriteJson(Path.Combine(runDir, "block-results.json"), results);
		bool ok = results.OfType<JObject>().All(r => (int?)r["exitCode"] == 0);
		return new CFB27ExperimentResult(
			ok,
			runId,
			runDir,
			ok ? $"Blocked {candidates.Count} CFB27 candidate IP(s)." : "One or more firewall block rules failed.",
			ok ? null : "Windows may require the launcher to run as administrator.");
	}

	public async Task<CFB27ExperimentResult> UnblockCandidatesAsync()
	{
		string runId = DateTime.Now.ToString("yyyyMMdd_HHmmss");
		string runDir = Path.Combine(Root, "endpoint-experiments", runId);
		Directory.CreateDirectory(runDir);
		var candidates = LoadCandidateAddresses();
		var results = new JArray();

		foreach (string address in candidates)
		{
			string ruleName = $"{RulePrefix} {address}";
			var del = await RunProcessAsync("netsh.exe", new[] { "advfirewall", "firewall", "delete", "rule", $"name={ruleName}" });
			results.Add(new JObject
			{
				["address"] = address,
				["rule"] = ruleName,
				["exitCode"] = del.ExitCode,
				["stdout"] = Truncate(del.StdOut, 2048),
				["stderr"] = Truncate(del.StdErr, 2048)
			});
		}

		WriteJson(Path.Combine(runDir, "unblock-results.json"), results);
		bool ok = results.Count == 0 || results.OfType<JObject>().All(r => (int?)r["exitCode"] == 0);
		return new CFB27ExperimentResult(
			ok,
			runId,
			runDir,
			ok ? "Removed CFB27 candidate firewall block rules." : "One or more firewall unblock operations failed.",
			ok ? null : "Windows may require the launcher to run as administrator.");
	}

	private List<string> LoadCandidateAddresses()
	{
		string path = Path.Combine(Root, "cfb27-endpoints.json");
		if (!File.Exists(path))
			return new List<string>();
		var json = JObject.Parse(File.ReadAllText(path));
		return (json["candidates"] as JArray ?? new JArray())
			.OfType<JObject>()
			.Select(c => ((string?)c["address"]) ?? "")
			.Where(a => !string.IsNullOrWhiteSpace(a))
			.Distinct(StringComparer.OrdinalIgnoreCase)
			.OrderBy(a => a, StringComparer.OrdinalIgnoreCase)
			.ToList();
	}

	private static async Task<ProcResult> RunProcessAsync(string fileName, IEnumerable<string> args)
	{
		var psi = new ProcessStartInfo
		{
			FileName = fileName,
			UseShellExecute = false,
			CreateNoWindow = true,
			RedirectStandardOutput = true,
			RedirectStandardError = true
		};
		foreach (string arg in args)
			psi.ArgumentList.Add(arg);
		using var proc = Process.Start(psi) ?? throw new InvalidOperationException("Failed to start " + fileName);
		string stdout = await proc.StandardOutput.ReadToEndAsync();
		string stderr = await proc.StandardError.ReadToEndAsync();
		await proc.WaitForExitAsync();
		return new ProcResult(proc.ExitCode, stdout, stderr);
	}

	private static string Truncate(string value, int max)
	{
		return value.Length <= max ? value : value[..max] + "...";
	}

	private static void WriteJson(string path, JToken token)
	{
		File.WriteAllText(path, token.ToString(Formatting.Indented));
	}

	private sealed record ProcResult(int ExitCode, string StdOut, string StdErr)
	{
		public string ToText() => $"exit={ExitCode}\r\nstdout:\r\n{StdOut}\r\nstderr:\r\n{StdErr}";
	}
}
