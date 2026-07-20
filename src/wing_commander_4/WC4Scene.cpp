#include "WC4Scene.h"
#include <cstring>
#include <SDL2/SDL.h>

uint32_t WC4Scene::readU32LE(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

uint32_t WC4Scene::readU32BE(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

WC4Scene::WC4Scene() {}
WC4Scene::~WC4Scene() {}

void WC4Scene::parseGump(const uint8_t* data, size_t size, WC4Gump& gump) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        char tag[5] = {};
        memcpy(tag, &data[pos], 4);
        uint32_t sz = readU32BE(&data[pos + 4]);
        size_t cend = pos + 8 + sz;
        if (cend > size) break;

        if (strcmp(tag, "_ID_") == 0 && sz == 4)
            gump.id = readU32LE(&data[pos + 8]);
        else if (strcmp(tag, "TYPE") == 0 && sz == 4)
            gump.type = readU32LE(&data[pos + 8]);
        else if (strcmp(tag, "CODE") == 0)
            gump.code.assign(&data[pos + 8], &data[pos + 8 + sz]);
        else if (strcmp(tag, "FORM") == 0 && sz >= 4) {
            char ft[5] = {};
            memcpy(ft, &data[pos + 8], 4);
            if (strcmp(ft, "SCRN") == 0) {
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    char st[5] = {};
                    memcpy(st, &data[j], 4);
                    uint32_t ss = readU32BE(&data[j + 4]);
                    if (strcmp(st, "_ID_") == 0 && ss == 4)
                        gump.scrn_id = static_cast<int>(readU32LE(&data[j + 8]));
                    else if (strcmp(st, "CORD") == 0 && ss >= 8) {
                        gump.x = static_cast<int>(readU32LE(&data[j + 8]));
                        gump.y = static_cast<int>(readU32LE(&data[j + 12]));
                        if (ss >= 16) {
                            int x2 = static_cast<int>(readU32LE(&data[j + 16]));
                            int y2 = static_cast<int>(readU32LE(&data[j + 20]));
                            gump.w = x2 - gump.x;
                            gump.h = y2 - gump.y;
                        }
                    } else if (strcmp(st, "FORM") == 0 && ss >= 4) {
                        char sft[5] = {};
                        memcpy(sft, &data[j + 8], 4);
                        if (strcmp(sft, "TITL") == 0) {
                            size_t m = j + 12;
                            size_t te = j + 8 + ss;
                            while (m + 8 <= te) {
                                uint32_t ts = readU32BE(&data[m + 4]);
                                if (memcmp(&data[m], "_ID_", 4) == 0 && ts == 4) {
                                    gump.titl_id = static_cast<int>(readU32LE(&data[m + 8]));
                                } else if (memcmp(&data[m], "ENG_", 4) == 0 && ts > 8) {
                                    // TYPE_MENU_ACTION inline menu text:
                                    // uint32 total_chars, uint32 num_items, then null-separated strings
                                    const uint8_t* txt = &data[m + 8];
                                    uint32_t numItems = readU32LE(txt + 4);
                                    const char* p = reinterpret_cast<const char*>(txt + 8);
                                    const char* pe = reinterpret_cast<const char*>(txt + ts);
                                    for (uint32_t ni = 0; ni < numItems && p < pe; ni++) {
                                        size_t len = strnlen(p, pe - p);
                                        gump.menu_items.emplace_back(p, len);
                                        p += len + 1;
                                    }
                                }
                                m += 8 + ts;
                                if (ts & 1) m++;
                            }
                        }
                    }
                    j += 8 + ss;
                    if (ss & 1) j++;
                }
            } else if (strcmp(ft, "PNTR") == 0) {
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    uint32_t ss = readU32BE(&data[j + 4]);
                    if (memcmp(&data[j], "_ID_", 4) == 0 && ss == 4)
                        gump.pntr_id = static_cast<int>(readU32LE(&data[j + 8]));
                    j += 8 + ss;
                    if (ss & 1) j++;
                }
            } else if (strcmp(ft, "ANIM") == 0) {
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    uint32_t ss = readU32BE(&data[j + 4]);
                    if (memcmp(&data[j], "FPS_", 4) == 0 && ss == 4)
                        gump.anim_fps = static_cast<int>(readU32LE(&data[j + 8]));
                    j += 8 + ss;
                    if (ss & 1) j++;
                }
            } else if (strcmp(ft, "TITL") == 0) {
                // Top-level FORM TITL (sibling to SCRN): tooltip shape ID
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    uint32_t ss = readU32BE(&data[j + 4]);
                    if (memcmp(&data[j], "_ID_", 4) == 0 && ss == 4)
                        gump.titl_id = static_cast<int>(readU32LE(&data[j + 8]));
                    j += 8 + ss;
                    if (ss & 1) j++;
                }
            }
        }

        pos = cend;
        if (sz & 1) pos++;
    }
}

