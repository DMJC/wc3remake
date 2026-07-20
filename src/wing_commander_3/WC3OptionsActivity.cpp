#include "WC3OptionsActivity.h"
#include "WC3Globals.h"
#include "WC3Font.h"
#include "../commons/GraphicsSettings.h"
#include "../engine/EventManager.h"
#include "../engine/Config.hpp"
#include "../realspace/AssetManager.h"
#include "../realspace/RSWC3Shape.h"
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

WC3OptionsActivity::WC3OptionsActivity(WC3Options* options) : m_options(options) {}

void WC3OptionsActivity::init() {
    loadAssets();
}

// Decodes DATA\SCREENS\ESFOPTS* — see WC3OptionsActivity.h's class comment
// for the format (byte-verified against the real files, not guessed):
// ESFOPTSD.DAT is a leading uint32 record count followed by that many
// [buttonIndex, groupId, x, y] uint32 quadruples; ESFOPTSI.VFX/ESFOPTSS.VFX
// are both the same "1.11" WC3 shape-pak format already used for cockpit
// instrument art (RSWC3Shape.h).
void WC3OptionsActivity::loadAssets() {
    AssetManager& assets = AssetManager::getInstance();

    TreEntry* datEntry = assets.GetEntryByName("..\\..\\DATA\\SCREENS\\ESFOPTSD.DAT");
    if (datEntry != nullptr && datEntry->size >= 4) {
        uint32_t count;
        memcpy(&count, datEntry->data, 4);
        size_t needed = 4 + (size_t)count * 16;
        if (needed <= datEntry->size) {
            m_layout.reserve(count);
            const uint8_t* p = datEntry->data + 4;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t a, b, c, d;
                memcpy(&a, p + 0, 4);
                memcpy(&b, p + 4, 4);
                memcpy(&c, p + 8, 4);
                memcpy(&d, p + 12, 4);
                m_layout.push_back({(int)a, (int)b, (int)c, (int)d});
                p += 16;
            }
        }
    }

    TreEntry* iEntry = assets.GetEntryByName("..\\..\\DATA\\SCREENS\\ESFOPTSI.VFX");
    if (iEntry != nullptr) {
        m_buttonSprites = RSWC3DecodeShapeEntry(iEntry->data, iEntry->size);
    }
    TreEntry* sEntry = assets.GetEntryByName("..\\..\\DATA\\SCREENS\\ESFOPTSS.VFX");
    if (sEntry != nullptr) {
        m_background = RSWC3DecodeShapeEntry(sEntry->data, sEntry->size);
    }

    RSPalette palette;
    TreEntry* palEntry = assets.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (palEntry != nullptr) {
        palette.initFromFileRam(palEntry->data, palEntry->size);
        m_palette = *palette.GetColorPalette();
    }

    m_fb = new FrameBuffer(640, 480);

    m_assetsLoaded = (m_layout.size() >= 36 && m_background != nullptr &&
                       m_background->GetNumImages() > 0 && m_buttonSprites != nullptr &&
                       m_buttonSprites->GetNumImages() >= 5);
    if (!m_assetsLoaded) {
        printf("WC3Options: real screen assets unavailable (layout=%zu bg=%d btn=%d) — falling back to blank panel\n",
               m_layout.size(), m_background ? (int)m_background->GetNumImages() : -1,
               m_buttonSprites ? (int)m_buttonSprites->GetNumImages() : -1);
    }
}

