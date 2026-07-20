#include "WC3QuitConfirmActivity.h"
#include "WC3Globals.h"
#include "WC3Font.h"
#include "../engine/EventManager.h"
#include <SDL2/SDL_opengl.h>

void WC3QuitConfirmActivity::init() {}

void WC3QuitConfirmActivity::runFrame() {
    // Y/N only — deliberately NOT calling the base IActivity::checkKeyboard()
    // here (it would dismiss on ESC/controller-back) and not accepting
    // Enter either, per explicit request: this prompt is a real "are you
    // sure", so it shouldn't have a fast "just hit Enter/Esc" way out.
    Keyboard* kb = Game->getKeyboard();
    if (kb && kb->isKeyJustPressed(SDL_SCANCODE_Y)) {
        Game->terminate("Player confirmed quit from flight (Alt+X)");
    }
    if (kb && kb->isKeyJustPressed(SDL_SCANCODE_N)) {
        this->stop();
        return;
    }
    EventManager& events = EventManager::getInstance();
    if (events.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
        // Keyboard (Y/N) is the primary input for a 2-choice prompt like
        // this; any click just dismisses rather than needing a full
        // button hit-test layout.
        this->stop();
        return;
    }

    const int fbW = 640, fbH = 480;
    std::vector<uint32_t> rgba(fbW * fbH, 0xFF201008u); // solid dark background, same convention as WC3OptionsActivity

    WC3Font* font = WC3Globals::getInstance().getFont("GFSM");
    if (font && font->isLoaded()) {
        const std::string line1 = "QUIT GAME?";
        const std::string line2 = "Y = YES     N = NO";
        int line1W = font->measureText(line1);
        int line2W = font->measureText(line2);
        int textH = font->getHeight();
        int centerY = fbH / 2;
        font->drawTextRGBA(rgba.data(), fbW, fbH, line1, (fbW - line1W) / 2, centerY - textH);
        font->drawTextRGBA(rgba.data(), fbW, fbH, line2, (fbW - line2W) / 2, centerY + 10);
    }

    // Flip vertically for glDrawPixels (GL origin is bottom-left) — same
    // tail as WC3OptionsActivity/WC3GameFlow::displaySceneFramebuffer.
    for (int y = 0; y < fbH / 2; y++)
        for (int x = 0; x < fbW; x++)
            std::swap(rgba[y * fbW + x], rgba[(fbH - 1 - y) * fbW + x]);

    glViewport(Screen->viewport_x, Screen->viewport_y, Screen->viewport_w, Screen->viewport_h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, Screen->viewport_w, 0, Screen->viewport_h, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);   glDisable(GL_FOG); glDisable(GL_BLEND);
    glRasterPos2i(0, 0);
    glPixelZoom((float)Screen->viewport_w / fbW, (float)Screen->viewport_h / fbH);
    glDrawPixels(fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}
