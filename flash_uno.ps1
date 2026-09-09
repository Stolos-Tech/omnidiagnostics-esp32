# ESP32-OS -> прошивка Arduino UNO R3. Порт за замовч. COM7.
param([string]$Port = "COM7")
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
Set-Location "$PSScriptRoot\arduino\uno_r3"
Write-Host "== [UNO] build + upload -> $Port ==" -ForegroundColor Cyan
& $pio run -e uno -t upload --upload-port $Port
Write-Host "== [UNO] DONE. Monitor: pio device monitor -p $Port -b 9600 =="
