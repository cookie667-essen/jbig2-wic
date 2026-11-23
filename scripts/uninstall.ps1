#Requires -RunAsAdministrator
$DllPath = "C:\Program Files\JBIG2-WIC\jbig2_wic.dll"
if (Test-Path $DllPath) {
    & regsvr32.exe /u /s $DllPath
    Stop-Process -Name explorer, dllhost -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 600
    Remove-Item "C:\Program Files\JBIG2-WIC" -Recurse -Force -ErrorAction SilentlyContinue
    Start-Process explorer
}
Write-Host "JBIG2-WIC entfernt." -ForegroundColor Green