void WC4Scene::parseGumps(const uint8_t* data, size_t size, std::vector<WC4Gump>& out,
                          bool skipMenu) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t sz = readU32BE(&data[pos + 4]);
        size_t cend = pos + 8 + sz;
        if (cend > size) break;

        if (memcmp(&data[pos], "FORM", 4) == 0 && sz >= 4 && memcmp(&data[pos + 8], "GUMP", 4) == 0) {
            WC4Gump gump;
            parseGump(&data[pos + 12], sz - 4, gump);
            if (!(skipMenu && gump.type == WC4Gump::TYPE_MENU))
                out.push_back(gump);
        }

        pos = cend;
        if (sz & 1) pos++;
    }
}

// ---------------------------------------------------------------------------
// Stage loading — VICTORY.IFF (FORM STGE)
// ---------------------------------------------------------------------------

bool WC4Scene::loadStage(const uint8_t* data, size_t size) {
    if (size < 12 || memcmp(data, "FORM", 4) != 0) return false;
    if (memcmp(&data[8], "STGE", 4) != 0) return false;

    uint32_t formSize = readU32BE(&data[4]);
    size_t end = std::min(size, static_cast<size_t>(formSize) + 8);
    stageData = {};

    size_t pos = 12;
    while (pos + 8 <= end) {
        char tag[5] = {};
        memcpy(tag, &data[pos], 4);
        uint32_t sz = readU32BE(&data[pos + 4]);
        size_t cend = pos + 8 + sz;
        if (cend > end) break;

        if (strcmp(tag, "FORM") == 0 && sz >= 4) {
            char ft[5] = {};
            memcpy(ft, &data[pos + 8], 4);

            if (strcmp(ft, "INIT") == 0) {
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    uint32_t cs = readU32BE(&data[j + 4]);
                    if (memcmp(&data[j], "CODE", 4) == 0)
                        stageData.init_code.assign(&data[j + 8], &data[j + 8 + cs]);
                    j += 8 + cs;
                    if (cs & 1) j++;
                }
            } else if (strcmp(ft, "ROOM") == 0) {
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    if (memcmp(&data[j], "FORM", 4) != 0) break;
                    uint32_t rsz = readU32BE(&data[j + 4]);
                    size_t rend = j + 8 + rsz;

                    WC4StageRoom room;
                    room.id = std::string(reinterpret_cast<const char*>(&data[j + 8]), 4);

                    size_t k = j + 12;
                    while (k + 8 <= rend) {
                        char rt[5] = {};
                        memcpy(rt, &data[k], 4);
                        uint32_t rs = readU32BE(&data[k + 4]);
                        size_t re = k + 8 + rs;

                        if (strcmp(rt, "FORM") == 0 && rs >= 4) {
                            char rft[5] = {};
                            memcpy(rft, &data[k + 8], 4);

                            if (strcmp(rft, "PALT") == 0) {
                                size_t m = k + 12;
                                while (m + 8 <= re) {
                                    uint32_t ms = readU32BE(&data[m + 4]);
                                    if (memcmp(&data[m], "_ID_", 4) == 0 && ms == 4)
                                        room.palette_id = static_cast<int>(readU32LE(&data[m + 8]));
                                    m += 8 + ms;
                                    if (ms & 1) m++;
                                }
                            } else if (strcmp(rft, "BKGD") == 0) {
                                std::vector<WC4Gump> bgGumps;
                                parseGumps(&data[k + 12], rs - 4, bgGumps);
                                if (!bgGumps.empty()) {
                                    room.bkgd_shape_id = bgGumps[0].scrn_id;
                                    room.bkgd_x = bgGumps[0].x;
                                    room.bkgd_y = bgGumps[0].y;
                                }
                            } else if (strcmp(rft, "ACTR") == 0) {
                                parseGumps(&data[k + 12], rs - 4, room.actors, true);
                            } else if (strcmp(rft, "ACTV") == 0) {
                                std::vector<WC4Gump> actvGumps;
                                parseGumps(&data[k + 12], rs - 4, actvGumps);
                                for (auto& ag : actvGumps)
                                    if (!ag.code.empty())
                                        room.actv_codes.push_back({ag.id, ag.code});
                            } else if (strcmp(rft, "INIT") == 0) {
                                size_t m = k + 12;
                                while (m + 8 <= re) {
                                    uint32_t ms = readU32BE(&data[m + 4]);
                                    if (memcmp(&data[m], "CODE", 4) == 0)
                                        room.init_code.assign(&data[m + 8], &data[m + 8 + ms]);
                                    m += 8 + ms;
                                    if (ms & 1) m++;
                                }
                            } else if (strcmp(rft, "EXIT") == 0) {
                                size_t m = k + 12;
                                while (m + 8 <= re) {
                                    uint32_t ms = readU32BE(&data[m + 4]);
                                    if (memcmp(&data[m], "CODE", 4) == 0)
                                        room.exit_code.assign(&data[m + 8], &data[m + 8 + ms]);
                                    m += 8 + ms;
                                    if (ms & 1) m++;
                                }
                            }
                        }

                        k = re;
                        if (rs & 1) k++;
                    }

                    stageData.rooms.push_back(room);
                    j = rend;
                    if (rsz & 1) j++;
                }
            }
        }

        pos = cend;
        if (sz & 1) pos++;
    }

    printf("WC4Scene: loaded stage with %zu rooms\n", stageData.rooms.size());
    return true;
}

