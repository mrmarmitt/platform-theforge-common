#pragma once

// O terceiro irmao das pontes de desenho (ForgeUi = texto, ForgeSpriteUi =
// quads texturizados): um batcher de LINHAS 2D.
//
// Por que linhas e nao sprites rotacionados: os arcades vetoriais (Asteroids,
// Lunar Lander, Tempest) sao desenhados a linha, nao a bitmap — e linha
// dispensa atlas: nao ha arte para produzir, so geometria. Um poligono girado
// e so uma lista de pontos girados; nao existe o problema de "sprite rotacionado
// fica serrilhado" porque nao existe sprite.
//
// Mesma mecanica do batcher de sprites: as cenas desenham em modo imediato, o
// lote acumula num vertex buffer dinamico (um trecho por frame in flight) e sai
// UM draw call. O ciclo de vida (init/load/begin/flush) e do casco da
// plataforma; as cenas so enxergam drawLine/drawPolyline.
//
// Camadas 2D = ordem de chamada: o forgeui::drawText da flush no lote pendente
// antes de gravar texto, entao "linhas primeiro, texto depois" dentro do draw()
// da cena poe o texto por cima.

#include <cstdint>

// The-Forge (o include path do projeto aponta para a raiz do The-Forge).
#include "Common_3/Graphics/Interfaces/IGraphics.h"

namespace forgeline {

/// Ponto em PIXELS de tela (origem no canto superior esquerdo) — a mesma
/// convencao do forgeui::drawText. A projecao para NDC acontece aqui dentro.
struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

// Configuracao do batcher — fornecida pelo jogo na montagem do casco.
struct LineBatcherDesc
{
    // false DESLIGA o batcher: todas as funcoes viram no-op (jogo que nao
    // desenha linha nao paga pelo pipeline). Mesmo espirito do atlasPath nulo
    // do forgesprite.
    bool enabled = false;

    // Capacidade do lote por quadro; estourou, dropa a linha e loga uma vez.
    uint32_t maxLines = 4096;

    // Frames in flight do casco (trechos do vertex buffer dinamico).
    uint32_t frameCount = 2;
};

// Contadores do quadro ANTERIOR (fechados no begin() seguinte).
struct Stats
{
    uint32_t lines = 0;
    uint32_t drawCalls = 0;
};

// --- ciclo de vida (chamado pelo casco da plataforma) ---

void init(Renderer* renderer, const LineBatcherDesc& desc);
void exit();

void load(const ReloadDesc* reloadDesc, TinyImageFormat colorFormat, SampleCount sampleCount, uint32_t sampleQuality);
void unload(const ReloadDesc* reloadDesc);

void begin(Cmd* cmd, float width, float height, uint32_t frameIndex);

// Desenha o lote acumulado (1 draw call) e reinicia a acumulacao. Chamado pelo
// forgeui::drawText (linhas pendentes ficam SOB o texto) e pelo casco no fim do
// quadro. No-op com lote vazio.
void flush();

// --- consumo pelas cenas ---

/// Um segmento, em pixels, na cor ABGR (mesmo formato do forgeui::color).
void drawLine(Point from, Point to, uint32_t colorAbgr);

/// Uma sequencia de pontos ligados. `closed` fecha o ultimo no primeiro — e o
/// que desenha um poligono (a nave, uma rocha) numa chamada so.
void drawPolyline(const Point* points, uint32_t count, bool closed, uint32_t colorAbgr);

Stats lastFrameStats();

} // namespace forgeline
