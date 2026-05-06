#nullable enable
using System;
using System.IO;

namespace CypressLauncher;

internal static class PatchCli
{
	public static int Run(string[] args)
	{
		if (args.Length == 0)
		{
			PrintUsage();
			return 1;
		}

		string command = args[0].Trim().ToLowerInvariant();
		if (command is "--help" or "-h" or "help")
		{
			PrintUsage();
			return 0;
		}

		if (args.Length < 3)
		{
			PrintUsage();
			return 1;
		}

		if (!PatchManager.TryParseGame(args[1], out MessageHandler.PVZGame game))
		{
			Console.Error.WriteLine("unknown game: " + args[1]);
			PrintUsage();
			return 1;
		}

		string gameDirectory = args[2];
		string exeName = MessageHandler.s_gameToExecutableName[game];
		string dllPath = Path.Combine(AppContext.BaseDirectory, $"cypress_{game}.dll");

		void Log(string text, string level)
		{
			string prefix = level.ToLowerInvariant() switch
			{
				"error" => "[error]",
				"success" => "[ ok ]",
				_ => "[info]"
			};
			Console.WriteLine(prefix + " " + text);
		}

		switch (command)
		{
		case "patch":
		case "--patch":
			return PatchManager.EnsurePatched(game, gameDirectory, exeName, dllPath, Log) ? 0 : 1;
		case "restore":
		case "--restore":
			return PatchManager.RestorePatched(game, gameDirectory, exeName, Log) ? 0 : 1;
		case "status":
		case "--status":
			Console.WriteLine(PatchManager.IsPatched(game, gameDirectory, exeName) ? "patched" : "unpatched");
			return 0;
		default:
			Console.Error.WriteLine("unknown command: " + args[0]);
			PrintUsage();
			return 1;
		}
	}

	private static void PrintUsage()
	{
		Console.WriteLine("cypress patch cli");
		Console.WriteLine("usage:");
		Console.WriteLine("  CypressLauncher patch <gw1|gw2|bfn> <game_dir>");
		Console.WriteLine("  CypressLauncher restore <gw1|gw2|bfn> <game_dir>");
		Console.WriteLine("  CypressLauncher status <gw1|gw2|bfn> <game_dir>");
		Console.WriteLine();
		Console.WriteLine("linux:");
		Console.WriteLine("  install xdelta3 or set CYPRESS_XDELTA3 to the binary path");
	}
}