// ---------------------------------------------------------------------------
// Scene loading — SCEN####.IFF (FORM SCNE)
// Merges XTRA gumps and script codes directly into the already-loaded stageData.
// ---------------------------------------------------------------------------

std::string WC4Scene::extractStageName(const uint8_t* data, size_t size) {
    if (size < 12 || memcmp(data, "FORM", 4) != 0) return {};
    if (memcmp(&data[8], "SCNE", 4) != 0) return {};

    uint32_t formSize = readU32BE(&data[4]);
    size_t end = std::min(size, static_cast<size_t>(formSize) + 8);
    size_t pos = 12;
    while (pos + 8 <= end) {
        uint32_t sz = readU32BE(&data[pos + 4]);
        size_t cend = pos + 8 + sz;
        if (cend > end) break;
        if (memcmp(&data[pos], "STGE", 4) == 0 && sz > 0) {
            std::string name(reinterpret_cast<const char*>(&data[pos + 8]), sz);
            while (!name.empty() && name.back() == '\0') name.pop_back();
            return name;
        }
        pos = cend;
        if (sz & 1) pos++;
    }
    return {};
}

bool WC4Scene::loadScene(const uint8_t* data, size_t size) {
    if (size < 12 || memcmp(data, "FORM", 4) != 0) return false;
    if (memcmp(&data[8], "SCNE", 4) != 0) return false;

    uint32_t formSize = readU32BE(&data[4]);
    size_t end = std::min(size, static_cast<size_t>(formSize) + 8);

    std::string stageName;
    int roomsOverridden = 0;

    size_t pos = 12;
    while (pos + 8 <= end) {
        char tag[5] = {};
        memcpy(tag, &data[pos], 4);
        uint32_t sz = readU32BE(&data[pos + 4]);
        size_t cend = pos + 8 + sz;
        if (cend > end) break;

        if (strcmp(tag, "STGE") == 0 && sz > 0) {
            stageName.assign(reinterpret_cast<const char*>(&data[pos + 8]), sz);
            while (!stageName.empty() && stageName.back() == '\0') stageName.pop_back();
        } else if (strcmp(tag, "FORM") == 0 && sz >= 4) {
            char ft[5] = {};
            memcpy(ft, &data[pos + 8], 4);

            if (strcmp(ft, "ROOM") == 0) {
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    if (memcmp(&data[j], "FORM", 4) != 0) break;
                    uint32_t rsz = readU32BE(&data[j + 4]);
                    size_t rend = j + 8 + rsz;

                    std::string roomId(reinterpret_cast<const char*>(&data[j + 8]), 4);

                    // Find the matching stage room to override
                    WC4StageRoom* stageRoom = nullptr;
                    for (auto& sr : stageData.rooms) {
                        if (sr.id == roomId) { stageRoom = &sr; break; }
                    }

                    size_t k = j + 12;
                    while (k + 8 <= rend) {
                        char rt[5] = {};
                        memcpy(rt, &data[k], 4);
                        uint32_t rs = readU32BE(&data[k + 4]);
                        size_t re = k + 8 + rs;

                        if (strcmp(rt, "FORM") == 0 && rs >= 4) {
                            char rft[5] = {};
                            memcpy(rft, &data[k + 8], 4);

                            if (strcmp(rft, "XTRA") == 0 && stageRoom) {
                                // Merge XTRA gumps into stage actors
                                parseGumps(&data[k + 12], rs - 4, stageRoom->actors);
                                roomsOverridden++;
                            } else if (strcmp(rft, "ACTV") == 0 && stageRoom) {
                                // Scene ACTV overrides stage ACTV
                                std::vector<WC4Gump> actvGumps;
                                parseGumps(&data[k + 12], rs - 4, actvGumps);
                                for (auto& ag : actvGumps)
                                    if (!ag.code.empty())
                                        stageRoom->actv_codes.push_back({ag.id, ag.code});
                            } else if (strcmp(rft, "INIT") == 0 && stageRoom) {
                                size_t m = k + 12;
                                while (m + 8 <= re) {
                                    uint32_t ms = readU32BE(&data[m + 4]);
                                    if (memcmp(&data[m], "CODE", 4) == 0)
                                        stageRoom->init_code.assign(&data[m + 8], &data[m + 8 + ms]);
                                    m += 8 + ms;
                                    if (ms & 1) m++;
                                }
                            } else if (strcmp(rft, "EXIT") == 0 && stageRoom) {
                                size_t m = k + 12;
                                while (m + 8 <= re) {
                                    uint32_t ms = readU32BE(&data[m + 4]);
                                    if (memcmp(&data[m], "CODE", 4) == 0)
                                        stageRoom->exit_code.assign(&data[m + 8], &data[m + 8 + ms]);
                                    m += 8 + ms;
                                    if (ms & 1) m++;
                                }
                            }
                        }

                        k = re;
                        if (rs & 1) k++;
                    }

                    j = rend;
                    if (rsz & 1) j++;
                }
            }
        }

        pos = cend;
        if (sz & 1) pos++;
    }

    printf("WC4Scene: applied scene '%s' — %d rooms overridden\n",
           stageName.c_str(), roomsOverridden);
    return true;
}

