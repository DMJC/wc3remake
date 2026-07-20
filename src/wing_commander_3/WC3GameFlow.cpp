#include "WC3GameFlow.h"
#include "WC3Globals.h"
#include "WC3MVEPlayer.h"
#include "WC3OptionsActivity.h"
#include "WC3LoadoutData.h"
#include "../commons/XtreDecompressor.h"
#include "../realspace/XtreArchive.h"
#include "../engine/EventManager.h"
#include <sys/stat.h>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// WC3 SHP ("1.11") decoder
// ---------------------------------------------------------------------------

static uint32_t wc3_u32(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}
static uint16_t wc3_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1]<<8));
}
// Big-endian: IFF chunk sizes (as opposed to WC3's own little-endian data fields).
static uint32_t wc3_u32be(const uint8_t* p) {
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3];
}

// Lift (room 0005) floor index 0=Flight, 1=Living, 2=Bridge <-> the room's
// own numeric SCEN id and floor-select button gump id — confirmed via room
// 0005's ACTV disassembly (see WC3GameFlow.h's m_liftSelectedFloor comment).
static constexpr int kLiftFloorRoomId[3] = {3, 6, 0};
static constexpr uint32_t kLiftFloorButtonGump[3] = {4, 3, 2};

// The Loadout/Options/Terminal/Kill Board "computer console" screens render
// ~23px higher than authentic DOSBox — confirmed by pixel-comparing
// wc3_011.png (DOSBox) against a neowc3 capture: a static background
// element and a real button, at very different screen depths, were both
// off by exactly 23px, ruling out a scale error. Applied only to these
// screens (via WC3Scene::render()'s yOffset param and by shifting these
// pages' own rect/text/sprite math) — ordinary rooms haven't been
// confirmed to share the same discrepancy, so they're left untouched.
static constexpr int kTerminalYOffset = 23;

// Room 0008 ("Loadout Terminal") gump ids per page — confirmed via a
// headless decode of its own SHAPES.PAK art (see the terminal-screens
// implementation plan). The Options checkbox/slider gumps (27,28,33-46) and
// volume-arrow gumps (29-32), plus the in-room "joystick calibration"/
// "return" buttons (47,48), the alt Refueling-Depot title (26), and the
// hidden credits egg (54) are deliberately left out of every page's set —
// and so always hidden — since Options is rendered by the standalone
// WC3OptionsActivity instead (see openOptionsActivity()), not this room's
// own art.
static constexpr uint32_t kTermTextInputGump = 4294967283u; // id=-13 as uint32_t
static constexpr uint32_t kTermLoginGumps[]    = {3, kTermTextInputGump};
static constexpr uint32_t kTermHubGumps[]      = {2, 5, 6, 7};
// 19/20 and 21/22 are the list's up/down scroll-arrow pairs (top and bottom
// of the box, matching wc3_001.png's stacked ▲▲/▼▼) — confirmed by their own
// x=51 column position, distinct from the Options page's volume/checkbox
// icons at x=67/223/382.
static constexpr uint32_t kTermDutyLogsGumps[] = {14, 15, 16, 17, 18, 19, 20, 21, 22};
static constexpr uint32_t kTermAllPageGumps[]  = {
    2, 3, 4, 5, 6, 7, 14, 15, 16, 17, 18, 19, 20, 21, 22, kTermTextInputGump,
    26, 54,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
    47, 48,
};

// Looks up a gump's own CORD rect within a room's actor list — used to lay
// out engine-drawn text/lists inside a real bounding-box gump (e.g. Login's
// text-input box, Kill Board's table area) instead of hardcoding pixels.
static bool findActorRect(const std::vector<WC3Gump>& actors, uint32_t gumpId,
                           int& x, int& y, int& w, int& h) {
    for (auto& a : actors) {
        if (a.id == gumpId) { x = a.x; y = a.y; w = a.w; h = a.h; return true; }
    }
    return false;
}

// Decode WC3 RLE pixel data into a flat uint8_t frame buffer.
// Encoded pixels are written at (x_blit+col, y_min+row).
// Positions not covered by any run keep their current value (caller pre-fills
// with 0xFF so unencoded positions appear transparent in RLEShape::Expand).
static void wc3_decode_rle(
    const uint8_t* src, size_t src_len,
    size_t y_span, size_t row_width,
    uint8_t* frame, size_t fw, size_t fh,
    size_t x_blit, size_t y_min)
{
    size_t row = 0, col = 0, i = 0;
    while (i < src_len && row < y_span) {
        uint8_t key = src[i++];
        if (key == 0) { row++; col = 0; continue; }
        if (key == 1) { if (i < src_len) col += src[i++]; continue; }
        size_t count = (size_t)(key >> 1);
        bool   lit   = (key & 1) != 0;
        if (count == 0) continue;
        if (lit) {
            for (size_t k = 0; k < count && i < src_len && row < y_span; k++, col++) {
                uint8_t color = src[i++];
                size_t fy = y_min + row, fx = x_blit + col;
                if (fy < fh && fx < fw) frame[fy * fw + fx] = color;
            }
        } else {
            if (i >= src_len) break;
            uint8_t color = src[i++];
            for (size_t k = 0; k < count && row < y_span; k++, col++) {
                size_t fy = y_min + row, fx = x_blit + col;
                if (fy < fh && fx < fw) frame[fy * fw + fx] = color;
            }
        }
    }
}

// ---------------------------------------------------------------------------
WC3GameFlow::WC3GameFlow() {}
WC3GameFlow::~WC3GameFlow() {
    if (m_cursor_owned) delete m_cursor;
    delete scene;
    delete sceneFB;
    if (sceneTexture) glDeleteTextures(1, &sceneTexture);
    for (auto& [id, img] : shapeCache) delete img;
}

void WC3GameFlow::initSceneFramebuffer() {
    sceneFB = new FrameBuffer(640, 480);

    // Prefer cursor shape 0 from globals.iff (30×31 px, authentic WC3 cursor).
    // Fall back to the hardcoded 12×12 arrow if globals weren't loaded.
    WC3Globals& globals = WC3Globals::getInstance();
    RSImageSet* cursors = globals.getCursors();
    if (cursors && cursors->GetNumImages() > 0) {
        m_cursor = cursors->GetShape(0);
        m_cursor_owned = false;
    } else {
        // 12×12 arrow cursor: 0=black outline, 15=white fill, 255=transparent
        static const uint8_t kCursorPixels[12 * 12] = {
              0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
              0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
              0,  15,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255,
              0,  15,  15,   0, 255, 255, 255, 255, 255, 255, 255, 255,
              0,  15,  15,  15,   0, 255, 255, 255, 255, 255, 255, 255,
              0,  15,  15,  15,  15,   0, 255, 255, 255, 255, 255, 255,
              0,  15,  15,   0,   0, 255, 255, 255, 255, 255, 255, 255,
              0,  15,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255,
              0,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
              0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        };
        m_cursor = new RLEShape();
        m_cursor->InitFromPixels(kCursorPixels, 12, 12);
        m_cursor_owned = true;
    }
}

void WC3GameFlow::drawCursor(int vx, int vy) {
    if (!sceneFB) return;

    // Select cursor frame from globals.iff based on hovered zone type:
    //   frame 0 — default (no zone)
    //   frame 6 — actor zone (TYPE_CHARACTER, TYPE_PERSON, TYPE_MOVIE_FG, etc.)
    //   frame 7 — room-change zone (TYPE_NAVPOINT, when it's a real GoRoom)
    //   frame 8 — computer/terminal/button zone (TYPE_BUTTON, or a
    //             TYPE_NAVPOINT whose action isn't a room change — e.g.
    //             "Activate MAIN TERMINAL." shares TYPE_NAVPOINT with real
    //             room-change gumps like "Go to LIFT.", so the gump type
    //             alone can't tell them apart; see
    //             WC3Scene::Zone::isRoomChange)
    RLEShape* shape = nullptr;
    RSImageSet* cursors = WC3Globals::getInstance().getCursors();
    if (cursors) {
        int frame = 0;
        switch (m_hoveredZoneType) {
        case WC3Gump::TYPE_NAVPOINT:  frame = m_hoveredIsRoomChange ? 7 : 8; break;
        case WC3Gump::TYPE_BUTTON:    frame = 8; break;
        case WC3Gump::TYPE_CHARACTER:
        case WC3Gump::TYPE_CHARACTER2:
        case WC3Gump::TYPE_PERSON:
        case WC3Gump::TYPE_MOVIE_FG:
        case WC3Gump::TYPE_MOVIE_BG:
        case WC3Gump::TYPE_MOVIE_BG2: frame = 6; break;
        default:                      frame = 0; break;
        }
        int nCursors = static_cast<int>(cursors->GetNumImages());
        if (frame < nCursors) shape = cursors->GetShape(frame);
        if (!shape && nCursors > 0) shape = cursors->GetShape(0);
    }
    if (!shape) shape = m_cursor; // fallback to hardcoded arrow
    if (!shape) return;

    Point2D p = {vx, vy};
    shape->SetPosition(&p);
    sceneFB->drawShape(shape);
}

void WC3GameFlow::drawShapeIntoScene(int shapeId, int frame, int x, int y) {
    if (!sceneFB) return;
    // Every current caller (the Loadout screen's 183/184/185.SHP) is a
    // "gallery" shape whose frames are unrelated independent images, not an
    // animation — see getShape()'s independentFrames parameter.
    RSImageSet* img = getShape(shapeId, /*independentFrames=*/true);
    if (!img) return;
    int n = (int)img->GetNumImages();
    if (n <= 0) return;
    if (frame < 0 || frame >= n) frame = 0;
    RLEShape* shape = img->GetShape(frame);
    if (!shape) return;
    Point2D p = {x, y};
    shape->SetPosition(&p);
    sceneFB->drawShape(shape);
}

void WC3GameFlow::fillSceneRect(int x, int y, int w, int h, uint8_t colorIndex) {
    if (!sceneFB || !sceneFB->framebuffer) return;
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(sceneFB->width, x + w), y1 = std::min(sceneFB->height, y + h);
    for (int yy = y0; yy < y1; yy++)
        memset(sceneFB->framebuffer + (size_t)yy * sceneFB->width + x0, colorIndex, (size_t)std::max(0, x1 - x0));
}

void WC3GameFlow::displaySceneFramebuffer(float darken, bool drawHoverLabel,
                                            const std::function<void(uint32_t*, int, int)>& drawExtra) {
    if (!sceneFB) return;

    RSScreen& screen = RSScreen::instance();
    VGAPalette* pal = VGA.getPalette();

    int fbW = sceneFB->width;
    int fbH = sceneFB->height;
    uint32_t* rgba = new uint32_t[fbW * fbH];
    for (int i = 0; i < fbW * fbH; i++) {
        uint8_t idx = sceneFB->framebuffer[i];
        const Texel* c = pal->GetRGBColor(idx);
        uint8_t r = c->r, g = c->g, b = c->b;
        if (r == 0xFF && g == 0xFF && b == 0x02) { r = g = b = 0xFF; }
        if (darken != 1.0f) {
            r = (uint8_t)(r * darken); g = (uint8_t)(g * darken); b = (uint8_t)(b * darken);
        }
        rgba[i] = r | (g << 8) | (b << 16) | (0xFF << 24);
    }

    // Hover label: white text at the bottom of the screen in pre-flip screen coords.
    // The RGBA buffer uses natural y-down coords here; the flip below handles GL.
    if (drawHoverLabel && !m_hoverLabel.empty()) {
        WC3Font* font = WC3Globals::getInstance().getFont("GFMD");
        if (font && font->isLoaded()) {
            int textW = font->measureText(m_hoverLabel);
            int tx = (fbW - textW) / 2;
            int ty = fbH - font->getHeight() - 12;
            font->drawTextRGBA(rgba, fbW, fbH, m_hoverLabel, tx, ty);
        }
    }

    if (drawExtra) drawExtra(rgba, fbW, fbH);

    // Flip vertically for glDrawPixels (GL origin is bottom-left)
    for (int y = 0; y < fbH / 2; y++) {
        for (int x = 0; x < fbW; x++) {
            std::swap(rgba[y * fbW + x], rgba[(fbH - 1 - y) * fbW + x]);
        }
    }

    // Letterboxed 4:3 viewport (see RSScreen::viewport_x/y/w/h) instead of
    // the raw window rect — stretching this 640x480 frame to fill an
    // arbitrary (e.g. 16:9) window would distort every room/UI sprite,
    // since all of this game's art is authored for a 4:3 canvas.
    glViewport(screen.viewport_x, screen.viewport_y, screen.viewport_w, screen.viewport_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screen.viewport_w, 0, screen.viewport_h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);

    glRasterPos2i(0, 0);
    glPixelZoom((float)screen.viewport_w / fbW, (float)screen.viewport_h / fbH);
    glDrawPixels(fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    delete[] rgba;
}

void WC3GameFlow::ensurePalIndices() {
    if (!m_palIndices.empty()) return;
    for (size_t i = 0; i < shapePak.GetNumEntries(); i++) {
        PakEntry* e = shapePak.GetEntry((int)i);
        if (e && e->size == 768) m_palIndices.push_back((int)i);
    }
}

int WC3GameFlow::findOwningPaletteId(int shapeId) {
    ensurePalIndices();
    int owner = -1;
    for (int p : m_palIndices) {
        if (p > shapeId) break;
        owner = p;
    }
    return owner;
}

const std::array<uint8_t, 256>& WC3GameFlow::getPaletteRemap(int fromPaletteId, int toPaletteId) {
    uint64_t key = ((uint64_t)(uint32_t)fromPaletteId << 32) | (uint32_t)toPaletteId;
    auto it = m_paletteRemapCache.find(key);
    if (it != m_paletteRemapCache.end()) return it->second;

    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)i;

    PakEntry* fromEntry = shapePak.GetEntry(fromPaletteId);
    PakEntry* toEntry = shapePak.GetEntry(toPaletteId);
    if (fromEntry && toEntry && fromEntry->size == 768 && toEntry->size == 768) {
        for (int i = 0; i < 255; i++) { // 255 (transparent) always maps to itself
            int fr = convertFrom6To8(fromEntry->data[i * 3 + 0]);
            int fg = convertFrom6To8(fromEntry->data[i * 3 + 1]);
            int fb = convertFrom6To8(fromEntry->data[i * 3 + 2]);
            int best = 0, bestDist = INT32_MAX;
            for (int j = 0; j < 255; j++) {
                int tr = convertFrom6To8(toEntry->data[j * 3 + 0]);
                int tg = convertFrom6To8(toEntry->data[j * 3 + 1]);
                int tb = convertFrom6To8(toEntry->data[j * 3 + 2]);
                int dr = fr - tr, dg = fg - tg, db = fb - tb;
                int dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist) { bestDist = dist; best = j; }
            }
            table[i] = (uint8_t)best;
        }
    }
    auto [ins, _] = m_paletteRemapCache.emplace(key, table);
    return ins->second;
}

RSImageSet* WC3GameFlow::getShape(int shapeId, bool independentFrames) {
    auto& cache = independentFrames ? shapeCacheIndependent : shapeCache;
    auto it = cache.find(shapeId);
    if (it != cache.end())
        return it->second;

    if (shapeId < 0 || shapeId >= static_cast<int>(shapePak.GetNumEntries()))
        return nullptr;

    PakEntry* entry = shapePak.GetEntry(shapeId);
    if (!entry || entry->size < 12)
        return nullptr;

    RSImageSet* img = new RSImageSet();

    bool isWC3Shape = (entry->data[0] == '1' && entry->data[1] == '.' &&
                       entry->data[2] == '1' && entry->data[3] == '1');
    if (!isWC3Shape) {
        cache[shapeId] = img;
        return img;
    }

    uint32_t N = wc3_u32(entry->data + 4);
    if (N == 0 || N > 1024 || 8 + N * 8 > entry->size) {
        cache[shapeId] = img;
        return img;
    }

    struct SubInfo {
        uint16_t full_w, full_h, x_blit, y_min, row_width, y_span;
        size_t   pdata_off, pdata_len;
    };
    std::vector<SubInfo> subs;
    subs.reserve(N);

    for (uint32_t i = 0; i < N; i++) {
        uint32_t si_off = wc3_u32(entry->data + 8 + i * 8);
        if (si_off + 24 > entry->size) continue;

        const uint8_t* h = entry->data + si_off;
        uint16_t full_h     = (uint16_t)(wc3_u16(h +  0) + 1);
        uint16_t full_w     = (uint16_t)(wc3_u16(h +  2) + 1);
        uint16_t x_blit     = wc3_u16(h +  8);
        uint16_t y_min      = wc3_u16(h + 12);
        uint16_t x_scan_max = wc3_u16(h + 16);
        uint16_t y_max      = wc3_u16(h + 20);

        if (y_min  > y_max)       y_max      = y_min;
        if (x_scan_max >= full_w) x_scan_max = (uint16_t)(full_w - 1);
        if (y_max      >= full_h) y_max      = (uint16_t)(full_h - 1);

        uint16_t row_width = (uint16_t)(x_scan_max + 1);
        uint16_t y_span    = (uint16_t)(y_max - y_min + 1);

        size_t pdata_off = si_off + 24;
        size_t pdata_len;
        if (i + 1 < N) {
            uint32_t next_off = wc3_u32(entry->data + 8 + (i + 1) * 8);
            pdata_len = (next_off > pdata_off) ? next_off - pdata_off : 0;
        } else {
            pdata_len = (entry->size > pdata_off) ? entry->size - pdata_off : 0;
        }

        subs.push_back({ full_w, full_h, x_blit, y_min, row_width, y_span,
                         pdata_off, pdata_len });
    }

    if (subs.empty()) {
        cache[shapeId] = img;
        return img;
    }

    uint16_t full_w = subs[0].full_w;
    uint16_t full_h = subs[0].full_h;
    if ((size_t)full_w * full_h == 0 || (size_t)full_w * full_h > 4 * 1024 * 1024) {
        cache[shapeId] = img;
        return img;
    }

    // A shape whose scrn_id falls outside the current room's own PAL group
    // was tried once (remapping its raw indices into the current room's
    // palette via nearest-RGB match), but reverted: the two known
    // out-of-group cases (room 0003's gump 18 -> shapeId 188, room 0008's
    // gump 54 -> shapeId 189) both sit past the last of the 14 real
    // per-room [PAL,SHP...] groups, in a trailing block of extra/appended
    // shapes — "the nearest preceding PAL entry" for them is room 0014's,
    // but that PAL is nearly degenerate (pure black at almost every index
    // these shapes actually use — confirmed for shapeId 188: 12 of its 15
    // most-used indices are literally (0,0,0) under PAL 177), so it isn't
    // a meaningful source palette at all, just an accident of file layout.
    // Remapping through it produced a "mostly black" shape — worse than
    // the original wrong-color bug. Interpreting these trailing shapes'
    // raw indices directly through whichever room's palette is already
    // active (i.e. no remap) gives a sane-looking result instead (checked
    // shapeId 188 against room 0003's own palette directly: a plausible
    // grayscale door, not noise) — so out-of-group shapes are just left
    // alone for now. ensurePalIndices()/findOwningPaletteId()/
    // getPaletteRemap() are kept as-is in case a real cross-group case
    // (one whose owning PAL isn't degenerate) turns up later.
    //
    // A separate "some shapes use a specific raw index for a small bright/
    // white highlight that the room-specific palette generation didn't
    // preserve as white" bug (portrait sprites' index 162, Mess Hall
    // background's index 164) was also investigated this session. Three
    // fix attempts (a hardcoded global reserved palette slot, a per-room
    // computed one, and finally an RGBA-space overlay pass bypassing the
    // palette entirely) were each reverted after live testing found a new
    // regression — the last one caused unwanted white pixels showing up on
    // characters. Left unfixed for now rather than risk another regression
    // — see todo.MD for the full investigation history if picking this
    // back up.
    auto addFrame = [&](const std::vector<uint8_t>& src) {
        RLEShape* shape = new RLEShape();
        shape->InitFromPixels(src.data(), full_w, full_h);
        img->Add(shape);
    };

    // Decode sub0 as keyframe; pre-fill with 0xFF (transparent in RLEShape::Expand).
    std::vector<uint8_t> running_frame((size_t)full_w * full_h, 0xFF);
    {
        const SubInfo& s = subs[0];
        wc3_decode_rle(entry->data + s.pdata_off, s.pdata_len,
                       s.y_span, s.row_width,
                       running_frame.data(), full_w, full_h,
                       s.x_blit, s.y_min);
    }
    addFrame(running_frame);

    // Every subsequent sub is normally decoded as a delta patch on top of
    // the running frame — never reset to blank. Previously this was gated
    // on a "sub1's data is much smaller than sub0's" size-ratio heuristic
    // (delta_mode), which picked "independent keyframe" (reset-to-blank)
    // for any sub whose RLE stream wasn't under half of sub0's size. That's
    // not a reliable signal: a shape's own "key=1 skip N" RLE opcode
    // already means "leave these pixels as they are" regardless of how
    // large the encoded delta is (e.g. a long multi-frame walk/door-open
    // animation can have large, non-tiny per-frame deltas that still only
    // ever *add* coverage, never erase it) — confirmed by decoding
    // GAMEFLOW/room 0006's 60-frame door actor (071.SHP): treated as
    // "independent keyframe" its frames dropped from 100% opaque to under
    // 25%, rendering as increasingly transparent/broken exactly as
    // reported ("door looks semi-transparent and when it opens the sprite
    // looks wrong"); decoded as a running delta every frame stays fully
    // opaque, which is what a solid door/figure should do. Since a plain
    // RLE stream can only ever draw color (never explicitly clear a pixel
    // back to transparent), delta accumulation is safe for the reverse
    // case too — a sub that redraws its own full footprint (e.g. a
    // button's dark->lit state) just overwrites every pixel it covers,
    // identical to before.
    //
    // independentFrames (see the header comment on getShape()) opts out of
    // that accumulation entirely: each sub is its own complete image (a
    // "gallery" shape, e.g. the Loadout screen's per-craft/per-weapon
    // selector art) rather than incremental animation frames, so the
    // running frame is reset to fully transparent before every sub instead
    // of only before sub0 — otherwise frame N would keep showing whatever
    // frame N-1's art didn't happen to overwrite.
    for (size_t i = 1; i < subs.size(); i++) {
        const SubInfo& s = subs[i];
        if (independentFrames) std::fill(running_frame.begin(), running_frame.end(), 0xFF);
        wc3_decode_rle(entry->data + s.pdata_off, s.pdata_len,
                       s.y_span, s.row_width,
                       running_frame.data(), full_w, full_h,
                       s.x_blit, s.y_min);
        addFrame(running_frame);
    }

    cache[shapeId] = img;
    return img;
}

