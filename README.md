# sWAVe

framework pessoal para reprodução contínua de vídeo do navegador com upscale e interpolação configuráveis.

## estado

núcleo do MVP executável: pipeline limitado, catálogo de modelos, backends fake,
adapter ONNX Runtime opcional e backend WGC/D3D11 opcional para Windows. a janela
de apresentação ainda usa o shell GDI de QA; extensão, áudio e renderização D3D11
final são gates seguintes.

o corte alpha portátil é montado por `scripts/build-release.ps1` e
`scripts/package-runtime.ps1`. a montagem GPU exige que o operador forneça um
runtime ONNX Runtime/CUDA/TensorRT compatível e modelos licenciados.

## objetivo

detectar automaticamente vídeos em Chrome/Edge, processá-los com atraso configurável de aproximadamente 1–2 s e exibir o resultado numa janela separada. o áudio deve sair pelo app; o navegador deve permanecer mudo/pausado conforme o modo de entrada.

## alvo inicial

- Windows 11
- RTX 3060 12 GB
- Ryzen 5 5600G
- 16 GB RAM
- C++20, CMake, D3D11, CUDA, ONNX Runtime com TensorRT EP/CUDA EP opcional

## plano

o plano sistemático está em:

- [plano de implementação](docs/plan.md);
- [fluxos do aplicativo](docs/flows.md);
- [telas e linguagem visual](docs/ui.md).

ordem resumida: contratos e passthrough → ONNX Runtime com TensorRT EP →
SRVGGNetCompact → RIFE → scheduler e métricas → D3D11 → captura/extensão →
áudio, perfis e empacotamento.

## limites

DRM pode impedir acesso direto ou captura. o processamento médio precisa acompanhar o fps de saída; o atraso apenas absorve picos.

## inferência

ONNX é o formato de distribuição preferencial. o primeiro runtime será ONNX
Runtime com TensorRT Execution Provider em FP16; CUDA Execution Provider será
fallback explícito. engines TensorRT são locais e dependentes da máquina; pesos
de modelos não entram no repositório.

## smoke test

sem SDK externo, o fluxo determinístico pode ser validado com:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
build/swave_cli --frames 120 --fps 30 --width 640 --height 360
```

para habilitar o runtime real, configure `SWAVE_ENABLE_ONNXRUNTIME=ON` e informe
`SWAVE_ONNXRUNTIME_ROOT`. CUDA/TensorRT e o driver NVIDIA precisam estar
compatíveis com a versão do ONNX Runtime escolhida.

no Windows, `scripts/build-alpha.ps1` automatiza build, ctest, smoke fake e
empacotamento ZIP. ele não baixa dependências externas nem modelos.

atalho para a montagem shell no Developer PowerShell x64:

```powershell
.\scripts\build-shell.ps1
```
