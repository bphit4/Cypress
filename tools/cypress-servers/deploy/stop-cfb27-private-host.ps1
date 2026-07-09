param(
    [string]$GameDir = "C:\Program Files\EA Games\EA SPORTS College Football 27"
)

$ErrorActionPreference = "Continue"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdministrator)) {
    Start-Process -FilePath powershell.exe -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-GameDir", "`"$GameDir`""
    ) -Verb RunAs
    return
}

Stop-Process -Name dynasty, cfb27blaze, cfb27gateway, master, relay, CollegeFB27_Trial, CollegeFB27, EAAntiCheat.GameServiceLauncher -Force -ErrorAction SilentlyContinue

Get-NetFirewallRule -DisplayName "Cypress CFB27 Candidate Block*" -ErrorAction SilentlyContinue |
    Remove-NetFirewallRule -ErrorAction SilentlyContinue

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$activeDll = Join-Path $GameDir "dinput8.dll"
$activeEndpoints = Join-Path $GameDir "cfb27-endpoints.json"

if (Test-Path -LiteralPath $activeDll) {
    Rename-Item -LiteralPath $activeDll -NewName "dinput8.cypress-disabled-$stamp.dll" -Force
}
if (Test-Path -LiteralPath $activeEndpoints) {
    Rename-Item -LiteralPath $activeEndpoints -NewName "cfb27-endpoints.cypress-disabled-$stamp.json" -Force
}

Write-Host "CFB27 private host stopped."
Write-Host "Active injected files disabled in: $GameDir"
