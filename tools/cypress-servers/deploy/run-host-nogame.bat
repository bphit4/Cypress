@echo off
REM Starts the CFB27 private stack (dynasty + cfb27blaze), installs the freshly
REM built dinput8.dll, and writes the bridge config with the ProtoSSL verify
REM probe enabled -- but does NOT launch the game (-NoLaunchGame). Launch the
REM game yourself when ready. The PowerShell script self-elevates via UAC to
REM install the DLL into the game directory.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-cfb27-private-host.ps1" -NoLaunchGame
