param(
    [string]$Config = "$PSScriptRoot\cfb27-private.example.json",
    [string]$BinDir = "$PSScriptRoot\..\build"
)

$ErrorActionPreference = "Stop"
$cfg = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $cfg.paths.dataDir | Out-Null

$masterArgs = @(
    "-bind", $cfg.master.bind,
    "-port", [string]$cfg.master.port,
    "-db", $cfg.paths.masterDb,
    "-secret-file", (Join-Path $cfg.paths.dataDir "moderator_secret.txt")
)
if ($cfg.master.behindProxy) { $masterArgs += "-behind-proxy" }

$relayArgs = @(
    "-bind", $cfg.relay.bind,
    "-port", [string]$cfg.relay.port,
    "-api-bind", $cfg.relay.apiBind,
    "-api-port", [string]$cfg.relay.apiPort,
    "-relay-host", $cfg.relay.relayHost,
    "-server-timeout", [string]$cfg.relay.serverTimeoutSeconds,
    "-client-timeout", [string]$cfg.relay.clientTimeoutSeconds,
    "-lease-file", $cfg.paths.relayLeaseFile,
    "-log-file", $cfg.paths.relayLogFile,
    "-master-url", ("http://127.0.0.1:{0}" -f $cfg.master.port),
    "-no-dashboard"
)

$dynastyArgs = @(
    "-bind", $cfg.dynasty.bind,
    "-port", [string]$cfg.dynasty.port,
    "-schema-root", $cfg.paths.dynastySchemaRoot,
    "-db", $cfg.paths.dynastyDb
)

Write-Host "Starting CFB27 private Cypress stack..."
Start-Process -FilePath (Join-Path $BinDir "master.exe") -ArgumentList $masterArgs -WorkingDirectory $cfg.paths.dataDir -WindowStyle Hidden
Start-Process -FilePath (Join-Path $BinDir "relay.exe") -ArgumentList $relayArgs -WorkingDirectory $cfg.paths.dataDir -WindowStyle Hidden
Start-Process -FilePath (Join-Path $BinDir "dynasty.exe") -ArgumentList $dynastyArgs -WorkingDirectory $cfg.paths.dataDir -WindowStyle Hidden
Write-Host "Master:  http://$($cfg.publicHost):$($cfg.master.port)"
Write-Host "Relay:   udp://$($cfg.publicHost):$($cfg.relay.port)"
Write-Host "Dynasty: http://$($cfg.publicHost):$($cfg.dynasty.port)"
