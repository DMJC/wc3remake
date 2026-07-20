#include "WC4GameFlow.h"
#include "WC4Globals.h"
#include "WC4MVEPlayer.h"
#include "../commons/XtreDecompressor.h"
#include "../realspace/XtreArchive.h"
#include "../engine/EventManager.h"
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// WC4 SHP ("1.11") decoder
// ---------------------------------------------------------------------------

static uint32_t wc4_u32(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}
static uint16_t wc4_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1]<<8));
}

// Decode WC4 RLE pixel data into a flat uint8_t frame buffer.
// Encoded pixels are written at (x_blit+col, y_min+row).
// Positions not covered by any run keep their current value (caller pre-fills
// with 0xFF so unencoded positions appear transparent in RLEShape::Expand).
static void wc4_decode_rle(
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
WC4GameFlow::WC4GameFlow() {}
WC4GameFlow::~WC4GameFlow() {
    if (m_cursor_owned) delete m_cursor;
    delete scene;
    delete sceneFB;
    if (sceneTexture) glDeleteTextures(1, &sceneTexture);
    for (auto& [id, img] : shapeCache) delete img;
}

void WC4GameFlow::initSceneFramebuffer() {
    sceneFB = new FrameBuffer(640, 480);

    // Prefer cursor shape 0 from globals.iff (30×31 px, authentic WC4 cursor).
    // Fall back to the hardcoded 12×12 arrow if globals weren't loaded.
    WC4Globals& globals = WC4Globals::getInstance();
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

void WC4GameFlow::drawCursor(int vx, int vy) {
    if (!sceneFB) return;

    // Select cursor frame from globals.iff based on hovered zone type:
    //   frame 0 — default (no zone)
    //   frame 6 — actor zone (TYPE_CHARACTER, TYPE_PERSON, TYPE_MOVIE_FG, etc.)
    //   frame 7 — room-change zone (TYPE_NAVPOINT)
    //   frame 8 — computer/terminal/button zone (TYPE_BUTTON)
    RLEShape* shape = nullptr;
    RSImageSet* cursors = WC4Globals::getInstance().getCursors();
    if (cursors) {
        int frame = 0;
        switch (m_hoveredZoneType) {
        case WC4Gump::TYPE_NAVPOINT:  frame = 7; break;
        case WC4Gump::TYPE_BUTTON:    frame = 8; break;
        case WC4Gump::TYPE_CHARACTER:
        case WC4Gump::TYPE_PERSON:
        case WC4Gump::TYPE_MOVIE_FG:
        case WC4Gump::TYPE_MOVIE_BG:
        case WC4Gump::TYPE_MOVIE_BG2: frame = 6; break;
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

void WC4GameFlow::displaySceneFramebuffer() {
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
        rgba[i] = r | (g << 8) | (b << 16) | (0xFF << 24);
    }

    // Hover label: white text at the bottom of the screen in pre-flip screen coords.
    // The RGBA buffer uses natural y-down coords here; the flip below handles GL.
    if (!m_hoverLabel.empty()) {
        WC4Font* font = WC4Globals::getInstance().getFont("GFMD");
        if (font && font->isLoaded()) {
            int textW = font->measureText(m_hoverLabel);
            int tx = (fbW - textW) / 2;
            int ty = fbH - font->getHeight() - 12;
            font->drawTextRGBA(rgba, fbW, fbH, m_hoverLabel, tx, ty);
        }
    }

    // Flip vertically for glDrawPixels (GL origin is bottom-left)
    for (int y = 0; y < fbH / 2; y++) {
        for (int x = 0; x < fbW; x++) {
            std::swap(rgba[y * fbW + x], rgba[(fbH - 1 - y) * fbW + x]);
        }
    }

    glViewport(0, 0, screen.width, screen.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screen.width, 0, screen.height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);

    glRasterPos2i(0, 0);
    glPixelZoom((float)screen.width / fbW, (float)screen.height / fbH);
    glDrawPixels(fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    delete[] rgba;
}

RSImageSet* WC4GameFlow::getShape(int shapeId) {
    auto it = shapeCache.find(shapeId);
    if (it != shapeCache.end())
        return it->second;

    if (shapeId < 0 || shapeId >= static_cast<int>(shapePak.GetNumEntries()))
        return nullptr;

    PakEntry* entry = shapePak.GetEntry(shapeId);
    if (!entry || entry->size < 12)
        return nullptr;

    RSImageSet* img = new RSImageSet();

    bool isWC4Shape = (entry->data[0] == '1' && entry->data[1] == '.' &&
                       entry->data[2] == '1' && entry->data[3] == '1');
    if (!isWC4Shape) {
        shapeCache[shapeId] = img;
        return img;
    }

    uint32_t N = wc4_u32(entry->data + 4);
    if (N == 0 || N > 1024 || 8 + N * 8 > entry->size) {
        shapeCache[shapeId] = img;
        return img;
    }

    struct SubInfo {
        uint16_t full_w, full_h, x_blit, y_min, row_width, y_span;
        size_t   pdata_off, pdata_len;
    };
    std::vector<SubInfo> subs;
    subs.reserve(N);

    for (uint32_t i = 0; i < N; i++) {
        uint32_t si_off = wc4_u32(entry->data + 8 + i * 8);
        if (si_off + 24 > entry->size) continue;

        const uint8_t* h = entry->data + si_off;
        uint16_t full_h     = (uint16_t)(wc4_u16(h +  0) + 1);
        uint16_t full_w     = (uint16_t)(wc4_u16(h +  2) + 1);
        uint16_t x_blit     = wc4_u16(h +  8);
        uint16_t y_min      = wc4_u16(h + 12);
        uint16_t x_scan_max = wc4_u16(h + 16);
        uint16_t y_max      = wc4_u16(h + 20);

        if (y_min  > y_max)       y_max      = y_min;
        if (x_scan_max >= full_w) x_scan_max = (uint16_t)(full_w - 1);
        if (y_max      >= full_h) y_max      = (uint16_t)(full_h - 1);

        uint16_t row_width = (uint16_t)(x_scan_max + 1);
        uint16_t y_span    = (uint16_t)(y_max - y_min + 1);

        size_t pdata_off = si_off + 24;
        size_t pdata_len;
        if (i + 1 < N) {
            uint32_t next_off = wc4_u32(entry->data + 8 + (i + 1) * 8);
            pdata_len = (next_off > pdata_off) ? next_off - pdata_off : 0;
        } else {
            pdata_len = (entry->size > pdata_off) ? entry->size - pdata_off : 0;
        }

        subs.push_back({ full_w, full_h, x_blit, y_min, row_width, y_span,
                         pdata_off, pdata_len });
    }

    if (subs.empty()) {
        shapeCache[shapeId] = img;
        return img;
    }

    uint16_t full_w = subs[0].full_w;
    uint16_t full_h = subs[0].full_h;
    if ((size_t)full_w * full_h == 0 || (size_t)full_w * full_h > 4 * 1024 * 1024) {
        shapeCache[shapeId] = img;
        return img;
    }

    // Sub1's data being much smaller than sub0's indicates delta animation mode.
    bool delta_mode = subs.size() > 1 && subs[1].pdata_len * 2 < subs[0].pdata_len;

    // Decode sub0 as keyframe; pre-fill with 0xFF (transparent in RLEShape::Expand).
    std::vector<uint8_t> running_frame((size_t)full_w * full_h, 0xFF);
    {
        const SubInfo& s = subs[0];
        wc4_decode_rle(entry->data + s.pdata_off, s.pdata_len,
                       s.y_span, s.row_width,
                       running_frame.data(), full_w, full_h,
                       s.x_blit, s.y_min);
    }
    {
        RLEShape* shape = new RLEShape();
        shape->InitFromPixels(running_frame.data(), full_w, full_h);
        img->Add(shape);
    }

    // Decode remaining subs as delta patches or independent keyframes.
    for (size_t i = 1; i < subs.size(); i++) {
        const SubInfo& s = subs[i];
        if (!delta_mode) {
            running_frame.assign((size_t)full_w * full_h, 0xFF);
        }
        wc4_decode_rle(entry->data + s.pdata_off, s.pdata_len,
                       s.y_span, s.row_width,
                       running_frame.data(), full_w, full_h,
                       s.x_blit, s.y_min);
        RLEShape* shape = new RLEShape();
        shape->InitFromPixels(running_frame.data(), full_w, full_h);
        img->Add(shape);
    }

    shapeCache[shapeId] = img;
    return img;
}

void WC4GameFlow::logRoomMenu() const {
    if (!scene) return;
    auto& items = scene->getRoomMenuItems();
    if (items.empty()) return;
    printf("--- Room menu (%zu items) ---\n", items.size());
    for (size_t i = 0; i < items.size(); i++)
        printf("  [%zu] %s\n", i, items[i].c_str());
}

void WC4GameFlow::loadBranchPaks() {
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
            printf("WC4: %s not found\n", b.path);
            continue;
        }
        branchPaks[b.idx].load(e->data, e->size);
        delete[] e->data;
        delete e;
    }
}

void WC4GameFlow::ensureSC205() {
    if (!sc205Data.empty()) return;

    const char* path = "..\\..\\DATA\\MOVIES\\SC_205.MVE";
    TreEntry* e = Assets.GetEntryByName(path);
    if (e) {
        sc205Data.assign(e->data, e->data + e->size);
        return;
    }
    static const char* cdTres[] = {
        "MOVIES1.TRE","MOVIES2.TRE","MOVIES3.TRE","MOVIES4.TRE","MOVIES5.TRE","MOVIES6.TRE",
        "cd1miss.tre", "cd2miss.tre", "cd3miss.tre", "cd4miss.tre", nullptr
    };
    for (const char** tre = cdTres; *tre; tre++) {
        e = XtreArchive::LoadSingleEntry(*tre, path);
        if (!e) continue;
        sc205Data.assign(e->data, e->data + e->size);
        delete[] e->data; delete e;
        return;
    }
    printf("WC4: SC_205.MVE not found\n");
}

void WC4GameFlow::playSC205Shot(int shotIndex) {
    ensureSC205();
    if (sc205Data.empty()) return;
    WC4MVEPlayer::playShot(sc205Data.data(), sc205Data.size(), shotIndex);
}

void WC4GameFlow::playMovie(const char* name) {
    char assetPath[128];
    snprintf(assetPath, sizeof(assetPath), "..\\..\\DATA\\MOVIES\\%s", name);

    // 1. Try the preloaded archives (movies.tre etc.)
    TreEntry* entry = Assets.GetEntryByName(assetPath);
    if (entry) {
        WC4MVEPlayer::play(entry->data, entry->size);
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
        WC4MVEPlayer::play(e->data, e->size);
        delete[] e->data;
        delete e;
        return;
    }

    printf("WC4: movie not found: %s\n", name);
}

void WC4GameFlow::loadRoomPalette(int roomIndex) {
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

void WC4GameFlow::loadScene(int sceneId) {
    char scenName[64];
    snprintf(scenName, sizeof(scenName), "..\\..\\DATA\\GAMEFLOW\\SCEN%04d.IFF", sceneId);

    TreEntry* scenEntry = Assets.GetEntryByName(scenName);
    if (!scenEntry) {
        printf("WC4GameFlow: Could not find %s\n", scenName);
        return;
    }

    // Derive stage filename from the SCEN's STGE field (e.g. "victory" → "VICTORY.IFF")
    std::string stageName = WC4Scene::extractStageName(scenEntry->data, scenEntry->size);
    if (stageName.empty()) stageName = "victory";
    for (auto& c : stageName) c = (char)toupper((unsigned char)c);

    char stagePath[128];
    snprintf(stagePath, sizeof(stagePath), "..\\..\\DATA\\GAMEFLOW\\%s.IFF", stageName.c_str());

    briefingDone        = false;
    m_rightClickZoneIdx = -1;
    m_hoveredGumpId     = 0;
    m_hoveredZoneType   = 0;

    if (!scene) scene = new WC4Scene();

    // Load VICTORY.IFF (or whatever stage the SCEN references) first
    TreEntry* stageEntry = Assets.GetEntryByName(stagePath);
    if (stageEntry) {
        scene->loadStage(stageEntry->data, stageEntry->size);
    } else {
        printf("WC4GameFlow: Could not find stage %s\n", stagePath);
    }

    // Apply SCEN overrides on top of the loaded stage
    scene->loadScene(scenEntry->data, scenEntry->size);

    // SCEN INIT bytecode selects room "0001" as the starting room (VAR(76) vs VAR(77) for all others).
    // Find it by ID rather than assuming a fixed index, since VICTORY.IFF loads rooms out of ID order.
    int startRoom = 0;
    for (int i = 0; i < scene->getRoomCount(); i++) {
        if (scene->getStageData().rooms[i].id == "0001") { startRoom = i; break; }
    }
    scene->setRoom(startRoom, [this](int id){ return this->getShape(id); });
    loadRoomPalette(startRoom);

    printf("WC4GameFlow: Loaded scene %d (stage=%s), %d rooms\n",
           sceneId, stagePath, scene->getRoomCount());
    logRoomMenu();
}

void WC4GameFlow::init() {
    if (Game == nullptr)
        this->Game = &GameEngine::getInstance();
    m_keyboard = Game->getKeyboard();

    this->start();
    this->focus();

    // Load SHAPES.PAK
    TreEntry* shapesEntry = Assets.GetEntryByName("..\\..\\DATA\\GAMEFLOW\\SHAPES.PAK");
    if (shapesEntry) {
        shapePak.InitFromRAM("SHAPES.PAK", shapesEntry->data, shapesEntry->size);
        printf("WC4GameFlow: SHAPES.PAK loaded with %zu entries\n", shapePak.GetNumEntries());
        fflush(stdout);
    } else {
        printf("WC4GameFlow: WARNING: SHAPES.PAK not found\n");
    }

    initSceneFramebuffer();
    loadBranchPaks();

    // Check for savegame
    struct stat st;
    savegame_exists = (stat("wc4save.dat", &st) == 0);

    if (savegame_exists) {
        state = State::LOAD_SCENE;
    } else {
        state = State::PLAY_ORIGIN_MOVIE;
    }
}

void WC4GameFlow::runFrame(void) {
    switch (state) {
    case State::PLAY_ORIGIN_MOVIE:
        playMovie("ORIGIN_S.MVE");
        state = State::PLAY_OPENING_MOVIE;
        break;

    case State::PLAY_OPENING_MOVIE:
        playMovie("SC_0010.MVE");
        state = State::LOAD_SCENE;
        break;

    case State::LOAD_SCENE:
        loadScene(current_scene);
        state = State::SCENE_ACTIVE;
        break;

    case State::SCENE_ACTIVE: {
        sceneFB->clear();

        if (scene) {
            scene->render(sceneFB, &shapePak,
                          [this](int id) { return this->getShape(id); },
                          m_hoveredGumpId);
        }

        // Map SDL window coords → 640×480 virtual coords
        EventManager& events = EventManager::getInstance();
        RSScreen& screen = RSScreen::instance();
        int mx, my;
        events.getMousePosition(mx, my);
        int vx = (screen.width  > 0) ? mx * 640 / screen.width  : mx;
        int vy = (screen.height > 0) ? my * 480 / screen.height : my;

        // Hover: identify zone under cursor, update label and cursor type
        m_hoverLabel.clear();
        m_hoveredGumpId   = 0;
        m_hoveredZoneType = 0;
        if (scene) {
            int zoneIdx = scene->hitTest(vx, vy);
            if (zoneIdx >= 0) {
                const auto& z = scene->getZones()[zoneIdx];
                m_hoverLabel      = z.label;
                m_hoveredGumpId   = z.gump_id;
                m_hoveredZoneType = z.type;
            }
        }

        // Right-click: cycle cursor to the centre of each zone in sequence
        if (events.isMouseButtonJustPressed(SDL_BUTTON_RIGHT)) {
            if (scene && !scene->getZones().empty()) {
                const auto& zones = scene->getZones();
                m_rightClickZoneIdx = (m_rightClickZoneIdx + 1) % (int)zones.size();
                const auto& z = zones[m_rightClickZoneIdx];
                int wx = (z.x + z.w / 2) * screen.width  / 640;
                int wy = (z.y + z.h / 2) * screen.height / 480;
                SDL_WarpMouseInWindow(screen.getSDLWindow(), wx, wy);
            }
        }

        // Left-click: hit-test the clickable zones and execute ACTV action
        if (events.isMouseButtonJustPressed(SDL_BUTTON_LEFT)) {
            if (scene) {
                int zoneIdx = scene->hitTest(vx, vy);
                if (zoneIdx >= 0) {
                    const auto& zone = scene->getZones()[zoneIdx];
                    printf("WC4: clicked zone %d gump_id=%u titl=%d rect=(%d,%d %dx%d) at (%d,%d)\n",
                           zoneIdx, zone.gump_id, zone.titl_id,
                           zone.x, zone.y, zone.w, zone.h, vx, vy);

                    auto action = scene->activateGump(zone.gump_id);

                    // Play Blair's voice line before the transition
                    if (action.branchEntry >= 0) {
                        for (int pi = 0; pi < 4; pi++) {
                            if (!branchPaks[pi].isEmpty() &&
                                branchPaks[pi].getEntry(action.branchEntry)) {
                                branchPaks[pi].playEntry(action.branchEntry);
                                break;
                            }
                        }
                    }

                    // Handle dialogue subscene (VAR(7) N > 18).
                    // Scene 164 = Colonel Eisen mission briefing; attending it unlocks
                    // loadout (room 4) and wingman selection (room 7).
                    if (action.subscene >= 0) {
                        if (action.subscene == 164 && !briefingDone) {
                            briefingDone = true;
                            printf("WC4: Mission briefing complete — loadout and wingman unlocked.\n");
                        }
                        printf("WC4: subscene %d\n", action.subscene);
                        // TODO: play subscene video/dialogue when subscene loader is implemented
                    }

                    if (action.transitionShot >= 0 && opt_transition_videos)
                        playSC205Shot(action.transitionShot);

                    if (action.type == WC4Scene::ActionResult::GoRoom) {
                        // Gate loadout (room 4) and wingman selection (room 7) behind briefing
                        if (!briefingDone && (action.target == 4 || action.target == 7)) {
                            printf("WC4: Attend the mission briefing first.\n");
                        } else {
                            int stageIdx = scene->scenRoomToStageIndex(action.target);
                            if (stageIdx >= 0) {
                                for (auto& [id, img] : shapeCache) delete img;
                                shapeCache.clear();
                                m_rightClickZoneIdx = -1;
                                scene->setRoom(stageIdx, [this](int id){ return this->getShape(id); });
                                loadRoomPalette(stageIdx);
                                logRoomMenu();
                            } else {
                                printf("WC4: GoRoom %d — no matching stage room\n", action.target);
                            }
                        }
                    }
                } else {
                    printf("WC4: click at (%d,%d) — no zone\n", vx, vy);
                }
            }
        }

        drawCursor(vx, vy);
        displaySceneFramebuffer();

        if (m_keyboard) {
            for (int i = 0; i < 10; i++) {
                if (m_keyboard->isKeyJustPressed(static_cast<SDL_Scancode>(SDL_SCANCODE_0 + i))) {
                    if (scene) {
                        int stageIdx = scene->scenRoomToStageIndex(i);
                        if (stageIdx >= 0) {
                            m_rightClickZoneIdx = -1;
                            for (auto& [id, img] : shapeCache) delete img;
                            shapeCache.clear();
                            scene->setRoom(stageIdx, [this](int id){ return this->getShape(id); });
                            loadRoomPalette(stageIdx);
                        }
                    }
                }
            }
            bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
            if (altHeld && m_keyboard->isKeyJustPressed(SDL_SCANCODE_X)) {
                state = State::CONFIRM_QUIT;
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
        RSScreen& screen2 = RSScreen::instance();
        VGAPalette* pal2 = VGA.getPalette();
        int fbW = sceneFB->width, fbH = sceneFB->height;
        uint32_t* rgba = new uint32_t[fbW * fbH];
        for (int i = 0; i < fbW * fbH; i++) {
            uint8_t idx = sceneFB->framebuffer[i];
            const Texel* c = pal2->GetRGBColor(idx);
            uint8_t r = c->r, g = c->g, b = c->b;
            if (r == 0xFF && g == 0xFF && b == 0x02) { r = g = b = 0xFF; }
            // Darken scene pixels to 40% so the dialog stands out
            rgba[i] = (uint8_t)(r * 0.4f) | ((uint8_t)(g * 0.4f) << 8) |
                      ((uint8_t)(b * 0.4f) << 16) | (0xFF << 24);
        }

        WC4Font* font = WC4Globals::getInstance().getFont("GFMD");
        if (font && font->isLoaded()) {
            const std::string prompt = "Quit? (Y/N)";
            int tw = font->measureText(prompt);
            int tx = (fbW - tw) / 2;
            int ty = (fbH - font->getHeight()) / 2;
            font->drawTextRGBA(rgba, fbW, fbH, prompt, tx, ty);
        }

        // Flip for GL
        for (int y = 0; y < fbH / 2; y++)
            for (int x = 0; x < fbW; x++)
                std::swap(rgba[y * fbW + x], rgba[(fbH - 1 - y) * fbW + x]);

        glViewport(0, 0, screen2.width, screen2.height);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0, screen2.width, 0, screen2.height, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);   glDisable(GL_FOG); glDisable(GL_BLEND);
        glRasterPos2i(0, 0);
        glPixelZoom((float)screen2.width / fbW, (float)screen2.height / fbH);
        glDrawPixels(fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        delete[] rgba;

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
