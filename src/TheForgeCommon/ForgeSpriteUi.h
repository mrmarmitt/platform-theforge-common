#pragma once

// O irmao do ForgeUi para sprites: as cenas desenham via drawSprite() em modo
// imediato, e o batcher acumula os quads num vertex buffer dinamico (um trecho
// por frame in flight) e emite UM draw call por lote. O ciclo de vida
// (init/load/begin/flush) e do casco da plataforma (TheForgeWindowManager ou
// IApp); as cenas so enxergam drawSprite + as regioes de atlas DO JOGO.
//
// Extraido da PoC do spaceinvaders (task 01 deste repo). O que era suposicao
// de jogo virou configuracao: o atlas, o limite de lote e a tabela de regioes
// pertencem ao jogo — este modulo so recebe SpriteRegion por chamada.
//
// Camadas 2D = ordem de chamada, atravessando as duas pontes: o
// forgeui::drawText da flush no lote pendente antes de gravar texto, entao
// "sprites primeiro, texto depois" dentro do draw() da cena produz o texto
// por cima — a mesma semantica de um renderer 2D imediato de verdade.

#include <cstdint>

// The-Forge (o include path do projeto aponta para a raiz do The-Forge).
#include "Common_3/Graphics/Interfaces/IGraphics.h"

namespace forgesprite {

// Regiao do atlas em PIXELS (origem no canto superior esquerdo da textura).
// A tabela de regioes e do JOGO (vocabulario de jogo nao entra aqui) — cada
// consumidor declara as suas, casadas com o atlas que ele mesmo gera.
struct SpriteRegion
{
    float x;
    float y;
    float w;
    float h;
};

// Configuracao do batcher — fornecida pelo jogo na montagem do casco.
struct SpriteBatcherDesc
{
    // Caminho da textura de atlas no resource loader (ex.: "atlas.dds").
    // nullptr DESLIGA o batcher: todas as funcoes viram no-op (jogo so de
    // texto nao paga pelo pipeline de sprites).
    const char* atlasPath = nullptr;

    // Capacidade do lote por quadro; estourou, dropa o sprite e loga uma vez.
    uint32_t maxSprites = 2048;

    // Frames in flight do casco (trechos do vertex buffer dinamico).
    uint32_t frameCount = 2;
};

// Contadores do quadro ANTERIOR (fechados no begin() seguinte) — e o que a
// cena mostra na tela sem depender da ordem draw-sprites/draw-texto.
struct Stats
{
    uint32_t sprites = 0;
    uint32_t drawCalls = 0;
};

// --- ciclo de vida (chamado pelo casco da plataforma) ---

// Init do app: cria textura do atlas + sampler + vertex buffer dinamico
// (desc.frameCount trechos). Requer resource loader + root signature prontos;
// o casco chama waitForAllResourceLoads() depois. Com desc.atlasPath nulo o
// batcher fica desligado e todas as demais chamadas sao no-ops seguros.
void init(Renderer* renderer, const SpriteBatcherDesc& desc);
void exit();

// Load/Unload do app: shader + descriptor set (RELOAD_TYPE_SHADER) e
// pipeline (SHADER|RENDERTARGET), como os recursos proprios do casco.
void load(const ReloadDesc* reloadDesc, TinyImageFormat colorFormat, SampleCount sampleCount, uint32_t sampleQuality);
void unload(const ReloadDesc* reloadDesc);

// Abre o lote do quadro: publica cmd + dimensoes, avanca o trecho do vertex
// buffer (frameIndex) e fecha os contadores do quadro anterior.
void begin(Cmd* cmd, float width, float height, uint32_t frameIndex);

// Desenha o lote acumulado (1 draw call) e reinicia a acumulacao. Chamado
// pelo forgeui::drawText (sprites pendentes ficam SOB o texto) e pelo casco
// no fim do quadro (cauda sem texto). No-op com lote vazio.
void flush();

// --- consumo pelas cenas ---

// Enfileira um sprite: regiao do atlas, canto superior esquerdo em pixels,
// escala (multiplica o tamanho nativo da regiao) e tint ABGR (mesmo formato
// do forgeui::color; com atlas branco, o tint E a cor).
//
// A escala e UNIFORME — o formato que o spaceinvaders precisava, onde o sprite
// era 1:1 com as unidades do mundo.
void drawSprite(const SpriteRegion& region, float x, float y, float scale, uint32_t colorAbgr);

// Enfileira um sprite num RETANGULO DE DESTINO arbitrario (x, y, w, h em
// pixels): a regiao e esticada para caber, sem preservar a proporcao.
//
// Existe porque a escala uniforme nao da conta de um jogo cujos corpos tem
// proporcoes diferentes entre si (o breakout: tijolo 60x20, raquete 110x16,
// bola 12x12) — nem de uma projecao arena->tela que estica X e Y de forma
// diferente. O quad ja e montado a partir de dois cantos; esta funcao so para
// de derivar o segundo canto de uma escala.
void drawSpriteRect(const SpriteRegion& region, float x, float y, float w, float h, uint32_t colorAbgr);

Stats lastFrameStats();

} // namespace forgesprite
