# sWAVe alpha para Windows

## escopo

esta alpha é um pacote portátil para validação técnica em Windows 11. o shell
fake é reproduzível; o modo GPU depende de uma combinação local compatível de
driver NVIDIA, CUDA, cuDNN, TensorRT e ONNX Runtime.

o pacote não contém driver NVIDIA, SDK de desenvolvimento ou pesos não
licenciados. engines TensorRT são geradas na máquina de teste e não devem ser
copiadas entre GPUs diferentes.

## requisitos

- Windows 11 x64;
- driver NVIDIA compatível com a matriz escolhida;
- para GPU: DLLs redistribuíveis compatíveis incluídas no pacote;
- para captura: permissão normal de captura de janela e Chrome/Edge aberto;
- espaço gravável para `cache/engines`.

## execução

```powershell
.\swave_app.exe
.\swave_cli.exe --frames 120 --fps 30 --width 640 --height 360
```

para automatizar build, testes, smoke fake e ZIP shell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-alpha.ps1 -Flavor shell
```

o script usa `Visual Studio 17 2022` com arquitetura `x64` por padrão, sem
depender de abrir um Developer PowerShell. para outra instalação, informe
`-Generator` e `-Architecture`.

para GPU, informe o root do ONNX Runtime. DLLs CUDA, cuDNN, TensorRT e modelos
continuam sendo dependências locais e licenciadas:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-alpha.ps1 `
  -Flavor gpu `
  -OnnxRuntimeRoot C:\deps\onnxruntime `
  -ModelsDir .\models `
  -CudaBinDir C:\deps\cuda\bin `
  -CuDnnBinDir C:\deps\cudnn\bin `
  -TensorRtBinDir C:\deps\tensorrt\lib
```

adicione `-RunGpuSmoke` somente quando os manifestos reais estiverem prontos.
o script não baixa SDKs, drivers, runtimes ou pesos.

para provider real, os manifestos devem apontar para modelos existentes:

```powershell
.\swave_cli.exe --provider tensorrt `
  --upscaler-manifest models/srvgv-test.swave-model `
  --interpolator-manifest models/rife-test.swave-model
```

o provider ausente deve falhar explicitamente. não trate uma execução fake como
validação de TensorRT.

## limitações conhecidas da alpha

- áudio próprio ainda não está incluído;
- stream direto, extensão Chromium e DRM não são garantidos;
- a captura WGC precisa ser validada no PC alvo, especialmente resize, DPI e
  encerramento;
- modelos e licenças devem ser fornecidos separadamente quando necessário;
- métricas de throughput e VRAM só são válidas na máquina com GPU.

## diagnóstico mínimo

registre provider, versão do driver, modelo, resolução, fps, latência por estágio,
VRAM, temperatura, frames descartados e motivo de fallback. não inclua URLs,
conteúdo capturado ou credenciais no diagnóstico.
