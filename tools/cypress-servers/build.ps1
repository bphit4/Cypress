$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot

Write-Host "building master server..."
go build -o build/master.exe ./cmd/master
Write-Host "building relay server..."
go build -o build/relay.exe ./cmd/relay
Write-Host "building dynasty service..."
go build -o build/dynasty.exe ./cmd/dynasty
Write-Host "building CFB27 gateway/logger..."
go build -o build/cfb27gateway.exe ./cmd/cfb27gateway
Write-Host "building CFB27 Blaze bridge..."
go build -o build/cfb27blaze.exe ./cmd/cfb27blaze

Write-Host "done. binaries in build/"
Pop-Location
