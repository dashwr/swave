# fluxos do aplicativo

## estados globais

```text
disconnected
  → source_detected
  → preparing
  → buffering
  → running
  → degraded
  → stalled
  → stopping
  → disconnected
```

qualquer estado operacional pode ir para `error`; erro recuperável tenta reiniciar
o estágio afetado sem perder a janela settings. erro de fonte volta a
`disconnected` e preserva as configurações.

## inicialização

1. iniciar processo principal;
2. validar modelos, manifestos, DLLs e provider disponível;
3. abrir IPC local e janela settings;
4. enumerar janelas/tabs/fontes conhecidas;
5. mostrar estado de prontidão sem iniciar captura automaticamente, salvo perfil
   que explicitamente peça isso.

ao detectar uma fonte, o primeiro preset usa a resolução do vídeo, limitada à
resolução do monitor. os presets seguintes multiplicam essa base e são recalculados
quando a fonte ou o monitor mudam.

## seleção de fonte

### modo capture

```text
selecionar janela Chrome/Edge
  → testar Windows.Graphics.Capture
  → localizar/capturar região do vídeo
  → confirmar crop na prévia
  → pedir mute no navegador
  → iniciar buffering
```

se o crop não for confiável, oferecer ajuste manual e não iniciar com região
silenciosamente errada.

### modo stream

```text
extensão detecta HTMLVideoElement
  → native messaging envia estado/posição
  → app tenta mídia direta
  → validar codec, URL e DRM
  → pausar/mutar navegador
  → iniciar decoder e buffering
```

`blob:`, MSE, URL assinada e DRM podem impedir esse modo. a falha deve oferecer
`capture`, sem exigir reconfiguração completa.

## reprodução

```text
source_detected → preparing → buffering → running
```

`buffering` termina quando há frames suficientes e as sessões TensorRT foram
aquecidas. durante `running`:

- o relógio de mídia decide o próximo frame;
- input e output têm limites independentes;
- frames atrasados podem ser descartados;
- o áudio do app, quando implementado, será o relógio mestre;
- sem áudio, usa-se o relógio de vídeo e QPC.

## degradação e recuperação

entradas para `degraded`:

- fila de saída abaixo do mínimo;
- latência média acima do intervalo de frame;
- VRAM próxima do teto;
- erro transitório do provider;
- queda de frames ou captura irregular.

recuperação em ordem: reduzir SRVGG, usar RIFE lite, reduzir resolução,
desativar interpolação, reconstruir sessão e, por último, parar com diagnóstico.
o usuário vê o motivo e pode restaurar o preset manualmente.

## troca de configuração

alterações que exigem rebuild ficam pendentes e mostram `aplicar e reaquecer`.
alterações simples, como janela e volume, aplicam imediatamente. ao aplicar:

1. pausar ingestão sem destruir o frame apresentado;
2. drenar ou descartar filas conforme a mudança;
3. carregar/cachear engines;
4. aquecer com frames sintéticos;
5. voltar a `buffering` e depois `running`.

## encerramento

```text
stop request
  → parar novas capturas
  → silenciar/finalizar áudio do app
  → liberar filas e superfícies
  → salvar perfil
  → fechar janelas
```

não salvar URLs, conteúdo capturado ou credenciais.
