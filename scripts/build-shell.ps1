[CmdletBinding()]
param(
    [string]$Generator = "Ninja",
    [ValidateSet("x64", "Win32", "ARM64")][string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"
$buildScript = Join-Path $PSScriptRoot "build-alpha.ps1"

& $buildScript `
    -Flavor shell `
    -Generator $Generator `
    -Architecture $Architecture

if ($LASTEXITCODE -ne 0) {
    throw "build shell falhou"
}
