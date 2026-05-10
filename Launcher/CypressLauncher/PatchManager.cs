#nullable enable
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace CypressLauncher;

public static class PatchManager
{
	private const string s_backupExtension = ".bak";
	private const string s_tempPatchedExtension = ".patched";
	private const string s_disabledExtension = ".disabled";
	private const string s_oldExtension = ".old";

	public static bool EnsurePatched(
		MessageHandler.PVZGame game,
		string gameDirectory,
		string sourceExeName,
		string dllSourcePath,
		Action<string, string> sendStatus)
	{
		string exePath = Path.Combine(gameDirectory, sourceExeName);
		if (!File.Exists(exePath))
		{
			sendStatus("Game executable not found.", "error");
			return false;
		}

		if (!File.Exists(dllSourcePath))
		{
			sendStatus("Server DLL not found.", "error");
			return false;
		}

		if (game == MessageHandler.PVZGame.GW1)
		{
			sendStatus("Installing dinput8 hook...", "info");
			if (!InjectDinput8(gameDirectory, dllSourcePath, out string dllError))
			{
				sendStatus(dllError, "error");
				return false;
			}

			return true;
		}

		if (IsPatched(game, gameDirectory, sourceExeName))
		{
			if (!InjectDinput8(gameDirectory, dllSourcePath, out string dllError))
			{
				sendStatus(dllError, "error");
				return false;
			}

			DisableEaServices(gameDirectory);
			return true;
		}

		sendStatus("Patching game files...", "info");

		if (!InjectDinput8(gameDirectory, dllSourcePath, out string injectError))
		{
			sendStatus(injectError, "error");
			return false;
		}

		if (!ApplyGamePatches(game, gameDirectory, exePath, out string patchError))
		{
			sendStatus(patchError, "error");
			return false;
		}

		DisableEaServices(gameDirectory);
		sendStatus("Patch complete.", "success");
		return true;
	}

	public static bool RestorePatched(
		MessageHandler.PVZGame game,
		string gameDirectory,
		string sourceExeName,
		Action<string, string> sendStatus)
	{
		string exePath = Path.Combine(gameDirectory, sourceExeName);
		if (!File.Exists(exePath) && !File.Exists(exePath + s_backupExtension))
		{
			sendStatus("Game executable not found.", "error");
			return false;
		}

		if (game != MessageHandler.PVZGame.GW1)
		{
			if (!RestoreGamePatches(game, gameDirectory, exePath, out string restoreError))
			{
				sendStatus(restoreError, "error");
				return false;
			}
		}

		if (!RestoreInjectedDll(gameDirectory, out string dllError))
		{
			sendStatus(dllError, "error");
			return false;
		}

		RestoreEaServices(gameDirectory);
		sendStatus("Patch restored.", "success");
		return true;
	}

	public static bool IsPatched(MessageHandler.PVZGame game, string gameDirectory, string sourceExeName)
	{
		if (game == MessageHandler.PVZGame.GW1)
			return File.Exists(Path.Combine(gameDirectory, "dinput8.dll"));

		string exePath = Path.Combine(gameDirectory, sourceExeName);
		return File.Exists(exePath + s_backupExtension);
	}

	public static bool TryParseGame(string value, out MessageHandler.PVZGame game)
	{
		switch (value.Trim().ToLowerInvariant())
		{
		case "gw1":
		case "gardenwarfare":
		case "gardenwarfare1":
			game = MessageHandler.PVZGame.GW1;
			return true;
		case "gw2":
		case "gardenwarfare2":
			game = MessageHandler.PVZGame.GW2;
			return true;
		case "bfn":
		case "gw3":
		case "neighborville":
			game = MessageHandler.PVZGame.BFN;
			return true;
		default:
			game = default;
			return false;
		}
	}

	private static bool ApplyGamePatches(
		MessageHandler.PVZGame game,
		string gameDirectory,
		string exePath,
		out string error)
	{
		switch (game)
		{
		case MessageHandler.PVZGame.GW2:
			return ApplyXdeltaPatch(
				exePath,
				Path.Combine(AppContext.BaseDirectory, "patches", "gw2", "GW2.Main_Win64_Retail.exe.xdelta"),
				out error,
				required: true);
		case MessageHandler.PVZGame.BFN:
			if (!ApplyXdeltaPatch(
				exePath,
				Path.Combine(AppContext.BaseDirectory, "patches", "bfn", "PVZBattleforNeighborville.exe.xdelta"),
				out error,
				required: true))
			{
				return false;
			}

			string gameDir = gameDirectory;
			string bfnPatchesDir = Path.Combine(AppContext.BaseDirectory, "patches", "bfn");

			ApplyXdeltaPatch(Path.Combine(gameDir, "Core", "Activation.dll"), Path.Combine(bfnPatchesDir, "Core", "Activation.dll.xdelta"), out _, required: false);
			ApplyXdeltaPatch(Path.Combine(gameDir, "Core", "Activation64.dll"), Path.Combine(bfnPatchesDir, "Core", "Activation64.dll.xdelta"), out _, required: false);

			foreach (string fileName in new[] { "Cleanup.dat", "Cleanup.exe", "Touchup.dat", "Touchup.exe", "installerdata.xml" })
			{
				ApplyXdeltaPatch(
					Path.Combine(gameDir, "__Installer", fileName),
					Path.Combine(bfnPatchesDir, "__Installer", fileName + ".xdelta"),
					out _,
					required: false);
			}

			return true;
		default:
			error = string.Empty;
			return true;
		}
	}