// ---------------------------------------------------------------------------
// Action execution
// ---------------------------------------------------------------------------

int WC4Scene::scenRoomToStageIndex(int scenRoomIndex) const {
    char id[5];
    snprintf(id, sizeof(id), "%04d", scenRoomIndex);
    for (int i = 0; i < static_cast<int>(stageData.rooms.size()); i++)
        if (stageData.rooms[i].id == id) return i;
    return -1;
}

WC4Scene::ActionResult WC4Scene::activateGump(uint32_t gump_id) const {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return {};
    auto& room = stageData.rooms[currentRoom];
    for (auto& [id, code] : room.actv_codes) {
        if (id != gump_id) continue;
        ActionResult result;
        // Scan 32-bit LE word stream for VAR(6) and VAR(7)
        for (size_t i = 0; i + 4 <= code.size(); i += 4) {
            uint32_t w = readU32LE(&code[i]);
            if (w == 0xFACE0006 && i + 8 <= code.size()) {
                // VAR(6) N — intra-stage room navigation
                uint32_t n = readU32LE(&code[i + 4]);
                printf("WC4Scene: gump %u → VAR(6) %u (go to SCEN room %u)\n", gump_id, n, n);
                // Bare VAR(6) with no prior VAR(7) shot → use shot 0 (default navpoint walk)
                if (result.transitionShot < 0)
                    result.transitionShot = 0;
                result.type   = ActionResult::GoRoom;
                result.target = static_cast<int>(n);
            } else if (w == 0xFACE0007 && i + 8 <= code.size()) {
                // VAR(7) N [mode] — N≤18: SC_205 shot; N>18: dialogue subscene
                // Always continue scanning — VAR(6) that follows is the room navigation.
                uint32_t n = readU32LE(&code[i + 4]);
                if (n <= 18) {
                    result.transitionShot = static_cast<int>(n);
                    printf("WC4Scene: gump %u → VAR(7) %u (SC_205 shot)\n", gump_id, n);
                } else {
                    result.subscene = static_cast<int>(n);
                    printf("WC4Scene: gump %u → VAR(7) %u (subscene)\n", gump_id, n);
                }
            } else if (w == 0xFACE0031 && i + 8 <= code.size()) {
                // VAR(49) K — play BRANCH PAK entry K (Blair's voice line)
                uint32_t k = readU32LE(&code[i + 4]);
                if (result.branchEntry < 0) {
                    result.branchEntry = static_cast<int>(k);
                    printf("WC4Scene: gump %u → VAR(49) %u (branch entry)\n", gump_id, k);
                }
            }
        }
        if (result.type != ActionResult::None || result.transitionShot >= 0 || result.subscene >= 0 || result.branchEntry >= 0)
            return result;
    }
    return {};
}

