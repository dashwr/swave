# arquitetura do sWAVe

```text
Chrome/Edge extension
        ↓ native messaging / IPC
capture or direct media → decode → timestamp → input queue → scheduler
                                                        ↓
                                      SRVGGNetCompact → RIFE → output queue
                                                        ↓
                                                   content window
settings window → control IPC → scheduler/profiles/metrics
```

## contratos

`FramePacket` carrega imagem, timestamp e metadados de origem. `UpscalerBackend` e
`InterpolatorBackend` expõem inicialização, processamento, flush, métricas e
encerramento; manifestos declaram escala, precisão, resolução, layout,
pré/pós-processamento, licença e VRAM estimada.

## inferência

ONNX é o formato comum dos modelos. a implementação inicial usa ONNX Runtime com
TensorRT Execution Provider e FP16; `OnnxRuntimeCudaBackend` é fallback explícito.
TensorRT nativo só será considerado após benchmark. `TorchCudaBackend` pode existir
apenas para validar modelos ainda não exportáveis.

## regras

- filas limitadas; buffer-alvo de 1–2 s;
- prioridade: continuidade, fps, qualidade;
- reduzir modelo/escala ou desativar interpolação quando a fila cair;
- manter proporção e arredondar dimensões conforme o modelo;
- janela content silenciosa se o áudio original ainda estiver ativo;
- áudio final deve ser reproduzido pelo app quando o backend de áudio estiver implementado.
- preferir FP16 e shapes fixas por preset (`720p`, `1080p`, `1440p`) para previsibilidade;
- usar teto operacional de aproximadamente 9–10 GB na RTX 3060 de 12 GB, com workspace TensorRT inicial de 4–6 GB e fallback antes de OOM;
- pré-alocar superfícies e buffers GPU; não buscar 100% de ocupação artificialmente;
- manter modelo, manifesto, licença e configuração de pré/pós-processamento separados.

## modos de entrada

`capture`: navegador continua produzindo frames, mas fica mudo; captura da janela e crop do vídeo.

`stream`: extensão/app obtém mídia direta; navegador pode pausar completamente. limitado por `blob:`, MSE, URLs assinadas e DRM.
