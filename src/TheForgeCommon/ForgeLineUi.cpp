#include "ForgeLineUi.h"

#include <cstring>
#include <vector>

#include "Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common_3/Utilities/Interfaces/ILog.h"

#include "Common_3/Graphics/FSL/defaults.h"

namespace {

// Vertice do batcher: posicao ja em NDC e cor RGBA8 (R8G8B8A8_UNORM no vertex
// layout; o input assembler entrega float4 ao shader). 12 bytes/vertice, 2
// vertices por segmento — sem textura, sem UV: e a economia de desenhar a linha.
struct LineVertex
{
    float2   position;
    uint32_t color; // ABGR na memoria = R,G,B,A em bytes little-endian
};

constexpr uint32_t kVertsPerLine = 2;

bool     gEnabled = false;
uint32_t gMaxLines = 0;
uint32_t gMaxVerts = 0;

Renderer* gRenderer = NULL;
Shader*   gShader = NULL;
Pipeline* gPipeline = NULL;
Buffer*   gVertexBuffer = NULL; // gMaxVerts * frameCount, CPU_TO_GPU

uint32_t gFrameCount = 0;

// estado do quadro corrente
Cmd*     gCmd = NULL;
float    gWidth = 0.0f;
float    gHeight = 0.0f;
uint32_t gBaseVertex = 0;   // inicio do trecho deste frame no vertex buffer
uint32_t gFlushedVerts = 0; // ja desenhados neste quadro (cursor)
uint32_t gPendingVerts = 0; // acumulados aguardando flush
bool     gOverflowLogged = false;

forgeline::Stats gCurrent = {};
forgeline::Stats gLastFrame = {};

// staging na CPU: drawLine escreve aqui; flush copia para o trecho do VB
std::vector<LineVertex> gStaging;

// pixels (origem no topo-esquerdo) -> NDC
float2 toNdc(const forgeline::Point p)
{
    return { p.x / gWidth * 2.0f - 1.0f, 1.0f - p.y / gHeight * 2.0f };
}

} // namespace

