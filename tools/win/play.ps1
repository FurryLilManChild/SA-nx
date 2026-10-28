# Plays the PC build in a window, with no time limit. F11 saves a rendering capture.
#   powershell -ExecutionPolicy Bypass -File tools\win\play.ps1
# Log: out\logs\play.log (plus out\logs\play-report.txt and suggested.toml when it exits)
. "$PSScriptRoot\common.ps1"
if (-not (Test-Path $GameExe)) { throw 'Not built yet. Run tools\win\build_pc.ps1 first.' }
Start-Log 'play.log' 'play' | Out-Null
$a = @('--config', $GameToml, '--window',
       '--report', (Join-Path $LogDir 'play-report.txt'),
       '--suggest-config', (Join-Path $LogDir 'suggested.toml'))
$rc = Invoke-Logged $GameExe $a 'play' '^frame'
Write-Host "Game closed (exit $rc). Logs are in out\logs."
