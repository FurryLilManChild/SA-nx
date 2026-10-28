# Builds the PC version of Sonic Adventure (dream-recomp, development interpreter on).
#   powershell -ExecutionPolicy Bypass -File tools\win\build_pc.ps1           incremental build
#   powershell -ExecutionPolicy Bypass -File tools\win\build_pc.ps1 -Clean    reconfigure first
# Log: out\logs\build_pc.log
param([switch]$Clean)
. "$PSScriptRoot\common.ps1"
Start-Log 'build_pc.log' 'build_pc' | Out-Null
Enter-DevShell

if (-not (Test-Path (Join-Path $Root "games\$GameId\extracted\fs\1ST_READ.BIN"))) {
    throw "The disc is not extracted yet (games\$GameId\extracted is missing). Tell Claude."
}
Apply-Patches
if ($Clean -and (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    Remove-Item (Join-Path $BuildDir 'CMakeCache.txt') -Force
}

if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
    Write-Host 'Configuring ...'
    $cfg = @('-S', $Root, '-B', $BuildDir, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        '-DDREAM_DEV_INTERPRETER=ON', '-DDREAM_BUILD_TESTS=OFF', '-DDREAM_WERROR=OFF',
        '-DDREAM_TRANSLATE_PARTS=32',
        "-DCMAKE_PREFIX_PATH=$Sdl3Dir",
        '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /bigobj')
    $rc = Invoke-Logged 'cmake' $cfg 'build_pc' '^--'
    if ($rc -ne 0) { throw "Configure failed (exit $rc). Logs are in out\logs; tell Claude." }
}

$jobs = Get-BuildJobs
Write-Host "Building with $jobs parallel jobs (the first build translates and compiles the whole game) ..."
Write-Log 'build_pc.log' "jobs $jobs"
$sw = [Diagnostics.Stopwatch]::StartNew()
$rc = Invoke-Logged 'cmake' @('--build', $BuildDir, '--target', "${GameId}_boot", '--parallel', "$jobs") 'build_pc'
$sw.Stop()
Write-Log 'build_pc.log' ("== build time {0:N0} s" -f $sw.Elapsed.TotalSeconds)
if ($rc -ne 0) { throw "Build failed (exit $rc) after $([int]$sw.Elapsed.TotalMinutes) min. Logs are in out\logs; tell Claude." }

Copy-Item (Join-Path $Sdl3Dir 'lib\x64\SDL3.dll') (Split-Path $GameExe) -Force
Write-Host ("Built in {0:N0} min: {1}" -f $sw.Elapsed.TotalMinutes, $GameExe)
Write-Host 'Next: tools\win\boot_test.ps1'
