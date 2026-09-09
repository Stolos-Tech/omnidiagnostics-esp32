# ESP32-OS -> прошивка T-Display (firmware + filesystem). Порт за замовч. COM5.
param([string]$Port = "COM5")
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
Set-Location $PSScriptRoot
Write-Host "== [ESP32] FIRMWARE -> $Port ==" -ForegroundColor Green
& $pio run -e ttgo-t-display -t upload --upload-port $Port
Write-Host "== [ESP32] FILESYSTEM -> $Port ==" -ForegroundColor Green
& $pio run -e ttgo-t-display -t uploadfs --upload-port $Port
Write-Host "== [ESP32] DONE. Monitor: pio device monitor -p $Port -b 115200 =="
