#include "TheForgeWindowManager.h"

#include "Common_3/Application/Interfaces/IFont.h"
#include "Common_3/Graphics/Interfaces/IGraphics.h"
#include "Common_3/OS/Interfaces/IOperatingSystem.h"
#include "Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common_3/Utilities/Interfaces/ILog.h"

#include "Common_3/Utilities/RingBuffer.h"

// fsl (INIT_RS_DESC)
#include "Common_3/Graphics/FSL/defaults.h"

// pontes: fila de teclado/estado segurado + batcher — o WndProc publica,
// as cenas consomem
#include "ForgeSpriteUi.h"
#include "ForgeUi.h"

#include <cstdlib>

#include "Common_3/Utilities/Interfaces/IMemory.h" // deve ser o ultimo include

// Cola minima do sistema de fontes (aprendizado da fase 2 do 8puzzle):
// simbolos do OS.lib fora dos headers de interface, declarados como o
// WindowsBase faz.
extern bool        initWindowSystem();       // WindowsWindow.cpp: classe + monitores
extern void        exitWindowSystem();       // WindowsWindow.cpp: libera monitores
extern WindowDesc* gWindow;                  // WindowsWindow.cpp: lido por getActiveMonitorIdx
extern bool        platformInitFontSystem(); // FontSystem.cpp: cria o contexto fontstash
extern void        platformExitFontSystem(); // FontSystem.cpp

namespace {

constexpr uint32_t kDataBufferCount = 2;
constexpr wchar_t  kWindowClass[] = L"TheForgeWindowManagerClass";

// Estado de janela/GPU em escopo de arquivo, como no WindowsBase do proprio
// The-Forge: UMA janela por processo (o WndProc e estatico e o fontstash ja
// depende do global gWindow — nao ha o que instanciar duas vezes).
HWND       gHwnd = NULL;
WindowDesc gWindowDesc = {}; // alvo do global gWindow (cola do fontstash)

Renderer*  pRenderer = NULL;
Queue*     pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};
SwapChain* pSwapChain = NULL;
Semaphore* pImageAcquiredSemaphore = NULL;
uint32_t   gFontID = 0;

// quadro em andamento (aberto no update(), fechado no present())
Cmd*              gFrameCmd = NULL;
GpuCmdRingElement gFrameElem = {};
uint32_t          gFrameImageIndex = 0;
uint32_t          gFrameIndex = 0; // trecho do VB do batcher (frames in flight)

// resize pendente (escrito pelo WndProc, consumido no update())
bool    gResizePending = false;
int32_t gResizeWidth = 0;
int32_t gResizeHeight = 0;

// cor de clear do swapchain (vinda da desc; lida pelo addGameSwapChain)
float gClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

// Fila de edges: um evento por aperto fisico (Space NAO entra aqui — o
// espaco chega como Key::Char via WM_CHAR, mantendo a semantica da fila).
Key vkToKey(const WPARAM vk)
{
    switch (vk)
    {
    case VK_UP:
        return Key::Up;
    case VK_DOWN:
        return Key::Down;
    case VK_LEFT:
        return Key::Left;
    case VK_RIGHT:
        return Key::Right;
    case VK_RETURN:
        return Key::Enter;
    case VK_ESCAPE:
        return Key::Escape;
    case VK_BACK:
        return Key::Backspace;
    default:
        return Key::None;
    }
}

// Estado SEGURADO: setas + espaco (movimento continuo de qualquer jogo 2D —
// cada jogo compoe seu esquema via forgeui::isHeld/heldAxis).
Key vkToHeldKey(const WPARAM vk)
{
    switch (vk)
    {
    case VK_UP:
        return Key::Up;
    case VK_DOWN:
        return Key::Down;
    case VK_LEFT:
        return Key::Left;
    case VK_RIGHT:
        return Key::Right;
    case VK_SPACE:
        return Key::Space;
    default:
        return Key::None;
    }
}