void WC3GameFlow::logRoomMenu() const {
    if (!scene) return;
    auto& items = scene->getRoomMenuItems();
    if (!items.empty()) {
        printf("--- Room menu (%zu items) ---\n", items.size());
        for (size_t i = 0; i < items.size(); i++)
            printf("  [%zu] %s\n", i, items[i].c_str());
    }
    printf("--- Active zones (%zu) ---\n", scene->getZones().size());
    for (auto& z : scene->getZones())
        printf("  gump_id=%u titl_id=%d type=%u x=%d y=%d w=%d h=%d label=\"%s\"\n",
               z.gump_id, z.titl_id, z.type, z.x, z.y, z.w, z.h, z.label.c_str());
}

void WC3GameFlow::loadBranchPaks() {
    static const struct { int idx; const char* tre; const char* path; } kBranches[] = {
        { 0, "cd1miss.tre", "..\\..\\DATA\\GAMEFLOW\\BRANCH1.PAK" },
        { 1, "cd2miss.tre", "..\\..\\DATA\\GAMEFLOW\\BRANCH2.PAK" },
        { 2, "cd3miss.tre", "..\\..\\DATA\\GAMEFLOW\\BRANCH3.PAK" },
        { 3, "cd4miss.tre", "..\\..\\DATA\\GAMEFLOW\\BRANCH4.PAK" },
    };
    for (auto& b : kBranches) {
        if (!branchPaks[b.idx].isEmpty()) continue;
        // Try preloaded assets first (no allocation to free)
        TreEntry* e = Assets.GetEntryByName(b.path);
        if (e) {
            branchPaks[b.idx].load(e->data, e->size);
            continue;
        }
        // Fall back to loading directly from the TRE file
        e = XtreArchive::LoadSingleEntry(b.tre, b.path);
        if (!e) {
            printf("WC3: %s not found\n", b.path);
            continue;
        }
        branchPaks[b.idx].load(e->data, e->size);
        delete[] e->data;
        delete e;
    }
}

void WC3GameFlow::loadGameflowMusic() {
    const char* path = "..\\..\\DATA\\SOUND\\GMMUSIC.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    if (e) {
        musicPak.load(e->data, e->size);
        return;
    }
    e = XtreArchive::LoadSingleEntry("gameflow.tre", path);
    if (!e) {
        printf("WC3: %s not found\n", path);
        return;
    }
    musicPak.load(e->data, e->size);
    delete[] e->data;
    delete e;
}

void WC3GameFlow::loadGameflowSamples() {
    const char* path = "..\\..\\DATA\\SOUND\\GFSAMPLE.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    if (e) {
        samplePak.load(e->data, e->size);
        return;
    }
    e = XtreArchive::LoadSingleEntry("gameflow.tre", path);
    if (!e) {
        printf("WC3: %s not found\n", path);
        return;
    }
    samplePak.load(e->data, e->size);
    delete[] e->data;
    delete e;
}

void WC3GameFlow::playGameflowSample(int index) {
    const std::vector<uint8_t>* wav = samplePak.getSampleWav(index);
    if (!wav) {
        printf("WC3: gameflow sample %d not found in GFSAMPLE.IFF\n", index);
        return;
    }
    Mixer.playSoundVoc(const_cast<uint8_t*>(wav->data()), wav->size());
}

void WC3GameFlow::playGameflowMusic(int trackIndex) {
    if (trackIndex < 0 || trackIndex == m_currentMusicTrack) return;
    const std::vector<uint8_t>* midi = musicPak.getTrackMidi(trackIndex);
    if (!midi) {
        printf("WC3: gameflow music track %d not found/convertible\n", trackIndex);
        return;
    }
    SDL_RWops* rw = SDL_RWFromConstMem(midi->data(), (int)midi->size());
    Mix_Music* music = Mix_LoadMUSType_RW(rw, MUS_MID, 1);
    if (!music) {
        printf("WC3: failed to load gameflow music track %d: %s\n", trackIndex, Mix_GetError());
        return;
    }
    if (m_currentMusicPtr) {
        Mix_HaltMusic();
        Mix_FreeMusic(m_currentMusicPtr);
    }
    m_currentMusicPtr = music;
    m_currentMusicTrack = trackIndex;
    if (Mix_PlayMusic(music, -1) == -1)
        printf("WC3: failed to play gameflow music track %d: %s\n", trackIndex, Mix_GetError());
}

// Room ID ("%04d", VICTORY.IFF's own room numbering) -> GMMUSIC.IFF track index.
// Reverse-engineered from a Windows-edition WC3 dev asset, musictyp.iff (FORM
// SNDS > GFGF), which names each ready-room-hub ambient loop by its .wav
// filename in the WAV-based Windows music system — cross-referenced against
// each name's real-world room by which room's "Go to X" menu link targets it:
//   24 barracks.wav, 25 bridge.wav, 26 flightco.wav (Flight Control),
//   27 flightde.wav (Flight Deck), 28 lift.wav, 29 recroom1.wav (Mess Hall).
// Confirmed exactly: room "0001" (Flight Deck, the game's starting room) ->
// track 27, matching the verified first-track-the-player-hears finding.
// Room "0004" (Gunnery Control) has no entry in this table — entering it
// leaves whatever's currently playing alone rather than guessing.
static const std::unordered_map<std::string, int> kRoomMusicTrack = {
    {"0002", 24}, // Barracks
    {"0000", 25}, // Bridge
    {"0003", 26}, // Flight Control
    {"0001", 27}, // Flight Deck
    {"0005", 28}, // Lift
    {"0006", 29}, // Mess Hall / Rec Room
};

void WC3GameFlow::playMusicForRoom(const std::string& roomId) {
    auto it = kRoomMusicTrack.find(roomId);
    if (it != kRoomMusicTrack.end())
        playGameflowMusic(it->second);
}

void WC3GameFlow::pauseGameflowMusic() {
    if (m_currentMusicPtr && Mix_PlayingMusic()) Mix_PauseMusic();
}

void WC3GameFlow::resumeGameflowMusic() {
    if (m_currentMusicPtr && Mix_PausedMusic()) Mix_ResumeMusic();
}

void WC3GameFlow::ensureSC205() {
    if (!sc205Data.empty()) return;

    const char* path = "..\\..\\DATA\\MOVIES\\SC_205.MVE";
    TreEntry* e = Assets.GetEntryByName(path);
    if (e) {
        sc205Data.assign(e->data, e->data + e->size);
        return;
    }
    static const char* cdTres[] = {
        "cd1movie.tre","cd2movie.tre","cd3movie.tre","cd4movie.tre",
        "cd1miss.tre", "cd2miss.tre", "cd3miss.tre", "cd4miss.tre", nullptr
    };
    for (const char** tre = cdTres; *tre; tre++) {
        e = XtreArchive::LoadSingleEntry(*tre, path);
        if (!e) continue;
        sc205Data.assign(e->data, e->data + e->size);
        delete[] e->data; delete e;
        return;
    }
    printf("WC3: SC_205.MVE not found\n");
}

void WC3GameFlow::playSC205Shot(int shotIndex) {
    MusicPauseScope musicGuard(this);
    printf("WC3: playSC205Shot(%d)\n", shotIndex);
    ensureSC205();
    if (sc205Data.empty()) return;
    // SC_205.MVE's own INDX/BRCH structure gives each catalog entry exactly one
    // raw SHOT chunk (1:1), so branch-group semantics are equivalent to the old
    // raw-shot behavior here — using them uniformly avoids a special case.
    WC3MVEPlayer::playBranchGroup(sc205Data.data(), sc205Data.size(), shotIndex);
}

// LOOKMOVI.IFF is an IFF "LOOK" form: each child chunk's own 4-ASCII-digit tag is
// a numeric index, and its data is a null-terminated movie basename (e.g. tag
// "0024" → data "sc_6\0"). Index 9997 → "sc_205", the shared transition movie.
void WC3GameFlow::loadLookMovi() {
    const char* path = "..\\..\\DATA\\GAMEFLOW\\LOOKMOVI.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    if (!e || e->size < 12 || memcmp(e->data, "FORM", 4) != 0 || memcmp(e->data + 8, "LOOK", 4) != 0) {
        printf("WC3: LOOKMOVI.IFF not found\n");
        return;
    }
    const uint8_t* data = e->data;
    size_t size = e->size;
    size_t pos = 12;
    while (pos + 8 <= size) {
        char tag[5] = {};
        memcpy(tag, data + pos, 4);
        uint32_t sz = wc3_u32be(data + pos + 4);
        size_t cend = pos + 8 + sz;
        if (cend > size) break;
        char* endp = nullptr;
        long idx = strtol(tag, &endp, 10);
        if (endp == tag + 4) {
            const char* str = reinterpret_cast<const char*>(data + pos + 8);
            m_lookMovi[(int)idx] = std::string(str, strnlen(str, sz));
        }
        pos = cend;
        if (sz & 1) pos++;
    }
    printf("WC3: loaded %zu LOOKMOVI entries\n", m_lookMovi.size());
}

void WC3GameFlow::loadLookMisn() {
    const char* path = "..\\..\\DATA\\GAMEFLOW\\LOOKMISN.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    if (!e || e->size < 12 || memcmp(e->data, "FORM", 4) != 0 || memcmp(e->data + 8, "LOOK", 4) != 0) {
        printf("WC3: LOOKMISN.IFF not found\n");
        return;
    }
    const uint8_t* data = e->data;
    size_t size = e->size;
    size_t pos = 12;
    while (pos + 8 <= size) {
        char tag[5] = {};
        memcpy(tag, data + pos, 4);
        uint32_t sz = wc3_u32be(data + pos + 4);
        size_t cend = pos + 8 + sz;
        if (cend > size) break;
        char* endp = nullptr;
        long idx = strtol(tag, &endp, 10);
        if (endp == tag + 4) {
            const char* str = reinterpret_cast<const char*>(data + pos + 8);
            m_lookMisn[(int)idx] = std::string(str, strnlen(str, sz));
        }
        pos = cend;
        if (sz & 1) pos++;
    }
    printf("WC3: loaded %zu LOOKMISN entries\n", m_lookMisn.size());
}

void WC3GameFlow::loadSimDesc() {
    const char* path = "..\\..\\DATA\\GAMEFLOW\\SIMDESC.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    if (!e || e->size < 12 || memcmp(e->data, "FORM", 4) != 0 || memcmp(e->data + 8, "SIM_", 4) != 0) {
        printf("WC3: SIMDESC.IFF not found\n");
        return;
    }
    const uint8_t* data = e->data;
    size_t size = e->size;
    size_t pos = 12;
    while (pos + 8 <= size) {
        char tag[5] = {};
        memcpy(tag, data + pos, 4);
        uint32_t sz = wc3_u32be(data + pos + 4);
        size_t cstart = pos + 8;
        size_t cend = cstart + sz;
        if (cend > size) break;
        if (memcmp(tag, "DEF_", 4) == 0 && sz >= 4) {
            uint32_t count = data[cstart] | (data[cstart+1]<<8) | (data[cstart+2]<<16) | (data[cstart+3]<<24);
            size_t rp = cstart + 4;
            for (uint32_t i = 0; i < count && rp + 25 <= cend; i++) {
                const char* namePtr = reinterpret_cast<const char*>(data + rp);
                std::string name(namePtr, strnlen(namePtr, 19));
                int idx = (int)(data[rp+21] | (data[rp+22]<<8) | (data[rp+23]<<16) | (data[rp+24]<<24));
                m_simMissions.push_back({name, idx});
                rp += 25;
            }
        }
        pos = cend;
        if (sz & 1) pos++;
    }
    printf("WC3: loaded %zu SIMDESC training missions\n", m_simMissions.size());
}

void WC3GameFlow::loadBrfDesc() {
    const char* path = "..\\..\\DATA\\GAMEFLOW\\BRFDESC.IFF";
    TreEntry* e = Assets.GetEntryByName(path);
    if (!e || e->size < 12 || memcmp(e->data, "FORM", 4) != 0 || memcmp(e->data + 8, "BRF_", 4) != 0) {
        printf("WC3: BRFDESC.IFF not found\n");
        return;
    }
    const uint8_t* data = e->data;
    size_t size = e->size;
    size_t pos = 12;
    while (pos + 8 <= size) {
        char tag[5] = {};
        memcpy(tag, data + pos, 4);
        uint32_t sz = wc3_u32be(data + pos + 4);
        size_t cstart = pos + 8;
        size_t cend = cstart + sz;
        if (cend > size) break;
        char* endp = nullptr;
        long idx = strtol(tag, &endp, 10);
        if (endp == tag + 4 && sz >= 4) {
            uint32_t lineCount = data[cstart] | (data[cstart+1]<<8) | (data[cstart+2]<<16) | (data[cstart+3]<<24);
            size_t lp = cstart + 4;
            std::vector<std::string> lines;
            for (uint32_t i = 0; i < lineCount && lp < cend; i++) {
                const char* s = reinterpret_cast<const char*>(data + lp);
                size_t n = strnlen(s, cend - lp);
                lines.emplace_back(s, n);
                lp += n + 1;
            }
            m_briefingText[(int)idx] = std::move(lines);
        }
        pos = cend;
        if (sz & 1) pos++;
    }
    printf("WC3: loaded %zu BRFDESC briefing entries\n", m_briefingText.size());
}

// Resolves m_selectedMissionIndex through LOOKMISN.IFF and hands off to
// mission flight via a new WC3Strike activity (mirrors Strike Commander's own
// SCGameFlow::flyMission — see SCGameFlow.cpp). WC3GameFlow stays on the
// GameEngine activity stack, unfocused, and regains control automatically
// once WC3Strike calls Game->stopTopActivity() at mission end.
void WC3GameFlow::launchMission() {
    auto it = m_lookMisn.find(m_selectedMissionIndex);
    if (it == m_lookMisn.end()) {
        printf("WC3: launch mission — no LOOKMISN entry for index %d\n", m_selectedMissionIndex);
        return;
    }
    if (m_options.devStubMissions) {
        printf("WC3: [STUB] mission '%s' (LOOKMISN index %d) — showing Win/Loss prompt instead of flying it\n",
               it->second.c_str(), m_selectedMissionIndex);
        m_stubMissionName = it->second;
        state = State::MISSION_STUB;
        return;
    }
    printf("WC3: launching mission '%s' (LOOKMISN index %d)\n", it->second.c_str(), m_selectedMissionIndex);
    WC3Strike* strike = new WC3Strike();
    strike->init();
    strike->setOwnerGameFlow(this);
    Game->addActivity(strike);
    if (!strike->setMission((it->second + ".iff").c_str())) {
        // setMission() failed (e.g. no resolvable PLAYER actor) — strike is
        // left with a null player_plane/cockpit that runFrame() would
        // dereference unconditionally. Stop it immediately, before the
        // engine ever calls runFrame() on it, so it's popped cleanly next
        // tick and control falls back to WC3GameFlow (still beneath it on
        // the activity stack) instead of crashing.
        printf("WC3: mission '%s' failed to load, returning to gameflow\n", it->second.c_str());
        Game->stopTopActivity();
    }
}

void WC3GameFlow::launchTrainingMission(int simIndex) {
    char buf[16];
    snprintf(buf, sizeof(buf), "tsim%03d", simIndex + 1);
    std::string name = buf;
    if (m_options.devStubMissions) {
        printf("WC3: [STUB] training mission '%s' — showing Win/Loss prompt instead of flying it\n",
               name.c_str());
        m_stubMissionName = name;
        m_stubReturnToSimulator = true;
        state = State::MISSION_STUB;
        return;
    }
    printf("WC3: launching training mission '%s'\n", name.c_str());
    WC3Strike* strike = new WC3Strike();
    strike->init();
    // No setOwnerGameFlow() — see the field comment on m_stubReturnToSimulator/
    // launchTrainingMission's declaration for why training missions must not
    // feed onMissionComplete().
    Game->addActivity(strike);
    if (!strike->setMission((name + ".iff").c_str())) {
        printf("WC3: training mission '%s' failed to load, returning to gameflow\n", name.c_str());
        Game->stopTopActivity();
    }
}

