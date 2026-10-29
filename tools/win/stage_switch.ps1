# Puts the game data next to the Switch build in out\sd\switch\sonicadv, ready to copy to the SD
# card: the disc (GDI and tracks) into disc\, 1ST_READ.BIN, and the memory card into save\.
# The NRO and sonicadv.toml are put there by Claude's Switch build.
#   powershell -ExecutionPolicy Bypass -File tools\win\stage_switch.ps1
. "$PSScriptRoot\common.ps1"
$Dest = Join-Path $Root 'out\sd\switch\sonicadv'
New-Item -ItemType Directory -Force -Path (Join-Path $Dest 'disc'), (Join-Path $Dest 'save') | Out-Null

$assets = Join-Path $Root 'assets'
$disc = @(Get-ChildItem $assets -File | Where-Object { $_.Extension -in '.gdi', '.bin', '.raw' })
if (-not ($disc | Where-Object { $_.Extension -eq '.gdi' })) { throw 'No .gdi in assets\. Tell Claude.' }
foreach ($f in $disc) {
    $to = Join-Path (Join-Path $Dest 'disc') $f.Name
    # Copy unless an identical-size copy is already there (a cut-off earlier copy is replaced).
    if (-not (Test-Path $to) -or (Get-Item $to).Length -ne $f.Length) {
        Write-Host "copying $($f.Name) ($([math]::Round($f.Length / 1MB)) MB) ..."
        Copy-Item -LiteralPath $f.FullName -Destination $to -Force
    }
}
Copy-Item -LiteralPath (Join-Path $Root 'games\sonicadv\extracted\fs\1ST_READ.BIN') -Destination $Dest -Force
if ((Test-Path $VmuFile) -and -not (Test-Path (Join-Path $Dest 'save\vmu_A1.bin'))) {
    Copy-Item -LiteralPath $VmuFile -Destination (Join-Path $Dest 'save') -Force
}

$need = @('sonicadv.nro', 'sonicadv.toml', '1ST_READ.BIN')
$missing = @($need | Where-Object { -not (Test-Path (Join-Path $Dest $_)) })
Write-Host ''
Get-ChildItem $Dest -Recurse -File | ForEach-Object {
    Write-Host ("  {0,-60} {1,12:N0} bytes" -f $_.FullName.Substring($Dest.Length + 1), $_.Length)
}
if ($missing.Count) {
    Write-Host "Not there yet (Claude's build puts them here): $($missing -join ', ')"
} else {
    Write-Host ''
    Write-Host "Ready. Copy the folder $Dest to the SD card as sdmc:\switch\sonicadv\"
}
