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

# Windows CMake and Ninja, never MSYS2 or Git-for-Windows copies (devkitPro installs MSYS2, and
# its cmake turns C:\SA-nx into /c/SA-nx/C:/SA-nx). Visual Studio's bundled copies come first.
function Find-WinTool([string]$Name) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $cands = @()
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        if ($vs) {
            $ext = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
            if ($Name -eq 'cmake') { $cands += Join-Path $ext 'CMake\bin\cmake.exe' }
            if ($Name -eq 'ninja') { $cands += Join-Path $ext 'Ninja\ninja.exe' }
        }
    }
    if ($Name -eq 'cmake') { $cands += "$env:ProgramFiles\CMake\bin\cmake.exe" }
    $cands += (Get-Command "$Name.exe" -All -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
    foreach ($c in $cands) {
        if ($c -and (Test-Path $c) -and ($c -notmatch '(?i)msys|devkitpro|\\usr\\bin|\\mingw')) { return $c }
    }
    throw "No Windows $Name.exe found. Run tools\win\setup_pc.ps1 (it installs one), then try again."
}

# Applies patches\dream-recomp\*.patch to the submodule as one set, in name order. Later patches
# may edit lines earlier ones added, so a patch cannot be checked on its own. A stamp records which
# set is applied; when the set changes, the submodule is reset to its pinned files and the whole
# set is applied again. Unchanged set: nothing is touched, so nothing is rebuilt.
function Apply-Patches {
    $sub = Join-Path $Root 'dream-recomp'
    $stamp = Join-Path $Root 'out\dream-patches.stamp'
    $patches = @(Get-ChildItem (Join-Path $Root 'patches\dream-recomp\*.patch') | Sort-Object Name)
    $sha = [Security.Cryptography.SHA256]::Create()
    $all = New-Object IO.MemoryStream
    foreach ($p in $patches) { $b = [IO.File]::ReadAllBytes($p.FullName); $all.Write($b, 0, $b.Length) }
    $want = -join ($sha.ComputeHash($all.ToArray()) | ForEach-Object { $_.ToString('x2') })
    $have = ''
    if (Test-Path $stamp) { $have = (Get-Content $stamp -Raw).Trim() }
    if ($have -eq $want) { return }

    $ErrorActionPreference = 'Continue'  # git writes progress and notes to stderr
    Write-Host 'Applying dream-recomp patches ...'
    & git -C $sub checkout -- . 2>&1 | Out-Null
    & git -C $sub clean -fdq 2>&1 | Out-Null  # files a patch adds must not exist when it applies
    foreach ($p in $patches) {
        $msg = & git -C $sub apply --whitespace=nowarn $p.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Log 'build_pc.log' "patch $($p.Name) failed: $msg"
            throw "Patch $($p.Name) does not apply. Tell Claude."
        }
        Write-Host "  applied $($p.Name)"
        Write-Log 'build_pc.log' "applied $($p.Name)"
    }
    [IO.File]::WriteAllText($stamp, $want)
}

# Parallel compile jobs: one per logical core, but no more than one per 3 GB of RAM, since each
# 10 MB piece of translated game code needs a few GB to compile.
function Get-BuildJobs {
    $cores = [Environment]::ProcessorCount
    $ramGb = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    return [math]::Max(1, [math]::Min($cores, [math]::Floor($ramGb / 3)))
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
    $null = $p.Handle  # without this, Windows PowerShell 5.1 loses the exit code
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
