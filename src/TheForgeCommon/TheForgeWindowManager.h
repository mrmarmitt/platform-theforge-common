#pragma once

// TheForgeWindowManager — o The-Forge como BIBLIOTECA atras do port
// IWindowManager da cengine (receita validada no 8puzzle fase 2 e no
// spaceinvaders; extraida para este repo na task 01). A cengine e dona do
// loop (EngineManager::owned(...).start()); este window manager encapsula
// TUDO de janela/GPU no par update()/present() da cengine >= 0.5.0:
//
//   init()     janela Win32 propria + cola do fontstash + renderer + fontes
//              + batcher de sprites (forgesprite, opcional) + swapchain
//   update()   pump de mensagens (WndProc alimenta a fila de teclado E o
//              estado segurado do forgeui), resize pendente (recria
//              swapchain), acquire da imagem, beginCmd + barriers +
//              forgesprite::begin + forgeui::beginDraw
//   present()  forgesprite::flush (cauda do lote), fecha o command buffer,
//              submit e queuePresent
//   cleanup()  teardown na ordem inversa
//
// As cenas nao conhecem esta classe: desenham/leem teclado via forgeui e
// forgesprite. O que era suposicao de jogo (fonte, atlas, cor de fundo,
// tamanho) chega pela TheForgeWindowDesc do composition root.

#include <cstdint>

#include <cengine/core/IWindowManager.hpp>

#include "ForgeLineUi.h"
#include "ForgeSpriteUi.h"

// Configuracao do casco — montada pelo composition root do jogo. Os const
// char* devem apontar para literais/strings vivos durante toda a execucao.
struct TheForgeWindowDesc
{
    // Nome do app (initRenderer, titulo da janela).
    const char* appName = "Game";

    // Tamanho INICIAL do client area; a janela e redimensionavel (WM_SIZE
    // recria o swapchain no update()).
    int32_t width = 1280;
    int32_t height = 720;

    // Fonte do fontstash, relativa ao RD_FONTS do PathStatement do jogo
    // (ex.: "TitilliumText/TitilliumText-Bold.otf").
    const char* fontPath = "TitilliumText/TitilliumText-Bold.otf";

    // Batcher de sprites: atlasPath nulo desliga (jogo so de texto).
    // O frameCount e preenchido pelo casco (frames in flight).
    forgesprite::SpriteBatcherDesc sprites = {};

    // Batcher de linhas (wireframe vetorial): enabled=false desliga.
    // O frameCount tambem e preenchido pelo casco.
    forgeline::LineBatcherDesc lines = {};

    // Cor de clear do swapchain (RGBA 0..1).
    float clearColor[4] = { 0.02f, 0.02f, 0.05f, 1.0f };
};

class TheForgeWindowManager final : public cengine::core::IWindowManager
{
public:
    explicit TheForgeWindowManager(const TheForgeWindowDesc& desc);

    void init() override;
    void update() override;
    void present() override;
    void cleanup() override;

private:
    bool initGraphics();
    void applyPendingResize();

    TheForgeWindowDesc m_desc;
    int32_t            m_width;
    int32_t            m_height;
};