	private static bool RestoreGamePatches(
		MessageHandler.PVZGame game,
		string gameDirectory,
		string exePath,
		out string error)
	{
		if (!RestoreFromBackup(exePath, out error))
			return false;

		if (game == MessageHandler.PVZGame.BFN)
		{
			RestoreFromBackup(Path.Combine(gameDirectory, "Core", "Activation.dll"), out _);
			RestoreFromBackup(Path.Combine(gameDirectory, "Core", "Activation64.dll"), out _);
			foreach (string fileName in new[] { "Cleanup.dat", "Cleanup.exe", "Touchup.dat", "Touchup.exe", "installerdata.xml" })
				RestoreFromBackup(Path.Combine(gameDirectory, "__Installer", fileName), out _);
		}

		return true;
	}

	private static bool ApplyXdeltaPatch(
		string targetPath,
		string patchPath,
		out string error,
		bool required)
	{
		error = string.Empty;
		if (!File.Exists(targetPath) || !File.Exists(patchPath))
		{
			if (required)
				error = "Required file missing for patching: " + Path.GetFileName(targetPath);
			return !required;
		}

		string backupPath = targetPath + s_backupExtension;
		string tempPath = targetPath + s_tempPatchedExtension;

		try
		{
			if (File.Exists(tempPath))
				File.Delete(tempPath);

			if (!File.Exists(backupPath))
				File.Copy(targetPath, backupPath, overwrite: false);

			if (!RunXdelta(targetPath, patchPath, tempPath, out error))
			{
				TryDelete(tempPath);
				return false;
			}

			File.Move(tempPath, targetPath, overwrite: true);
			return true;
		}
		catch (Exception ex)
		{
			TryDelete(tempPath);
			error = "Failed to patch file: " + ex.Message;
			return false;
		}
	}

	private static bool RunXdelta(
		string sourcePath,
		string patchPath,
		string outputPath,
		out string error)
	{
		error = string.Empty;
		ProcessStartInfo? startInfo = CreateXdeltaProcessStartInfo(sourcePath, patchPath, outputPath, out error);
		if (startInfo == null)
			return false;

		try
		{
			using Process? process = Process.Start(startInfo);
			process?.WaitForExit();
			if (process?.ExitCode != 0)
			{
				error = "xdelta3 failed (Code: " + process?.ExitCode.ToString("X") + ")";
				return false;
			}

			if (!File.Exists(outputPath))
			{
				error = "xdelta3 did not produce an output file.";
				return false;
			}

			return true;
		}
		catch (Exception ex)
		{
			error = "Failed to start xdelta3: " + ex.Message;
			return false;
		}
	}

	private static ProcessStartInfo? CreateXdeltaProcessStartInfo(
		string sourcePath,
		string patchPath,
		string outputPath,
		out string error)
	{
		error = string.Empty;
		string[] args = new[] { "-d", "-f", "-s", sourcePath, patchPath, outputPath };
		string? configured = Environment.GetEnvironmentVariable("CYPRESS_XDELTA3");
		if (!string.IsNullOrWhiteSpace(configured))
		{
			return new ProcessStartInfo
			{
				FileName = configured,
				Arguments = JoinArguments(args),
				UseShellExecute = false,
				WorkingDirectory = AppContext.BaseDirectory
			};
		}

		string bundledWindowsBinary = Path.Combine(AppContext.BaseDirectory, "tools", "xdelta3", "xdelta3-x86_64-3.0.10.exe");
		if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
		{
			if (!File.Exists(bundledWindowsBinary))
			{
				error = "xdelta3.exe not found.";
				return null;
			}

			return new ProcessStartInfo
			{
				FileName = bundledWindowsBinary,
				Arguments = JoinArguments(args),
				Verb = "runas",
				UseShellExecute = true,
				WorkingDirectory = AppContext.BaseDirectory
			};
		}

		foreach (string candidate in new[] { "xdelta3", "xdelta" })
		{
			if (CommandExists(candidate))
			{
				return new ProcessStartInfo
				{
					FileName = candidate,
					Arguments = JoinArguments(args),
					UseShellExecute = false,
					WorkingDirectory = AppContext.BaseDirectory
				};
			}
		}

		error = "No xdelta3 binary found. Install xdelta3 or set CYPRESS_XDELTA3.";
		return null;
	}

