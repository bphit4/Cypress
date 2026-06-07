function ConvertTo-Hashtable($obj) {
    $ht = @{}
    $obj.PSObject.Properties | ForEach-Object { $ht[$_.Name] = $_.Value }
    return $ht
}

$source = ConvertTo-Hashtable (Get-Content "$PSScriptRoot\en-US.json" | ConvertFrom-Json)
$source.Remove('_meta')

foreach ($file in Get-ChildItem $PSScriptRoot -Filter "*.json" | Where-Object { $_.Name -ne "en-US.json" }) {
    $target = ConvertTo-Hashtable (Get-Content $file.FullName | ConvertFrom-Json)
    $target.Remove('_meta')

    $missing = @{}
    foreach ($key in $source.Keys) {
        if (-not $target.ContainsKey($key)) { $missing[$key] = $source[$key] }
    }

    $stale = $target.Keys | Where-Object { -not $source.ContainsKey($_) }

    Write-Host "`n=== $($file.Name) ===" -ForegroundColor Cyan
    if ($missing.Count -eq 0 -and @($stale).Count -eq 0) {
        Write-Host "  up to date" -ForegroundColor Green
        continue
    }
    if ($missing.Count -gt 0) {
        Write-Host "  $($missing.Count) missing key(s).. paste into translation file and translate:" -ForegroundColor Yellow
        Write-Host ($missing | ConvertTo-Json -Depth 1)
    }
    if (@($stale).Count -gt 0) {
        Write-Host "  $(@($stale).Count) stale key(s) no longer in source (safe to remove):" -ForegroundColor DarkYellow
        $stale | ForEach-Object { Write-Host "    $_" }
    }
}

Read-Host "`nDone. Press Enter to exit"
