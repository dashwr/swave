[CmdletBinding()]
param([string]$BinDir = "$PSScriptRoot/../build/release-shell")

$ErrorActionPreference = "Stop"
$cli = Join-Path (Resolve-Path $BinDir) "swave_cli.exe"
if (-not (Test-Path $cli)) { throw "swave_cli.exe não encontrado: $cli" }
& $cli --frames 120 --fps 30 --width 640 --height 360
if ($LASTEXITCODE -ne 0) { throw "smoke com interpolação falhou" }
& $cli --frames 120 --fps 30 --width 640 --height 360 --no-interpolation
if ($LASTEXITCODE -ne 0) { throw "smoke sem interpolação falhou" }
Write-Host "smoke fake concluído"
