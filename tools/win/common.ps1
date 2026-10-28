# Shared helpers for the SA-nx Windows scripts. Dot-source it: . "$PSScriptRoot\common.ps1"
# Logs are written as UTF-8 to out\logs so Claude can read them through the bridge.

$ErrorActionPreference = 'Stop'
$Root    = (Resolve-Path "$PSScriptRoot\..\..").Path
$LogDir  = Join-Path $Root 'out\logs'
$BuildDir = Join-Path $Root 'build-pc'
$DepsDir = Join-Path $Root '.deps'
$Sdl3Dir = Join-Path $DepsDir 'SDL3-3.4.16'
$GameId  = 'sonicadv'
$GameToml = Join-Path $Root "games\$GameId\$GameId.toml"
$GameExe = Join-Path $BuildDir "games\$GameId\${GameId}_boot.exe"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Write-Log([string]$File, [string]$Text) {
    [IO.File]::AppendAllText((Join-Path $LogDir $File), $Text + "`r`n", (New-Object Text.UTF8Encoding $false))
}

function Start-Log([string]$File, [string]$Title) {
    $p = Join-Path $LogDir $File
    [IO.File]::WriteAllText($p, "== $Title  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`r`n", (New-Object Text.UTF8Encoding $false))
    return $p
}

# Puts the x64 MSVC compiler, linker and Windows SDK on PATH for this PowerShell window.
function Enter-DevShell {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2022 (or its Build Tools) is not installed. Run tools\win\setup_pc.ps1 first.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio install with the C++ x64 tools. Run tools\win\setup_pc.ps1 first.' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# Runs a program with its output going straight to a UTF-8 log, and every few seconds shows the
# newest progress line (Ninja's [n/N]) so a long build is not a silent window.
function Invoke-Logged([string]$Exe, [string[]]$ArgList, [string]$LogName, [string]$Pattern = '^\[\d+/\d+\]') {
    $out = Join-Path $LogDir "$LogName.out.tmp"
    $err = Join-Path $LogDir "$LogName.err.tmp"
    # Windows PowerShell 5.1 joins -ArgumentList with plain spaces, so quote anything that has one.
    $quoted = $ArgList | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } }
    $p = Start-Process -FilePath $Exe -ArgumentList $quoted -NoNewWindow -PassThru `
        -RedirectStandardOutput $out -RedirectStandardError $err -WorkingDirectory $Root
    $last = ''
    while (-not $p.HasExited) {
        Start-Sleep -Seconds 5
        if (Test-Path $out) {
            $line = Get-Content $out -Tail 40 -ErrorAction SilentlyContinue | Where-Object { $_ -match $Pattern } | Select-Object -Last 1
            if ($line -and $line -ne $last) {
                $m = [regex]::Match($line, '^\[(\d+)/(\d+)\]')
                if ($m.Success) { Write-Host ("  {0} of {1} steps" -f $m.Groups[1].Value, $m.Groups[2].Value) }
                else { Write-Host "  $line" }
                $last = $line
            }
        }
    }
    $p.WaitForExit()
    $code = $p.ExitCode
    $log = Join-Path $LogDir "$LogName.log"
    foreach ($f in @($out, $err)) {
        if (Test-Path $f) {
            [IO.File]::AppendAllText($log, [IO.File]::ReadAllText($f), (New-Object Text.UTF8Encoding $false))
            Remove-Item $f -Force
        }
    }
    Write-Log "$LogName.log" "== exit code $code"
    return $code
}
