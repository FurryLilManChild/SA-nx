# Plays the PC build in a window, with no time limit. F11 saves a rendering capture.
#   powershell -ExecutionPolicy Bypass -File tools\win\play.ps1
# The intro movie is hidden from the game unless -KeepIntro.
# Log: out\logs\play.log (plus out\logs\play-report.txt and suggested.toml when it exits)
# out\logs\profile.txt: where the game thread spent its time (sampled about 1,000 times a second).
param([switch]$KeepIntro, [switch]$Us)
. "$PSScriptRoot\common.ps1"
if (-not (Test-Path $GameExe)) { throw 'Not built yet. Run tools\win\build_pc.ps1 first.' }
Start-Log 'play.log' 'play' | Out-Null
$a = @('--config', $GameToml, '--window',
       '--report', (Join-Path $LogDir 'play-report.txt'),
       '--suggest-config', (Join-Path $LogDir 'suggested.toml'))
# The boot SR and the other per-version settings are in each config's [port] table. -Us runs the
# US disc (sonicadv_us.toml names it); either way the other config is offered too, and the disc's
# product number picks.
if ($Us) { $a[1] = $GameTomlUs; $a += @('--alt-config', $GameToml) }
else { $a += @('--alt-config', $GameTomlUs) }
# Memory card: created blank on first run, kept between runs (out\saves\vmu_A1.bin).
$a += @('--vmu', $VmuFile)
if (-not $KeepIntro) { $a += @('--hide-file', 'SA1_600.SFD') }
# Sampling profiler: a small speed cost (not yet measured), writes its report when the window closes (naming the
# functions reads the debug symbols, so closing can take a little longer than usual).
$a += @('--profile', (Join-Path $LogDir 'profile.txt'))
$rc = Invoke-Logged $GameExe $a 'play' '^frame'
Write-Host "Game closed (exit $rc). Logs are in out\logs."
