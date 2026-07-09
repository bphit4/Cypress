param(
    [string]$GameDir = "C:\Program Files\EA Games\EA SPORTS College Football 27",
    [string]$Profile = "LocalPlayer",
    [string]$TlsMode = $env:CYPRESS_CFB27_TLS_MODE,
    [int]$BlazePort = 27920,
    [int]$BlazeDiagnosticsPort = 27921,
    [switch]$NoLaunchGame,
    [switch]$SkipInstallDll
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-SelfElevated {
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-GameDir", "`"$GameDir`"",
        "-Profile", "`"$Profile`"",
        "-TlsMode", "`"$TlsMode`"",
        "-BlazePort", "$BlazePort",
        "-BlazeDiagnosticsPort", "$BlazeDiagnosticsPort"
    )
    if ($NoLaunchGame) { $args += "-NoLaunchGame" }
    if ($SkipInstallDll) { $args += "-SkipInstallDll" }
    Start-Process -FilePath powershell.exe -ArgumentList ($args -join " ") -Verb RunAs
}

if (-not $SkipInstallDll -and -not (Test-IsAdministrator)) {
    Invoke-SelfElevated
    return
}

$scriptRoot = Split-Path -Parent $PSCommandPath
$servicesDir = Resolve-Path (Join-Path $scriptRoot "..")
$packageRoot = Resolve-Path (Join-Path $scriptRoot "..\..\..")
$buildDir = Join-Path $servicesDir "build"
$launcherDir = Join-Path $packageRoot "Launcher"
$privateRoot = Join-Path $env:APPDATA "Cypress\CFB27\Private"
$dataDir = Join-Path $privateRoot "data"
$runDir = Join-Path $privateRoot ("runs\cli_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
$bridgeConfigPath = Join-Path $privateRoot "cfb27-bridge.ini"
$logFile = Join-Path $runDir "private-start.log"
$runId = Split-Path -Leaf $runDir

New-Item -ItemType Directory -Force -Path $dataDir, $runDir | Out-Null

function Write-Log {
    param([string]$Message)
    $line = "{0:O} {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $logFile -Value $line
}

function Resolve-FirstExistingPath {
    param([string[]]$Candidates, [string]$Label)
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "$Label was not found. Checked: $($Candidates -join '; ')"
}

function Wait-HttpHealthy {
    param(
        [string]$Name,
        [string]$Url,
        [Diagnostics.Process]$Process,
        [int]$TimeoutSeconds,
        [string]$ExpectedRunId = ""
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $response = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 3
            if ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300) {
                if (-not [string]::IsNullOrWhiteSpace($ExpectedRunId)) {
                    $health = $response.Content | ConvertFrom-Json
                    if ($health.runId -ne $ExpectedRunId) {
                        Write-Log "$Name health at $Url had stale runId=$($health.runId); expected=$ExpectedRunId"
                        Start-Sleep -Milliseconds 500
                        continue
                    }
                }
                Write-Log "$Name healthy at $Url"
                return
            }
        } catch { }

        if ($Process.HasExited) {
            $stdout = Get-Content -LiteralPath (Join-Path $runDir "$Name.stdout.log") -Raw -ErrorAction SilentlyContinue
            $stderr = Get-Content -LiteralPath (Join-Path $runDir "$Name.stderr.log") -Raw -ErrorAction SilentlyContinue
            throw "$Name exited early with code $($Process.ExitCode). stderr=$stderr stdout=$stdout"
        }
        Start-Sleep -Milliseconds 500
    }
    throw "$Name did not become healthy at $Url within $TimeoutSeconds seconds."
}

