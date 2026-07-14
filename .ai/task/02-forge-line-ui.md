# 02 - ForgeLineUi: batcher de linhas 2D (wireframe vetorial)

- **Status:** done (0.2.0)
- **Prioridade:** media - o asteroids nao consegue se desenhar sem isto.
- **Categoria:** Ponte de plataforma
- **Consumidor real:** `asteroids` (nave, rochas e tiros em wireframe).

## Contexto

O `forgesprite` (0.1.0) desenha quads texturizados a partir de um atlas, e nao
rotaciona. O asteroids precisa desenhar corpos que GIRAM — a nave, e as rochas.
A task 01 daquele jogo ja previa "rotacao no forgesprite" como pre-requisito, e
a task 02 dele ADIOU a decisao de proposito: escolher o renderizador antes de o
jogo existir seria decidir sem o consumidor na mao.

Com o jogo pronto (nave, rochas, colisao, placar, recordes — tudo desenhado com
texto provisorio), a decisao ficou facil.

## Decisao: LINHA, nao sprite rotacionado

Duas razoes, e a segunda e a decisiva:

1. **Fidelidade.** Asteroids e um arcade VETORIAL (como Lunar Lander e Tempest):
   a tela original e desenhada a linha, nao a bitmap. Wireframe nao e uma
   aproximacao do original — e o original.
2. **Linha dispensa ATLAS.** Nao ha arte para produzir, gerar, versionar ou
   carregar: um poligono girado e uma lista de pontos girados. Rotacionar
   sprites, alem de exigir um atlas que nao existe, traria o problema classico
   do bitmap girado (serrilhado, filtragem, pivot).

O `forgesprite` fica como esta: nao ganha rotacao. Quando um jogo de sprites
girados aparecer, ele traz a evidencia — e ai a rotacao entra com um consumidor
real (mesma disciplina da ADR 0002 da cengine).

## Escopo

`forgeline` (`ForgeLineUi.h/.cpp`), no molde do `forgesprite`:

- `drawLine(from, to, cor)` e `drawPolyline(pontos, n, closed, cor)` em PIXELS
  (mesma convencao do `forgeui::drawText`); a projecao para NDC e interna.
- Vertice de 12 bytes (posicao + cor RGBA8) — **sem UV, sem textura, sem sampler
  e sem SRT**: o shader nao le recurso nenhum, a cor vem no vertice.
  Topologia `PRIMITIVE_TOPO_LINE_LIST`.
- Vertex buffer dinamico com um trecho por frame in flight (mesma receita e
  mesma armadilha do batcher de sprites: escrever no trecho que a GPU ainda le).
- `LineBatcherDesc::enabled = false` por padrao — desligado, tudo vira no-op.
- Shaders `line.vert/frag.fsl` na `Shaders.list` do repo.
- Ciclo de vida ligado no `TheForgeWindowManager` (init/load/begin/flush/
  unload/exit) e o `forgeui::drawText` passa a dar flush TAMBEM no lote de
  linhas — texto continua por cima.

## Criterios de Aceite

- [x] Batcher opt-in; com `enabled=false` o jogo nao paga pipeline nenhum.
- [x] Pipeline sem SRT (sem descriptor set) — o D3D12 aceita e o log sobe limpo.
- [x] Consumidor real validado: o asteroids desenha nave/rochas/tiros em
      wireframe (e o wrap-around aparece inteiro nas bordas).
- [x] README documenta a decisao linha x sprite rotacionado.