// ---------------------------------------------------------------------------

void WC4Scene::setRoom(int roomIndex, std::function<RSImageSet*(int)> getShape) {
    if (roomIndex >= 0 && roomIndex < static_cast<int>(stageData.rooms.size())) {
        currentRoom = roomIndex;
        printf("WC4Scene: entering room %d (%s)\n",
               roomIndex, stageData.rooms[roomIndex].id.c_str());
    }
    buildZones(getShape);
}

const std::vector<std::string>& WC4Scene::getRoomMenuItems() const {
    static const std::vector<std::string> empty;
    if (currentRoom < 0 || currentRoom >= (int)stageData.rooms.size())
        return empty;
    return stageData.rooms[currentRoom].menu_items;
}

void WC4Scene::buildZones(std::function<RSImageSet*(int)> getShape) {
    zones.clear();
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return;

    auto& room = stageData.rooms[currentRoom];

    // Populate room menu_items from the SCEN XTRA TYPE_MENU gump.
    // Stage ACTR TYPE_MENU gumps are excluded at parse time (skipMenu=true),
    // so whatever is here came from the SCEN file.
    room.menu_items.clear();
    for (auto& actor : room.actors) {
        if (actor.type == WC4Gump::TYPE_MENU && !actor.menu_items.empty()) {
            room.menu_items = actor.menu_items;
            break;
        }
    }

    // Helper: look up sprite dimensions via getShape
    auto spriteSize = [&](int scrn_id, int& w, int& h) {
        if (scrn_id >= 0 && getShape) {
            RSImageSet* img = getShape(scrn_id);
            if (img && img->GetNumImages() > 0) {
                if (RLEShape* s = img->GetShape(0)) {
                    w = s->GetWidth();
                    h = s->GetHeight();
                    return;
                }
            }
        }
        w = 64; h = 64; // fallback
    };

    for (auto& actor : room.actors) {
        if (actor.type == WC4Gump::TYPE_NAVPOINT) {
            if (actor.w <= 0 || actor.h <= 0) continue;
            Zone z;
            z.x = actor.x;  z.y = actor.y;
            z.w = actor.w;  z.h = actor.h;
            z.gump_id = actor.id;
            z.type    = actor.type;
            z.titl_id = actor.titl_id;
            z.anim_fps = actor.anim_fps;
            zones.push_back(z);
        } else if (actor.type == WC4Gump::TYPE_BUTTON) {
            // Floor selector buttons (e.g. lift): position from CORD, size from sprite
            if (actor.scrn_id < 0) continue;
            Zone z;
            z.x = actor.x;  z.y = actor.y;
            z.gump_id = actor.id;
            z.type    = actor.type;
            z.titl_id = actor.titl_id;
            z.anim_fps = actor.anim_fps;
            if (actor.w > 0 && actor.h > 0) {
                z.w = actor.w;  z.h = actor.h;
            } else {
                spriteSize(actor.scrn_id, z.w, z.h);
            }
            zones.push_back(z);
        } else if (actor.type == WC4Gump::TYPE_CHARACTER  ||
                   actor.type == WC4Gump::TYPE_MOVIE_FG   ||
                   actor.type == WC4Gump::TYPE_MOVIE_BG2  ||
                   actor.type == WC4Gump::TYPE_PERSON      ||
                   actor.type == WC4Gump::TYPE_MOVIE_BG) {
            if (actor.scrn_id < 0) continue;
            Zone z;
            z.x = actor.x;  z.y = actor.y;
            z.gump_id = actor.id;
            z.type    = actor.type;
            z.titl_id = actor.titl_id;
            z.anim_fps = actor.anim_fps;
            if (actor.w > 0 && actor.h > 0) {
                z.w = actor.w;  z.h = actor.h;
            } else {
                spriteSize(actor.scrn_id, z.w, z.h);
            }
            zones.push_back(z);
        } else if (actor.type == WC4Gump::TYPE_MENU) {
            Zone z;
            z.x = actor.x;  z.y = actor.y;
            z.w = actor.w > 0 ? actor.w : 200;
            z.h = actor.h > 0 ? actor.h : 40;
            z.gump_id = actor.id;
            z.type    = actor.type;
            z.titl_id = actor.titl_id;
            z.anim_fps = actor.anim_fps;
            zones.push_back(z);
        }
    }

    // Assign labels from the room menu items to zones that have navigation actions,
    // in the order they appear. Zones with no ACTV action (None) are skipped.
    const auto& items = room.menu_items;
    if (!items.empty()) {
        int menuIdx = 0;
        for (auto& z : zones) {
            if (menuIdx >= (int)items.size()) break;
            ActionResult r = activateGump(z.gump_id);
            if (r.type != ActionResult::None || r.subscene >= 0 || r.branchEntry >= 0) {
                z.label = items[menuIdx++];
            }
        }
    }
}

