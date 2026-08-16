# plano de implementação

## decisão de produto

o sWAVe é um reprodutor de vídeo processado em tempo real, não um conversor offline.
continuidade e sincronismo têm precedência sobre qualidade máxima. o atraso-alvo é
de 1–2 s e só absorve picos; a vazão média precisa acompanhar o fps de saída.

alvo inicial:

- Windows 11;
- RTX 3060 12 GB, Ryzen 5 5600G e 16 GB RAM;
- C++20/CMake;
- janela content separada da janela settings;
- entrada `capture` ou `stream`;
- áudio final no app; navegador mudo quando necessário.
- o MVP deve fechar o fluxo ponta a ponta; as fases abaixo são gates internos,
  não produtos separados.

## pipeline lógico

```text
source
  → capture/direct decoder
  → timestamp normalizer
  → bounded input queue (1–2 s)
  → frame scheduler
  → SRVGGNetCompact upscaling to selected output size
  → interpolation at output resolution
  → bounded output queue
  → D3D11 content window
```

o padrão será `upscale-before-interpolation`, priorizando a qualidade do frame
ampliado antes da síntese temporal. o modo alternativo `interpolate-before-upscale`
será mantido para benchmark e fallback de desempenho, pois reduz o custo do RIFE.

### processamento

1. capturar/decodificar frames com timestamp monotônico e timestamp de mídia;
2. normalizar formato, proporção, rotação e dimensões para o preset;
3. derivar a resolução-alvo da fonte, do fator escolhido e do limite do monitor;
4. manter frames de origem necessários ao par temporal;
5. executar SRVGGNetCompact nos frames de origem;
6. gerar frames intermediários com Practical-RIFE 4.25/IFNet;
7. publicar textura/superfície no swap chain D3D11 sem alterar o tamanho da janela;
8. medir cada estágio e alimentar o scheduler.

se o frame não puder ser processado no prazo, a política é descartar trabalho
antigo controladamente, nunca crescer fila sem limite.

## runtime de inferência

```text
model manifest + ONNX
  → OnnxRuntimeTensorRtSession
  → TensorRT Execution Provider
  → CUDA buffers / FP16
```

`OnnxRuntimeCudaSession` será fallback explícito para incompatibilidade de operador,
falha de engine ou diagnóstico. não haverá fallback silencioso.

### TensorRT

- shapes fixas por preset de resolução; a resolução base é a do vídeo, limitada
  pelo monitor, e não uma lista fixa que force 720p/1080p;
- engines geradas localmente e armazenadas em cache;
- cache identificado por hash do ONNX, modelo, GPU, driver, CUDA, TensorRT,
  shape e precisão;
- FP16 padrão;
- workspace inicial de 4–6 GB, ajustável;
- buffers de entrada, saída e conversão pré-alocados na GPU;
- CUDA Graphs somente após a sessão estar estável;
- usar aproximadamente 9–10 GB como teto operacional, preservando margem para
  desktop, NVDEC, D3D11 e outros processos;
- falhar antes de OOM: reduzir buffers, preset ou backend conforme política.

TensorRT nativo só será considerado depois de benchmark demonstrar ganho que
justifique duplicar a camada de inferência.

## contratos principais

```text
FramePacket
  image/surface, media_pts, capture_qpc, duration, format, width, height

IUpscalerBackend
  initialize(manifest, session)
  process(input, output)
  flush()
  metrics()
  shutdown()

IInterpolatorBackend
  initialize(manifest, session)
  process(frame_a, frame_b, timestep, output)
  flush()
  metrics()
  shutdown()

IInferenceSession
  load(model, shape, precision)
  run(inputs, outputs)
  synchronize()
  metrics()
  close()
```

SRVGG e RIFE não conhecerão TensorRT, CUDA EP ou detalhes de alocação. o manifesto
descreverá layout, normalização, escala, shapes, entradas, saídas, licença e
pré/pós-processamento.

## componentes e ordem de construção

```text
core/frame/          formatos, timestamps, superfícies e proporção
core/queue/          filas limitadas e política de descarte
core/model/          manifesto, catálogo e validação de artefato
core/inference/      sessões ONNX Runtime/TensorRT/CUDA
core/backend/        SRVGGNetCompact, RIFE e passthrough
core/pipeline/       scheduler, estados, fallback e métricas
platform/windows/    D3D11, WGC, NVDEC, janelas e IPC
ui/                   settings, overlay e diagnósticos
extension/            Chromium/native messaging
tests/                type tests e testes determinísticos sem GPU
```

fases:

1. contratos de frame, manifesto, filas e passthrough;
2. sessão ONNX Runtime com TensorRT EP, cache, seleção de provider e fallback;
3. SRVGGNetCompact com entrada sintética/arquivo;
4. RIFE com `t=0.5`, depois timesteps arbitrários;
5. scheduler, buffer, relógio de mídia, métricas e degradação;
6. janela content D3D11;
7. captura WGC, crop e modo `capture`;
8. extensão Chromium e modo `stream` quando a mídia for acessível;
9. áudio no app, perfis, empacotamento e recuperação;
10. TensorRT nativo apenas se benchmark justificar.

## presets iniciais

```text
base:        fator 1.0, resolução da fonte limitada pelo monitor
balanced:    fator 2.0, RIFE 4.25 + SRVGG + FP16
performance: fator 1.5, RIFE 4.25.lite + SRVGG ou bypass
quality:     fator 4.0, RIFE 4.25 + SRVGG + FP16
```

fatores padrão disponíveis: `1.0`, `1.5`, `2.0`, `3.0`, `4.0`, `5.0`. o usuário
pode criar outros entre `1.1` e `5.0`. o modelo declara sua escala nativa; quando
ela não coincide com o fator desejado, o backend combina inferência com resize
final de alta qualidade, sem prometer que isso equivale a um modelo nativo.

modelos e engines são customizáveis por catálogo local: o aplicativo não fixa
caminhos de pesos. o catálogo valida manifesto, formato, entradas, licença
informada pelo usuário, engine e compatibilidade antes de ativar o modelo.

degradação automática:

1. reduzir escala do SRVGG;
2. trocar RIFE 4.25 por 4.25.lite;
3. reduzir resolução de processamento;
4. desativar interpolação;
5. manter o frame processado mais recente.

## validação por fase

no ambiente de desenvolvimento serão feitos apenas type tests, validação de
contratos, análise estática, validação de manifestos e validação de código. a
validação de throughput, latência, VRAM, captura, áudio e playback será executada
na máquina principal após commit/push.

cada benchmark da máquina principal deverá registrar: fonte, resolução, fps de
entrada/saída, preset, provider, latência por estágio, fps sustentável, fila,
VRAM, temperatura, quedas e motivo de fallback.
