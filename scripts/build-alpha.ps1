[CmdletBinding()]
param(
    [ValidateSet("shell", "gpu")][string]$Flavor = "shell",
    [string]$OnnxRuntimeRoot = $env:SWAVE_ONNXRUNTIME_ROOT,
    [string]$ModelsDir,
    [string]$CudaBinDir,
    [string]$CuDnnBinDir,
    [string]$TensorRtBinDir,
    [string]$Generator = "Visual Studio 17 2022",
    [ValidateSet("x64", "Win32", "ARM64")][string]$Architecture = "x64",
    [ValidateSet("cuda", "tensorrt")][string]$GpuProvider = "tensorrt",
    [switch]$RunGpuSmoke,
    [int]$GpuWidth = 640,
    [int]$GpuHeight = 360,
    [string]$OutputZip = "$PSScriptRoot/../artifacts/swave-alpha-$Flavor-win64.zip"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$buildDir = Join-Path $repoRoot "build/release-$Flavor"
$stageDir = Join-Path $repoRoot "artifacts/stage-$Flavor"
$buildScript = Join-Path $PSScriptRoot "build-release.ps1"
$packageScript = Join-Path $PSScriptRoot "package-runtime.ps1"
$smokeScript = Join-Path $PSScriptRoot "smoke-test.ps1"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "comando obrigatório não encontrado: $Name"
    }
}

function Require-Directory([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "diretório obrigatório não informado: $Description"
    }
    if (-not (Test-Path $Path -PathType Container)) {
        throw "$Description não encontrado: $Path"
    }
}

Require-Command "cmake"
if ($Flavor -eq "gpu") {
    Require-Directory $OnnxRuntimeRoot "ONNX Runtime root"
}

Write-Host "[1/5] compilando flavor $Flavor"
$buildArgs = @{
    Flavor = $Flavor
    BuildDir = $buildDir
    StageDir = $stageDir
    Generator = $Generator
    Architecture = $Architecture
}
if (-not [string]::IsNullOrWhiteSpace($OnnxRuntimeRoot)) {
    $buildArgs.OnnxRuntimeRoot = $OnnxRuntimeRoot
}
& $buildScript @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build Release falhou" }

Write-Host "[2/5] executando ctest"
& cmake --build $buildDir --config Release --target swave_type_tests
if ($LASTEXITCODE -ne 0) { throw "build dos type tests falhou" }
& ctest --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "ctest falhou" }

Write-Host "[3/5] executando smoke fake"
& $smokeScript -BinDir $stageDir
if ($LASTEXITCODE -ne 0) { throw "smoke fake falhou" }

if ($RunGpuSmoke) {
    if ($Flavor -ne "gpu") { throw "-RunGpuSmoke exige -Flavor gpu" }
    Require-Directory $ModelsDir "diretório de modelos"
    $upscalerManifest = Join-Path $ModelsDir "srvgv.swave-model"
    $interpolatorManifest = Join-Path $ModelsDir "rife.swave-model"
    if (-not (Test-Path $upscalerManifest) -or -not (Test-Path $interpolatorManifest)) {
        throw "smoke GPU exige models/srvgv.swave-model e models/rife.swave-model"
    }
    $cli = Join-Path $stageDir "swave_cli.exe"
    Write-Host "[4/5] executando smoke $GpuProvider"
    & $cli --provider $GpuProvider `
        --upscaler-manifest $upscalerManifest `
        --interpolator-manifest $interpolatorManifest `
        --frames 120 --fps 30 --width $GpuWidth --height $GpuHeight
    if ($LASTEXITCODE -ne 0) { throw "smoke $GpuProvider falhou" }
} else {
    Write-Host "[4/5] smoke GPU não solicitado"
}

$outputDirectory = Split-Path $OutputZip -Parent
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force $outputDirectory | Out-Null
}

Write-Host "[5/5] criando pacote"
$packageArgs = @{
    StageDir = $stageDir
    OutputZip = $OutputZip
}
foreach ($name in @("ModelsDir", "OnnxRuntimeRoot", "CudaBinDir", "CuDnnBinDir", "TensorRtBinDir")) {
    $value = Get-Variable $name -ValueOnly
    if (-not [string]::IsNullOrWhiteSpace($value)) {
        $packageArgs[$name] = $value
    }
}
& $packageScript @packageArgs
if ($LASTEXITCODE -ne 0) { throw "empacotamento falhou" }

Write-Host "alpha criada: $OutputZip"