LRESULT CALLBACK wmWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    {
        const Key heldKey = vkToHeldKey(wParam);
        if (heldKey != Key::None)
        {
            forgeui::pushHeldKey(heldKey, true);
        }
        // fila de edges: ignora o auto-repeat do teclado (bit 30 = tecla ja
        // estava pressionada) — um evento por aperto fisico
        if ((lParam & (1 << 30)) == 0)
        {
            const Key key = vkToKey(wParam);
            if (key != Key::None)
            {
                forgeui::pushKey({ key, '\0' });
            }
        }
        return 0;
    }
    case WM_KEYUP:
    {
        const Key heldKey = vkToHeldKey(wParam);
        if (heldKey != Key::None)
        {
            forgeui::pushHeldKey(heldKey, false);
        }
        return 0;
    }
    case WM_KILLFOCUS:
        // alt-tab com tecla presa: solta tudo (o WM_KEYUP nunca chegara)
        forgeui::clearHeldKeys();
        return 0;
    case WM_CHAR:
        // caracteres imprimiveis (o WM_CHAR ja aplica shift/layout do teclado)
        if (wParam >= 32 && wParam < 127)
        {
            forgeui::pushKey({ Key::Char, (char)wParam });
        }
        return 0;
    case WM_SIZE:
        // Minimizar chega como 0x0 — nunca redimensionar o swapchain para 0.
        if (wParam != SIZE_MINIMIZED && LOWORD(lParam) != 0 && HIWORD(lParam) != 0)
        {
            gResizeWidth = (int32_t)LOWORD(lParam);
            gResizeHeight = (int32_t)HIWORD(lParam);
            gResizePending = true;
        }
        return 0;
    case WM_CLOSE:
        // Fechar e decisao do JOGO (cena/router), nao da janela: o X entra na
        // fila como ESC e a cena roteia para o estado de saida. A janela so e
        // destruida no cleanup(), depois do loop da cengine terminar.
        forgeui::pushKey({ Key::Escape, '\0' });
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void pumpMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void addGameSwapChain(const int32_t width, const int32_t height)
{
    WindowHandle handle = {};
    handle.type = WINDOW_HANDLE_TYPE_WIN32;
    handle.window = gHwnd;

    SwapChainDesc swapChainDesc = {};
    swapChainDesc.mWindowHandle = handle;
    swapChainDesc.mPresentQueueCount = 1;
    swapChainDesc.ppPresentQueues = &pGraphicsQueue;
    swapChainDesc.mWidth = (uint32_t)width;
    swapChainDesc.mHeight = (uint32_t)height;
    swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &handle);
    swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, COLOR_SPACE_SDR_SRGB);
    swapChainDesc.mColorSpace = COLOR_SPACE_SDR_SRGB;
    swapChainDesc.mColorClearValue = { { gClearColor[0], gClearColor[1], gClearColor[2], gClearColor[3] } };
    swapChainDesc.mEnableVsync = true; // aprendizado dos jogos: OFF e desperdicio de iGPU
    addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);
}

void loadGameFontSystem(const int32_t width, const int32_t height, const ReloadType loadType)
{
    FontSystemLoadDesc fontLoad = {};
    fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
    fontLoad.mWidth = (uint32_t)width;
    fontLoad.mHeight = (uint32_t)height;
    fontLoad.mLoadType = loadType;
    loadFontSystem(&fontLoad);
}

} // namespace

TheForgeWindowManager::TheForgeWindowManager(const TheForgeWindowDesc& desc):
    m_desc(desc), m_width(desc.width), m_height(desc.height)
{
    // Frames in flight sao do CASCO, nao do jogo: o desc do consumidor nao tem
    // como saber quantos, entao o casco preenche.
    m_desc.sprites.frameCount = kDataBufferCount;
    m_desc.lines.frameCount = kDataBufferCount;
    gClearColor[0] = m_desc.clearColor[0];
    gClearColor[1] = m_desc.clearColor[1];
    gClearColor[2] = m_desc.clearColor[2];
    gClearColor[3] = m_desc.clearColor[3];
}

void TheForgeWindowManager::init()
{
    // IWindowManager::init() nao tem canal de erro; numa falha de plataforma
    // (sem GPU, sem janela) o processo loga e encerra — escopo dos jogos de
    // estudo.
    const HINSTANCE instance = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wmWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc))
    {
        LOGF(eERROR, "[wm] RegisterClassEx falhou");
        exit(EXIT_FAILURE);
    }

    const DWORD style = WS_OVERLAPPEDWINDOW; // redimensionavel
    RECT        rect = { 0, 0, m_width, m_height };
    AdjustWindowRect(&rect, style, FALSE);

    wchar_t title[128] = {};
    MultiByteToWideChar(CP_UTF8, 0, m_desc.appName, -1, title, 127);
    gHwnd = CreateWindowExW(0, kWindowClass, title, style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                            rect.bottom - rect.top, NULL, NULL, instance, NULL);
    if (!gHwnd)
    {
        LOGF(eERROR, "[wm] CreateWindowEx falhou");
        exit(EXIT_FAILURE);
    }

    // Cola do fontstash: monitores enumerados + gWindow apontando para o
    // nosso HWND ANTES do platformInitFontSystem (que le o DPI dali).
    if (!initWindowSystem())
    {
        LOGF(eERROR, "[wm] initWindowSystem falhou");
        exit(EXIT_FAILURE);
    }
    gWindowDesc.handle.type = WINDOW_HANDLE_TYPE_WIN32;
    gWindowDesc.handle.window = gHwnd;
    gWindow = &gWindowDesc;

    if (!platformInitFontSystem())
    {
        LOGF(eERROR, "[wm] platformInitFontSystem falhou");
        exit(EXIT_FAILURE);
    }

    if (!initGraphics())
    {
        exit(EXIT_FAILURE);
    }

    LOGF(eINFO, "[wm] The-Forge como biblioteca atras do IWindowManager (platform-theforge-common)");
    ShowWindow(gHwnd, SW_SHOW);
}