void WC3GameFlow::onMissionComplete(bool won, const std::string& nextMissionOverrideFilename) {
    // Confirmed campaign structure: SCEN0000 -> MISNA001 (LOOKMISN index 0)
    // -> SCEN0001 -> MISNA002 (index 1) -> SCEN0002 -> ... i.e. completing
    // mission index N *successfully* advances to scene N+1 — the win path
    // is a straight sequential walk through LOOKMISN, one scene per
    // mission. m_selectedMissionIndex is exactly that N (set from the
    // gump's own OP5 when the player picked this mission — see
    // finishGumpAction).
    //
    // Not handled: the loss path, which branches to an entirely different
    // mission track instead of continuing sequentially (e.g. failing early
    // enough leads to MISNR001 "Proxima" / MISNR002 "SOL", LOOKMISN indices
    // 49/50) — the exact per-mission branch-selection rule (which failures
    // route where) isn't confirmed yet, so a loss currently leaves
    // current_scene/state untouched rather than guessing. Per-pilot morale
    // presumably factors into some of these decisions too but isn't wired
    // to this hook yet (see m_pilotMorale — tracked, not yet consumed here).
    //
    // The "special bridge missions at LOOKMISN indices 51-54" this comment
    // used to call entirely unhandled are now partly understood: misnd3bd
    // (the Flint-rescue side-mission) is reachable via the
    // nextMissionOverrideFilename mechanism just below. misnb2ne/misnc2ne/
    // misnd1b are still not — they're ship/state variants selected by
    // scene-level bytecode this engine doesn't replay (misnd1b specifically
    // confirmed to have no Flint content at all, so it isn't this mechanism
    // — see OP_SELECT_NEXT_MISSION's comment in SCenums.h).
    if (won && !nextMissionOverrideFilename.empty()) {
        int resolvedIdx = -1;
        for (auto& [idx, name] : m_lookMisn) {
            if (name == nextMissionOverrideFilename) { resolvedIdx = idx; break; }
        }
        if (resolvedIdx < 0) {
            printf("WC3: mission selected next-mission override '%s' via its own PROG, "
                   "but no matching LOOKMISN entry was found — ignoring\n",
                   nextMissionOverrideFilename.c_str());
        } else if (resolvedIdx != m_selectedMissionIndex + 1) {
            // Genuinely out-of-sequence (e.g. misnd3bd, LOOKMISN 53, instead
            // of Laconda2's own normal-sequence successor at index 14) —
            // defer the usual scene advance and fly it first. If it happens
            // to equal the normal successor (as e.g. misnd002's own
            // "misnd003" branch does), there's nothing special to do; the
            // ordinary path below already produces the right result.
            printf("WC3: mission's own PROG selected an out-of-sequence next mission "
                   "'%s' (LOOKMISN index %d) — launching it before resuming the "
                   "campaign at scene %d\n",
                   nextMissionOverrideFilename.c_str(), resolvedIdx, m_selectedMissionIndex + 1);
            m_pendingResumeScene = m_selectedMissionIndex + 1;
            m_selectedMissionIndex = resolvedIdx;
            state = State::LAUNCH_EXTRA_MISSION;
            return; // Skip the normal advance/cleanup below — see
                    // State::LAUNCH_EXTRA_MISSION's own comment for why this
                    // can't launch synchronously here. Runs again, with
                    // m_pendingResumeScene set, once the extra mission itself
                    // completes.
        }
    }

    if (won && m_pendingResumeScene >= 0) {
        current_scene = m_pendingResumeScene;
        m_pendingResumeScene = -1;
        state = State::LOAD_SCENE;
        printf("WC3: extra mission complete — resuming campaign at scene %d\n", current_scene);
    } else if (won && m_selectedMissionIndex >= 0) {
        current_scene = m_selectedMissionIndex + 1;
        state = State::LOAD_SCENE;
        printf("WC3: mission won (index %d) — advancing to scene %d\n",
               m_selectedMissionIndex, current_scene);
    } else {
        printf("WC3: mission %s — loss-path branching not yet implemented, "
               "staying on scene %d\n", won ? "won (no mission index recorded?)" : "lost",
               current_scene);
    }

    // End-of-mission cleanup, regardless of outcome — these are per-mission-
    // cycle flags, not campaign-persistent ones, and nothing else clears
    // varsA/varsB anywhere (they otherwise persist for the whole session —
    // see the shapeCache fix above, same root cause category as that bug).
    // Confirmed via existing finishGumpAction handling, which sets both as
    // an approximation of real subscene side effects we don't play back:
    //   bank-B 191 — "Mission Briefing"/Flight Control's crew-talk trigger
    //     (subscene 146).
    //   bank-B 155 — "wingman selection Completed" (subscenes 174-180,
    //     comment there calls it "wingman selection committed").
    // "Loadout available" is NOT cleared here — the exact bank slot isn't
    // confirmed yet. Room 0003's "Activate MAIN TERMINAL" gumps (17/20)
    // aren't gated by that room's own INIT (never explicitly hidden), so
    // whatever controls their availability lives elsewhere (likely their
    // own ACTV code — GoRoom 8, into the loadout room — not yet fully
    // disassembled).
    if (scene) {
        scene->setBankB(191, 0);
        scene->setBankB(155, 0);
        // bank-A 12 = "was the just-completed mission successful", read by
        // wingman-portrait "talk to X" gumps (Hobbes at Flight Control, a
        // different pilot in the Barracks — see setBankA's own comment) to
        // pick their reaction movie's success vs failure shot-list. Unlike
        // the two bank-B flags above, this isn't cleared to 0 — it's set to
        // reflect this mission's actual outcome, and stays set (the "last
        // mission's outcome" is presumably meant to persist as context
        // until the next mission is flown, not reset immediately).
        scene->setBankA(12, won ? 1 : 0);
    }
}

void WC3GameFlow::playNamedMovieShot(const std::string& name, int shotIndex) {
    MusicPauseScope musicGuard(this);
    std::string upper = name;
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    std::string path = "..\\..\\DATA\\MOVIES\\" + upper + ".MVE";

    printf("WC3: playNamedMovieShot(%s, %d)\n", upper.c_str(), shotIndex);

    TreEntry* e = Assets.GetEntryByName(path.c_str());
    if (e) {
        WC3MVEPlayer::playBranchGroup(e->data, e->size, shotIndex);
        return;
    }
    static const char* cdTres[] = {
        "cd1movie.tre","cd2movie.tre","cd3movie.tre","cd4movie.tre", nullptr
    };
    for (const char** tre = cdTres; *tre; tre++) {
        e = XtreArchive::LoadSingleEntry(*tre, path.c_str());
        if (!e) continue;
        WC3MVEPlayer::playBranchGroup(e->data, e->size, shotIndex);
        delete[] e->data; delete e;
        return;
    }
    printf("WC3: %s not found\n", path.c_str());
}

// Plays each pending movie in order (INIT/EXIT bytecode side effects — e.g. the
// elevator playing its own entrance shot on room INIT), routing "sc_205" to the
// shared cached loader and everything else to playNamedMovieShot.
void WC3GameFlow::playPendingMovies(const std::vector<WC3Scene::PendingMovie>& movies) {
    if (!m_options.transitionsOn) return;
    for (auto& m : movies) {
        if (m.name == "sc_205") playSC205Shot(m.shot);
        else playNamedMovieShot(m.name, m.shot);
        // The Mess Hall's own INIT bytecode already gates sc_8 (its
        // first-time-entering-the-ship-tour clip, LOOKMOVI index 26) behind
        // "IF GET-B(4) == 0" — a real, correctly-authored one-shot check.
        // But nothing in this interpreter can ever write bank-B (confirmed
        // via an exhaustive scan of every opcode across all 55 SCEN files:
        // the only bank-B write anywhere in this codebase is a single
        // constructor seed for an unrelated flag), so GET-B(4) always reads
        // its 0 default and the IF is permanently true — sc_8 replayed on
        // every single Mess Hall visit instead of just the first. Since the
        // real "SET bank-B" opcode couldn't be identified with confidence
        // (searched every opcode-58 call site in the game; none target slot
        // 4), set the flag directly here instead of guessing at bytecode
        // semantics — this is the same "confirmed data, approximated
        // mechanism" approach already used elsewhere in this file (roster
        // flags, scene progression).
        if (m.name == "sc_8" && scene) scene->setBankB(4, 1);
    }
}

// Advance m_focusZoneIdx to the next zone in the current room and warp the
// mouse cursor to its centre. Shared by right-click and Tab.
void WC3GameFlow::cycleZoneFocus() {
    if (!scene || scene->getZones().empty()) return;
    const auto& zones = scene->getZones();
    m_focusZoneIdx = (m_focusZoneIdx + 1) % (int)zones.size();
    const auto& z = zones[m_focusZoneIdx];
    RSScreen& screen = RSScreen::instance();
    // Zones are in unshifted room-CORD space; the Terminal Hub page (the
    // only m_inTerminal page that still uses this shared zone system — see
    // kTerminalYOffset) renders kTerminalYOffset lower, so the warp target
    // needs the same correction to land on the visible button.
    int targetY = z.y + z.h / 2 + (m_inTerminal ? kTerminalYOffset : 0);
    int wx, wy;
    screen.logicalToWindow(z.x + z.w / 2, targetY, 640, 480, wx, wy);
    SDL_WarpMouseInWindow(screen.getSDLWindow(), wx, wy);
}

// Runs the ACTV action for a clickable zone. Shared by mouse-click and
// keyboard activation (Enter) of the focused zone.
void WC3GameFlow::activateZone(int zoneIdx) {
    if (!scene || zoneIdx < 0 || zoneIdx >= (int)scene->getZones().size()) return;
    const auto& zone = scene->getZones()[zoneIdx];
    printf("WC3: activated zone %d gump_id=%u titl=%d rect=(%d,%d %dx%d)\n",
           zoneIdx, zone.gump_id, zone.titl_id, zone.x, zone.y, zone.w, zone.h);

    // Lift floor buttons/Exit LIFT — intercepted before running their own
    // (unreliable) ACTV code. See m_liftSelectedFloor's comment.
    if (m_inLift) {
        if (zone.gump_id == 12) { exitLift(); return; }
        for (int i = 0; i < 3; i++) {
            if (zone.gump_id == kLiftFloorButtonGump[i]) { selectLiftFloor(i); return; }
        }
    }

    // Terminal (room 0008) Hub tab buttons — intercepted the same way, since
    // this room's own ACTV can't drive multi-page switching either. (Duty
    // Logs is handled separately, in renderDutyLogsPage() — it bypasses this
    // normal scene->hitTest()/activateZone() path entirely, the same way
    // State::SIMULATOR's list/buttons do, since its list rows aren't
    // gump-zones this pipeline knows about.)
    if (m_inTerminal && m_terminalPage == TerminalPage::Hub) {
        if (zone.gump_id == 5) { // "duty logs" tab
            m_terminalPage = TerminalPage::DutyLogs;
            applyTerminalPageVisibility();
            return;
        }
        if (zone.gump_id == 6) { // "controls" tab — the game's own name for Options
            openOptionsActivity();
            return;
        }
        if (zone.gump_id == 7) { // "logoff" — leaves the terminal back to wherever it was activated from
            WC3Scene::ActionResult action;
            action.type = WC3Scene::ActionResult::GoRoom;
            action.target = m_terminalOriginRoom;
            finishGumpAction(action);
            return;
        }
    }

    bool isActorConversation = (zone.type == WC3Gump::TYPE_CHARACTER ||
                                 zone.type == WC3Gump::TYPE_CHARACTER2);
    runGumpAction(zone.gump_id, isActorConversation);
}

// Runs a gump's own ACTV code and processes the resulting action (branch-choice
// defer, subscene/movie/room-nav). Shared by real clicks (activateZone) and a
// VAR(44)-triggered animation completing on its own (see SCENE_ACTIVE's poll of
// scene->pollFinishedAnimation() — the elevator's travel-animation chain fires
// its next link exactly this way, e.g. gump 8's clip finishing hides itself and
// triggers gump 9's, all the way to the door opening).
void WC3GameFlow::runGumpAction(uint32_t gumpId, bool isActorConversation) {
    auto action = scene->activateGump(gumpId, [this](int id){ return this->getShape(id); });

    // See m_talkedToActorsThisVisit's own comment — actually removing the
    // actor is deferred until the player leaves this room (or into a
    // terminal), handled in finishGumpAction()'s GoRoom branch.
    if (isActorConversation) {
        m_talkedToActorsThisVisit.insert(gumpId);
    }

    // Play Blair's voice line before the transition. When the entry has
    // both a TRUE and a FALS line, that's a real player dialogue choice —
    // play the gump's own intro movie/shot now (the setup line), then defer
    // the rest of the action until the player picks one.
    bool deferred = false;
    if (action.branchEntry >= 0) {
        for (int pi = 0; pi < 4; pi++) {
            const WC3BranchEntry* entry = branchPaks[pi].getEntry(action.branchEntry);
            if (!branchPaks[pi].isEmpty() && entry) {
                bool hasTrue  = !entry->true_line.isEmpty();
                bool hasFalse = !entry->false_line.isEmpty();
                if (hasTrue && hasFalse) {
                    if (m_options.transitionsOn && !action.movieName.empty())
                        playNamedMovieShot(action.movieName, action.movieShot);
                    // The intro shot's own MusicPauseScope already resumed music on
                    // return — re-pause it here so it stays paused for the whole
                    // choice-waiting period (potentially many frames), not just
                    // during the intro/reaction shots themselves. Resumed once the
                    // player commits, in commitChoice() below.
                    pauseGameflowMusic();
                    m_pendingBranchPak       = pi;
                    m_pendingBranchEntry     = action.branchEntry;
                    m_pendingBranchMovieName = action.movieName;
                    m_pendingGumpAction      = action;
                    // The intro shot above already covers movieName/movieShot — clear
                    // them so finishGumpAction() (run after the choice resolves) won't
                    // replay the intro instead of falling through to any subscene/room-nav.
                    m_pendingGumpAction.movieName.clear();
                    m_pendingGumpAction.movieShot = 0;
                    m_branchChoiceSelected = -1;
                    state = State::AWAITING_BRANCH_CHOICE;
                    deferred = true;
                } else {
                    branchPaks[pi].playEntry(action.branchEntry, !hasTrue);
                }
                break;
            }
        }
    }

    if (!deferred)
        finishGumpAction(action);
}

// Runs the non-dialogue part of a gump click (subscene marker, transition shot,
// room navigation). Called either immediately after activateGump(), or once the
// player has resolved an AWAITING_BRANCH_CHOICE prompt.
void WC3GameFlow::finishGumpAction(const WC3Scene::ActionResult& action) {
    // Handle dialogue subscene (VAR(7) N > 18).
    if (action.subscene >= 0) {
        // Subscene 146 is Flight Control's crew-talk trigger that leads into
        // wingman selection (room 0007). Room 0007's own INIT already plays
        // sc_200's "Intro" BRCH group on first entry, gated on bank-B slot
        // 191 — real subscene playback presumably sets that flag as a
        // scripted side effect; approximate it directly here since we don't
        // play subscene dialogue content at all yet.
        if (action.subscene == 146)
            scene->setBankB(191, 1);
        // Subscenes 174-180 are room 0007's own per-pilot "pick" dialogue
        // (one per portrait). Real subscene playback sets bank-B slot 155
        // ("wingman selection committed") as a scripted side effect;
        // approximate that here too. This is what reveals Flight Control's
        // "ready to launch" door variant (gid 19, INIT-swapped in for gid 2
        // once B155/B161 is set) in place of the plain "Go to FLIGHT DECK."
        // room-nav door. The picked pilot's own BRCH group + the shared
        // "Outro" BRCH are played automatically by room 0007's own EXIT code
        // (a VAR(23)-keyed dispatch table) once WC3Scene's VAR(45)/(47)
        // movie-macro interpreter supports the deferred shot-list form — see
        // WC3Scene.cpp's case 45/47.
        if (action.subscene >= 174 && action.subscene <= 180)
            scene->setBankB(155, 1);
        printf("WC3: subscene %d\n", action.subscene);
        // TODO: play subscene video/dialogue when subscene loader is implemented
    }

    // OP5: records which LOOKMISN.IFF entry this playthrough is about to fly
    // (set by talking to Flight Control's crew, consumed later by the launch
    // trigger below once the player actually reaches the "ready" door).
    if (action.missionIndex >= 0)
        m_selectedMissionIndex = action.missionIndex;

    // A gump-specific movie (VAR(45)) always wins over the generic SC_205 transition shot.
    // Every VAR(45) movie selection uses BRCH-group semantics (see playNamedMovieShot) —
    // a single narrative "shot" can itself span multiple raw SHOT/palette-slot chunks.
    if (m_options.transitionsOn) {
        if (!action.movieShots.empty()) {
            // Deferred/conditional VAR(47) form: e.g. a wingman's own
            // reaction movie is "win or loss opener, then a shared closer"
            // (see ActionResult::movieShots) — play every shot in order,
            // not just the first (which is all movieShot alone gives you).
            for (int shot : action.movieShots)
                playNamedMovieShot(action.movieName, shot);
        } else if (!action.movieName.empty()) {
            playNamedMovieShot(action.movieName, action.movieShot);
        } else if (action.transitionShot >= 0) {
            playSC205Shot(action.transitionShot);
        }
    }

    if (action.type == WC3Scene::ActionResult::GoRoom) {
        int stageIdx = scene->scenRoomToStageIndex(action.target);
        if (stageIdx >= 0) {
            // Captured before setRoom() below overwrites "current" — the
            // room the player is about to leave, used to seed
            // m_terminalOriginRoom if this GoRoom lands on the terminal
            // (room 0008), so its "logoff" can return here later instead
            // of a fixed room.
            int fromStageIdx = scene->getCurrentRoom();
            std::string fromRoomId = (fromStageIdx >= 0 && fromStageIdx < scene->getRoomCount())
                ? scene->getStageData().rooms[fromStageIdx].id : std::string();

            // Any actor talked to during this room visit is now leaving for
            // good — whether the player is headed to another ordinary room
            // or into a terminal-type one (Loadout Terminal/Simulator/
            // Loadout/Kill Board are just GoRoom targets 8/9/10/11, handled
            // uniformly by this same branch). See m_talkedToActorsThisVisit/
            // m_hiddenActorsByRoom's own comments.
            if (!m_talkedToActorsThisVisit.empty() && fromStageIdx >= 0) {
                auto& hidden = m_hiddenActorsByRoom[fromStageIdx];
                hidden.insert(m_talkedToActorsThisVisit.begin(), m_talkedToActorsThisVisit.end());
            }
            m_talkedToActorsThisVisit.clear();

            auto getShapeFn = [this](int id){ return this->getShape(id); };
            playPendingMovies(scene->runRoomExit(scene->getCurrentRoom(), getShapeFn));
            for (auto& [id, img] : shapeCache) delete img;
            shapeCache.clear();
            m_focusZoneIdx = -1;
            scene->setRoom(stageIdx, getShapeFn);
            loadRoomPalette(stageIdx);
            playMusicForRoom(scene->getStageData().rooms[stageIdx].id);
            playPendingMovies(scene->runRoomInit(stageIdx, getShapeFn));
            // runRoomInit() just wiped WC3Scene's own gumpVisible overrides
            // (it clears them on every room entry) — reapply any actors
            // previously hidden in this room so they stay gone on return.
            // runRoomInit() also already rebuilt the zone list once, from
            // that fresh (not-yet-rehidden) visibility — reapplying
            // setGumpVisible() alone stops the actor from rendering but
            // leaves its zone entry stale and still clickable, so
            // buildZones() has to run again after, same as runRoomInit()'s
            // own comment describes for INIT's VAR(55)/(56) calls.
            auto hiddenIt = m_hiddenActorsByRoom.find(stageIdx);
            if (hiddenIt != m_hiddenActorsByRoom.end() && !hiddenIt->second.empty()) {
                for (uint32_t hiddenGumpId : hiddenIt->second) {
                    scene->setGumpVisible(hiddenGumpId, false);
                }
                scene->buildZones(getShapeFn);
            }
            logRoomMenu();

            // Room 0009 is the Simulator terminal — its own ACTV bytecode
            // can't drive the mission list/detail/launch UI (see the
            // State::SIMULATOR comment in WC3GameFlow.h), so take over
            // input/rendering with a custom overlay the moment we land here.
            if (action.target == 9) {
                m_simShowDetail = false;
                m_simSelectedIndex = 0;
                m_simPendingReturn = false;
                state = State::SIMULATOR;
            }

            // Room 0005 is the lift — see m_liftSelectedFloor's comment for
            // why its floor buttons/Exit are driven explicitly instead of
            // through this room's own ACTV bytecode.
            m_inLift = (action.target == 5);
            if (m_inLift) enterLiftRoom();

            // Room 0008 is the personnel/"Loadout" Terminal — same reasoning
            // as the Lift/Simulator above, see the TerminalPage comment.
            m_inTerminal = (action.target == 8);
            if (m_inTerminal) {
                if (!fromRoomId.empty()) m_terminalOriginRoom = atoi(fromRoomId.c_str());
                enterTerminalRoom();
            }

            // Room 0011 is the Kill Board.
            m_inKillBoard = (action.target == 11);
            if (m_inKillBoard) enterKillBoardRoom();

            // Room 0010 is the real ship/weapon Loadout screen.
            m_inLoadout = (action.target == 10);
            if (m_inLoadout) enterLoadoutRoom();
        } else {
            printf("WC3: GoRoom %d — no matching stage room\n", action.target);
        }
    }

    if (action.launchMission)
        launchMission();
}

