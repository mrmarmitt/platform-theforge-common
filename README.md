# platform-theforge-common

Codigo comum para jogos de estudo que usam The Forge como plataforma grafica.

Este repositorio existe para concentrar adaptadores e utilitarios que sao
reutilizaveis entre jogos, mas que nao pertencem ao `cengine` generico porque
conhecem detalhes concretos do The Forge: renderer, command buffers, resource
loader, shaders FSL, input da plataforma, fontes, texturas e ciclo
`Init/Load/Unload/Draw`.

## Objetivo

Separar responsabilidades entre os projetos:

- `cengine`: loop, portas e mecanismos independentes de plataforma.
- `platform-theforge-common`: infraestrutura reutilizavel especifica do
  The Forge.
- jogos (`8puzzle`, `spaceinvaders`, `asteroids`, ...): dominio, regras,
  cenas concretas e assets de cada jogo.

## Conteudo (0.4.0)

### Novo na 0.4.0

- `forgesprite::drawSpriteRect(region, x, y, w, h, cor)` — desenha a regiao do
  atlas num **retangulo de destino arbitrario**, em vez de uma escala uniforme.

  O `drawSprite(region, x, y, escala, cor)` continua existindo: era o formato de
  que o spaceinvaders precisava, onde o sprite era 1:1 com as unidades do mundo.
  Mas escala uniforme nao da conta de um jogo cujos corpos tem proporcoes
  diferentes entre si (o breakout: tijolo 60x20, raquete 110x16, bola 12x12), nem
  de uma projecao arena->tela que estica X e Y de forma diferente. O quad ja era
  montado a partir de dois cantos — a funcao nova so para de derivar o segundo
  canto de uma escala.

### Novo na 0.3.0

- `ForgeUi` **deixou de ter vocabulario proprio**: `Key`/`KeyEvent` e o contrato
  (fila de edges + estado segurado) subiram para a `cengine::input` (task 20 da
  engine, >= 0.8.0). Esta ponte guarda o que sempre foi dela — a **captura** (o
  WndProc traduzindo `VK_*` para `Key`) — e delega o resto a um
  `cengine::input::Keyboard`.

  Era a **quarta copia** do mesmo enum no ecossistema. Os aliases
  (`using Key = cengine::input::Key;`) mantiveram a ergonomia: o asteroids
  compilou sem mudar uma linha de cena. Quem quiser falar com a porta direto tem
  `forgeui::keyboard()`.

  Consumo: incluir `$(CengineRoot)modules/input/include` e compilar
  `modules/input/src/Keyboard.cpp`.

### Novo na 0.2.0

- `ForgeLineUi` (`forgeline`): **batcher de LINHAS 2D** — `drawLine` e
  `drawPolyline` em pixels, cor por vertice, um draw call por lote. Mesma
  mecanica do batcher de sprites (vertex buffer dinamico com um trecho por frame
  in flight), mas **sem textura, sem sampler e sem SRT**: a cor vem no vertice.
  Ligado por `LineBatcherDesc::enabled` (default `false` — quem nao desenha
  linha nao paga pelo pipeline).

  Trazido pelo `asteroids`, que precisava desenhar corpos que GIRAM. A
  alternativa avaliada era rotacionar sprites; linha ganhou porque os arcades
  vetoriais (Asteroids, Lunar Lander, Tempest) sao desenhados a linha e porque
  **linha dispensa atlas**: nao ha arte para produzir, so geometria. Um poligono
  girado e uma lista de pontos girados.

### Base (0.1.0)

Extraido da PoC do Space Invaders (task 01), com o vocabulario de jogo
convertido em configuracao:

- `TheForgeWindowManager`: o casco completo — The Forge como biblioteca atras
  do port `IWindowManager` da cengine (>= 0.5.0). Janela Win32 propria,
  renderer, fontes, swapchain com resize, input via WndProc e o par
  `update()`/`present()` envolvendo as fases do jogo. Configurado por
  `TheForgeWindowDesc` (nome, tamanho, fonte, atlas, cor de clear).
- `ForgeUi`: ponte de texto/input/hints para cenas The Forge. Fila de edges
  (`readKey`) + estado segurado generico por tecla (`isHeld`/`heldAxis` —
  cada jogo compoe seu esquema de controles).
- `ForgeSpriteUi`: sprite batcher 2D com atlas, tint e flush por lote.
  Configurado por `SpriteBatcherDesc` (atlas, capacidade); a tabela de
  regioes e do jogo. `atlasPath` nulo desliga o batcher (jogo so de texto).
- `Shaders/FSL`: shaders do batcher (`sprite.vert/frag.fsl` + `sprite.srt.h`)
  e `Shaders.list` com os rootsigs padrao.
- `Format.h`: utilitarios pequenos de formatacao usados por cenas (std puro).

## Estrutura e consumo

```
src/TheForgeCommon/          <- adicionar ao include path do jogo
  TheForgeWindowManager.h/.cpp   (depende de cengine + The Forge)
  ForgeUi.h/.cpp                 (depende de The Forge)
  ForgeSpriteUi.h/.cpp           (depende de The Forge)
  ForgeLineUi.h/.cpp             (depende de The Forge)
  Format.h                       (std puro)
  Shaders/FSL/                   (Shaders.list para o passo FSL do jogo)
```

As camadas 2D sao a ORDEM DE CHAMADA, atravessando as tres pontes: o
`forgeui::drawText` da flush nos lotes pendentes de sprites e linhas antes de
gravar texto. Entao, dentro do `draw()` da cena, "geometria primeiro, texto
depois" poe o HUD por cima do jogo.

O consumo segue a receita dos jogos (vcxproj MSBuild, layout de checkouts
irmaos na mesma pasta — `The-Forge`, `cengine`, este repo e o jogo):

1. incluir `src/TheForgeCommon` no include path (os includes internos sao
   relativos: `"ForgeUi.h"`, `"Shaders/FSL/sprite.srt.h"`);
2. compilar os quatro `.cpp` junto do jogo;
3. apontar o passo FSL para `src/TheForgeCommon/Shaders/FSL/Shaders.list`
   (o `#include` do `defaults.h` assume o layout de checkouts irmaos) — ou
   copiar/estender a lista se o jogo tiver shaders proprios;
4. o jogo continua dono de `PathStatement.txt`, `gpu.cfg`, fontes e atlas
   (`TheForgeWindowDesc` recebe os caminhos).

Nada deve ser promovido para ca apenas por parecer reutilizavel. O codigo deve
entrar quando existir um consumidor real alem do jogo original ou quando uma
nova PoC precisar da mesma ponte com pouca variacao.

> **Jogos estacionados:** `8puzzle` e `spaceinvaders` NAO migram para este
> repo — ficaram congelados como documentacao viva (decisao registrada no
> ADR 0003 da cengine). As copias deles sao a evidencia de duplicacao que
> justificou a extracao; o primeiro consumidor real e o `asteroids`.

## Principios

- Codigo deste repo pode depender do The Forge.
- Codigo deste repo nao deve conter regra de jogo.
- APIs devem receber configuracao do jogo, como atlas, regioes, cores e limites
  de lote, em vez de fixar nomes como `atlas.dds`.
- O ciclo de vida deve continuar explicito para casar com The Forge:
  `init/load/begin/flush/unload/exit`.
- Cenas dos jogos continuam sendo donas da intencao: o common so fornece a
  ponte de plataforma.

## Plano

As tarefas ficam em [`.ai/task/`](.ai/task/). Comece por
[`01-extract-forge-sprite-ui.md`](.ai/task/01-extract-forge-sprite-ui.md).
