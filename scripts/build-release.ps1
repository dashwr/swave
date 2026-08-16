[CmdletBinding()]
param(
    [ValidateSet("shell", "gpu")][string]$Flavor = "shell",
    [string]$BuildDir = "$PSScriptRoot/../build/release-$Flavor",
    [string]$StageDir = "$PSScriptRoot/../artifacts/stage-$Flavor",
    [string]$OnnxRuntimeRoot = $env:SWAVE_ONNXRUNTIME_ROOT
)

$ErrorActionPreference = "Stop"
if ($Flavor -eq "gpu" -and [string]::IsNullOrWhiteSpace($OnnxRuntimeRoot)) {
    throw "Flavor gpu exige -OnnxRuntimeRoot ou SWAVE_ONNXRUNTIME_ROOT."
}
$sourceDir = (Resolve-Path "$PSScriptRoot/..").Path
$configureArgs = @("-S", $sourceDir, "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=Release")
if ($Flavor -eq "gpu") {
    $configureArgs += "-DSWAVE_ENABLE_ONNXRUNTIME=ON"
    $configureArgs += "-DSWAVE_ONNXRUNTIME_ROOT=$OnnxRuntimeRoot"
} else {
    $configureArgs += "-DSWAVE_ENABLE_ONNXRUNTIME=OFF"
}
cmake @configureArgs
cmake --build $BuildDir --config Release
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
cmake --install $BuildDir --config Release --prefix $StageDir
New-Item -ItemType Directory -Force "$StageDir/cache/engines" | Out-Null
New-Item -ItemType Directory -Force "$StageDir/models" | Out-Null
Write-Host "stage criado em $StageDir"