	private static string JoinArguments(IEnumerable<string> args)
	{
		var parts = new List<string>();
		foreach (string arg in args)
			parts.Add("\"" + arg.Replace("\"", "\\\"") + "\"");
		return string.Join(" ", parts);
	}

	private static bool CommandExists(string command)
	{
		string probe = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "where" : "which";
		try
		{
			using var process = Process.Start(new ProcessStartInfo
			{
				FileName = probe,
				Arguments = command,
				UseShellExecute = false,
				RedirectStandardOutput = true,
				RedirectStandardError = true
			});
			process?.WaitForExit();
			return process?.ExitCode == 0;
		}
		catch
		{
			return false;
		}
	}

	private static bool InjectDinput8(string gameDirectory, string dllSourcePath, out string error)
	{
		error = string.Empty;
		string targetPath = Path.Combine(gameDirectory, "dinput8.dll");
		string backupPath = targetPath + s_oldExtension;

		try
		{
			if (File.Exists(targetPath) && !SameFileContents(dllSourcePath, targetPath))
			{
				if (!File.Exists(backupPath))
					File.Move(targetPath, backupPath);
				else
					File.Delete(targetPath);
			}

			File.Copy(dllSourcePath, targetPath, overwrite: true);
			return true;
		}
		catch (Exception ex)
		{
			error = "Failed to install dinput8.dll: " + ex.Message;
			return false;
		}
	}

	private static bool RestoreInjectedDll(string gameDirectory, out string error)
	{
		error = string.Empty;
		string targetPath = Path.Combine(gameDirectory, "dinput8.dll");
		string backupPath = targetPath + s_oldExtension;

		try
		{
			TryDelete(targetPath);
			if (File.Exists(backupPath))
				File.Move(backupPath, targetPath, overwrite: true);
			return true;
		}
		catch (Exception ex)
		{
			error = "Failed to restore dinput8.dll: " + ex.Message;
			return false;
		}
	}

	private static bool RestoreFromBackup(string targetPath, out string error)
	{
		error = string.Empty;
		string backupPath = targetPath + s_backupExtension;
		if (!File.Exists(backupPath))
			return true;

		try
		{
			if (File.Exists(targetPath))
				File.Delete(targetPath);
			File.Move(backupPath, targetPath);
			return true;
		}
		catch (Exception ex)
		{
			error = "Failed to restore executable: " + ex.Message;
			return false;
		}
	}

	private static void DisableEaServices(string gameDirectory)
	{
		DisableFile(Path.Combine(gameDirectory, "EAAntiCheat.GameServiceLauncher.exe"));
		DisableFile(Path.Combine(gameDirectory, "installScript.vdf"));
	}

	private static void RestoreEaServices(string gameDirectory)
	{
		RestoreDisabledFile(Path.Combine(gameDirectory, "EAAntiCheat.GameServiceLauncher.exe"));
		RestoreDisabledFile(Path.Combine(gameDirectory, "installScript.vdf"));
	}

	private static void DisableFile(string path)
	{
		string disabledPath = path + s_disabledExtension;
		try
		{
			if (!File.Exists(path))
				return;
			if (File.Exists(disabledPath))
				File.Delete(disabledPath);
			File.Move(path, disabledPath);
		}
		catch
		{
		}
	}

	private static void RestoreDisabledFile(string path)
	{
		string disabledPath = path + s_disabledExtension;
		try
		{
			if (!File.Exists(disabledPath))
				return;
			if (File.Exists(path))
				File.Delete(path);
			File.Move(disabledPath, path);
		}
		catch
		{
		}
	}

	internal static bool SameFileContentsSafe(string leftPath, string rightPath)
	{
		try
		{
			return SameFileContents(leftPath, rightPath);
		}
		catch
		{
			return false;
		}
	}

	private static bool SameFileContents(string leftPath, string rightPath)
	{
		var leftInfo = new FileInfo(leftPath);
		var rightInfo = new FileInfo(rightPath);
		if (!leftInfo.Exists || !rightInfo.Exists || leftInfo.Length != rightInfo.Length)
			return false;

		using FileStream left = File.OpenRead(leftPath);
		using FileStream right = File.OpenRead(rightPath);
		for (int leftByte = left.ReadByte(), rightByte = right.ReadByte();
			leftByte != -1 && rightByte != -1;
			leftByte = left.ReadByte(), rightByte = right.ReadByte())
		{
			if (leftByte != rightByte)
				return false;
		}

		return true;
	}

	private static void TryDelete(string path)
	{
		try
		{
			if (File.Exists(path))
				File.Delete(path);
		}
		catch
		{
		}
	}
}