void WC3OptionsActivity::layoutAndDraw() {
    m_hitRects.clear();
    WC3Font* font = WC3Globals::getInstance().getFont("GFSM");

    // Column layout reuses ESFOPTSD.DAT's real x positions (89/240/431) and
    // 15px row spacing, but this engine's actual settings (WC3Options.h)
    // are placed into those rows in this activity's own order — see the
    // class comment in the header for why the authentic per-row mapping
    // isn't recoverable from this data. Row indices below are simply
    // sequential slots within each of the layout's 3 columns.
    std::vector<Binding> bindings;
    auto checkbox = [&](size_t row, const std::string& label, bool checked, std::function<void()> onClick) {
        bindings.push_back({row, label, checked, std::move(onClick), false});
    };
    auto slider = [&](size_t row, const std::string& label, int& value, int step) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %3d", label.c_str(), value);
        int* valuePtr = &value;
        Binding b;
        b.rowIndex = row;
        b.label = std::string("<  ") + buf + "  >";
        b.checked = true;
        b.isSlider = true;
        b.onClick = [valuePtr, step]() { *valuePtr = (std::max)(0, *valuePtr - step); };
        b.onClickAlt = [valuePtr, step]() { *valuePtr = (std::min)(128, *valuePtr + step); };
        bindings.push_back(std::move(b));
    };

    // Column 1 (x=89): sound.
    checkbox(0, "music", m_options->musicEnabled, [this]() { m_options->musicEnabled = !m_options->musicEnabled; });
    checkbox(1, "sound fx", m_options->soundEnabled, [this]() { m_options->soundEnabled = !m_options->soundEnabled; });
    slider(2, "music volume", m_options->musicVolume, 8);
    slider(3, "sound volume", m_options->soundVolume, 8);

    // Column 2 (x=240): graphics mode (true radio pair — exactly one of
    // VGA/SVGA is ever checked, both driven off the single global g_ifVGA
    // flag) + gamma. Clicking either box writes the choice straight to its
    // own dedicated wc3_options.cfg (not config.ini) so it's the one
    // authoritative source main.cpp reads at next startup — see
    // WC3Options.h's loadGraphicsSettings/saveGraphicsSettings.
    checkbox(10, "vga", g_ifVGA,
              [this]() { g_ifVGA = true; VGA.SetCanvasResolution(320, 200); saveGraphicsSettings(); });
    checkbox(11, "svga", !g_ifVGA,
              [this]() { g_ifVGA = false; VGA.SetCanvasResolution(640, 480); saveGraphicsSettings(); });
    checkbox(14, "gamma: off", m_options->gamma == WC3Options::Gamma::Off,
              [this]() { m_options->gamma = WC3Options::Gamma::Off; });
    checkbox(15, "gamma: low", m_options->gamma == WC3Options::Gamma::Low,
              [this]() { m_options->gamma = WC3Options::Gamma::Low; });
    checkbox(16, "gamma: high", m_options->gamma == WC3Options::Gamma::High,
              [this]() { m_options->gamma = WC3Options::Gamma::High; });

    // Column 3 (x=431): language + misc.
    checkbox(21, "sub-titles", m_options->subtitlesOn, [this]() { m_options->subtitlesOn = !m_options->subtitlesOn; });
    checkbox(22, "english", m_options->language == WC3Options::Language::English,
              [this]() { m_options->language = WC3Options::Language::English; });
    checkbox(23, "french", m_options->language == WC3Options::Language::French,
              [this]() { m_options->language = WC3Options::Language::French; });
    checkbox(24, "german", m_options->language == WC3Options::Language::German,
              [this]() { m_options->language = WC3Options::Language::German; });
    checkbox(25, "transitions", m_options->transitionsOn, [this]() { m_options->transitionsOn = !m_options->transitionsOn; });
    checkbox(26, "stars", m_options->starsOn, [this]() { m_options->starsOn = !m_options->starsOn; });
    checkbox(27, "descriptions", m_options->descriptionsOn, [this]() { m_options->descriptionsOn = !m_options->descriptionsOn; });

    // --- Pass 1: icons (palette-indexed FrameBuffer) ---
    m_fb->fillWithColor(0);
    if (m_assetsLoaded) {
        m_fb->drawShape(m_background->GetShape(0));
        for (auto& b : bindings) {
            if (b.rowIndex >= m_layout.size()) continue;
            if (b.isSlider || b.checked) {
                LayoutRow& row = m_layout[b.rowIndex];
                RLEShape* icon = m_buttonSprites->GetShape(0);
                Point2D pos = {row.x, row.y};
                icon->SetPosition(&pos);
                m_fb->drawShape(icon);
            }
        }
        // Bottom action buttons — 3 of the 4 available button-label
        // sprites are used (see header comment); the 4th layout slot is
        // left undrawn rather than inventing a fake action for it.
        struct BottomButton { size_t row; RLEShape* sprite; std::function<void()> onClick; };
        std::vector<BottomButton> bottomButtons;
        if (m_layout.size() >= 36) {
            bottomButtons.push_back({32, m_buttonSprites->GetShape(1), []() {
                printf("WC3Options: joystick calibration is not implemented yet\n");
            }});
            bottomButtons.push_back({33, m_buttonSprites->GetShape(2), [this]() {
                *m_options = WC3Options{};
                saveOptions(*m_options, Config::getInstance());
            }});
            bottomButtons.push_back({35, m_buttonSprites->GetShape(4), [this]() { this->stop(); }});
            for (auto& bb : bottomButtons) {
                LayoutRow& row = m_layout[bb.row];
                Point2D pos = {row.x, row.y};
                bb.sprite->SetPosition(&pos);
                m_fb->drawShape(bb.sprite);
                m_hitRects.push_back({row.x, row.y, bb.sprite->GetWidth(), bb.sprite->GetHeight(), bb.onClick});
            }
        }
    }

    // --- Convert to RGBA (see WC3GameFlow::displaySceneFramebuffer for the
    // same palette-index-to-RGBA pattern) ---
    std::vector<uint32_t> rgba(640 * 480);
    if (m_assetsLoaded) {
        for (int i = 0; i < 640 * 480; i++) {
            const Texel* c = m_palette.GetRGBColor(m_fb->framebuffer[i]);
            rgba[i] = c->r | (c->g << 8) | (c->b << 16) | (0xFFu << 24);
        }
    } else {
        std::fill(rgba.begin(), rgba.end(), 0xFF201008u); // solid dark background fallback
    }

    // --- Pass 2: labels (RGBA) + hit-rect registration for checkboxes/sliders ---
    if (font && font->isLoaded()) {
        font->drawTextRGBA(rgba.data(), 640, 480, "OPTIONS", 40, 20);
        font->drawTextRGBA(rgba.data(), 640, 480, "sound", 40, 90);
        font->drawTextRGBA(rgba.data(), 640, 480, "graphics modes", 191, 90);
        font->drawTextRGBA(rgba.data(), 640, 480, "language", 382, 90);

        int iconW = m_assetsLoaded ? m_buttonSprites->GetShape(0)->GetWidth() : 0;
        for (auto& b : bindings) {
            if (b.rowIndex >= m_layout.size()) continue;
            LayoutRow& row = m_layout[b.rowIndex];
            int labelX = row.x + iconW + 4;
            font->drawTextRGBA(rgba.data(), 640, 480, b.label, labelX, row.y);
            int w = font->measureText(b.label);
            int h = font->getHeight();
            if (b.isSlider) {
                int arrowW = font->measureText("<  ");
                m_hitRects.push_back({labelX, row.y, arrowW, h, b.onClick});
                int rightX = labelX + w - font->measureText("  >");
                m_hitRects.push_back({rightX, row.y, font->measureText("  >"), h, b.onClickAlt});
            } else {
                m_hitRects.push_back({row.x, row.y, iconW + 4 + w, h, b.onClick});
            }
        }
    }

    // --- Mouse hit-testing (same coordinate space as the old implementation) ---
    EventManager& events = EventManager::getInstance();
    if (events.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
        int mx, my;
        events.getMousePosition(mx, my);
        int vx, vy;
        Screen->windowToLogical(mx, my, 640, 480, vx, vy);
        for (auto& r : m_hitRects) {
            if (vx >= r.x && vx < r.x + r.w && vy >= r.y && vy < r.y + r.h) {
                r.onClick();
                Mixer.setVolume(m_options->musicVolume, -1);
                Mixer.setVolume(m_options->soundVolume, 0);
                saveOptions(*m_options, Config::getInstance());
                break;
            }
        }
    }

    // Flip vertically for glDrawPixels (GL origin is bottom-left).
    for (int y = 0; y < 480 / 2; y++)
        for (int x = 0; x < 640; x++)
            std::swap(rgba[y * 640 + x], rgba[(480 - 1 - y) * 640 + x]);

    glViewport(Screen->viewport_x, Screen->viewport_y, Screen->viewport_w, Screen->viewport_h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, Screen->viewport_w, 0, Screen->viewport_h, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);   glDisable(GL_FOG); glDisable(GL_BLEND);
    glRasterPos2i(0, 0);
    glPixelZoom((float)Screen->viewport_w / 640, (float)Screen->viewport_h / 480);
    glDrawPixels(640, 480, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

void WC3OptionsActivity::runFrame() {
    checkKeyboard(); // ESC (via IActivity::checkKeyboard) also closes this overlay
    layoutAndDraw();
}