function Start-LoggedProcess {
    param(
        [string]$Name,
        [string]$Exe,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )
    Write-Log "$Name exe=$Exe"
    Write-Log "$Name args=$($Arguments -join ' ')"
    $process = Start-Process `
        -FilePath $Exe `
        -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory `
        -RedirectStandardOutput (Join-Path $runDir "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $runDir "$Name.stderr.log") `
        -WindowStyle Hidden `
        -PassThru
    Write-Log "$Name pid=$($process.Id)"
    return $process
}

Write-Log "CFB27 private host start"
Write-Log "packageRoot=$packageRoot"
Write-Log "servicesDir=$servicesDir"
Write-Log "runDir=$runDir"
Write-Log "gameDir=$GameDir"
Write-Log "profile=$Profile"
if ([string]::IsNullOrWhiteSpace($TlsMode)) {
    $TlsMode = "tls13"
}
Write-Log "tlsMode=$TlsMode"
Write-Log "blazePort=$BlazePort"
Write-Log "blazeDiagnosticsPort=$BlazeDiagnosticsPort"

$dynastyExe = Resolve-FirstExistingPath @(
    (Join-Path $buildDir "dynasty.exe"),
    (Join-Path $servicesDir "dynasty.exe")
) "dynasty.exe"

$blazeExe = Resolve-FirstExistingPath @(
    (Join-Path $buildDir "cfb27blaze.exe"),
    (Join-Path $servicesDir "cfb27blaze.exe")
) "cfb27blaze.exe"

$schemaRoot = Resolve-FirstExistingPath @(
    (Join-Path $scriptRoot "Dynasty_Files"),
    (Join-Path ([Environment]::GetFolderPath("DesktopDirectory")) "CFB27\Dynasty_Files")
) "Dynasty_Files"

$dllPath = Resolve-FirstExistingPath @(
    (Join-Path $launcherDir "cypress_CFB27.dll"),
    (Join-Path $packageRoot "Server\build\Release\cypress_CFB27.dll")
) "cypress_CFB27.dll"

$endpointsPath = Resolve-FirstExistingPath @(
    (Join-Path $packageRoot "cfb27-endpoints.json"),
    (Join-Path $scriptRoot "cfb27-endpoints.example.json")
) "cfb27-endpoints.json"

Stop-Process -Name dynasty, cfb27blaze -Force -ErrorAction SilentlyContinue

$dynastyProc = Start-LoggedProcess "dynasty" $dynastyExe @(
    "-bind", "127.0.0.1",
    "-port", "27910",
    "-schema-root", $schemaRoot,
    "-db", (Join-Path $dataDir "cfb27_dynasty.db")
) $runDir
Wait-HttpHealthy "dynasty" "http://127.0.0.1:27910/health" $dynastyProc 300

$blazeProc = Start-LoggedProcess "cfb27blaze" $blazeExe @(
    "-bind", "127.0.0.1",
    "-port", ([string]$BlazePort),
    "-diagnostics-bind", "127.0.0.1",
    "-diagnostics-port", ([string]$BlazeDiagnosticsPort),
    "-dynasty-url", "http://127.0.0.1:27910",
    "-profile", $Profile,
    "-tls-mode", $TlsMode,
    "-run-id", $runId,
    "-log-file", (Join-Path $runDir "cfb27-blaze.jsonl")
) $runDir
Wait-HttpHealthy "cfb27blaze" "http://127.0.0.1:$BlazeDiagnosticsPort/health" $blazeProc 30 $runId

if (-not $SkipInstallDll) {
    if (-not (Test-Path -LiteralPath $GameDir)) {
        throw "Game directory not found: $GameDir"
    }
    Stop-Process -Name CollegeFB27, CollegeFB27_Trial, EAAntiCheat.GameServiceLauncher -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $dllPath -Destination (Join-Path $GameDir "dinput8.dll") -Force
    Copy-Item -LiteralPath $endpointsPath -Destination (Join-Path $GameDir "cfb27-endpoints.json") -Force
    Write-Log "installed dinput8.dll and cfb27-endpoints.json"
}

$bridgeEndpointsFile = Join-Path $GameDir "cfb27-endpoints.json"
if ($SkipInstallDll -and -not (Test-Path -LiteralPath $bridgeEndpointsFile)) {
    $bridgeEndpointsFile = $endpointsPath
}

$bridgeConfig = @(
    "# Written by start-cfb27-private-host.ps1",
    "blazeHost=127.0.0.1",
    "blazePort=$BlazePort",
    "profile=$Profile",
    "runDirectory=$runDir",
    "endpointsFile=$bridgeEndpointsFile",
    "enableBearSslBypass=false",
    "dumpRuntimeCodeBytes=false",
    "enableCandidateEndpointRedirects=false"
)
Set-Content -LiteralPath $bridgeConfigPath -Value $bridgeConfig -Encoding ASCII
Write-Log "wrote persistent bridge config=$bridgeConfigPath"

if ($NoLaunchGame) {
    Write-Log "services ready; game launch skipped"
    Write-Host ""
    Write-Host "Services are ready. Run folder:"
    Write-Host $runDir
    return
}

$gameExe = Resolve-FirstExistingPath @(
    (Join-Path $GameDir "CollegeFB27.exe"),
    (Join-Path $GameDir "CollegeFB27_Trial.exe")
) "CFB27 executable"

$env:CYPRESS_EMBEDDED = "1"
$env:CYPRESS_CFB27_DISCOVERY = "1"
$env:CYPRESS_CFB27_BLAZE_HOST = "127.0.0.1"
$env:CYPRESS_CFB27_BLAZE_PORT = [string]$BlazePort
$env:CYPRESS_CFB27_PROFILE = $Profile
$env:CYPRESS_CFB27_RUN_DIR = $runDir
$env:CYPRESS_CFB27_BRIDGE_CONFIG = $bridgeConfigPath
$env:CYPRESS_CFB27_ENDPOINTS_FILE = (Join-Path $GameDir "cfb27-endpoints.json")
$env:CYPRESS_CFB27_DYNASTY_URL = "http://127.0.0.1:27910"
$env:CYPRESS_CFB27_DYNASTY_PROFILE = $Profile

$gameArgs = @(
    "-playerName", $Profile,
    "-console",
    "-allowMultipleInstances",
    "-Game.Platform", "GamePlatform_Win32",
    "-name", "Cypress CFB27 Private"
)
Write-Log "launching game exe=$gameExe"
Write-Log "game args=$($gameArgs -join ' ')"
$gameProc = Start-Process -FilePath $gameExe -ArgumentList $gameArgs -WorkingDirectory $GameDir -PassThru
Write-Log "game launch pid=$($gameProc.Id)"

Write-Host ""
Write-Host "CFB27 private host launched. Run folder:"
Write-Host $runDir
