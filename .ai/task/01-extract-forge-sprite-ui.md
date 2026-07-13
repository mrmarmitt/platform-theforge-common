# 01 - Extrair pontes The Forge reutilizaveis

- **Status:** todo
- **Prioridade:** media - a necessidade apareceu com `spaceinvaders` e tende a
  crescer com os proximos jogos 2D.
- **Categoria:** Plataforma
- **Origem:** `spaceinvaders/src/platform/theforge/src/SpaceInvadersForge`
- **Consumidores candidatos:** `spaceinvaders`, `8puzzle`, `asteroids`

## Contexto

O Space Invaders criou pecas de plataforma mais genericas que o jogo em si:

- `ForgeUi`: input, texto, hints e medidas de fonte.
- `ForgeSpriteUi`: sprite batcher 2D com atlas, tint por vertice, vertex buffer
  dinamico e flush por lote.
- convencoes de FSL/SRT, descriptor set, sampler nearest e alpha blending.

Essas pecas nao pertencem ao `cengine`, porque conhecem The Forge diretamente.
Tambem nao deveriam ser copiadas indefinidamente entre jogos. Este repositorio
e o lugar para transformar essas pontes em infraestrutura reutilizavel de
plataforma.

## Objetivo

Extrair primeiro o `ForgeSpriteUi` para uma API configuravel, mantendo o
contrato que funcionou no Space Invaders:

```cpp
drawSprite(region, x, y, scale, tint);
flush();
```

A extracao deve remover suposicoes de jogo, principalmente:

- atlas fixo chamado `atlas.dds`;
- tabela de regioes fixa com invasores/jogador/tiro;
- limite de lote hardcoded sem configuracao;
- nomes ligados a Space Invaders.

## Escopo Proposto

### Degrau 1 - desenho da API

Definir a forma minima de configuracao:

```cpp
struct SpriteRegion {
    float x;
    float y;
    float w;
    float h;
};

struct SpriteBatcherDesc {
    const char* atlasPath;
    uint32_t maxSprites;
    uint32_t frameCount;
};
```

Pontos a decidir:

- namespace do projeto;
- ownership dos recursos The Forge;
- se a API sera global como a PoC ou objeto instanciavel;
- como expor estatisticas de frame;
- como integrar flush automatico antes de texto.

### Degrau 2 - extracao mecanica

Copiar a implementacao do `spaceinvaders`, remover nomes de jogo e adaptar para
a API configuravel.

Nao adicionar funcionalidades novas neste degrau.

### Degrau 3 - primeiro consumidor real

Migrar o `spaceinvaders` para consumir este repo. O aceite real e o jogo seguir
renderizando:

- horda, jogador, tiros e bombas;
- texto por cima dos sprites;
- resize/reload;
- um draw call por lote contiguo.

### Degrau 4 - segundo consumidor

Validar com outro jogo ou PoC. Se a API exigir alteracao por causa desse
segundo consumidor, registrar o aprendizado antes de estabilizar.

## Fora do Escopo

- Engine generica independente de The Forge.
- Regras de jogo.
- Tabela de sprites especifica de qualquer jogo.
- ECS, scene graph ou sistema de UI completo.
- Som.
- Rotacao/camera no primeiro corte, salvo se o consumidor real exigir.

## Criterios de Aceite

- [ ] README documenta objetivo e fronteiras do repo.
- [ ] API do sprite batcher nao contem vocabulario de jogo.
- [ ] `spaceinvaders` consome o common sem regressao visual.
- [ ] Recursos The Forge continuam com ciclo de vida claro:
      `init/load/begin/flush/unload/exit`.
- [ ] O codigo permanece isolado do `cengine`.

## Riscos

- Extrair cedo demais e congelar a API no formato do Space Invaders.
- Esconder demais o ciclo de vida do The Forge e dificultar debug.
- Misturar input/texto/sprites em uma abstracao grande antes de haver dois
  consumidores reais.

## Relacionado

- `spaceinvaders` task 01 - PoC sprites concluida.
- `cengine` ADR 0002 - filtro anti-deposito: codigo de plataforma vai para um
  pacote de plataforma, nao para a engine generica.
