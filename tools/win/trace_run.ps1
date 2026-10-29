# Diagnostic: runs the trace build (build_pc.ps1 -Trace) twice without a window, with the same
# scripted Start presses Claude uses, once translated and once fully interpreted, logging every
# guest function return after guest cycle $From. Claude diffs the two to find where they part.
#   powershell -ExecutionPolicy Bypass -File tools\win\trace_run.ps1 [-From 7043000000] [-Seconds 40]
# Writes out\logs\returns-T.txt, returns-I.txt, trace-T.log, trace-I.log (and the reports).
param([long]$From = 7043000000, [int]$Seconds = 40)
. "$PSScriptRoot\common.ps1"
Use-TraceBuild
if (-not (Test-Path $GameExe)) { throw 'No trace build yet. Run tools\win\build_pc.ps1 -Trace first.' }
$press = ((1..74) | ForEach-Object { "start@$($_ * 120)" }) -join ','
$env:DREAM_TRACE_RETURNS_FROM = "$From"
foreach ($mode in @('T', 'I')) {
    Start-Log "trace-$mode.log" "trace_run $mode from $From" | Out-Null
    $env:DREAM_TRACE_RETURNS = Join-Path $LogDir "returns-$mode.txt"
    $a = @('--config', $GameToml, '--boot-sr', '0x600000F1', '--max-seconds', $Seconds, '--no-audio',
           '--press', $press, '--report', (Join-Path $LogDir "trace-$mode-report.txt"))
    if ($mode -eq 'I') { $a += '--interpret' }
    Write-Host "Run $mode ($(if ($mode -eq 'T') { 'translated' } else { 'interpreted, slower' })) ..."
    $rc = Invoke-Logged $GameExe $a "trace-$mode" '^frame'
    Write-Host "  finished (exit $rc)"
}
Remove-Item Env:DREAM_TRACE_RETURNS, Env:DREAM_TRACE_RETURNS_FROM -ErrorAction SilentlyContinue
Write-Host 'Done. Tell Claude the trace run finished; logs are in out\logs.'