bool TheForgeWindowManager::initGraphics()
{
    RendererDesc settings;
    memset(&settings, 0, sizeof(settings));
    initGPUConfiguration(settings.pExtendedSettings);
    initRenderer(m_desc.appName, &settings, &pRenderer);
    if (!pRenderer)
    {
        LOGF(eERROR, "[wm] GPU sem suporte: %s", getUnsupportedGPUMsg());
        return false;
    }
    setupGPUConfigurationPlatformParameters(pRenderer, settings.pExtendedSettings);

    QueueDesc queueDesc = {};
    queueDesc.mType = QUEUE_TYPE_GRAPHICS;
    initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

    GpuCmdRingDesc cmdRingDesc = {};
    cmdRingDesc.pQueue = pGraphicsQueue;
    cmdRingDesc.mPoolCount = kDataBufferCount;
    cmdRingDesc.mCmdPerPoolCount = 1;
    cmdRingDesc.mAddSyncPrimitives = true;
    initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

    initSemaphore(pRenderer, &pImageAcquiredSemaphore);

    initResourceLoaderInterface(pRenderer);

    // rootsigs por-app compilados pelo passo FSL (mesma receita de sempre)
    RootSignatureDesc rootDesc = {};
    INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
    initRootSignature(pRenderer, &rootDesc);

    // batchers: sprites (atlas + sampler + VB) e linhas (so VB) — ambos no-op
    // quando desligados na desc
    forgesprite::init(pRenderer, m_desc.sprites);
    forgeline::init(pRenderer, m_desc.lines);

    // fontes: o contexto fontstash ja existe (platformInitFontSystem no
    // init()); aqui entram a fonte e os recursos de GPU do sistema
    FontDesc font = {};
    font.pFontPath = m_desc.fontPath;
    fntDefineFonts(&font, 1, &gFontID);

    FontSystemDesc fontRenderDesc = {};
    fontRenderDesc.pRenderer = pRenderer;
    if (!initFontSystem(&fontRenderDesc))
    {
        LOGF(eERROR, "[wm] initFontSystem falhou");
        return false;
    }

    addGameSwapChain(m_width, m_height);
    if (!pSwapChain)
    {
        return false;
    }

    loadGameFontSystem(m_width, m_height, RELOAD_TYPE_ALL);

    // shader + descriptor set + pipeline dos batchers (ALL = tudo)
    ReloadDesc reloadAll = { RELOAD_TYPE_ALL };
    forgesprite::load(&reloadAll, pSwapChain->ppRenderTargets[0]->mFormat, pSwapChain->ppRenderTargets[0]->mSampleCount,
                      pSwapChain->ppRenderTargets[0]->mSampleQuality);
    forgeline::load(&reloadAll, pSwapChain->ppRenderTargets[0]->mFormat, pSwapChain->ppRenderTargets[0]->mSampleCount,
                    pSwapChain->ppRenderTargets[0]->mSampleQuality);

    waitForAllResourceLoads();
    return true;
}

void TheForgeWindowManager::applyPendingResize()
{
    if (!gResizePending)
    {
        return;
    }
    gResizePending = false;
    if (gResizeWidth == m_width && gResizeHeight == m_height)
    {
        return;
    }
    m_width = gResizeWidth;
    m_height = gResizeHeight;

    // Recria o swapchain no novo tamanho e recarrega a parte dependente de
    // render target — mesmo protocolo do RELOAD_TYPE_RESIZE dos apps IApp.
    // O batcher nao depende do tamanho (projecao px->NDC e por quadro) nem
    // do formato (inalterado), entao fica intocado.
    waitQueueIdle(pGraphicsQueue);
    unloadFontSystem(RELOAD_TYPE_RESIZE);
    removeSwapChain(pRenderer, pSwapChain);
    pSwapChain = NULL;

    addGameSwapChain(m_width, m_height);
    if (!pSwapChain)
    {
        LOGF(eERROR, "[wm] recriar swapchain %dx%d falhou", m_width, m_height);
        exit(EXIT_FAILURE);
    }
    loadGameFontSystem(m_width, m_height, RELOAD_TYPE_RESIZE);
    waitForAllResourceLoads();

    LOGF(eINFO, "[wm] swapchain recriado: %dx%d", m_width, m_height);
}