// Applies one branch choice's worth of consequences — see kBranchConsequences
// (defined locally in AWAITING_BRANCH_CHOICE's commitChoice, the only
// caller) for what these effects actually mean per entry. Called after
// finishGumpAction() so a missionIndexOverride can't be clobbered by that
// call's own choice-independent action.missionIndex handling — see
// commitChoice's own comment for the full reasoning (same ordering
// constraint the old isExcaliburChoice special case already required).
void WC3GameFlow::applyBranchEffects(const std::vector<WC3BranchEffect>& effects) {
    for (const auto& fx : effects) {
        if (fx.pilot != WC3Pilot::Count && fx.pilotMoraleDelta != 0)
            m_pilotMorale[(size_t)fx.pilot] += fx.pilotMoraleDelta;
        if (fx.shipMoraleDelta != 0)
            applyShipMoraleDelta(fx.shipMoraleDelta);
        if (fx.rosterPilot != WC3Pilot::Count && fx.rosterOverride != WC3RosterOverride::None)
            m_pilotRosterOverride[(size_t)fx.rosterPilot] = fx.rosterOverride;
        if (fx.relationshipPilot != WC3Pilot::Count && fx.relationshipStatus >= 0)
            m_pilotRelationshipStatus[(size_t)fx.relationshipPilot] = fx.relationshipStatus;
        if (fx.missionIndexOverride >= 0)
            m_selectedMissionIndex = fx.missionIndexOverride;
    }
}

// User-confirmed rule: a ship-wide morale change also nudges every
// individual character's own morale by the same amount, on top of
// whatever else a given effect/event carries. Public (see the header
// comment) so WC3Strike::onMissionEnded() can also reach it directly for
// in-flight mission events (e.g. Orsini4's escort outcome), not just
// applyBranchEffects' own dialogue-choice ship-morale effects.
void WC3GameFlow::applyShipMoraleDelta(int delta) {
    if (delta == 0) return;
    m_shipMorale += delta;
    for (size_t p = 0; p < (size_t)WC3Pilot::Count; p++)
        m_pilotMorale[p] += delta;
}

void WC3GameFlow::setLiftButtonHighlight(int floorIndex) {
    for (int i = 0; i < 3; i++)
        scene->setGumpPinnedFrame(kLiftFloorButtonGump[i], i == floorIndex ? 1 : 0);
}

void WC3GameFlow::setLiftSignFrame(int floorIndex) {
    scene->setGumpPinnedFrame(5, floorIndex); // sign: 3 frames, one per floor
}

void WC3GameFlow::playLiftSound(uint32_t soundActorGumpId) {
    const auto* sids = scene->getActorSoundIds(soundActorGumpId);
    if (sids && !sids->empty()) playGameflowSample((*sids)[0]);
}

void WC3GameFlow::playLiftTravelMovie(bool goingUp) {
    MusicPauseScope musicGuard(this);
    const char* assetPath = "..\\..\\DATA\\MOVIES\\LIFT.MVE";
    int groupIndex = goingUp ? 0 : 1;

    TreEntry* entry = Assets.GetEntryByName(assetPath);
    if (entry) {
        WC3MVEPlayer::playBranchGroup(entry->data, entry->size, groupIndex);
        return;
    }

    static const char* cdTres[] = {
        "cd1movie.tre", "cd2movie.tre", "cd3movie.tre", "cd4movie.tre",
        "cd1miss.tre",  "cd2miss.tre",  "cd3miss.tre",  "cd4miss.tre",
        nullptr
    };
    for (const char** tre = cdTres; *tre; tre++) {
        TreEntry* e = XtreArchive::LoadSingleEntry(*tre, assetPath);
        if (!e) continue;
        WC3MVEPlayer::playBranchGroup(e->data, e->size, groupIndex);
        delete[] e->data;
        delete e;
        return;
    }

    printf("WC3: movie not found: LIFT.MVE\n");
}

void WC3GameFlow::enterLiftRoom() {
    int arrivalRoomId = scene->getBankA(4);
    int arrivalFloor = 1; // default Living, matches the room's own INIT "else" case
    if (arrivalRoomId == 3) arrivalFloor = 0;
    else if (arrivalRoomId == 0) arrivalFloor = 2;
    m_liftSelectedFloor = arrivalFloor;
    m_liftPhase = LiftPhase::Idle;
    setLiftButtonHighlight(arrivalFloor);
    setLiftSignFrame(arrivalFloor); // already there — no travel animation to wait for

    // Resting state — nothing animates until the player actually clicks a
    // floor button or Exit LIFT.
    scene->setGumpVisible(8, false);
    scene->setGumpVisible(9, false);
    scene->setGumpVisible(10, false);
    scene->setGumpVisible(11, false);
    scene->setGumpVisible(13, false);
    scene->setGumpVisible(12, true); // Exit LIFT usable immediately
}

void WC3GameFlow::selectLiftFloor(int floorIndex) {
    if (m_liftPhase != LiftPhase::Idle) return; // already mid-travel/door — ignore
    if (floorIndex == m_liftSelectedFloor) return; // reselecting the current floor — no travel
    bool goingUp = floorIndex > m_liftSelectedFloor; // higher index = toward Bridge (top)

    // LIFT.MVE plays in full (blocking) before the room's own travel-
    // indicator gump animation/sign change below — same up/down branch
    // split as the gump animation that follows it.
    playLiftTravelMovie(goingUp);

    m_liftSelectedFloor = floorIndex;
    m_liftPhase = LiftPhase::Traveling;
    // Button highlight reflects the newly-requested floor right away; the
    // sign only updates once travel actually finishes — see
    // handleLiftAnimFinished().
    setLiftButtonHighlight(floorIndex);

    // Travel-indicator animation, left side of the lift: gumps 8/9 play
    // when heading up toward Bridge, 10/11 when heading down toward
    // Flight — confirmed directionality from the ACTV disassembly
    // (triggerAnim(8) fires on every "not already there" Bridge click,
    // triggerAnim(10) on every "not already there" Flight click). 8/10 are
    // armed for completion (handleLiftAnimFinished watches for them); 9/11
    // are purely visual companions, not watched.
    auto getShapeFn = [this](int id) { return this->getShape(id); };
    scene->setGumpVisible(8, goingUp);
    scene->setGumpVisible(9, goingUp);
    scene->setGumpVisible(10, !goingUp);
    scene->setGumpVisible(11, !goingUp);
    scene->setGumpVisible(13, false);
    scene->triggerGumpAnim(goingUp ? 8 : 10, getShapeFn, /*armCompletionPoll=*/true);
    scene->triggerGumpAnim(goingUp ? 9 : 11, getShapeFn, /*armCompletionPoll=*/false);
    playLiftSound(16); // "start travel" — matches gump 2/3/4's own OP50(16,0)
}

void WC3GameFlow::exitLift() {
    if (m_liftPhase != LiftPhase::Idle) return; // already mid-travel/door — ignore
    m_liftPhase = LiftPhase::DoorOpening;
    auto getShapeFn = [this](int id) { return this->getShape(id); };
    scene->setGumpVisible(13, true);
    scene->triggerGumpAnim(13, getShapeFn, /*armCompletionPoll=*/true);
    playLiftSound(15); // "door" — matches gump 2/3/4/12's own OP50(15,0) re-open case
}

