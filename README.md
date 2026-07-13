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

## Candidatos a extracao

Os primeiros candidatos vieram da PoC do Space Invaders:

- `ForgeUi`: ponte de texto/input/hints para cenas The Forge.
- `ForgeSpriteUi`: sprite batcher 2D com atlas, tint e flush por lote.
- `Format.h`: utilitarios pequenos de formatacao usados por cenas.
- convencoes de shader FSL/SRT e ciclo `Load/Unload`.
- tooling de atlas/texturas DDS, se ficar generico o bastante.

Nada deve ser promovido para ca apenas por parecer reutilizavel. O codigo deve
entrar quando existir um consumidor real alem do jogo original ou quando uma
nova PoC precisar da mesma ponte com pouca variacao.

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
