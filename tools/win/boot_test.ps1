# Boots the game for up to 120 GAME seconds in a window, then closes it. Game time runs slower
# than real time while code still goes through the interpreter (0.4x on the first boot). Writes:
#   out\logs\boot.log          everything the launcher printed
#   out\logs\boot-report.txt   where the run got to and why it stopped
#   out\logs\suggested.toml    code the run reached that is not translated yet (Claude merges it)
#   powershell -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 [-Seconds 120] [-Headless] [-KeepIntro]
# The intro movie (SA1_600.SFD) is hidden from the game unless -KeepIntro, so boots skip it.
param([int]$Seconds = 120, [switch]$Headless, [switch]$KeepIntro)
. "$PSScriptRoot\common.ps1"
if (-not (Test-Path $GameExe)) { throw 'Not built yet. Run tools\win\build_pc.ps1 first.' }
Start-Log 'boot.log' "boot_test $Seconds s" | Out-Null

$a = @('--config', $GameToml, '--max-seconds', $Seconds,
       '--report', (Join-Path $LogDir 'boot-report.txt'),
       '--suggest-config', (Join-Path $LogDir 'suggested.toml'),
       '--dump', '0x8C900000:0x200')  # what is loaded at 0x8C900000 when the run stops
# Sonic Adventure's renderer runs on CPU register bank 1 (SR.RB = 1); starting on bank 0 made
# it lose model pointers (black Station Square, wrong character-select models).
$a += @('--boot-sr', '0x600000F1')
if (-not $KeepIntro) { $a += @('--hide-file', 'SA1_600.SFD') }
if (-not $Headless) { $a += '--window' }
Write-Host "Booting for up to $Seconds game seconds (more in real time while it runs slow) ..."
$rc = Invoke-Logged $GameExe $a 'boot' '^frame'
Write-Host "Finished (exit $rc). Tell Claude the boot test is done; logs are in out\logs."