void TheForgeWindowManager::update()
{
    // Inicio do quadro (contrato do IWindowManager >= 0.5.0): eventos de SO
    // (o WndProc alimenta a fila E o estado segurado do forgeui), resize
    // pendente e preparo do desenho — o command buffer fica ABERTO para as
    // fases do jogo desenharem via forgeui/forgesprite.
    pumpMessages();
    applyPendingResize();

    acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &gFrameImageIndex);

    RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[gFrameImageIndex];
    gFrameElem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

    FenceStatus fenceStatus;
    getFenceStatus(pRenderer, gFrameElem.pFence, &fenceStatus);
    if (fenceStatus == FENCE_STATUS_INCOMPLETE)
        waitForFences(pRenderer, 1, &gFrameElem.pFence);

    resetCmdPool(pRenderer, gFrameElem.pCmdPool);

    gFrameCmd = gFrameElem.pCmds[0];
    beginCmd(gFrameCmd);

    RenderTargetBarrier barrier = { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(gFrameCmd, 0, NULL, 0, NULL, 1, &barrier);

    BindRenderTargetsDesc bindRenderTargets = {};
    bindRenderTargets.mRenderTargetCount = 1;
    bindRenderTargets.mRenderTargets[0] = { pRenderTarget, LOAD_ACTION_CLEAR };
    bindRenderTargets.mDepthStencil = { NULL, LOAD_ACTION_DONTCARE };
    cmdBindRenderTargets(gFrameCmd, &bindRenderTargets);
    cmdSetViewport(gFrameCmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
    cmdSetScissor(gFrameCmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

    forgesprite::begin(gFrameCmd, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, gFrameIndex);
    forgeline::begin(gFrameCmd, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, gFrameIndex);
    forgeui::beginDraw(gFrameCmd, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, gFontID);
}

void TheForgeWindowManager::present()
{
    // Fim do quadro (task 16 da cengine): a cauda dos lotes (sprites e linhas),
    // fecha o command buffer que as fases desenharam, submete e apresenta. Roda
    // inclusive no ultimo quadro.
    forgesprite::flush();
    forgeline::flush();

    RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[gFrameImageIndex];

    cmdBindRenderTargets(gFrameCmd, NULL);

    RenderTargetBarrier barrier = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
    cmdResourceBarrier(gFrameCmd, 0, NULL, 0, NULL, 1, &barrier);

    endCmd(gFrameCmd);

    FlushResourceUpdateDesc flushUpdateDesc = {};
    flushUpdateDesc.mNodeIndex = 0;
    flushResourceUpdates(&flushUpdateDesc);
    Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore };

    QueueSubmitDesc submitDesc = {};
    submitDesc.mCmdCount = 1;
    submitDesc.mSignalSemaphoreCount = 1;
    submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
    submitDesc.ppCmds = &gFrameCmd;
    submitDesc.ppSignalSemaphores = &gFrameElem.pSemaphore;
    submitDesc.ppWaitSemaphores = waitSemaphores;
    submitDesc.pSignalFence = gFrameElem.pFence;
    queueSubmit(pGraphicsQueue, &submitDesc);

    QueuePresentDesc presentDesc = {};
    presentDesc.mIndex = (uint8_t)gFrameImageIndex;
    presentDesc.mWaitSemaphoreCount = 1;
    presentDesc.pSwapChain = pSwapChain;
    presentDesc.ppWaitSemaphores = &gFrameElem.pSemaphore;
    presentDesc.mSubmitDone = true;
    queuePresent(pGraphicsQueue, &presentDesc);

    gFrameCmd = NULL;
    gFrameIndex = (gFrameIndex + 1) % kDataBufferCount;
}

void TheForgeWindowManager::cleanup()
{
    waitQueueIdle(pGraphicsQueue);

    ReloadDesc reloadAll = { RELOAD_TYPE_ALL };
    forgesprite::unload(&reloadAll);
    forgeline::unload(&reloadAll);

    unloadFontSystem(RELOAD_TYPE_ALL);
    exitFontSystem();

    forgesprite::exit();
    forgeline::exit();

    removeSwapChain(pRenderer, pSwapChain);
    exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
    exitSemaphore(pRenderer, pImageAcquiredSemaphore);

    exitRootSignature(pRenderer);
    exitResourceLoaderInterface(pRenderer);

    exitQueue(pRenderer, pGraphicsQueue);
    exitRenderer(pRenderer);
    exitGPUConfiguration();
    pRenderer = NULL;

    platformExitFontSystem();
    exitWindowSystem();

    DestroyWindow(gHwnd);
    gHwnd = NULL;
    UnregisterClassW(kWindowClass, GetModuleHandleW(NULL));
}
