# One-time PC setup for SA-nx. Safe to run again: it only installs what is missing.
#   powershell -ExecutionPolicy Bypass -File tools\win\setup_pc.ps1
. "$PSScriptRoot\common.ps1"
$log = Start-Log 'setup_pc.log' 'setup_pc'

function Have([string]$cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }
function WingetInstall([string]$id, [string]$extra = '') {
    Write-Host "Installing $id ..."
    $a = @('install', '--id', $id, '-e', '--accept-source-agreements', '--accept-package-agreements')
    if ($extra) { $a += @('--override', $extra) }
    & winget @a | Out-Host
    Write-Log 'setup_pc.log' "winget $id exit $LASTEXITCODE"
}

# 1. Visual Studio 2022 C++ tools (MSVC, Windows SDK, CMake and Ninja come with them)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) { $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
if (-not $vs) {
    WingetInstall 'Microsoft.VisualStudio.2022.BuildTools' '--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended --passive --wait'
} else { Write-Host "OK  Visual Studio C++ tools: $vs" }

# 2. Git, Python, CMake, Ninja (standalone copies, in case the VS ones are not on PATH)
if (-not (Have git))    { WingetInstall 'Git.Git' }            else { Write-Host 'OK  git' }
if (-not (Have python)) { WingetInstall 'Python.Python.3.12' } else { Write-Host 'OK  python' }
if (-not (Have cmake))  { WingetInstall 'Kitware.CMake' }      else { Write-Host 'OK  cmake' }
if (-not (Have ninja))  { WingetInstall 'Ninja-build.Ninja' }  else { Write-Host 'OK  ninja' }

# 3. Vulkan SDK (the renderer and its shader compiler, glslc)
$vk = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
if (-not $vk -or -not (Test-Path (Join-Path $vk 'Bin\glslc.exe'))) {
    WingetInstall 'KhronosGroup.VulkanSDK'
    Write-Host 'If that failed, install it by hand from https://vulkan.lunarg.com/sdk/home#windows'
} else { Write-Host "OK  Vulkan SDK: $vk" }

# 4. SDL3 3.4.16, official Visual C++ package, checked against its SHA-256
if (-not (Test-Path (Join-Path $Sdl3Dir 'cmake\SDL3Config.cmake'))) {
    New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null
    $zip = Join-Path $DepsDir 'SDL3-devel-3.4.16-VC.zip'
    Write-Host 'Downloading SDL3 3.4.16 ...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-devel-3.4.16-VC.zip' -OutFile $zip -UseBasicParsing
    $h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    if ($h -ne '1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de') { throw "SDL3 download hash mismatch: $h" }
    Expand-Archive $zip -DestinationPath $DepsDir -Force
    Write-Host "OK  SDL3 unpacked to $Sdl3Dir"
} else { Write-Host 'OK  SDL3 3.4.16' }

# 5. Submodules (dream-recomp and its libchdr)
Push-Location $Root
git submodule update --init --recursive | Out-Host
Pop-Location

Write-Host ''
Write-Host 'Setup finished. If anything was installed just now, close this PowerShell window,'
Write-Host 'open a new one (so PATH and VULKAN_SDK update), and then run tools\win\build_pc.ps1.'
Write-Log 'setup_pc.log' 'done'
