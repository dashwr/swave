# telas e linguagem visual

## direção

interface desktop densa, sóbria e funcional, inspirada na linguagem visual de
SDKs da Valve sem copiar marca ou componentes proprietários. o conteúdo visual é
neutro; o destaque vem de hierarquia, espaçamento, estados e foco.

## tokens de cor

```text
background       #171717
surface           #202020
surface-raised   #2b2b2b
surface-hover    #363636
border           #4a4a4a
text-primary     #f2f2f2
text-secondary   #c4c4c4
text-muted       #8e8e8e
focus            #ffffff
disabled         #666666
```

usar somente a escala neutra como regra visual. estados de erro, atenção e sucesso
também terão ícone, texto e padrão/contraste; nunca dependerão apenas de cor.

## shell de settings

janela redimensionável, título com fonte e estado, navegação lateral fixa e área
principal rolável:

```text
[ sWAVe ]  [fonte atual] [buffer] [fps] [estado]
┌──────────────┬─────────────────────────────────────────┐
│ reprodução   │ título da seção                         │
│ processamento│ controles e prévia                      │
│ fonte        │                                         │
│ desempenho   │                                         │
│ áudio        │                                         │
│ modelos      │                                         │
│ diagnóstico  │                                         │
└──────────────┴─────────────────────────────────────────┘
```

### reprodução

- fonte atual, play/pause, atraso alvo e sincronismo;
- fps de saída: original, 24, 25, 30, 48, 50, 60, 90, 120;
- botão para abrir/ocultar a janela content;
- estado de buffer e mensagens de recuperação.

### processamento

- preset `balanced`, `performance`, `quality`;
- SRVGGNetCompact: bypass e fator de saída configurável;
- resolução-base da fonte, resolução-alvo e limite do monitor;
- RIFE: desligado, 4.25, 4.25.lite;
- ordem `interpolate → upscale` ou `upscale → interpolate`;
- preservação de proporção e resolução de processamento;
- janela pode ser redimensionada ou maximizada independentemente da resolução
  processada.

### fonte

- lista de janelas/tabs detectadas;
- modo `capture`/`stream`;
- crop com prévia, ajuste manual e reset;
- mute/pausa do navegador com confirmação do efeito;
- aviso claro quando DRM ou MSE impedir stream.

### desempenho

- provider ativo: TensorRT EP ou CUDA EP;
- latência por estágio, fps sustentável e frames descartados;
- input/output buffer;
- VRAM usada/teto, workspace e engine cache;
- política de degradação automática;
- ação `reativar preset` depois de degradação.

### áudio

na primeira versão, mostrar estado `planejado` sem fingir suporte. depois incluir
dispositivo, volume, atraso, mute e prevenção de eco.

### modelos

- catálogo de manifestos instalados e editáveis;
- caminho do ONNX/engine e validade de licença/configuração;
- status do engine cache;
- provider, precision e shapes;
- aquecimento manual e limpeza de cache.

### diagnóstico

- log filtrável sem conteúdo de vídeo nem URLs sensíveis;
- exportação de diagnóstico sanitizado;
- versões de app, driver, CUDA, TensorRT e ONNX Runtime;
- cópia de métricas para benchmark.

## content window

janela mínima e sem controles permanentes sobre o vídeo. overlay temporário mostra
fonte, preset, fps e estado; desaparece por timeout e volta com atalho. estados sem
fonte, buffering, degraded e error têm mensagem central acionável.

## interação e acessibilidade

- toda função de mouse tem atalho equivalente quando aplicável;
- navegação por teclado, foco visível e ordem lógica;
- controles têm rótulo, unidade e valor atual;
- sliders permitem entrada precisa por teclado;
- contraste mínimo de texto validado contra a superfície;
- diálogos destrutivos ou que mutem/pausam o navegador pedem confirmação;
- layout funciona em 1280×720 e cresce até 4K sem esconder diagnóstico.

## tecnologia de UI do MVP

settings será uma janela Win32 com Dear ImGui sobre D3D11, adequada para uma
ferramenta desktop de controles e diagnóstico. a content window será Win32/D3D11
separada. a camada de estado não dependerá da biblioteca visual, permitindo trocar
a UI sem reescrever pipeline ou IPC.