int WC4Scene::hitTest(int mx, int my) const {
    for (int i = 0; i < static_cast<int>(zones.size()); i++) {
        auto& z = zones[i];
        if (mx >= z.x && mx < z.x + z.w && my >= z.y && my < z.y + z.h)
            return i;
    }
    return -1;
}

void WC4Scene::render(FrameBuffer* fb, PakArchive* shapes,
                      std::function<RSImageSet*(int)> getShape, uint32_t hoveredGumpId) {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return;

    auto& room = stageData.rooms[currentRoom];

    // Draw background
    if (room.bkgd_shape_id >= 0) {
        RSImageSet* bg = getShape(room.bkgd_shape_id);
        if (bg && bg->GetNumImages() > 0) {
            RLEShape* shape = bg->GetShape(0);
            if (shape) {
                Point2D p = {room.bkgd_x, room.bkgd_y};
                shape->SetPosition(&p);
                fb->drawShape(shape);
            }
        }
    }

    uint32_t now = SDL_GetTicks();

    // Draw all actors (stage ACTR + scene XTRA merged).
    // Animation policy:
    //   TYPE_CHARACTER — hover-only: only animate while the cursor is over this gump.
    //   All other animated types — auto-loop: advance unconditionally.
    for (auto& actor : room.actors) {
        if (actor.scrn_id < 0 || actor.type == WC4Gump::TYPE_MENU) continue;
        // TYPE_MOVIE_BG / TYPE_MOVIE_BG2 are FMV video slots — never render as static sprites.
        if (actor.type == WC4Gump::TYPE_MOVIE_BG || actor.type == WC4Gump::TYPE_MOVIE_BG2) continue;
        RSImageSet* img = getShape(actor.scrn_id);
        if (!img || img->GetNumImages() == 0) continue;
        int n = img->GetNumImages();
        int frame = 0;
        if (actor.anim_fps > 0 && n > 1) {
            bool hoverOnly = (actor.type == WC4Gump::TYPE_CHARACTER);
            if (!hoverOnly || actor.id == hoveredGumpId) {
                frame = (now / (1000u / (uint32_t)actor.anim_fps)) % (uint32_t)n;
            }
        }
        RLEShape* shape = img->GetShape(frame);
        if (shape) {
            Point2D p = {actor.x, actor.y};
            shape->SetPosition(&p);
            fb->drawShape(shape);
        }
    }
}
