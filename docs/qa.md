# primeiro gate de QA

## smoke local sem SDK

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
build/swave_cli --frames 120 --fps 30 --width 640 --height 360
build/swave_cli --frames 120 --fps 30 --width 640 --height 360 --no-interpolation
```

resultado esperado com interpolação: `2 * frames - 1` outputs. sem interpolação:
`frames` outputs. `dropped_input`, `dropped_output` e `backend_errors` devem ser 0.

## runtime Windows

configurar uma árvore local contendo headers e biblioteca do ONNX Runtime, então:

```text
cmake -S . -B build -DSWAVE_ENABLE_ONNXRUNTIME=ON \
  -DSWAVE_ONNXRUNTIME_ROOT=C:/sdk/onnxruntime
cmake --build build --config Release
```

o executável precisa das DLLs redistribuíveis do ONNX Runtime, CUDA e TensorRT
compatíveis. engines TensorRT devem ser geradas na própria RTX 3060; não copiar
engine de outra GPU.

## montagem alpha portátil

```powershell
.\scripts\build-release.ps1 -Flavor shell
.\scripts\package-runtime.ps1 `
  -StageDir .\artifacts\stage-shell `
  -OutputZip .\artifacts\swave-alpha-shell-win64.zip
```

para GPU, informe explicitamente `-OnnxRuntimeRoot` e `-ModelsDir`. o script não
baixa DLLs nem modelos e não instala SDKs.

## critérios do primeiro QA funcional

- o smoke test passa sem queda ou erro;
- manifesto inválido é rejeitado antes de carregar sessão;
- provider ausente produz erro explícito, sem fallback silencioso;
- backend ONNX rejeita formato sem pixels e layout incompatível;
- uma fonte real entrega frames monotônicos ao pipeline;
- o primeiro teste de GPU registra provider, engine, VRAM e latência por estágio.

no Windows, o target `swave_app` abre as janelas content/settings e roda uma fonte
sintética por padrão. ele aceita `--provider fake|tensorrt|cuda`,
`--upscaler-manifest FILE` e `--interpolator-manifest FILE`; o provider fake continua
sendo o modo seguro para validar o shell. o target `swave_windows` já contém a
captura WGC por `HWND`; a compilação e o teste efetivo ainda precisam ser feitos no
PC Windows principal.
