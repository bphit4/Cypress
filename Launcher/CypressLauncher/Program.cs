#nullable enable
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Photino.NET;

namespace CypressLauncher;

internal static class Program
{
	private static readonly bool s_isWindows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);

	[DllImport("shell32.dll", CharSet = CharSet.Unicode)]
	private static extern int SetCurrentProcessExplicitAppUserModelID(string appID);

	[DllImport("user32.dll")]
	private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
	private const int SW_HIDE = 0;
	private const int SW_SHOW = 5;

	[DllImport("user32.dll")]
	private static extern short GetAsyncKeyState(int vKey);
	private const int VK_MENU = 0x12;

	[DllImport("user32.dll")]
	private static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint dwFlags);
	[DllImport("shcore.dll")]
	private static extern int GetDpiForMonitor(IntPtr hMon, int dpiType, out uint dpiX, out uint dpiY);
	[DllImport("user32.dll")]
	private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
	private const uint SWP_NOMOVE = 0x0002;
	private const uint SWP_NOZORDER = 0x0004;

	private static void ApplyPhysicalSize(PhotinoWindow window, int logicalW, int logicalH)
	{
		var hwnd = window.WindowHandle;
		var mon = MonitorFromWindow(hwnd, 2); // MONITOR_DEFAULTTONEAREST
		GetDpiForMonitor(mon, 0, out uint dpiX, out _);
		int physW = (int)Math.Round(logicalW * dpiX / 96.0);
		int physH = (int)Math.Round(logicalH * dpiX / 96.0);
		Console.Error.WriteLine($"[debug] dpi={dpiX} phys={physW}x{physH}");
		SetWindowPos(hwnd, IntPtr.Zero, 0, 0, physW, physH, SWP_NOMOVE | SWP_NOZORDER);
	}

	[STAThread]
	private static void Main(string[] args)
	{
		try
		{
			// ensure working directory is the app folder (start menu shortcuts set it to system32)
			Environment.CurrentDirectory = AppContext.BaseDirectory;

			if (args.Length > 0)
			{
				Environment.ExitCode = PatchCli.Run(args);
				return;
			}

#if WINDOWS
			SetCurrentProcessExplicitAppUserModelID("CypressLauncher.App");
#endif

			var handler = new MessageHandler();
			string html = ResourceBuilder.BuildHtml(Assembly.GetExecutingAssembly());
			string iconPath = Path.Combine(AppContext.BaseDirectory, "assets", "cypressicons", "ico", "Burbank-CypressIcon.ico");

			IDisposable? trayCleanup = null;
#if WINDOWS
			trayCleanup = SetupWindowsTray(handler, iconPath);
#endif

			var window = new PhotinoWindow()
				.SetTitle("Cypress Launcher")
				.SetSize(1280, 800)
				.SetMinSize(900, 600)
				.Center()
				.SetResizable(true)
				.SetContextMenuEnabled(false)
				.SetDevToolsEnabled(true)
				.SetFileSystemAccessEnabled(false)
				.SetGrantBrowserPermissions(false)
				.SetLogVerbosity(2);

			if (File.Exists(iconPath))
				window.SetIconFile(iconPath);

			window.RegisterWebMessageReceivedHandler(handler.HandleMessage);

			window.RegisterWindowCreatedHandler((_, _) =>
			{
				float scale = window.ScreenDpi / 96f;
				Console.Error.WriteLine($"[debug] ScreenDpi={window.ScreenDpi} scale={scale} size={window.Size}");
				ApplyPhysicalSize(window, 1280, 800);
				window.Center();
			});

#if WINDOWS
			SetupWindowsWindowHooks(window, handler);
#endif

			string tempHtml = Path.Combine(Path.GetTempPath(), "cypress_launcher_" + Guid.NewGuid().ToString("N") + ".html");
			File.WriteAllText(tempHtml, html);
			window.Load(tempHtml);
			window.WaitForClose();

			handler.KillAllInstances();
			try { File.Delete(tempHtml); } catch { }
			trayCleanup?.Dispose();
		}
		catch (Exception ex)
		{
			Console.Error.WriteLine("Fatal error: " + ex);
#if WINDOWS
			WindowsMessageBox("Fatal error:\n" + ex.ToString(), "Cypress Launcher Error");
#endif
		}
	}

#if WINDOWS
	[System.Runtime.Versioning.SupportedOSPlatform("windows")]
	private static void WindowsMessageBox(string text, string caption)
	{
		System.Windows.Forms.MessageBox.Show(text, caption);
	}

	[System.Runtime.Versioning.SupportedOSPlatform("windows")]
	private static IDisposable SetupWindowsTray(MessageHandler handler, string iconPath)
	{
		var trayIcon = new System.Windows.Forms.NotifyIcon();
		if (File.Exists(iconPath))
			trayIcon.Icon = new System.Drawing.Icon(iconPath);
		trayIcon.Text = "Cypress Launcher";
		trayIcon.Visible = false;

		var trayMenu = new System.Windows.Forms.ContextMenuStrip();
		trayMenu.Items.Add("Show Cypress Launcher", null, (_, _) =>
		{
			trayIcon.Visible = false;
			ShowWindow(handler.Window!.WindowHandle, SW_SHOW);
			handler.Window.SetMinimized(false);
		});
		trayMenu.Items.Add("-");
		trayMenu.Items.Add("Exit", null, (_, _) =>
		{
			trayIcon.Visible = false;
			trayIcon.Dispose();
			handler.KillAllInstances();
			Environment.Exit(0);
		});
		trayIcon.ContextMenuStrip = trayMenu;
		trayIcon.DoubleClick += (_, _) =>
		{
			trayIcon.Visible = false;
			ShowWindow(handler.Window!.WindowHandle, SW_SHOW);
			handler.Window.SetMinimized(false);
		};

		handler.TrayIcon = trayIcon;
		return new TrayCleanup(trayIcon);
	}

	[System.Runtime.Versioning.SupportedOSPlatform("windows")]
	private static void SetupWindowsWindowHooks(PhotinoWindow window, MessageHandler handler)
	{
		// alt+f4 -> actually close, X button -> minimize to tray
		window.WindowClosing += (_, _) =>
		{
			bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
			if (altHeld)
			{
				// fire deregisters in background, give them up to 1.5s, then hard exit
				var deregTask = System.Threading.Tasks.Task.Run(() => handler.DeregisterAllHeartbeats());
				deregTask.Wait(TimeSpan.FromMilliseconds(1500));
				handler.KillAllInstances(skipHeartbeatShutdown: true);
				Environment.Exit(0);
				return false;
			}
			ShowWindow(window.WindowHandle, SW_HIDE);
			if (handler.TrayIcon != null)
				((dynamic)handler.TrayIcon).Visible = true;
			return true;
		};
	}

	private sealed class TrayCleanup : IDisposable
	{
		private readonly System.Windows.Forms.NotifyIcon _icon;
		public TrayCleanup(System.Windows.Forms.NotifyIcon icon) => _icon = icon;
		[System.Runtime.Versioning.SupportedOSPlatform("windows")]
		public void Dispose()
		{
			_icon.Visible = false;
			_icon.Dispose();
		}
	}
#endif
}
