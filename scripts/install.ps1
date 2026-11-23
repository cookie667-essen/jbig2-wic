#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$InstallDir = "C:\Program Files\JBIG2-WIC"
$DllSource = @(
    (Join-Path $PSScriptRoot "jbig2_wic.dll"),
    (Join-Path $PSScriptRoot "..\build\Release\jbig2_wic.dll")
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $DllSource) { throw "jbig2_wic.dll nicht gefunden." }

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
Stop-Process -Name explorer, dllhost -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

Copy-Item $DllSource (Join-Path $InstallDir "jbig2_wic.dll") -Force
& regsvr32.exe /s (Join-Path $InstallDir "jbig2_wic.dll")
Start-Process explorer

Write-Host "JBIG2-WIC erfolgreich installiert & registriert." -ForegroundColor Green