bool WC3GameFlow::handleLiftAnimFinished(uint32_t gumpId) {
    if (m_liftPhase == LiftPhase::Traveling && (gumpId == 8 || gumpId == 10)) {
        setLiftSignFrame(m_liftSelectedFloor);
        m_liftPhase = LiftPhase::Idle;
        // No sound here — per user feedback, the "door" sample should only
        // play when the player actually clicks Exit LIFT (see exitLift()),
        // not automatically once travel/level-change finishes.
        return true;
    }
    if (m_liftPhase == LiftPhase::DoorOpening && gumpId == 13) {
        m_liftPhase = LiftPhase::Idle;
        int floor = (m_liftSelectedFloor >= 0 && m_liftSelectedFloor <= 2) ? m_liftSelectedFloor : 1;
        WC3Scene::ActionResult action;
        action.type = WC3Scene::ActionResult::GoRoom;
        action.target = kLiftFloorRoomId[floor];
        finishGumpAction(action);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Personnel/"Loadout" Terminal (room 0008) — Login, Hub, Duty Logs
// ---------------------------------------------------------------------------

// Bare relative filename, same directory-resolution convention as the
// existing wc3save.dat check (WC3GameFlow.cpp's boot-time savegame_exists
// stat) — both relative to whatever directory the process was launched
// from, not the ./assets/ subfolder config.ini uses.
static constexpr const char* kProfilePath = "wc3profile.dat";

void WC3GameFlow::loadProfile() {
    FILE* f = fopen(kProfilePath, "rb");
    if (!f) return; // no profile yet — m_profile keeps its defaults (never logged in)
    uint8_t everLoggedIn = 0;
    uint32_t nameLen = 0;
    if (fread(&everLoggedIn, 1, 1, f) == 1 && fread(&nameLen, sizeof(nameLen), 1, f) == 1 &&
        nameLen < 256) {
        std::string name(nameLen, '\0');
        if (fread(name.data(), 1, nameLen, f) == nameLen) {
            m_profile.everLoggedIn = (everLoggedIn != 0);
            m_profile.callsign = name;
        }
    }
    fclose(f);
}

void WC3GameFlow::saveProfile() {
    FILE* f = fopen(kProfilePath, "wb");
    if (!f) { printf("WC3: failed to write %s\n", kProfilePath); return; }
    uint8_t everLoggedIn = m_profile.everLoggedIn ? 1 : 0;
    uint32_t nameLen = (uint32_t)m_profile.callsign.size();
    fwrite(&everLoggedIn, 1, 1, f);
    fwrite(&nameLen, sizeof(nameLen), 1, f);
    fwrite(m_profile.callsign.data(), 1, nameLen, f);
    fclose(f);
}

void WC3GameFlow::applyTerminalPageVisibility() {
    if (!scene) return;
    for (uint32_t gid : kTermAllPageGumps) scene->setGumpVisible(gid, false);

    const uint32_t* activeGumps = nullptr;
    size_t activeCount = 0;
    switch (m_terminalPage) {
        case TerminalPage::Login:    activeGumps = kTermLoginGumps;    activeCount = std::size(kTermLoginGumps);    break;
        case TerminalPage::Hub:      activeGumps = kTermHubGumps;      activeCount = std::size(kTermHubGumps);      break;
        case TerminalPage::DutyLogs: activeGumps = kTermDutyLogsGumps; activeCount = std::size(kTermDutyLogsGumps); break;
    }
    for (size_t i = 0; i < activeCount; i++) scene->setGumpVisible(activeGumps[i], true);
}

void WC3GameFlow::enterTerminalRoom() {
    m_terminalPage = m_profile.everLoggedIn ? TerminalPage::Hub : TerminalPage::Login;

    if (m_terminalPage == TerminalPage::Login && !m_loginEditor && m_keyboard) {
        m_loginEditor = m_keyboard->createTextEditor();
    }
    if (m_loginEditor) m_loginEditor->setActive(m_terminalPage == TerminalPage::Login);

    applyTerminalPageVisibility();
}

void WC3GameFlow::commitLogin() {
    if (!m_loginEditor || m_loginEditor->getText().empty()) return;
    m_profile.callsign = m_loginEditor->getText();
    m_profile.everLoggedIn = true;
    saveProfile();
    m_loginEditor->setActive(false);
    m_terminalPage = TerminalPage::Hub;
    applyTerminalPageVisibility();
}

void WC3GameFlow::renderLoginPage() {
    if (!scene) return;

    // Fallback matches the real gump's own CORD, in case the lookup ever fails.
    int bx = 135, by = 181, bw = 395, bh = 98;
    int roomIdx = scene->getCurrentRoom();
    if (roomIdx >= 0 && roomIdx < scene->getRoomCount())
        findActorRect(scene->getStageData().rooms[roomIdx].actors, kTermTextInputGump, bx, by, bw, bh);
    by += kTerminalYOffset; // see kTerminalYOffset's own comment

    drawCursor(0, 0);
    displaySceneFramebuffer(1.0f, /*drawHoverLabel=*/false,
        [&](uint32_t* rgba, int fbW, int fbH) {
            WC3Font* font = WC3Globals::getInstance().getFont("GFMD");
            if (!font || !font->isLoaded() || !m_loginEditor) return;
            constexpr uint32_t kWhite = 0xFFFFFFFFu;
            font->drawTextRGBA(rgba, fbW, fbH, m_loginEditor->getDisplayText(), bx, by, kWhite);
        });
}

// Stubs — implemented in later phases (Options/Duty-Logs/Kill-Board/Loadout);
// declared and wired up now so the terminal's page-switching compiles and
// runs end-to-end even before those phases land.
void WC3GameFlow::openOptionsActivity() {
    if (!Game) return;
    // Same instance either way (the Hub's "controls" tab or a global Alt+O
    // during flight) — one Options implementation, pushed on top of
    // whatever's currently running and popped via its own "return"/ESC.
    auto* activity = new WC3OptionsActivity(&m_options);
    activity->init();
    Game->addActivity(activity);
}
void WC3GameFlow::dutyLogsLoad() {
    WC3SaveGame save;
    if (!loadSaveGame(m_dutyLogsSelectedSlot, save)) {
        printf("WC3: duty logs slot %d is empty, nothing to load\n", m_dutyLogsSelectedSlot);
        return;
    }
    current_scene = save.currentScene;
    m_pilotStats = save.pilots;
    m_loadoutShipIndex = save.loadoutShipIndex;
    m_loadoutHardpoints = save.loadoutHardpoints;
    // Direct assignment, unlike bankA/bankB below — nothing else ever resets
    // these members, so (unlike bank vars) there's no loadScene()-clobber to
    // dodge via m_hasPendingRestore/m_pendingRestoreBankA/B's deferred-apply
    // dance. See m_pilotRosterOverride's own comment for why this state
    // isn't stored in bank vars in the first place.
    m_shipMorale = save.shipMorale;
    m_pilotMorale = save.pilotMorale;
    m_pilotRelationshipStatus = save.pilotRelationshipStatus;
    for (size_t p = 0; p < (size_t)WC3Pilot::Count; p++)
        m_pilotRosterOverride[p] = (WC3RosterOverride)save.pilotRosterOverride[p];
    m_pendingRestoreBankA = save.bankA;
    m_pendingRestoreBankB = save.bankB;
    m_hasPendingRestore = true;
    m_terminalPage = TerminalPage::Hub; // settle the terminal before the reload takes over rendering
    state = State::LOAD_SCENE;
}

void WC3GameFlow::dutyLogsSave(const std::string& name) {
    WC3SaveGame save;
    save.currentScene = current_scene;
    save.name = name;
    if (scene && scene->getCurrentRoom() >= 0 && scene->getCurrentRoom() < scene->getRoomCount())
        save.roomId = scene->getStageData().rooms[scene->getCurrentRoom()].id;
    if (scene) {
        for (auto& [slot, val] : scene->getAllBankA()) save.bankA[slot] = val;
        for (auto& [slot, val] : scene->getAllBankB()) save.bankB[slot] = val;
    }
    save.pilots = m_pilotStats;
    save.loadoutShipIndex = m_loadoutShipIndex;
    save.loadoutHardpoints = m_loadoutHardpoints;
    save.shipMorale = m_shipMorale;
    save.pilotMorale = m_pilotMorale;
    save.pilotRelationshipStatus = m_pilotRelationshipStatus;
    for (size_t p = 0; p < (size_t)WC3Pilot::Count; p++)
        save.pilotRosterOverride[p] = (uint8_t)m_pilotRosterOverride[p];
    if (writeSaveGame(m_dutyLogsSelectedSlot, save))
        printf("WC3: saved duty logs slot %d (\"%s\")\n", m_dutyLogsSelectedSlot, name.c_str());
    else
        printf("WC3: failed to save duty logs slot %d\n", m_dutyLogsSelectedSlot);
}

void WC3GameFlow::renderDutyLogsPage() {
    if (!scene) return;

    RSScreen& screenDL = RSScreen::instance();
    EventManager& eventsDL = EventManager::getInstance();
    int mxDL, myDL;
    eventsDL.getMousePosition(mxDL, myDL);
    int vxDL, vyDL;
    screenDL.windowToLogical(mxDL, myDL, 640, 480, vxDL, vyDL);
    m_hoveredZoneType = 0;
    m_hoveredIsRoomChange = false;
    drawCursor(vxDL, vyDL);

    const std::vector<WC3Gump>* actors = nullptr;
    int roomIdx = scene->getCurrentRoom();
    if (roomIdx >= 0 && roomIdx < scene->getRoomCount())
        actors = &scene->getStageData().rooms[roomIdx].actors;

    auto rectOf = [&](uint32_t gid, int fx, int fy, int fw, int fh) {
        int x = fx, y = fy, w = fw, h = fh;
        if (actors) findActorRect(*actors, gid, x, y, w, h);
        if (w <= 0) w = 64;
        if (h <= 0) h = 32;
        y += kTerminalYOffset; // see kTerminalYOffset's own comment
        return std::array<int, 4>{x, y, w, h};
    };
    auto loadRect   = rectOf(16, 31, 375, 64, 32);
    auto saveRect   = rectOf(17, 194, 376, 64, 32);
    auto returnRect = rectOf(18, 408, 376, 64, 32);
    auto upRect     = rectOf(19, 51, 93, 32, 47);
    auto downRect   = rectOf(21, 51, 248, 32, 47);

    constexpr int listX = 90, listY0 = 108 + kTerminalYOffset, listLineH = 24, kVisibleRows = 11;
    // No real gump exists for a name-entry box (this UI wasn't part of the
    // original room's own art) — a plain centered text prompt, same
    // minimalist-text approach already used for Options/Kill Board's own
    // added UI, rather than inventing hand-drawn box art.
    constexpr int nameBoxX = 160, nameBoxLabelY = 200 + kTerminalYOffset, nameBoxTextY = 224 + kTerminalYOffset;

    displaySceneFramebuffer(1.0f, /*drawHoverLabel=*/false,
        [&](uint32_t* rgba, int fbW, int fbH) {
            // GFSM ("gump font small", 9px) — GFMD (17px) is far too tall
            // for a dense 24px-per-row slot list, same reasoning as Kill
            // Board's table and the Simulator's mission list.
            WC3Font* font = WC3Globals::getInstance().getFont("GFSM");
            if (!font || !font->isLoaded()) return;
            constexpr uint32_t kWhite = 0xFFFFFFFFu;
            constexpr uint32_t kGrey  = 0xFF808080u;
            for (int row = 0; row < kVisibleRows; row++) {
                int slot = m_dutyLogsScrollOffset + row;
                if (slot > kNumSaveSlots) break;
                int y = listY0 + row * listLineH;
                int peekedScene; std::string peekedRoom, peekedName;
                std::string label;
                if (peekSaveGameHeader(slot, peekedScene, peekedRoom, peekedName)) {
                    label = "GAME" + std::to_string(slot);
                    if (!peekedName.empty()) label += " " + peekedName;
                } else {
                    label = "EMPTY.";
                }
                font->drawTextRGBA(rgba, fbW, fbH, label, listX, y,
                                    (slot == m_dutyLogsSelectedSlot) ? kWhite : kGrey);
            }

            if (m_dutyLogsNaming && m_dutyLogsSaveNameEditor) {
                font->drawTextRGBA(rgba, fbW, fbH,
                    "SAVE GAME " + std::to_string(m_dutyLogsSelectedSlot) + " AS:",
                    nameBoxX, nameBoxLabelY, kWhite);
                font->drawTextRGBA(rgba, fbW, fbH,
                    "[ " + m_dutyLogsSaveNameEditor->getDisplayText() + " ]",
                    nameBoxX, nameBoxTextY, kWhite);
            }
        });

    if (m_dutyLogsNaming) {
        // Short-circuits the normal list/button click handling entirely
        // while typing, same reasoning as the Login page's own text-entry
        // short-circuit — only Enter (commit) and Escape (cancel) apply.
        if (m_keyboard && m_keyboard->isKeyJustPressed(SDL_SCANCODE_RETURN)) {
            dutyLogsSave(m_dutyLogsSaveNameEditor->getText());
            m_dutyLogsSaveNameEditor->setActive(false);
            m_dutyLogsNaming = false;
        } else if (m_keyboard && m_keyboard->isKeyJustPressed(SDL_SCANCODE_ESCAPE)) {
            m_dutyLogsSaveNameEditor->setActive(false);
            m_dutyLogsNaming = false;
        }
        return;
    }

    if (eventsDL.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
        auto hit = [&](const std::array<int, 4>& r) {
            return vxDL >= r[0] && vxDL < r[0] + r[2] && vyDL >= r[1] && vyDL < r[1] + r[3];
        };
        if (hit(loadRect)) {
            dutyLogsLoad();
        } else if (hit(saveRect)) {
            if (m_keyboard) {
                if (!m_dutyLogsSaveNameEditor) m_dutyLogsSaveNameEditor = m_keyboard->createTextEditor();
                // Seed with the slot's existing name (if any) so re-saving
                // over a named slot doesn't silently blank the name out —
                // the player can still clear it deliberately.
                int peekedScene; std::string peekedRoom, peekedName;
                peekSaveGameHeader(m_dutyLogsSelectedSlot, peekedScene, peekedRoom, peekedName);
                m_dutyLogsSaveNameEditor->setText(peekedName);
                SDL_Rect inputRect{nameBoxX, nameBoxTextY, 320, 24};
                m_dutyLogsSaveNameEditor->setActive(true, &inputRect);
                m_dutyLogsNaming = true;
            }
        } else if (hit(returnRect)) {
            m_terminalPage = TerminalPage::Hub;
            applyTerminalPageVisibility();
        } else if (hit(upRect)) {
            if (m_dutyLogsScrollOffset > 1) m_dutyLogsScrollOffset--;
        } else if (hit(downRect)) {
            if (m_dutyLogsScrollOffset + kVisibleRows <= kNumSaveSlots) m_dutyLogsScrollOffset++;
        } else {
            for (int row = 0; row < kVisibleRows; row++) {
                int slot = m_dutyLogsScrollOffset + row;
                if (slot > kNumSaveSlots) break;
                int y = listY0 + row * listLineH;
                if (vxDL >= listX && vxDL < listX + 300 && vyDL >= y && vyDL < y + listLineH) {
                    m_dutyLogsSelectedSlot = slot;
                    break;
                }
            }
        }
    }
}

void WC3GameFlow::enterKillBoardRoom() {
    // Seed once — if a Duty Logs load has never populated this (or no save
    // has ever been made), start from the reference starting values: ACES=0
    // for everyone, KILLS matching wc3_003.png/wc3_004.png exactly. Fixed
    // order preserved; the player's own row (if logged in) is always
    // appended last, not stored here (see renderKillBoardPage()).
    if (m_pilotStats.empty()) {
        m_pilotStats = {
            {"Cobra", 0, 5},
            {"Vagabond", 0, 10},
            {"Hobbes", 0, 0},
            {"Vaquero", 0, 8},
            {"Maniac", 0, 9},
            {"Flint", 0, 3},
        };
    }
}

void WC3GameFlow::renderKillBoardPage() {
    if (!scene) return;

    RSScreen& screenKB = RSScreen::instance();
    EventManager& eventsKB = EventManager::getInstance();
    int mxKB, myKB;
    eventsKB.getMousePosition(mxKB, myKB);
    int vxKB, vyKB;
    screenKB.windowToLogical(mxKB, myKB, 640, 480, vxKB, vyKB);
    m_hoveredZoneType = 0;
    m_hoveredIsRoomChange = false;
    drawCursor(vxKB, vyKB);

    const std::vector<WC3Gump>* actors = nullptr;
    int roomIdx = scene->getCurrentRoom();
    if (roomIdx >= 0 && roomIdx < scene->getRoomCount())
        actors = &scene->getStageData().rooms[roomIdx].actors;

    int tableX = 100, tableY = 130, tableW = 460;
    if (actors) { int tw, th; findActorRect(*actors, 4294967286u, tableX, tableY, tableW, th); (void)tw; (void)th; }
    tableY += kTerminalYOffset; // see kTerminalYOffset's own comment
    int logoffX = 406, logoffY = 397, logoffW = 64, logoffH = 32;
    if (actors) {
        // Confirmed via a data dump of room 0011's own actors: gump id 2 is
        // TYPE_BUTTON (a real logoff button), but its CORD only carries a
        // position (w=0, h=0) — button sprites have no size of their own in
        // this data, unlike the content-box gumps findActorRect is normally
        // used for (e.g. the CALLSIGN/ACES/KILLS table below). Passing
        // logoffW/logoffH straight through was overwriting the hardcoded
        // 64x32 fallback with that real 0x0, shrinking the click region to
        // nothing — hence "the logoff button isn't working" even though its
        // rendered position (x/y) was already correct. Take x/y only.
        int unusedW, unusedH;
        findActorRect(*actors, 2, logoffX, logoffY, unusedW, unusedH);
    }
    logoffY += kTerminalYOffset;

    bool loggedIn = m_profile.everLoggedIn && !m_killBoardLoggedOut;

    displaySceneFramebuffer(1.0f, /*drawHoverLabel=*/false,
        [&](uint32_t* rgba, int fbW, int fbH) {
            // GFSM ("gump font small", 9px) for the dense table rows —
            // GFMD (17px) was oversized for a multi-row list like this.
            WC3Font* font = WC3Globals::getInstance().getFont("GFSM");
            if (!font || !font->isLoaded()) return;
            constexpr uint32_t kWhite = 0xFFFFFFFFu;
            int lineH = font->getHeight() + 6;
            int y = tableY;
            int colCallsign = tableX, colAces = tableX + 260, colKills = tableX + 360;

            font->drawTextRGBA(rgba, fbW, fbH, "CALLSIGN", colCallsign, y);
            font->drawTextRGBA(rgba, fbW, fbH, "ACES", colAces, y);
            font->drawTextRGBA(rgba, fbW, fbH, "KILLS", colKills, y);
            y += lineH;

            int total = 0;
            for (auto& p : m_pilotStats) {
                font->drawTextRGBA(rgba, fbW, fbH, p.name, colCallsign, y);
                font->drawTextRGBA(rgba, fbW, fbH, std::to_string(p.aces), colAces, y);
                font->drawTextRGBA(rgba, fbW, fbH, std::to_string(p.kills), colKills, y);
                total += p.kills;
                y += lineH;
            }
            if (loggedIn) {
                font->drawTextRGBA(rgba, fbW, fbH, m_profile.callsign, colCallsign, y);
                font->drawTextRGBA(rgba, fbW, fbH, "0", colAces, y);
                font->drawTextRGBA(rgba, fbW, fbH, "0", colKills, y);
            } else {
                font->drawTextRGBA(rgba, fbW, fbH, "NOT LOGGED IN", colCallsign, y);
            }
            y += lineH * 2;
            font->drawTextRGBA(rgba, fbW, fbH, "TOTAL KITTIES WHACKED: " + std::to_string(total),
                                colCallsign, y, kWhite);
        });

    if (eventsKB.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
        if (vxKB >= logoffX && vxKB < logoffX + logoffW && vyKB >= logoffY && vyKB < logoffY + logoffH) {
            // "logoff": Login is one-shot-forever (see the terminal-screens
            // plan), so this can't clear the persisted callsign/profile —
            // it only flips the Kill Board to its guest view for the rest
            // of this session, then returns to the Mess Hall (room 0006 —
            // confirmed via its background, shape 57, matching todo.MD's
            // established "Mess Hall's own background, 057.SHP" record).
            m_killBoardLoggedOut = true;
            WC3Scene::ActionResult action;
            action.type = WC3Scene::ActionResult::GoRoom;
            action.target = 6;
            finishGumpAction(action);
        }
    }
}

void WC3GameFlow::ensureLoadoutDataLoaded() {
    if (m_loadoutDb.loaded) return;
    TreEntry* e = Assets.GetEntryByName("..\\..\\DATA\\GAMEFLOW\\LOADOUT.IFF");
    if (!e) { printf("WC3GameFlow: WARNING: LOADOUT.IFF not found\n"); return; }
    if (!m_loadoutDb.loadFromBytes(e->data, e->size))
        printf("WC3GameFlow: WARNING: failed to parse LOADOUT.IFF\n");
    else
        printf("WC3GameFlow: LOADOUT.IFF loaded: %zu craft, %zu weapons\n",
               m_loadoutDb.craft.size(), m_loadoutDb.weapons.size());
}

void WC3GameFlow::resetLoadoutHardpoints(int shipIndex) {
    if (shipIndex < 0 || shipIndex >= (int)m_loadoutDb.craft.size()) return;
    const auto& hardpoints = m_loadoutDb.craft[shipIndex].hardpoints;
    m_loadoutHardpoints.assign(hardpoints.size(), WC3LoadoutHardpointState{});
    for (size_t i = 0; i < m_loadoutHardpoints.size(); i++) {
        // Default: fully loaded with the first weapon compatible with this
        // hardpoint's mask (e.g. Arrow's hardpoints skip DUMBFIRE — mask 6
        // only admits HEATSEEK/IMAGE REC) — matches every reference
        // screenshot showing "LOADED: N/N" (full) on first view.
        m_loadoutHardpoints[i].missileTypeIndex = m_loadoutDb.firstCompatibleWeapon(hardpoints[i]);
        m_loadoutHardpoints[i].loadedCount = hardpoints[i].capacity;
    }
    m_loadoutSelectedHardpoint = 0;
}

void WC3GameFlow::enterLoadoutRoom() {
    ensureLoadoutDataLoaded();
    if (m_loadoutDb.craft.empty()) return;
    if (m_loadoutShipIndex < 0 || m_loadoutShipIndex >= (int)m_loadoutDb.craft.size()) m_loadoutShipIndex = 0;
    // Re-seed if this is a fresh session (never initialized) or a loaded
    // save's hardpoint count doesn't match the selected craft (shouldn't
    // normally happen, but keeps this robust against a hand-edited/stale
    // save file).
    if (m_loadoutHardpoints.size() != m_loadoutDb.craft[m_loadoutShipIndex].hardpoints.size())
        resetLoadoutHardpoints(m_loadoutShipIndex);
    if (m_loadoutSelectedHardpoint >= (int)m_loadoutHardpoints.size()) m_loadoutSelectedHardpoint = 0;
}

void WC3GameFlow::renderLoadoutPage() {
    if (!scene) return;
    ensureLoadoutDataLoaded();
    if (m_loadoutDb.craft.empty() || m_loadoutDb.weapons.empty()) return;

    auto& craftList = m_loadoutDb.craft;
    auto& weaponList = m_loadoutDb.weapons;
    if (m_loadoutShipIndex < 0 || m_loadoutShipIndex >= (int)craftList.size()) m_loadoutShipIndex = 0;
    const WC3CraftDef& craft = craftList[m_loadoutShipIndex];
    if (m_loadoutHardpoints.size() != craft.hardpoints.size()) resetLoadoutHardpoints(m_loadoutShipIndex);
    if (m_loadoutSelectedHardpoint < 0 || m_loadoutSelectedHardpoint >= (int)craft.hardpoints.size())
        m_loadoutSelectedHardpoint = 0;
    const WC3HardpointDef& hpDef = craft.hardpoints[m_loadoutSelectedHardpoint];
    WC3LoadoutHardpointState& hpState = m_loadoutHardpoints[m_loadoutSelectedHardpoint];
    if (hpState.missileTypeIndex < 0 || hpState.missileTypeIndex >= (int)weaponList.size())
        hpState.missileTypeIndex = m_loadoutDb.firstCompatibleWeapon(hpDef);
    // Cycles the selected hardpoint's missileTypeIndex by dir (+1/-1) among
    // only the weapons whose mask bit the hardpoint's own mask admits — a
    // hardpoint with just one compatible weapon (e.g. Excalibur's Temblor
    // Bomb/Cloaking Device bays) has nothing to cycle to and this is simply
    // a no-op, with no separate "special hardpoint" case required.
    auto cycleMissile = [&](int dir) {
        auto compat = m_loadoutDb.compatibleWeapons(hpDef);
        if (compat.empty()) return;
        int pos = 0;
        for (size_t i = 0; i < compat.size(); i++)
            if (compat[i] == hpState.missileTypeIndex) { pos = (int)i; break; }
        pos = ((pos + dir) % (int)compat.size() + (int)compat.size()) % (int)compat.size();
        hpState.missileTypeIndex = compat[pos];
    };

    RSScreen& screenLO = RSScreen::instance();
    EventManager& eventsLO = EventManager::getInstance();
    int mxLO, myLO;
    eventsLO.getMousePosition(mxLO, myLO);
    int vxLO, vyLO;
    screenLO.windowToLogical(mxLO, myLO, 640, 480, vxLO, vyLO);
    m_hoveredZoneType = 0;
    m_hoveredIsRoomChange = false;

    const std::vector<WC3Gump>* actors = nullptr;
    int roomIdx = scene->getCurrentRoom();
    if (roomIdx >= 0 && roomIdx < scene->getRoomCount())
        actors = &scene->getStageData().rooms[roomIdx].actors;

    auto rectOf = [&](uint32_t gid, int fx, int fy, int fw, int fh) {
        int x = fx, y = fy, w = fw, h = fh;
        if (actors) findActorRect(*actors, gid, x, y, w, h);
        if (w <= 0) w = 64;
        if (h <= 0) h = 32;
        return std::array<int, 4>{x, y, w, h};
    };
    // Left narrow panel: hardpoint info + top (hardpoint cycle) and bottom
    // (missile-type cycle) arrow pairs, plus +/-. Right wide panel: craft
    // name/render + the weapon-bank readout. Gump ids/positions confirmed
    // via a headless decode of room 0010's own SHAPES.PAK art — see the
    // Loadout section of the terminal-screens implementation plan.
    auto infoBoxRect    = rectOf(4294967288u, 73, 110, 102, 271);
    auto shipBoxRect     = rectOf(4294967289u, 200, 110, 371, 271);
    auto prevRect        = rectOf(10, 249, 373, 128, 32);
    auto nextRect        = rectOf(9, 408, 373, 128, 32);
    auto hardpointLeft    = rectOf(3, 58, 111, 32, 47);  // top-left  ◄ — cycle hardpoint
    auto hardpointRight   = rectOf(4, 89, 109, 32, 26);  // top-right ►
    auto missileLeft       = rectOf(5, 58, 319, 32, 47); // bottom-left  ◄ — cycle missile type
    auto missileRight      = rectOf(6, 89, 317, 32, 26); // bottom-right ►
    auto plusRect         = rectOf(7, 118, 304, 39, 35);
    auto minusRect        = rectOf(8, 118, 338, 40, 25);
    auto proceedRect     = rectOf(11, 41, 370, 128, 32);

    // Narrow panel's real top-to-bottom order (per the room's own gump
    // y-positions, confirmed via headless decode — hardpointLeft/Right sit
    // right at the panel's top edge (y~109-111, real sprite heights 37/38px
    // so the cluster's own visible bottom is ~148), missileLeft/Right +
    // plus/minus cluster at the panel's bottom (y~304-338):
    //   HARDPOINT:/LOADED: text  (inside the panel, just below its top edge
    //                             — sharing horizontal space with the
    //                             hardpoint arrows is unavoidable in a
    //                             102px-wide column already carrying two
    //                             arrow pairs and two more buttons)
    //   hardpoint ◄/► arrows     (already drawn — real room actors)
    //   weapon icon (184.SHP)
    //   name / type+tacval text
    //   missile ◄/► arrows + +/- (already drawn — real room actors)
    int weaponIconY = 155 + kTerminalYOffset; // see kTerminalYOffset's own comment

    // Ship render (183.SHP) fills the wide panel almost exactly (373x264 vs
    // this panel's 371x271 — confirmed via headless decode under room
    // 0010's own palette). Both this and the weapon icon must be blitted
    // into sceneFB (palette-index space) before displaySceneFramebuffer()'s
    // palette->RGBA conversion runs below — same mechanism WC3Scene::
    // render() uses for ordinary room actors.
    //
    // Every craft/weapon frame shares the same canvas size but not the same
    // silhouette/transparent margin, so switching selections without wiping
    // the panel first lets the outgoing sprite's opaque pixels show through
    // around the incoming one wherever the new frame is more "see-through"
    // than the old — wipe each panel to the background key immediately
    // before drawing its new sprite so no previous craft/weapon can bleed
    // through.
    // Ship render frame == this craft's own index into m_loadoutDb.craft
    // (which is in 183.SHP frame order by construction — see
    // WC3LoadoutData.h). Weapon big-icon frame == its own index into
    // m_loadoutDb.weapons (184.SHP is in the same ARMY-record order, all 9
    // entries including the two CLASSIFIED specials).
    fillSceneRect(shipBoxRect[0], shipBoxRect[1], shipBoxRect[2], shipBoxRect[3]);
    drawShapeIntoScene(183, m_loadoutShipIndex, shipBoxRect[0], shipBoxRect[1]);
    fillSceneRect(infoBoxRect[0], weaponIconY, infoBoxRect[2], 128);
    drawShapeIntoScene(184, hpState.missileTypeIndex, infoBoxRect[0], weaponIconY);

    // Small per-hardpoint icons (185.SHP), one per hardpoint, drawn at that
    // hardpoint's own real screen-space marker position (WC3HardpointDef::
    // cordX/cordY, read straight out of LOADOUT.IFF's CORD chunk — not a
    // layout guess), selected one framed with the "selected slot" chrome.
    // 185.SHP only has icons for the first 7 weapons (the ordinary missile
    // types); TEMBLOR BOMB/CLOAKING DEVICE (indices 7/8) have no small-icon
    // art, so hardpoints loaded with those just show the blank-slot frame
    // here — their name/state is spelled out in the narrow panel's text
    // instead when selected.
    for (size_t i = 0; i < craft.hardpoints.size(); i++) {
        const auto& def = craft.hardpoints[i];
        const auto& st = m_loadoutHardpoints[i];
        int iconFrame = kHardpointIconBlank;
        if (st.loadedCount > 0 && st.missileTypeIndex >= 0 && st.missileTypeIndex < 7)
            iconFrame = st.missileTypeIndex;
        // def.cordY is straight out of LOADOUT.IFF, unaware of
        // kTerminalYOffset — shift it here same as everything else drawn on
        // this screen.
        int hpY = def.cordY + kTerminalYOffset;
        drawShapeIntoScene(185, iconFrame, def.cordX, hpY);
        if ((int)i == m_loadoutSelectedHardpoint)
            drawShapeIntoScene(185, kHardpointIconSelectedSlot, def.cordX, hpY);
    }

    drawCursor(vxLO, vyLO);

    displaySceneFramebuffer(1.0f, /*drawHoverLabel=*/false,
        [&](uint32_t* rgba, int fbW, int fbH) {
            WC3Font* font = WC3Globals::getInstance().getFont("GFMD");
            // GFSM ("gump font small", 9px) — GFMD (17px) was oversized for
            // the hardpoint-index labels and the narrow (102px-wide) info
            // panel's multi-line readout; the craft name stays GFMD as a
            // real heading.
            WC3Font* fontSm = WC3Globals::getInstance().getFont("GFSM");
            if (!font || !font->isLoaded() || !fontSm || !fontSm->isLoaded()) return;
            constexpr uint32_t kWhite = 0xFFFFFFFFu;
            constexpr uint32_t kGrey  = 0xFF808080u;
            constexpr uint32_t kYellow = 0xFF00FFFFu; // r|g<<8|b<<16|a<<24 — full R+G, no B = yellow
            int lineH = fontSm->getHeight() + 4;

            // Craft name, large, top of the right panel.
            font->drawTextRGBA(rgba, fbW, fbH, craft.name, shipBoxRect[0] + 8, shipBoxRect[1] - 20, kWhite);

            // Alt+S toggles the SUBS spec-sheet text (TYPE/ARMOR/SHIELD/
            // SPEED/SERVICE/ARCHITECT — see WC3LoadoutData.h) underneath the
            // craft name, drawn directly over the ship render with no
            // backing rect.
            if (m_loadoutShowSubs && !craft.specLines.empty()) {
                int sx = shipBoxRect[0] + 8, sy = shipBoxRect[1];
                for (auto& line : craft.specLines) {
                    fontSm->drawTextRGBA(rgba, fbW, fbH, line, sx, sy, kWhite);
                    sy += lineH;
                }
            }

            // Hardpoint-index labels, one above each hardpoint's own real
            // marker position.
            for (size_t i = 0; i < craft.hardpoints.size(); i++) {
                std::string num = std::to_string(i + 1);
                const auto& def = craft.hardpoints[i];
                fontSm->drawTextRGBA(rgba, fbW, fbH, num, def.cordX + 2, def.cordY - 14 + kTerminalYOffset,
                                    ((int)i == m_loadoutSelectedHardpoint) ? kYellow : kGrey);
            }

            // Narrow panel, top to bottom (see the layout comment above
            // weaponIconY): HARDPOINT:/LOADED: text just inside the panel's
            // top edge (above the hardpoint ◄/► arrows, which are already
            // drawn as real room actors), then the weapon icon, then its
            // name/type/tacval readout, then the missile ◄/► + +/- cluster
            // (also already-drawn room actors).
            int hpTextLineH = fontSm->getHeight() + 2;
            int hpTextY = infoBoxRect[1] + 8;
            fontSm->drawTextRGBA(rgba, fbW, fbH,
                "HARDPOINT: " + std::to_string(m_loadoutSelectedHardpoint + 1), infoBoxRect[0], hpTextY, kWhite);
            hpTextY += hpTextLineH;
            // A single-capacity hardpoint (Excalibur's Temblor Bomb/Cloaking
            // Device bays, Thunderbolt's centre torpedo bay) reads as an
            // installed/not-installed toggle rather than a loaded count —
            // this falls directly out of capacity == 1, no special-hardpoint
            // flag needed.
            if (hpDef.capacity == 1) {
                fontSm->drawTextRGBA(rgba, fbW, fbH, hpState.loadedCount > 0 ? "INSTALLED" : "NOT INSTALLED",
                                    infoBoxRect[0], hpTextY, kWhite);
            } else {
                fontSm->drawTextRGBA(rgba, fbW, fbH,
                    "LOADED: " + std::to_string(hpState.loadedCount) + "/" + std::to_string(hpDef.capacity),
                    infoBoxRect[0], hpTextY, kWhite);
            }

            // Name/type/tacval, directly below the weapon icon. The 128px
            // icon plus these two lines exactly fills the narrow band
            // between the hardpoint arrows above and the missile arrows/
            // +/- cluster below, so type and tacval share one line rather
            // than each getting their own.
            const WC3WeaponDef& weapon = weaponList[hpState.missileTypeIndex];
            int weaponTextLineH = fontSm->getHeight();
            int wy = weaponIconY + 128 + 1;
            fontSm->drawTextRGBA(rgba, fbW, fbH, weapon.name, infoBoxRect[0], wy, kWhite);
            wy += weaponTextLineH;
            fontSm->drawTextRGBA(rgba, fbW, fbH, weapon.weaponClass + " / " + weapon.tacval,
                                infoBoxRect[0], wy, kGrey);
        });

    if (m_keyboard) {
        bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
        if (altHeld && m_keyboard->isKeyJustPressed(SDL_SCANCODE_S))
            m_loadoutShowSubs = !m_loadoutShowSubs;
    }

    if (eventsLO.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
        auto hit = [&](const std::array<int, 4>& r) {
            return vxLO >= r[0] && vxLO < r[0] + r[2] && vyLO >= r[1] && vyLO < r[1] + r[3];
        };
        int hpCount = (int)craft.hardpoints.size();
        if (hit(prevRect)) {
            m_loadoutShipIndex = (m_loadoutShipIndex - 1 + (int)craftList.size()) % (int)craftList.size();
            resetLoadoutHardpoints(m_loadoutShipIndex);
        } else if (hit(nextRect)) {
            m_loadoutShipIndex = (m_loadoutShipIndex + 1) % (int)craftList.size();
            resetLoadoutHardpoints(m_loadoutShipIndex);
        } else if (hit(hardpointLeft)) {
            m_loadoutSelectedHardpoint = (m_loadoutSelectedHardpoint - 1 + hpCount) % hpCount;
        } else if (hit(hardpointRight)) {
            m_loadoutSelectedHardpoint = (m_loadoutSelectedHardpoint + 1) % hpCount;
        } else if (hit(missileLeft)) {
            cycleMissile(-1);
        } else if (hit(missileRight)) {
            cycleMissile(+1);
        } else if (hit(plusRect)) {
            if (hpState.loadedCount < hpDef.capacity) hpState.loadedCount++;
        } else if (hit(minusRect)) {
            if (hpState.loadedCount > 0) hpState.loadedCount--;
        } else if (hit(proceedRect)) {
            // The in-memory state above already *is* the committed loadout
            // (nothing else to apply — no mission-launch weapon-application
            // system exists yet to hand it off to); this just leaves the
            // screen. Duty Logs' own "save" persists it beyond this session.
            WC3Scene::ActionResult action;
            action.type = WC3Scene::ActionResult::GoRoom;
            action.target = 3; // back to Flight Control
            finishGumpAction(action);
        }
    }
}

void WC3GameFlow::playMovie(const char* name) {
    MusicPauseScope musicGuard(this);
    char assetPath[128];
    snprintf(assetPath, sizeof(assetPath), "..\\..\\DATA\\MOVIES\\%s", name);

    // 1. Try the preloaded archives (movies.tre etc.)
    TreEntry* entry = Assets.GetEntryByName(assetPath);
    if (entry) {
        WC3MVEPlayer::play(entry->data, entry->size);
        return;
    }

    // 2. Search CD-specific TRE files on demand — only decompress the one file
    static const char* cdTres[] = {
        "cd1movie.tre", "cd2movie.tre", "cd3movie.tre", "cd4movie.tre",
        "cd1miss.tre",  "cd2miss.tre",  "cd3miss.tre",  "cd4miss.tre",
        nullptr
    };
    for (const char** tre = cdTres; *tre; tre++) {
        TreEntry* e = XtreArchive::LoadSingleEntry(*tre, assetPath);
        if (!e) continue;
        WC3MVEPlayer::play(e->data, e->size);
        delete[] e->data;
        delete e;
        return;
    }

    printf("WC3: movie not found: %s\n", name);
}

void WC3GameFlow::loadRoomPalette(int roomIndex) {
    if (!scene || roomIndex < 0 || roomIndex >= scene->getRoomCount())
        return;

    auto& room = scene->getStageData().rooms[roomIndex];
    if (room.palette_id < 0)
        return;

    PakEntry* palEntry = shapePak.GetEntry(room.palette_id);
    if (!palEntry || palEntry->size != 768)
        return;

    VGAPalette* pal = VGA.getPalette();
    for (int i = 0; i < 256; i++) {
        Texel t;
        t.r = convertFrom6To8(palEntry->data[i * 3 + 0]);
        t.g = convertFrom6To8(palEntry->data[i * 3 + 1]);
        t.b = convertFrom6To8(palEntry->data[i * 3 + 2]);
        t.a = (i == 255) ? 0 : 255;
        pal->SetColor(i, &t);
    }
}

void WC3GameFlow::loadScene(int sceneId) {
    char scenName[64];
    snprintf(scenName, sizeof(scenName), "..\\..\\DATA\\GAMEFLOW\\SCEN%04d.IFF", sceneId);

    TreEntry* scenEntry = Assets.GetEntryByName(scenName);
    if (!scenEntry) {
        printf("WC3GameFlow: Could not find %s\n", scenName);
        return;
    }

    // Derive stage filename from the SCEN's STGE field (e.g. "victory" → "VICTORY.IFF")
    std::string stageName = WC3Scene::extractStageName(scenEntry->data, scenEntry->size);
    if (stageName.empty()) stageName = "victory";
    for (auto& c : stageName) c = (char)toupper((unsigned char)c);

    char stagePath[128];
    snprintf(stagePath, sizeof(stagePath), "..\\..\\DATA\\GAMEFLOW\\%s.IFF", stageName.c_str());

    m_focusZoneIdx = -1;
    m_hoveredGumpId     = 0;
    m_hoveredZoneType   = 0;
    m_hoveredIsRoomChange = false;

    // scrn_id is just a small index into the current scene's own SHAPES.PAK
    // subset, freely reused by unrelated sprites across different scenes —
    // e.g. the mess hall's Rachel portrait in SCEN0000 can share a numeric
    // scrn_id with a completely different decorative sprite in some other
    // room under SCEN0001. Without clearing the cache here, a stale
    // scrn_id -> RSImageSet* entry from whatever scene was active before
    // this reload can get reused for the wrong sprite. Ordinary in-scene
    // room-to-room navigation already does this (see finishGumpAction's
    // GoRoom handling); a full scene reload needs it just as much.
    for (auto& [id, img] : shapeCache) delete img;
    shapeCache.clear();

    // Same staleness reasoning as shapeCache above: room indices aren't
    // stable across different SCEN files (VICTORY.IFF loads rooms out of ID
    // order per-scene — see the startRoom lookup below), so a hidden-actor
    // entry keyed by last scene's room index could wrongly hide an unrelated
    // actor in this new scene's room at the same index.
    m_talkedToActorsThisVisit.clear();
    m_hiddenActorsByRoom.clear();

    if (!scene) {
        scene = new WC3Scene();
        scene->setLookMovi(m_lookMovi);
    }

    // Load VICTORY.IFF (or whatever stage the SCEN references) first
    TreEntry* stageEntry = Assets.GetEntryByName(stagePath);
    if (stageEntry) {
        scene->loadStage(stageEntry->data, stageEntry->size);
    } else {
        printf("WC3GameFlow: Could not find stage %s\n", stagePath);
    }

    // Apply SCEN overrides on top of the loaded stage
    scene->loadScene(scenEntry->data, scenEntry->size);

    // Wingman-selection room (0007) INIT shows each pilot portrait only when
    // its own bank-B "available" flag is set (positive, not negated — e.g.
    // gid 5 (Hobbes) needs 146 AND NOT(138) AND NOT(157) to show; the other
    // two are secondary flags that default false anyway): 143=Flint(gid4),
    // 144=Flash(gid7), 145=Cobra(gid3), 146=Hobbes(gid5), 147=Vagabond(gid8),
    // 148=Vaquero(gid6), 149=Maniac(gid2). Real per-mission roster
    // availability is presumably computed by the still-undeciphered
    // squadron-roster opcodes (OP74/75/76/77) — searched for and NOT found
    // in the stage-level init_code (trivial, 44 bytes, no branching), room
    // 0007's own INIT (byte-identical across every scene checked), or
    // Flight Control's briefing gump (sets missionIndex + other subscene
    // triggers, but never these flags) — so this is approximated per-scene
    // from an external mission/pilot-roster reference instead, keyed by
    // scene index (= the LOOKMISN index of the mission about to be chosen
    // from that scene's own Flight Control — scene N picks mission N, see
    // onMissionComplete).
    //
    // The reference groups missions into named chapters (Orsini/Tamayo/
    // Locanda/Blackmane/...) that aren't LOOKMISN-numbered directly.
    // Cross-referenced against LOOKMISN.IFF's own letter-prefixed groups
    // (misna/b/c/d/...): Orsini(4)->A, Blackmane(3)->D, Ariel(3)->E,
    // Caliban(3)->F, Torgo(3)->G, Loki(3)->H all match their LOOKMISN
    // group's mission count exactly, giving high confidence in a direct
    // per-chapter sequential mapping. Tamayo(3)/Locanda(3) are one short of
    // their LOOKMISN groups' real count (B=4, C=4) — independently
    // confirmed anchors (Tamayo1/2 = misnb001/002, both "scramble/no
    // wingman choice", matching an earlier confirmed report) fix their
    // first 2 entries, but misnb003/004 and misnc003/004 aren't covered by
    // the reference's naming (misnb003 is presumably the reference's own
    // separately-noted "optional simulator" mission, reached via Flight
    // Control's "Run SIMULATOR." gump rather than the normal briefing flow,
    // not misnb003 itself).
    //
    // CORRECTION (superseding an earlier, wrong claim in this comment that
    // "Locanda3 = misnc003, confirmed"): direct inspection of every mission
    // file's own MISN>NAME chunk (not filename, not this reference) found
    // misnc001/misnc002 both internally self-identify as "Tamayo" — misnc00x
    // is a SECOND, alternate-track Tamayo arc, not Locanda at all — and
    // misnc003 is actually the Flash recruitment duel ("Simulator"/
    // "Destroy Flash", see kBranchConsequences' 0/7 comment), unrelated to
    // Flint or Locanda. The real Locanda ("Laconda" per its own NAME chunk)
    // group is misnd00x (LOOKMISN 12-14) — confirmed both by internal name
    // and by cast: misnd003 has a FLINTD3 actor and objective "Find Flint
    // and return to Victory". misne001 in turn self-identifies as
    // "Blackmane", not "Ariel" as this comment previously assumed. This
    // means the per-chapter sequential mapping below is very likely shifted
    // by one chapter letter starting at D — scenes 12-14 currently labeled
    // "Blackmane" are probably really Locanda, "Ariel" (15-17) probably
    // really Blackmane, and so on — NOT yet corrected in the table below,
    // since re-deriving it properly needs re-checking every remaining
    // chapter's own internal name, not just patching labels. The uniform
    // "all seven pilots available" values already used for every chapter
    // from Blackmane onward happen to be identical regardless of which
    // specific chapter each maps to, so this mislabeling likely hasn't
    // caused a real roster-flag bug yet — just wrong comments — but treat
    // every chapter name below D as unconfirmed until re-verified the same
    // way.
    //
    // Every chapter from Alcor onward has a mission count that doesn't
    // match its corresponding LOOKMISN group at all (e.g. LOOKMISN's I
    // group has 3 missions, Alcor has 4) — NOT mapped, pending
    // confirmation, rather than guessed.
    static const std::unordered_map<int, std::vector<int>> kRosterFlagsByScene = {
        // Orsini (misna00x, scenes 0-3)
        { 0,  { 146 } },                                    // Orsini1: Hobbes
        { 1,  { 146, 147, 148, 149, 145 } },                // Orsini2: +Vagabond,Vaquero,Maniac,Cobra
        { 2,  { 146, 147, 148, 149, 145 } },                // Orsini3: same
        { 3,  { 146, 147, 148, 149, 145, 143 } },           // Orsini4: +Flint
        // Tamayo (misnb00x, scenes 4-5 confirmed; 6/7 not mapped)
        { 4,  {} },                                         // Tamayo1: scramble, no choice
        { 5,  {} },                                         // Tamayo2: no choice (Excalibur-choice mission)
        // NOT actually Locanda — see this table's own header comment
        // (misnc00x internally self-identifies as "Tamayo", a second
        // alternate-track arc; misnc003/scene 10 is the Flash duel, not a
        // "rescue Flint" mission). Chapter name below left as originally
        // assigned pending a full re-derivation; only the "not Locanda"
        // correction is confirmed so far.
        { 8,  { 143, 144, 145, 146, 147, 148, 149 } },      // scene 8 (misnc001, "Tamayo"): all seven
        { 9,  { 146, 147, 148, 149, 144 } },                // scene 9 (misnc002, "Tamayo"): Hobbes,Vaquero,Vagabond,Maniac,Flash
        { 10, {} },                                         // scene 10 (misnc003): the Flash duel/Simulator — no roster choice
        // Mislabeled — see this table's own header comment: misnd00x
        // internally self-identifies as "Laconda"/Locanda (confirmed via
        // misnd003's own FLINTD3 cast + "Find Flint and return to Victory"
        // objective), not Blackmane. Values unchanged (uniform "all seven"
        // like every other high-confidence chapter), only the name is wrong.
        // "Blackmane" (really Locanda; misnd00x, scenes 12-14)
        { 12, { 143, 144, 145, 146, 147, 148, 149 } },
        { 13, { 143, 144, 145, 146, 147, 148, 149 } },
        { 14, { 143, 144, 145, 146, 147, 148, 149 } },
        // Likely mislabeled too (see header comment): misne001 internally
        // self-identifies as "Blackmane", not "Ariel" — not independently
        // re-confirmed for 16/17 yet.
        // "Ariel" (misne00x, scenes 15-17)
        { 15, { 143, 144, 145, 146, 147, 148, 149 } },
        { 16, { 143, 144, 145, 146, 147, 148, 149 } },
        { 17, { 143, 144, 145, 146, 147, 148, 149 } },
        // Caliban (misnf00x, scenes 18-20)
        { 18, { 143, 144, 145, 146, 147, 148, 149 } },
        { 19, { 143, 144, 145, 146, 147, 148, 149 } },
        { 20, { 143, 144, 145, 146, 147, 148, 149 } },
        // Torgo (misng00x, scenes 21-23)
        { 21, {} },                                         // Torgo1: no choice
        { 22, { 143, 144, 145, 146, 147, 148, 149 } },
        { 23, { 143, 144, 145, 146, 147, 148, 149 } },
        // Loki (misnh00x, scenes 24-26)
        { 24, { 143, 144, 145, 146, 147, 148, 149 } },
        { 25, { 143, 144, 145, 146, 147, 148, 149 } },
        { 26, { 143, 144, 145, 146, 147, 148, 149 } },
    };
    // varsB otherwise persists for the whole session (see the shapeCache/
    // onMissionComplete flag-reset entries in todo.MD, same root cause
    // category) — explicitly clear every known roster flag before applying
    // this scene's own set, so an earlier scene's pilots don't stay stuck
    // "available" once no longer confirmed for the current one.
    static const int kAllRosterFlags[] = { 143, 144, 145, 146, 147, 148, 149 };
    auto rosterIt = kRosterFlagsByScene.find(sceneId);
    if (rosterIt != kRosterFlagsByScene.end()) {
        for (int flag : kAllRosterFlags) scene->setBankB(flag, 0);
        for (int flag : rosterIt->second) scene->setBankB(flag, 1);
    }
    // Permanent player-driven roster changes (branch-choice consequences —
    // see kBranchConsequences/applyBranchEffects/m_pilotRosterOverride's own
    // comment) override the per-scene defaults just applied above.
    // kAllRosterFlags is already indexed exactly like WC3Pilot (143=Flint,
    // 144=Flash, 145=Cobra, 146=Hobbes, 147=Vagabond, 148=Vaquero,
    // 149=Maniac — same comment this whole block is under), so it doubles
    // as the flag-slot lookup here. Applied every loadScene() call, same as
    // the defaults themselves, so a removal/re-add made during one mission
    // cycle isn't silently undone by the next scene load. Bounded to
    // kRosterPilotCount, NOT WC3Pilot::Count — Rachel (appended after the 7
    // roster pilots) has no bank-B flag slot at all, so indexing
    // kAllRosterFlags past 6 would read out of bounds.
    for (size_t p = 0; p < kRosterPilotCount; p++) {
        if (m_pilotRosterOverride[p] == WC3RosterOverride::ForceUnavailable)
            scene->setBankB(kAllRosterFlags[p], 0);
        else if (m_pilotRosterOverride[p] == WC3RosterOverride::ForceAvailable)
            scene->setBankB(kAllRosterFlags[p], 1);
    }

    // SCEN INIT bytecode selects room "0001" as the starting room (VAR(76) vs VAR(77) for all others).
    // Find it by ID rather than assuming a fixed index, since VICTORY.IFF loads rooms out of ID order.
    int startRoom = 0;
    for (int i = 0; i < scene->getRoomCount(); i++) {
        if (scene->getStageData().rooms[i].id == "0001") { startRoom = i; break; }
    }
    auto getShapeFn = [this](int id){ return this->getShape(id); };
    scene->setRoom(startRoom, getShapeFn);
    loadRoomPalette(startRoom);
    playMusicForRoom(scene->getStageData().rooms[startRoom].id);
    playPendingMovies(scene->runRoomInit(startRoom, getShapeFn));

    printf("WC3GameFlow: Loaded scene %d (stage=%s), %d rooms\n",
           sceneId, stagePath, scene->getRoomCount());
    logRoomMenu();
}

void WC3GameFlow::init() {
    if (Game == nullptr)
        this->Game = &GameEngine::getInstance();
    m_keyboard = Game->getKeyboard();

    this->start();
    this->focus();

    // Load SHAPES.PAK
    TreEntry* shapesEntry = Assets.GetEntryByName("..\\..\\DATA\\GAMEFLOW\\SHAPES.PAK");
    if (shapesEntry) {
        shapePak.InitFromRAM("SHAPES.PAK", shapesEntry->data, shapesEntry->size);
        printf("WC3GameFlow: SHAPES.PAK loaded with %zu entries\n", shapePak.GetNumEntries());
        fflush(stdout);
    } else {
        printf("WC3GameFlow: WARNING: SHAPES.PAK not found\n");
    }

    initSceneFramebuffer();
    loadBranchPaks();
    loadLookMovi();
    loadLookMisn();
    loadSimDesc();
    loadBrfDesc();
    loadProfile();
    loadOptions(m_options, Config::getInstance());
    // Global Alt+O — same overlay reachable from the terminal Hub's
    // "controls" tab (see openOptionsActivity()) — checked once per frame
    // regardless of which activity is focused (GameEngine::run()). Alt+O
    // while WC3OptionsActivity is already on top closes it (toggle), same
    // as clicking "return".
    Game->setGlobalHotkeyHandler([this]() {
        bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
        if (!altHeld || !m_keyboard || !m_keyboard->isKeyJustPressed(SDL_SCANCODE_O)) return;
        IActivity* top = Game->hasActivity() ? Game->getCurrentActivity() : nullptr;
        if (dynamic_cast<WC3OptionsActivity*>(top)) {
            Game->stopTopActivity();
        } else {
            openOptionsActivity();
        }
    });

    // RSMixer's SDL_mixer subsystem (Mix_OpenAudio etc.) isn't opened anywhere
    // else in the neowc3 executable path — open it here before any music/sound
    // playback is attempted. Safe to call even if already initialized elsewhere.
    Mixer.init();
    loadGameflowMusic();
    loadGameflowSamples();

    // Check for savegame
    struct stat st;
    savegame_exists = (stat("wc3save.dat", &st) == 0);

    if (savegame_exists) {
        state = State::LOAD_SCENE;
    } else {
        state = State::PLAY_ORIGIN_MOVIE;
    }
}

void WC3GameFlow::runFrame(void) {
    switch (state) {
    case State::PLAY_ORIGIN_MOVIE:
        playMovie("ORIGIN_S.MVE");
        state = State::PLAY_OPENING_MOVIE;
        break;

    case State::PLAY_OPENING_MOVIE:
        playMovie("OPENING.MVE");
        state = State::LOAD_SCENE;
        break;

    case State::LOAD_SCENE:
        loadScene(current_scene);
        // A Duty Logs load (see dutyLogsLoad()) sets current_scene and
        // defers applying the save's bank-A/B overrides to here, after the
        // fresh loadScene() above — restoring them any earlier would just
        // get clobbered by loadStage()'s own stage-level INIT (which resets
        // a handful of bank-A slots to their "no selection yet" sentinel,
        // see WC3Scene::loadStage), since loadScene()/loadStage() never
        // reset varsA/varsB themselves.
        if (scene && m_hasPendingRestore) {
            for (auto& [slot, val] : m_pendingRestoreBankA) scene->setBankA(slot, val);
            for (auto& [slot, val] : m_pendingRestoreBankB) scene->setBankB(slot, val);
            m_hasPendingRestore = false;
            m_pendingRestoreBankA.clear();
            m_pendingRestoreBankB.clear();
        }
        state = State::SCENE_ACTIVE;
        break;

    case State::LAUNCH_EXTRA_MISSION:
        // See this state's own comment: deferred here (rather than launched
        // synchronously from onMissionComplete) so the previous mission's
        // own activity has actually been popped by the time launchMission()
        // pushes the next one. m_selectedMissionIndex was already set to
        // the extra mission's LOOKMISN index by onMissionComplete.
        launchMission();
        state = State::SCENE_ACTIVE;
        break;

    case State::SCENE_ACTIVE: {
        sceneFB->clear();

        if (scene) {
            int termOffset = (m_inTerminal || m_inKillBoard || m_inLoadout) ? kTerminalYOffset : 0;
            scene->render(sceneFB, &shapePak,
                          [this](int id) { return this->getShape(id); },
                          m_hoveredGumpId, termOffset);

            // A VAR(44)-triggered one-shot animation just finished. The lift's own
            // travel/door gumps (8/9/10/11/13) are driven explicitly rather than
            // through their own ACTV (see LiftPhase) — handleLiftAnimFinished()
            // consumes those; anything else fires that gump's own ACTV code,
            // exactly as if it had been clicked, to advance to the next link
            // (e.g. one segment of a chained door-opening animation elsewhere).
            uint32_t finishedGump = scene->pollFinishedAnimation();
            if (finishedGump != 0 && !(m_inLift && handleLiftAnimFinished(finishedGump)))
                runGumpAction(finishedGump);
        }

        // Terminal Login page: no clickable zones (the callsign field isn't
        // a gump-zone, just typed text), so it short-circuits the normal
        // hover/click handling below entirely — render its own overlay and
        // only watch for Enter-to-commit.
        if (m_inTerminal && m_terminalPage == TerminalPage::Login) {
            renderLoginPage();
            if (m_keyboard && m_keyboard->isKeyJustPressed(SDL_SCANCODE_RETURN))
                commitLogin();
            break;
        }

        // Terminal Duty Logs page: self-contained, same reasoning as
        // State::SIMULATOR (its list rows aren't gump-zones the normal
        // hitTest()/activateZone() pipeline knows about) — see
        // renderDutyLogsPage() for its own button/row hit-testing.
        if (m_inTerminal && m_terminalPage == TerminalPage::DutyLogs) {
            renderDutyLogsPage();
            break;
        }

        // Kill Board: same self-contained approach — the CALLSIGN/ACES/
        // KILLS table isn't gump-zones either.
        if (m_inKillBoard) {
            renderKillBoardPage();
            break;
        }

        // Loadout: same self-contained approach — the ship-portrait/weapon-
        // list panels aren't gump-zones either.
        if (m_inLoadout) {
            renderLoadoutPage();
            break;
        }

        // Map SDL window coords → 640×480 virtual coords
        EventManager& events = EventManager::getInstance();
        RSScreen& screen = RSScreen::instance();
        int mx, my;
        events.getMousePosition(mx, my);
        int vx, vy;
        screen.windowToLogical(mx, my, 640, 480, vx, vy);
        // Zones (built from the room's own unshifted CORD data) don't know
        // about kTerminalYOffset — only the Terminal Hub page reaches this
        // shared hitTest()/activateZone() path while m_inTerminal is still
        // true (Login/DutyLogs/KillBoard/Loadout all short-circuit above),
        // so compensate by hit-testing against the un-shifted Y instead of
        // shifting the zones themselves.
        int hitVy = m_inTerminal ? vy - kTerminalYOffset : vy;

        // Hover: identify zone under cursor, update label and cursor type
        m_hoverLabel.clear();
        m_hoveredGumpId   = 0;
        m_hoveredZoneType = 0;
        m_hoveredIsRoomChange = false;
        if (scene) {
            int zoneIdx = scene->hitTest(vx, hitVy);
            if (zoneIdx >= 0) {
                const auto& z = scene->getZones()[zoneIdx];
                m_hoverLabel      = z.label;
                m_hoveredGumpId   = z.gump_id;
                m_hoveredZoneType = z.type;
                m_hoveredIsRoomChange = z.isRoomChange;
            }

            // Doors (and similar hover-only actors) carry an attached
            // open/close sample pair in their own SOND/_ID_ chunk. Diff this
            // frame's effectively-hovered actor set against last frame's to
            // fire the sample once per hover-start/hover-end transition,
            // rather than every frame.
            std::vector<uint32_t> hoveredActors =
                scene->getHoveredActorIds(m_hoveredGumpId, [this](int id) { return this->getShape(id); });
            for (uint32_t id : hoveredActors) {
                if (std::find(m_prevHoveredActorIds.begin(), m_prevHoveredActorIds.end(), id) ==
                    m_prevHoveredActorIds.end()) {
                    const auto* sids = scene->getActorSoundIds(id);
                    if (sids && !sids->empty()) playGameflowSample((*sids)[0]);
                }
            }
            for (uint32_t id : m_prevHoveredActorIds) {
                if (std::find(hoveredActors.begin(), hoveredActors.end(), id) == hoveredActors.end()) {
                    const auto* sids = scene->getActorSoundIds(id);
                    if (sids && sids->size() > 1) playGameflowSample((*sids)[1]);
                }
            }
            m_prevHoveredActorIds = std::move(hoveredActors);
        }

        // Right-click: cycle focus to the next zone (mouse-driven variant of Tab)
        if (events.isMouseButtonJustPressed(SDL_BUTTON_RIGHT))
            cycleZoneFocus();

        // Left-click: hit-test the clickable zones and execute ACTV action
        if (events.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
            if (scene) {
                int zoneIdx = scene->hitTest(vx, hitVy);
                if (zoneIdx >= 0)
                    activateZone(zoneIdx);
                else
                    printf("WC3: click at (%d,%d) — no zone\n", vx, vy);
            }
        }

        drawCursor(vx, vy);
        displaySceneFramebuffer();

        if (m_keyboard) {
            // Tab: cycle keyboard focus between this room's gump fields.
            // Enter: activate the focused zone (keyboard equivalent of a click).
            if (m_keyboard->isKeyJustPressed(SDL_SCANCODE_TAB))
                cycleZoneFocus();
            if (m_keyboard->isKeyJustPressed(SDL_SCANCODE_RETURN) &&
                scene && m_focusZoneIdx >= 0 && m_focusZoneIdx < (int)scene->getZones().size())
                activateZone(m_focusZoneIdx);

            bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
            if (altHeld && m_keyboard->isKeyJustPressed(SDL_SCANCODE_X)) {
                state = State::CONFIRM_QUIT;
            }
        }
        break;
    }

    case State::AWAITING_BRANCH_CHOICE: {
        const WC3BranchEntry* entry = (m_pendingBranchPak >= 0)
            ? branchPaks[m_pendingBranchPak].getEntry(m_pendingBranchEntry) : nullptr;
        WC3Font* font = WC3Globals::getInstance().getFont("GFMD");

        // Lay out the two choice lines centred, stacked around mid-screen.
        // (x,y,w,h) rects are reused both for drawing and for mouse-click hit-testing.
        int trueY = -1, falseY = -1, trueX = 0, falseX = 0, trueW = 0, falseW = 0, lineH = 0;
        if (entry && font && font->isLoaded()) {
            lineH = font->getHeight();
            trueW  = font->measureText(entry->true_line.text_eng);
            falseW = font->measureText(entry->false_line.text_eng);
            trueX  = (640 - trueW) / 2;
            falseX = (640 - falseW) / 2;
            trueY  = 480 / 2 - lineH - 8;
            falseY = 480 / 2 + 8;
        }

        // Both lines are grey by default; drawChoicePrompt(highlightTrue, highlightFalse)
        // redraws with the given side in white — used both for the per-frame render and
        // for the one extra frame flushed to screen right before a keyboard pick commits,
        // so the player sees the highlight land before its audio line blocks playback.
        constexpr uint32_t kGrey  = 0xFF808080u; // r|g<<8|b<<16|a<<24, mid-grey
        constexpr uint32_t kWhite = 0xFFFFFFFFu;
        auto drawChoicePrompt = [&](bool highlightTrue, bool highlightFalse) {
            sceneFB->clear();
            if (scene) {
                scene->render(sceneFB, &shapePak,
                              [this](int id) { return this->getShape(id); },
                              0);
            }
            drawCursor(0, 0);

            displaySceneFramebuffer(0.4f, /*drawHoverLabel=*/false,
                [&](uint32_t* rgba, int fbW, int fbH) {
                    if (entry && font && font->isLoaded()) {
                        font->drawTextRGBA(rgba, fbW, fbH, entry->true_line.text_eng,  trueX,  trueY,
                                            highlightTrue  ? kWhite : kGrey);
                        font->drawTextRGBA(rgba, fbW, fbH, entry->false_line.text_eng, falseX, falseY,
                                            highlightFalse ? kWhite : kGrey);
                    }
                });
        };

        drawChoicePrompt(m_branchChoiceSelected == 0, m_branchChoiceSelected == 1);

        // Plays a line's branch-pak audio — called the moment it's highlighted
        // (arrow press or mouse click), not deferred to commit.
        auto playChoiceAudio = [&](bool pickedFalse) {
            branchPaks[m_pendingBranchPak].playEntry(m_pendingBranchEntry, pickedFalse);
        };

        // Small builders for kBranchConsequences below — a WC3BranchEffect has
        // 6 fields but any single row only ever sets 1-2 of them; these keep
        // the table itself readable instead of every entry spelling out all
        // 6 (mostly-default) fields positionally.
        auto moraleFx = [](WC3Pilot p, int delta) {
            WC3BranchEffect fx; fx.pilot = p; fx.pilotMoraleDelta = delta; return fx;
        };
        auto shipFx = [](int delta) {
            WC3BranchEffect fx; fx.shipMoraleDelta = delta; return fx;
        };
        auto rosterFx = [](WC3Pilot p, WC3RosterOverride ov) {
            WC3BranchEffect fx; fx.rosterPilot = p; fx.rosterOverride = ov; return fx;
        };
        auto missionFx = [](int idx) {
            WC3BranchEffect fx; fx.missionIndexOverride = idx; return fx;
        };
        auto relationshipFx = [](WC3Pilot p, int status) {
            WC3BranchEffect fx; fx.relationshipPilot = p; fx.relationshipStatus = status; return fx;
        };
        // Consequences of picking a branch-choice line, keyed by
        // pakIndex*1000 + entryIndex (pak 0-3 = BRANCH1.PAK-BRANCH4.PAK,
        // cd1miss.tre-cd4miss.tre respectively — see branchPaks' own
        // comment). Sourced from two passes of a user-supplied reference:
        // the first cross-checked each entry's TRUE/FALSE dialogue text
        // against its roster/mission-choice effect; the second gave precise
        // +1/-1 morale deltas per entry (confirmed as a real per-choice
        // unit, not just a direction) and superseded a few guesses from the
        // first pass outright — 004.IFF turned out to be ship morale, not
        // Flint+Maniac; 006.IFF's Flash direction was backwards and it also
        // moves ship morale; 020.IFF's direction and previously-unstated
        // false branch are now both confirmed; 007.IFF (the Flash duel,
        // still unimplemented — see below) also carries its own morale
        // deltas independent of the duel outcome. Rachel (the ship's
        // mechanic, a love-interest subplot character — see WC3Pilot's own
        // comment) appears as a morale target starting with this second
        // pass (0/5, 1/12, 2/34).
        //
        // A shipMoraleDelta effect (shipFx) also nudges every character's
        // own morale by the same amount — see applyBranchEffects() — so
        // rows combining a shipFx with a specific moraleFx (e.g. 0/6, 0/7,
        // 2/36, 3/26) are intentionally layering an extra nudge for that
        // one character on top of the ship-wide change everyone gets, not
        // double-counting a mistake.
        //
        // Roster/mission mechanics from the first pass are preserved here
        // unchanged (the second pass only ever refined morale numbers, and
        // where both passes gave a direction for the same entry — 013,
        // 014 — they agreed): 0/5 still overrides missionIndex (replacing
        // the old isExcaliburChoice special case), 1/13 and 1/14 still
        // toggle Flint's roster membership, 3/26 still toggles Vagabond's.
        // 0/7's "challenge Flash in the simulator, win=joins/lose-or-
        // refuse=leaves" roster mechanic is NOT implemented — no bridge
        // exists yet between this branch-choice flow and a simulator
        // mission's own win/lose outcome (see the historical Phase B note);
        // only 0/7's morale deltas are wired up here.
        //
        // Only entries with a clearly-stated mechanical effect get a row;
        // deliberately excluded (flavor-only dialogue or no effect stated
        // in either reference pass — do not add rows without new evidence):
        // 2/39, 3/37 (confirmed explicit no-op: "player loses either way,
        // no morale impact").
        //
        // The earlier-flagged "039 vs 030" numbering mix-up is now
        // resolved, not a typo: pak 3 genuinely uses entry 39 (not 30) as
        // the second half of the Flint/Rachel love-triangle "kiss her"
        // cluster (2/39, by contrast, is CD3MISS.TRE's own unrelated
        // flavor-only entry — two genuinely different entries, as
        // suspected). The cluster is 4 entries covering both approach
        // orders, since the game presents whichever character wasn't
        // kissed first as a second offer: 3/29 (talk to Flint first) ->
        // decline -> 3/39 (then talk to Rachel); 3/31 (talk to Rachel
        // first) -> decline -> 3/32 (then talk to Flint). Kissing either
        // one is exclusive (locks in that character as the endgame
        // companion via relationshipFx, forecloses the other) and skips
        // the second offer entirely; declining applies the ordinary -1
        // pilotMoraleDelta with no relationship-status write, so declining
        // both leaves m_pilotRelationshipStatus at 0 for both — "alone" for
        // the endgame, per the user's own framing. No endgame cutscene
        // exists in this engine yet to actually consume this — see
        // m_pilotRelationshipStatus's own comment. 3/38 ("only presented if
        // you chose Flint and haven't flown with her since") is a plain
        // Flint morale entry, gated by a room/session condition this table
        // doesn't need to model — it only fires the consequence once the
        // game's own bytecode has already decided to present it.
        static const std::unordered_map<int, WC3BranchConsequenceRow> kBranchConsequences = {
            // BRANCH1.PAK (pak 0, cd1miss.tre)
            { 0*1000+0, { {shipFx(+1)}, {shipFx(-1)} } },
            { 0*1000+1, { {moraleFx(WC3Pilot::Vagabond, +1)}, {moraleFx(WC3Pilot::Vagabond, -1)} } },
            { 0*1000+2, { {moraleFx(WC3Pilot::Vaquero, +1)}, {moraleFx(WC3Pilot::Vaquero, -1)} } },
            { 0*1000+3, { {moraleFx(WC3Pilot::Flint, +1)}, {moraleFx(WC3Pilot::Flint, -1)} } },
            { 0*1000+4, { {shipFx(+1)}, {shipFx(-1)} } },
            // Replaces the old isExcaliburChoice special case: accept flies
            // MISNB002.IFF (LOOKMISN index 5), decline flies MISNB2NE.IFF
            // (index 51) — see the historical investigation this special
            // case was originally added from. Also moves Rachel's morale
            // (new in the second reference pass): accepting the Excalibur
            // (and implicitly Rachel's offer) raises it, declining lowers it.
            { 0*1000+5, { {missionFx(5), moraleFx(WC3Pilot::Rachel, +1)},
                          {missionFx(51), moraleFx(WC3Pilot::Rachel, -1)} } },
            { 0*1000+6, { {moraleFx(WC3Pilot::Flash, +1), shipFx(-1)},
                          {moraleFx(WC3Pilot::Flash, -1), shipFx(+1)} } },
            // The "challenge Flash in the simulator" roster mechanic (win =
            // joins roster, lose/refuse = leaves ship) is NOT implemented —
            // see this table's own header comment. Only the morale side is
            // wired up here.
            { 0*1000+7, { {moraleFx(WC3Pilot::Flash, -1), shipFx(+1)},
                          {moraleFx(WC3Pilot::Flash, +1), shipFx(-1)} } },
            { 0*1000+8, { {moraleFx(WC3Pilot::Cobra, +1)}, {moraleFx(WC3Pilot::Cobra, -1)} } },
            { 0*1000+9, { {moraleFx(WC3Pilot::Flint, +1)}, {moraleFx(WC3Pilot::Flint, -1)} } },
            { 0*1000+10, { {moraleFx(WC3Pilot::Vagabond, -1)}, {moraleFx(WC3Pilot::Vagabond, +1)} } },
            // BRANCH2.PAK (pak 1, cd2miss.tre)
            { 1*1000+11, { {moraleFx(WC3Pilot::Maniac, +1)}, {moraleFx(WC3Pilot::Maniac, -1)} } },
            { 1*1000+12, { {moraleFx(WC3Pilot::Rachel, +1)}, {moraleFx(WC3Pilot::Rachel, -1)} } },
            { 1*1000+13, { {moraleFx(WC3Pilot::Flint, +1)},
                           {moraleFx(WC3Pilot::Flint, -1), rosterFx(WC3Pilot::Flint, WC3RosterOverride::ForceUnavailable)} } },
            { 1*1000+14, { {moraleFx(WC3Pilot::Flint, -1)},
                           {moraleFx(WC3Pilot::Flint, +1), rosterFx(WC3Pilot::Flint, WC3RosterOverride::ForceAvailable)} } },
            { 1*1000+15, { {moraleFx(WC3Pilot::Vaquero, +1)}, {moraleFx(WC3Pilot::Vaquero, -1)} } },
            { 1*1000+16, { {moraleFx(WC3Pilot::Cobra, +1)}, {moraleFx(WC3Pilot::Cobra, -1)} } },
            { 1*1000+17, { {moraleFx(WC3Pilot::Vagabond, +1)}, {moraleFx(WC3Pilot::Vagabond, -1)} } },
            { 1*1000+18, { {moraleFx(WC3Pilot::Flint, +1)}, {moraleFx(WC3Pilot::Flint, -1)} } },
            { 1*1000+19, { {moraleFx(WC3Pilot::Hobbes, -1), moraleFx(WC3Pilot::Cobra, +1)},
                           {moraleFx(WC3Pilot::Hobbes, +1), moraleFx(WC3Pilot::Cobra, -1)} } },
            { 1*1000+20, { {moraleFx(WC3Pilot::Maniac, +1)}, {moraleFx(WC3Pilot::Maniac, -1)} } },
            { 1*1000+21, { {moraleFx(WC3Pilot::Flint, +1)}, {moraleFx(WC3Pilot::Flint, -1)} } },
            { 1*1000+22, { {shipFx(-1)}, {shipFx(+1)} } },
            // BRANCH3.PAK (pak 2, cd3miss.tre)
            // False branch has no stated effect in the source reference.
            { 2*1000+23, { {shipFx(+1), rosterFx(WC3Pilot::Vaquero, WC3RosterOverride::ForceUnavailable)}, {} } },
            { 2*1000+33, { {shipFx(+1)}, {shipFx(-1)} } },
            { 2*1000+34, { {moraleFx(WC3Pilot::Rachel, +1)}, {moraleFx(WC3Pilot::Rachel, -1)} } },
            { 2*1000+35, { {moraleFx(WC3Pilot::Vagabond, +1)}, {moraleFx(WC3Pilot::Vagabond, -1)} } },
            { 2*1000+36, { {moraleFx(WC3Pilot::Cobra, +1), shipFx(+1)},
                           {moraleFx(WC3Pilot::Cobra, -1), shipFx(-1)} } },
            // BRANCH4.PAK (pak 3, cd4miss.tre)
            { 3*1000+24, { {shipFx(+1)}, {shipFx(-1)} } },
            { 3*1000+25, { {moraleFx(WC3Pilot::Maniac, +1)}, {moraleFx(WC3Pilot::Maniac, -1)} } },
            { 3*1000+26, { {rosterFx(WC3Pilot::Vagabond, WC3RosterOverride::ForceAvailable), shipFx(+1)},
                           {rosterFx(WC3Pilot::Vagabond, WC3RosterOverride::ForceUnavailable), shipFx(-1)} } },
            { 3*1000+27, { {moraleFx(WC3Pilot::Flash, +1)}, {moraleFx(WC3Pilot::Flash, -1)} } },
            { 3*1000+28, { {moraleFx(WC3Pilot::Vagabond, +1)}, {moraleFx(WC3Pilot::Vagabond, -1)} } },
            // Flint/Rachel love-triangle "kiss her" cluster — see this
            // table's own header comment for the full 4-entry structure.
            // Talk to Flint first: kiss -> Flint locked in, Rachel passed
            // over; decline -> Flint -1 morale only (Rachel's own offer, if
            // taken, comes from 3/39 next).
            { 3*1000+29, { {relationshipFx(WC3Pilot::Flint, 13), relationshipFx(WC3Pilot::Rachel, 2)},
                           {moraleFx(WC3Pilot::Flint, -1)} } },
            // Declined Flint, now offered Rachel: kiss -> Rachel locked in,
            // Flint passed over; decline -> Rachel -1 morale only (both
            // declined = alone, per m_pilotRelationshipStatus's comment).
            { 3*1000+39, { {relationshipFx(WC3Pilot::Rachel, 13), relationshipFx(WC3Pilot::Flint, 2)},
                           {moraleFx(WC3Pilot::Rachel, -1)} } },
            // Talk to Rachel first: kiss -> Rachel locked in, Flint passed
            // over; decline -> Rachel -1 morale only (Flint's own offer, if
            // taken, comes from 3/32 next).
            { 3*1000+31, { {relationshipFx(WC3Pilot::Rachel, 13), relationshipFx(WC3Pilot::Flint, 2)},
                           {moraleFx(WC3Pilot::Rachel, -1)} } },
            // Declined Rachel, now offered Flint: kiss -> Flint locked in,
            // Rachel passed over; decline -> Flint -1 morale only.
            { 3*1000+32, { {relationshipFx(WC3Pilot::Flint, 13), relationshipFx(WC3Pilot::Rachel, 2)},
                           {moraleFx(WC3Pilot::Flint, -1)} } },
            // Only presented if Flint was chosen above and the player
            // hasn't flown with her since — a room/session gate the game's
            // own bytecode already handles before this entry ever fires.
            { 3*1000+38, { {moraleFx(WC3Pilot::Flint, +1)}, {moraleFx(WC3Pilot::Flint, -1)} } },
        };

        // Commits to the highlighted choice: plays the reaction shot of the intro
        // movie (branchTrueShot/branchFalseShot — see WC3Scene's VAR(49) if/else
        // peek) and resumes any remaining gump action. Branch-pak audio has already
        // played (see playChoiceAudio above) by the time this runs.
        auto commitChoice = [&](bool pickedFalse) {
            int reactionShot = pickedFalse ? m_pendingGumpAction.branchFalseShot
                                            : m_pendingGumpAction.branchTrueShot;
            if (m_options.transitionsOn && reactionShot >= 0 && !m_pendingBranchMovieName.empty())
                playNamedMovieShot(m_pendingBranchMovieName, reactionShot);
            // The reaction shot's own MusicPauseScope (if one played) already resumed
            // music — but guarantee resumption even when no reaction shot exists,
            // since music was explicitly paused for the whole choice-waiting period
            // when this prompt first appeared.
            resumeGameflowMusic();
            WC3Scene::ActionResult pending = m_pendingGumpAction;
            // Captured before the pending-choice fields get reset below, for
            // the table-driven consequences applied after finishGumpAction().
            auto consequenceIt = kBranchConsequences.find(m_pendingBranchPak * 1000 + m_pendingBranchEntry);
            m_pendingBranchPak = -1;
            m_pendingBranchEntry = -1;
            m_pendingBranchMovieName.clear();
            m_branchChoiceSelected = -1;
            state = State::SCENE_ACTIVE;
            finishGumpAction(pending);
            // Applied after finishGumpAction() so a missionIndexOverride
            // effect can't be clobbered by that call's own (choice-
            // independent) missionIndex handling: the interpreter doesn't
            // yet resume choice-conditioned bytecode after a VAR(49)
            // branch-pak call (this is the general gap kBranchConsequences
            // approximates around — see that table's own comment), so no
            // ActionResult ever carries a missionIndex/GoRoom that depends
            // on which line the player picked. kBranchConsequences'
            // pak0/entry5 row (formerly the hardcoded "isExcaliburChoice"
            // special case) is just one row in this same general table now.
            if (consequenceIt != kBranchConsequences.end()) {
                applyBranchEffects(pickedFalse ? consequenceIt->second.onFalse : consequenceIt->second.onTrue);
            }
        };

        if (entry && font && font->isLoaded()) {
            // Up/Down move the highlight and play that line's audio immediately —
            // no commit yet. Enter commits whichever line is currently highlighted
            // (no-op if nothing has been highlighted yet).
            if (m_keyboard && m_keyboard->isKeyJustPressed(SDL_SCANCODE_UP)) {
                m_branchChoiceSelected = 0;
                drawChoicePrompt(true, false);
                playChoiceAudio(false);
            } else if (m_keyboard && m_keyboard->isKeyJustPressed(SDL_SCANCODE_DOWN)) {
                m_branchChoiceSelected = 1;
                drawChoicePrompt(false, true);
                playChoiceAudio(true);
            } else if (m_keyboard && m_branchChoiceSelected >= 0 &&
                       m_keyboard->isKeyJustPressed(SDL_SCANCODE_RETURN)) {
                commitChoice(m_branchChoiceSelected == 1);
                break;
            }

            EventManager& events3 = EventManager::getInstance();
            if (events3.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
                RSScreen& screen4 = RSScreen::instance();
                int mx, my;
                events3.getMousePosition(mx, my);
                int vx, vy;
                screen4.windowToLogical(mx, my, 640, 480, vx, vy);

                bool pickedFalse = false, picked = false;
                if (vx >= trueX && vx < trueX + trueW && vy >= trueY && vy < trueY + lineH) {
                    pickedFalse = false; picked = true;
                } else if (vx >= falseX && vx < falseX + falseW && vy >= falseY && vy < falseY + lineH) {
                    pickedFalse = true; picked = true;
                }

                if (picked) {
                    drawChoicePrompt(!pickedFalse, pickedFalse);
                    playChoiceAudio(pickedFalse);
                    commitChoice(pickedFalse);
                }
            }
        }
        break;
    }

    case State::MISSION_STUB: {
        // Dev stub for exercising the gameflow/scene-progression path without
        // the flight engine — see m_options.devStubMissions. Same frozen-scene-behind-
        // a-text-overlay pattern as CONFIRM_QUIT below.
        sceneFB->clear();
        if (scene) {
            scene->render(sceneFB, &shapePak,
                          [this](int id) { return this->getShape(id); },
                          0);
        }
        drawCursor(0, 0);

        RSScreen& screenMS = RSScreen::instance();
        VGAPalette* palMS = VGA.getPalette();
        int fbW = sceneFB->width, fbH = sceneFB->height;
        uint32_t* rgba = new uint32_t[fbW * fbH];
        for (int i = 0; i < fbW * fbH; i++) {
            uint8_t idx = sceneFB->framebuffer[i];
            const Texel* c = palMS->GetRGBColor(idx);
            uint8_t r = c->r, g = c->g, b = c->b;
            if (r == 0xFF && g == 0xFF && b == 0x02) { r = g = b = 0xFF; }
            rgba[i] = (uint8_t)(r * 0.4f) | ((uint8_t)(g * 0.4f) << 8) |
                      ((uint8_t)(b * 0.4f) << 16) | (0xFF << 24);
        }

        WC3Font* fontMS = WC3Globals::getInstance().getFont("GFMD");
        if (fontMS && fontMS->isLoaded()) {
            std::string line1 = m_stubReturnToSimulator
                ? ("Training Mission: " + m_stubMissionName)
                : ("Mission: " + m_stubMissionName + " (index " +
                   std::to_string(m_selectedMissionIndex) + ")");
            std::string line2 = "(W)in  /  (L)ose";
            int lineH = fontMS->getHeight();
            int tw1 = fontMS->measureText(line1);
            int tw2 = fontMS->measureText(line2);
            fontMS->drawTextRGBA(rgba, fbW, fbH, line1, (fbW - tw1) / 2, fbH / 2 - lineH - 4);
            fontMS->drawTextRGBA(rgba, fbW, fbH, line2, (fbW - tw2) / 2, fbH / 2 + 4);
        }

        for (int y = 0; y < fbH / 2; y++)
            for (int x = 0; x < fbW; x++)
                std::swap(rgba[y * fbW + x], rgba[(fbH - 1 - y) * fbW + x]);

        glViewport(screenMS.viewport_x, screenMS.viewport_y, screenMS.viewport_w, screenMS.viewport_h);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0, screenMS.viewport_w, 0, screenMS.viewport_h, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);   glDisable(GL_FOG); glDisable(GL_BLEND);
        glRasterPos2i(0, 0);
        glPixelZoom((float)screenMS.viewport_w / fbW, (float)screenMS.viewport_h / fbH);
        glDrawPixels(fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        delete[] rgba;

        if (m_keyboard) {
            if (m_keyboard->isKeyJustPressed(SDL_SCANCODE_W) ||
                m_keyboard->isKeyJustPressed(SDL_SCANCODE_L)) {
                bool won = m_keyboard->isKeyJustPressed(SDL_SCANCODE_W);
                if (m_stubReturnToSimulator) {
                    // Training mission: no campaign side effects, just back
                    // to the Simulator terminal's list view.
                    m_stubReturnToSimulator = false;
                    state = State::SIMULATOR;
                } else {
                    state = State::SCENE_ACTIVE;
                    onMissionComplete(won);
                }
            }
        }
        break;
    }

    case State::SIMULATOR: {
        // Custom overlay over room 0009's own background art — see the
        // State::SIMULATOR comment in WC3GameFlow.h for why this can't just
        // be driven by the room's own (animation-only) ACTV bytecode.

        // Deferred "return" navigation (see m_simPendingReturn's comment):
        // once gump 7's triggered lit-frame animation has actually finished
        // (i.e. it's been rendered), leave the room now instead of on the
        // same frame the click landed.
        if (scene && m_simPendingReturn) {
            uint32_t finishedSim = scene->pollFinishedAnimation();
            if (finishedSim == 7) {
                m_simPendingReturn = false;
                runGumpAction(7);
                state = State::SCENE_ACTIVE;
                break;
            }
        }

        sceneFB->clear();
        if (scene) {
            // Room 0009 is the same "computer console" screen family as
            // Loadout/Options/Terminal/Kill Board (see kTerminalYOffset's own
            // comment) — it renders through this identical hand-built-overlay
            // pattern, just wasn't included when that discrepancy was first
            // confirmed there. Live testing found its buttons/list visibly
            // misplaced without this same +23px correction, so it needs both
            // halves of the existing fix: shift the underlying scene render
            // (this yOffset param) and shift this overlay's own hit-test/text
            // math to match (see listY0/hit() calls below).
            scene->render(sceneFB, &shapePak,
                          [this](int id) { return this->getShape(id); },
                          0, kTerminalYOffset);
        }

        // This overlay doesn't do real zone-hover tracking (there's nothing
        // for the cursor to hover but the list/buttons drawn here), so force
        // the plain arrow (frame 0) rather than leaving drawCursor() to pick
        // a frame from whatever zone type was last hovered back in
        // SCENE_ACTIVE — that stale m_hoveredZoneType is what was making the
        // cursor look "stuck" showing a terminal/crosshair icon. Track the
        // real mouse position too, instead of the hardcoded (0,0) used
        // here previously, so the cursor actually follows the mouse.
        RSScreen& screenSim = RSScreen::instance();
        EventManager& eventsSim = EventManager::getInstance();
        int mxSim, mySim;
        eventsSim.getMousePosition(mxSim, mySim);
        int vxSim, vySim;
        screenSim.windowToLogical(mxSim, mySim, 640, 480, vxSim, vySim);
        m_hoveredZoneType = 0;
        m_hoveredIsRoomChange = false;
        drawCursor(vxSim, vySim);

        constexpr uint32_t kSimGrey  = 0xFF808080u;
        constexpr uint32_t kSimWhite = 0xFFFFFFFFu;

        // List rows are laid out here and reused for click hit-testing below —
        // same (x,y,w,h)-rects-computed-at-layout-time pattern as
        // AWAITING_BRANCH_CHOICE's trueX/falseX/etc.
        const int listX = 95, listY0 = 108 + kTerminalYOffset, listLineH = 24;
        std::vector<int> rowY(m_simMissions.size());

        displaySceneFramebuffer(1.0f, /*drawHoverLabel=*/false,
            [&](uint32_t* rgba, int fbW, int fbH) {
                // GFSM ("gump font small", 9px) — GFMD (17px) was oversized
                // for this 24px-per-row list, same fix as Duty Logs/Kill
                // Board's own dense lists.
                WC3Font* fontSim = WC3Globals::getInstance().getFont("GFSM");
                if (fontSim && fontSim->isLoaded()) {
                    if (!m_simShowDetail) {
                        for (size_t i = 0; i < m_simMissions.size(); i++) {
                            int y = listY0 + (int)i * listLineH;
                            rowY[i] = y;
                            fontSim->drawTextRGBA(rgba, fbW, fbH, m_simMissions[i].name, listX, y,
                                                   ((int)i == m_simSelectedIndex) ? kSimWhite : kSimGrey);
                        }
                    } else if (m_simSelectedIndex >= 0 && m_simSelectedIndex < (int)m_simMissions.size()) {
                        const auto& sel = m_simMissions[m_simSelectedIndex];
                        int y = listY0;
                        fontSim->drawTextRGBA(rgba, fbW, fbH, sel.name, listX, y, kSimWhite);
                        y += listLineH;
                        auto briefIt = m_briefingText.find(sel.lookmisnIndex);
                        if (briefIt != m_briefingText.end()) {
                            for (const auto& line : briefIt->second) {
                                fontSim->drawTextRGBA(rgba, fbW, fbH, line, listX, y, kSimGrey);
                                y += fontSim->getHeight() + 4;
                            }
                        }
                    }
                }
            });

        if (eventsSim.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
            int vx = vxSim, vy = vySim;

            // Fixed control buttons, confirmed from room 0009's own zone
            // dump (all real CORD x/y match exactly; w/h below are hardcoded
            // 64x64 since TYPE_BUTTON actors carry no CORD size, only a
            // position — see findActorRect's own w=0/h=0 pitfall, hit the
            // same way in the Kill Board logoff bug): gump 7 = "return" (its
            // own ACTV already does GoRoom 3 — reused via runGumpAction
            // below); gump 3/4 aren't distinguishable from their (data-only-
            // triggers-an-animation) ACTV code, so "run"/"default" are
            // inferred from screen position (3 bottom-right next to return
            // -> run; 4 top-right, separate -> default) — see the room-0009
            // investigation notes in todo.MD if this needs revisiting. 9/10
            // (upper) and 11/12 (lower) are the list's scroll arrows. All Y
            // coordinates below are shifted by kTerminalYOffset to match the
            // scene->render() shift above — see that call's own comment.
            auto hit = [&](int x, int y, int w, int h) {
                return vx >= x && vx < x + w && vy >= y && vy < y + h;
            };
            // Click feedback: flip each button to its lit/active frame the
            // same way the room's own OP44/case-44 would (see
            // WC3Scene::triggerGumpAnim) — their ACTV bytecode's own OP44
            // call can't be relied on for this (see the comment above), so
            // trigger it directly by the button's known gump id instead.
            auto getShapeFn = [this](int id) { return this->getShape(id); };
            if (hit(425, 367 + kTerminalYOffset, 64, 64)) {                 // return
                scene->triggerGumpAnim(7, getShapeFn);
                m_simPendingReturn = true; // navigation deferred — see the poll at the top of this case
            } else if (hit(420, 323 + kTerminalYOffset, 64, 64)) {           // run
                scene->triggerGumpAnim(3, getShapeFn);
                if (!m_simMissions.empty())
                    launchTrainingMission(m_simSelectedIndex);
            } else if (hit(423, 115 + kTerminalYOffset, 64, 64)) {           // default
                scene->triggerGumpAnim(4, getShapeFn);
                m_simShowDetail = false;
            } else if (hit(59, 93 + kTerminalYOffset, 64, 64) || hit(59, 172 + kTerminalYOffset, 64, 64)) {   // scroll up
                scene->triggerGumpAnim(hit(59, 93 + kTerminalYOffset, 64, 64) ? 9 : 10, getShapeFn);
                if (m_simSelectedIndex > 0) m_simSelectedIndex--;
            } else if (hit(59, 282 + kTerminalYOffset, 64, 64) || hit(59, 342 + kTerminalYOffset, 64, 64)) {  // scroll down
                scene->triggerGumpAnim(hit(59, 282 + kTerminalYOffset, 64, 64) ? 11 : 12, getShapeFn);
                if (m_simSelectedIndex + 1 < (int)m_simMissions.size()) m_simSelectedIndex++;
            } else if (!m_simShowDetail) {
                for (size_t i = 0; i < m_simMissions.size(); i++) {
                    if (vx >= listX && vx < listX + 300 && vy >= rowY[i] && vy < rowY[i] + listLineH) {
                        m_simSelectedIndex = (int)i;
                        m_simShowDetail = true;
                        break;
                    }
                }
            }
        }
        break;
    }

    case State::CONFIRM_QUIT: {
        // Render the scene frozen behind the dialog
        sceneFB->clear();
        if (scene) {
            scene->render(sceneFB, &shapePak,
                          [this](int id) { return this->getShape(id); },
                          0);
        }
        drawCursor(0, 0);

        // Overlay "Quit? (Y/N)" centred on screen in the RGBA pass
        displaySceneFramebuffer(0.4f, /*drawHoverLabel=*/false,
            [&](uint32_t* rgba, int fbW, int fbH) {
                WC3Font* font = WC3Globals::getInstance().getFont("GFMD");
                if (font && font->isLoaded()) {
                    const std::string prompt = "Quit? (Y/N)";
                    int tw = font->measureText(prompt);
                    int tx = (fbW - tw) / 2;
                    int ty = (fbH - font->getHeight()) / 2;
                    font->drawTextRGBA(rgba, fbW, fbH, prompt, tx, ty);
                }
            });

        if (m_keyboard) {
            if (m_keyboard->isKeyJustPressed(SDL_SCANCODE_Y))
                this->stop();
            else if (m_keyboard->isKeyJustPressed(SDL_SCANCODE_N))
                state = State::SCENE_ACTIVE;
        }
        break;
    }

    case State::DONE:
        this->stop();
        break;
    }
}
