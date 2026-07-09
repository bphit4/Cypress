param(
    [string]$Config = "$PSScriptRoot\cfb27-private.example.json",
    [string]$BinDir = "$PSScriptRoot\..\build"
)

$ErrorActionPreference = "Stop"
$cfg = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $cfg.paths.dataDir | Out-Null

$dynastyArgs = @(
    "-bind", $cfg.dynasty.bind,
    "-port", [string]$cfg.dynasty.port,
    "-schema-root", $cfg.paths.dynastySchemaRoot,
    "-db", $cfg.paths.dynastyDb
)

$blazeArgs = @(
    "-bind", $cfg.blaze.bind,
    "-port", [string]$cfg.blaze.port,
    "-diagnostics-bind", $cfg.blaze.diagnosticsBind,
    "-diagnostics-port", [string]$cfg.blaze.diagnosticsPort,
    "-dynasty-url", ("http://127.0.0.1:{0}" -f $cfg.dynasty.port),
    "-profile", $cfg.blaze.profile,
    "-log-file", $cfg.paths.blazeLogFile
)

Write-Host "Starting CFB27 offline private stack..."
Start-Process -FilePath (Join-Path $BinDir "dynasty.exe") -ArgumentList $dynastyArgs -WorkingDirectory $cfg.paths.dataDir -WindowStyle Hidden
Start-Process -FilePath (Join-Path $BinDir "cfb27blaze.exe") -ArgumentList $blazeArgs -WorkingDirectory $cfg.paths.dataDir -WindowStyle Hidden
Write-Host "Dynasty: http://$($cfg.publicHost):$($cfg.dynasty.port)"
Write-Host "Blaze:   tcp://$($cfg.publicHost):$($cfg.blaze.port)"
Write-Host "Health:  http://$($cfg.publicHost):$($cfg.blaze.diagnosticsPort)/health"
