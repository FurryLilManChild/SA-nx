# Diagnostic: runs the trace build (build_pc.ps1 -Trace) without a window, with the same scripted
# Start presses Claude uses, and logs every guest function return between guest cycles $From and
# $To, interleaved with the runtime's interrupt, resume and task-switch lines, into one log.
#   powershell -ExecutionPolicy Bypass -File tools\win\trace_run.ps1 [-Modes T,I] [-From N] [-To N]
# T = translated (default), I = fully interpreted (slow). Writes out\logs\trace-T.log / trace-I.log.
param([string[]]$Modes = @('T'), [long]$From = 7043000000, [long]$To = 7045000000, [int]$Seconds = 40)
. "$PSScriptRoot\common.ps1"
Use-TraceBuild
if (-not (Test-Path $GameExe)) { throw 'No trace build yet. Run tools\win\build_pc.ps1 -Trace first.' }
$press = ((1..74) | ForEach-Object { "start@$($_ * 120)" }) -join ','
$env:DREAM_TRACE_RETURNS = '-'
$env:DREAM_TRACE_RETURNS_FROM = "$From"
$env:DREAM_TRACE_RETURNS_TO = "$To"
$env:DREAM_TRACE_NONLOCAL = '1'
foreach ($mode in $Modes) {
    Start-Log "trace-$mode.log" "trace_run $mode cycles $From to $To" | Out-Null
    $a = @('--config', $GameToml, '--boot-sr', '0x600000F1', '--max-seconds', $Seconds, '--no-audio',
           '--press', $press, '--report', (Join-Path $LogDir "trace-$mode-report.txt"))
    if ($mode -eq 'I') { $a += '--interpret' }
    Write-Host "Run $mode ($(if ($mode -eq 'T') { 'translated, about a minute' } else { 'interpreted, several minutes' })) ..."
    $rc = Invoke-Logged $GameExe $a "trace-$mode" '^frame'
    Write-Host "  finished (exit $rc)"
}
Remove-Item Env:DREAM_TRACE_RETURNS, Env:DREAM_TRACE_RETURNS_FROM, Env:DREAM_TRACE_RETURNS_TO, Env:DREAM_TRACE_NONLOCAL -ErrorAction SilentlyContinue
Write-Host 'Done. Tell Claude the trace run finished; logs are in out\logs.'