namespace forgeline {

void init(Renderer* renderer, const LineBatcherDesc& desc)
{
    gEnabled = desc.enabled;
    if (!gEnabled)
    {
        return;
    }

    gRenderer = renderer;
    gMaxLines = desc.maxLines;
    gMaxVerts = gMaxLines * kVertsPerLine;
    gFrameCount = desc.frameCount;
    gStaging.resize(gMaxVerts);

    // Um unico buffer com um trecho por frame in flight, persistentemente
    // mapeado: escrever no trecho do frame N e seguro porque a fence do cmd
    // ring garante que a GPU ja consumiu o trecho ha frameCount quadros.
    BufferLoadDesc vbDesc = {};
    vbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
    vbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    vbDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    vbDesc.mDesc.mSize = (uint64_t)gMaxVerts * gFrameCount * sizeof(LineVertex);
    vbDesc.mDesc.pName = "Line Batcher Vertex Buffer";
    vbDesc.ppBuffer = &gVertexBuffer;
    addResource(&vbDesc, NULL);
}

void exit()
{
    if (!gEnabled)
    {
        return;
    }

    removeResource(gVertexBuffer);
    gRenderer = NULL;
    gEnabled = false;
}

void load(const ReloadDesc* reloadDesc, const TinyImageFormat colorFormat, const SampleCount sampleCount,
          const uint32_t sampleQuality)
{
    if (!gEnabled)
    {
        return;
    }

    if (reloadDesc->mType & RELOAD_TYPE_SHADER)
    {
        ShaderLoadDesc shaderDesc = {};
        shaderDesc.mVert.pFileName = "line.vert";
        shaderDesc.mFrag.pFileName = "line.frag";
        addShader(gRenderer, &shaderDesc, &gShader);
    }

    if (reloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
    {
        VertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mBindings[0].mStride = sizeof(LineVertex);
        vertexLayout.mAttribCount = 2;
        vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[1].mSemantic = SEMANTIC_TEXCOORD0;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = sizeof(float2);

        RasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

        BlendStateDesc blendStateDesc = {};
        blendStateDesc.mSrcFactors[0] = BC_SRC_ALPHA;
        blendStateDesc.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
        blendStateDesc.mSrcAlphaFactors[0] = BC_SRC_ALPHA;
        blendStateDesc.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
        blendStateDesc.mColorWriteMasks[0] = COLOR_MASK_ALL;
        blendStateDesc.mRenderTargetMask = BLEND_STATE_TARGET_ALL;
        blendStateDesc.mIndependentBlend = false;

        PipelineDesc desc = {};
        desc.mType = PIPELINE_TYPE_GRAPHICS;
        // Sem SRT: o shader nao le recurso nenhum (a cor vem no vertice).
        PIPELINE_LAYOUT_DESC(desc, NULL, NULL, NULL, NULL);
        GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_LINE_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = NULL; // 2D: camadas = ordem de draw
        pipelineSettings.pBlendState = &blendStateDesc;
        pipelineSettings.pColorFormats = (TinyImageFormat*)&colorFormat;
        pipelineSettings.mSampleCount = sampleCount;
        pipelineSettings.mSampleQuality = sampleQuality;
        pipelineSettings.pShaderProgram = gShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        addPipeline(gRenderer, &desc, &gPipeline);
    }
}

void unload(const ReloadDesc* reloadDesc)
{
    if (!gEnabled)
    {
        return;
    }

    if (reloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
    {
        removePipeline(gRenderer, gPipeline);
    }

    if (reloadDesc->mType & RELOAD_TYPE_SHADER)
    {
        removeShader(gRenderer, gShader);
    }
}

void begin(Cmd* cmd, const float width, const float height, const uint32_t frameIndex)
{
    if (!gEnabled)
    {
        return;
    }

    gCmd = cmd;
    gWidth = width;
    gHeight = height;
    gBaseVertex = frameIndex * gMaxVerts;
    gFlushedVerts = 0;
    gPendingVerts = 0;
    gOverflowLogged = false;

    gLastFrame = gCurrent;
    gCurrent = {};
}

void drawLine(const Point from, const Point to, const uint32_t colorAbgr)
{
    if (!gEnabled)
    {
        return;
    }

    if (gFlushedVerts + gPendingVerts + kVertsPerLine > gMaxVerts)
    {
        if (!gOverflowLogged)
        {
            LOGF(eWARNING, "[forgeline] lote cheio (%u linhas) - linha dropada", gMaxLines);
            gOverflowLogged = true;
        }
        return;
    }

    LineVertex* v = &gStaging[gFlushedVerts + gPendingVerts];
    v[0] = { toNdc(from), colorAbgr };
    v[1] = { toNdc(to), colorAbgr };

    gPendingVerts += kVertsPerLine;
    ++gCurrent.lines;
}

void drawPolyline(const Point* points, const uint32_t count, const bool closed, const uint32_t colorAbgr)
{
    if (!gEnabled || points == NULL || count < 2)
    {
        return;
    }

    for (uint32_t i = 0; i + 1 < count; ++i)
    {
        drawLine(points[i], points[i + 1], colorAbgr);
    }

    if (closed)
    {
        drawLine(points[count - 1], points[0], colorAbgr);
    }
}

void flush()
{
    if (!gEnabled || gPendingVerts == 0 || gCmd == NULL)
    {
        return;
    }

    // copia so o lote pendente para o trecho deste frame, a partir do cursor
    BufferUpdateDesc update = { gVertexBuffer, (uint64_t)(gBaseVertex + gFlushedVerts) * sizeof(LineVertex),
                                (uint64_t)gPendingVerts * sizeof(LineVertex) };
    beginUpdateResource(&update);
    memcpy(update.pMappedData, &gStaging[gFlushedVerts], gPendingVerts * sizeof(LineVertex));
    endUpdateResource(&update);

    const uint32_t stride = sizeof(LineVertex);
    cmdBindPipeline(gCmd, gPipeline);
    cmdBindVertexBuffer(gCmd, 1, &gVertexBuffer, &stride, NULL);
    cmdDraw(gCmd, gPendingVerts, gBaseVertex + gFlushedVerts);

    gFlushedVerts += gPendingVerts;
    gPendingVerts = 0;
    ++gCurrent.drawCalls;
}

Stats lastFrameStats() { return gLastFrame; }

} // namespace forgeline
