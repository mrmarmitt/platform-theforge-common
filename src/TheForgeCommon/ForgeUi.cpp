#include "ForgeUi.h"

#include <cstddef>
#include <vector>

#include "ForgeLineUi.h"
#include "ForgeSpriteUi.h"

namespace {

// estado segurado por tecla, publicado por pushHeldKey (WndProc do casco)
constexpr size_t kKeyCount = static_cast<size_t>(Key::Other) + 1;
bool gHeld[kKeyCount] = {};

// Fila de eventos: o casco enfileira no update; as cenas consomem 1 por input().
constexpr size_t      kQueueMax = 32;
std::vector<KeyEvent> gQueue;

Cmd*     gCmd = NULL;
float    gWidth = 0.0f;
float    gHeight = 0.0f;
uint32_t gFontID = 0;

void push(const Key key, const char character = '\0')
{
    if (gQueue.size() < kQueueMax)
    {
        gQueue.push_back({ key, character });
    }
}

} // namespace

namespace forgeui {

void pushKey(const KeyEvent event) { push(event.key, event.character); }

void pushHeldKey(const Key key, const bool held) { gHeld[static_cast<size_t>(key)] = held; }

void clearHeldKeys()
{
    for (bool& held : gHeld)
    {
        held = false;
    }
}

void beginDraw(Cmd* cmd, const float width, const float height, const uint32_t fontID)
{
    gCmd = cmd;
    gWidth = width;
    gHeight = height;
    gFontID = fontID;
}

KeyEvent readKey()
{
    if (gQueue.empty())
    {
        return {};
    }
    const KeyEvent event = gQueue.front();
    gQueue.erase(gQueue.begin());
    return event;
}

bool isHeld(const Key key) { return gHeld[static_cast<size_t>(key)]; }

float heldAxis(const Key negative, const Key positive)
{
    return (isHeld(positive) ? 1.0f : 0.0f) - (isHeld(negative) ? 1.0f : 0.0f);
}

float screenWidth() { return gWidth; }
float screenHeight() { return gHeight; }

float textWidth(const std::string& text, const float fontSize)
{
    FontDrawDesc desc = {};
    desc.pText = text.c_str();
    desc.mFontID = gFontID;
    desc.mFontSize = fontSize;
    return fntMeasureFontText(desc.pText, &desc).x;
}

void drawText(const std::string& text, const float x, const float y, const float fontSize, const uint32_t colorAbgr)
{
    // Camadas = ordem de chamada atravessando as pontes: o que estiver pendente
    // nos batchers e desenhado AGORA, para este texto ficar por cima.
    forgesprite::flush();
    forgeline::flush();

    FontDrawDesc desc = {};
    desc.pText = text.c_str();
    desc.mFontID = gFontID;
    desc.mFontColor = colorAbgr;
    desc.mFontSize = fontSize;
    cmdDrawTextWithFont(gCmd, float2(x, y), &desc);
}

void drawTextCentered(const std::string& text, const float y, const float fontSize, const uint32_t colorAbgr)
{
    drawText(text, (gWidth - textWidth(text, fontSize)) * 0.5f, y, fontSize, colorAbgr);
}

void drawHints(const std::string& text) { drawTextCentered(text, gHeight - 48.0f, 18.0f, color::kDim); }

} // namespace forgeui
