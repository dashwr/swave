[CmdletBinding()]
param(
    [ValidateSet("shell", "gpu")][string]$Flavor = "shell",
    [string]$BuildDir = "$PSScriptRoot/../build/release-$Flavor",
    [string]$StageDir = "$PSScriptRoot/../artifacts/stage-$Flavor",
    [string]$OnnxRuntimeRoot = $env:SWAVE_ONNXRUNTIME_ROOT,
    [string]$Generator = "Visual Studio 17 2022",
    [ValidateSet("x64", "Win32", "ARM64")][string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"
if ($Flavor -eq "gpu" -and [string]::IsNullOrWhiteSpace($OnnxRuntimeRoot)) {
    throw "Flavor gpu exige -OnnxRuntimeRoot ou SWAVE_ONNXRUNTIME_ROOT."
}
$sourceDir = (Resolve-Path "$PSScriptRoot/..").Path
$configureArgs = @("-S", $sourceDir, "-B", $BuildDir, "-G", $Generator)
if ($Generator -like "Visual Studio *") {
    $configureArgs += @("-A", $Architecture)
}
$configureArgs += "-DCMAKE_BUILD_TYPE=Release"

$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cachedGenerator = Select-String -Path $cacheFile -Pattern "^CMAKE_GENERATOR:INTERNAL=" |
        Select-Object -First 1
    if ($cachedGenerator -and $cachedGenerator.Line -ne "CMAKE_GENERATOR:INTERNAL=$Generator") {
        Write-Host "removendo cache de generator incompatível: $($cachedGenerator.Line)"
        Remove-Item $BuildDir -Recurse -Force
    }
}
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
