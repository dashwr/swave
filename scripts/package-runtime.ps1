[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$OutputZip,
    [string]$OnnxRuntimeRoot,
    [string]$ModelsDir,
    [string]$CudaBinDir,
    [string]$CuDnnBinDir,
    [string]$TensorRtBinDir
)

$ErrorActionPreference = "Stop"
$stage = (Resolve-Path $StageDir).Path
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("swave-alpha-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force $temp | Out-Null
Copy-Item "$stage/*" $temp -Recurse -Force
if ($OnnxRuntimeRoot) {
    $runtime = (Resolve-Path $OnnxRuntimeRoot).Path
    Get-ChildItem $runtime -Recurse -File -Include "onnxruntime*.dll" | ForEach-Object {
        Copy-Item $_.FullName $temp -Force
    }
}
function Copy-RuntimeDlls([string]$Root, [string[]]$Patterns) {
    if ([string]::IsNullOrWhiteSpace($Root)) { return }
    $resolved = (Resolve-Path $Root).Path
    foreach ($pattern in $Patterns) {
        Get-ChildItem $resolved -Recurse -File -Filter $pattern | ForEach-Object {
            Copy-Item $_.FullName $temp -Force
        }
    }
}
Copy-RuntimeDlls $CudaBinDir @("cudart64_*.dll", "cublas64_*.dll", "cublasLt64_*.dll")
Copy-RuntimeDlls $CuDnnBinDir @("cudnn*.dll")
Copy-RuntimeDlls $TensorRtBinDir @("nvinfer*.dll", "nvinfer_plugin*.dll", "nvonnxparser*.dll")
if ($ModelsDir) {
    Copy-Item ((Resolve-Path $ModelsDir).Path) (Join-Path $temp "models") -Recurse -Force
}
$notice = @"
sWAVe alpha runtime

Esta montagem não baixa nem licencia modelos ou runtimes externos. Verifique os
termos de ONNX Runtime, CUDA, cuDNN, TensorRT e de cada modelo antes de distribuir.
O driver NVIDIA não faz parte do pacote.
"@
Set-Content (Join-Path $temp "THIRD_PARTY_NOTICES.txt") $notice -Encoding UTF8
Get-ChildItem $temp -Recurse -File | Get-FileHash -Algorithm SHA256 |
    ForEach-Object { "$($_.Hash)  $($_.Path.Substring($temp.Length + 1))" } |
    Set-Content (Join-Path $temp "SHA256SUMS.txt") -Encoding ASCII
if (Test-Path $OutputZip) { Remove-Item $OutputZip -Force }
Compress-Archive -Path "$temp/*" -DestinationPath $OutputZip
Remove-Item $temp -Recurse -Force
Write-Host "pacote criado em $OutputZip"
