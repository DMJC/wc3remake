#include "WC3Scene.h"
#include <cstring>
#include <SDL2/SDL.h>

uint32_t WC3Scene::readU32LE(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

uint32_t WC3Scene::readU32BE(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

WC3Scene::WC3Scene() {
    // Bank-A slot 36 is read by many rooms' own INIT bytecode (idiom: "IF NOT
    // GET-A(36): HIDE(decorative-loop-background-gump)") but is never written by
    // any GAMEFLOW/SCEN/STGE script anywhere in the game data — confirmed by an
    // exhaustive scan of every SCEN file and VICTORY.IFF. It must be a flag set
    // by wc3.exe's native code outside the interpreted bytecode (plausibly a
    // hardware/graphics-mode "ready" flag), permanently true during normal
    // interactive play. Seed it true so those backgrounds render as intended
    // instead of staying hidden for lack of anything ever setting it.
    varsA[36] = 1;
    // Bank-B slot 154 is read by every mission's Flight Control (room 0003)
    // INIT, at the exact same spot, in every single SCEN file checked (idiom:
    // "IF B154 AND NOT B155: show(crew/briefing-trigger gumps) ELSE hide
    // them" — B155 is "wingman selection committed", set once the player
    // picks a pilot). Like bank-A slot 36 above, nothing anywhere in
    // GAMEFLOW/SCEN/STGE bytecode ever writes bank-B slot 154 — it must be
    // another native-code "ready" flag, true throughout normal play until
    // superseded by B155. Seed it true so the crew-talk/"Attend BRIEFING."
    // trigger is reachable at all.
    varsB[154] = 1;
}
WC3Scene::~WC3Scene() {}

void WC3Scene::parseGump(const uint8_t* data, size_t size, WC3Gump& gump) {
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
            } else if (strcmp(ft, "SOND") == 0) {
                // Top-level FORM SOND: attached sound-effect sample index(es)
                // (GAMEFLOW/SOUND/GFSAMPLE.IFF record indices). _ID_'s payload
                // is one or more [index:u16LE][marker:u16LE=0xEAEA] pairs.
                size_t j = pos + 12;
                while (j + 8 <= cend) {
                    uint32_t ss = readU32BE(&data[j + 4]);
                    if (memcmp(&data[j], "_ID_", 4) == 0) {
                        for (size_t o = 0; o + 4 <= ss; o += 4)
                            gump.sound_ids.push_back(
                                static_cast<int>(data[j + 8 + o] | (data[j + 8 + o + 1] << 8)));
                    }
                    j += 8 + ss;
                    if (ss & 1) j++;
                }
            }
        }

        pos = cend;
        if (sz & 1) pos++;
    }
}

void WC3Scene::parseGumps(const uint8_t* data, size_t size, std::vector<WC3Gump>& out,
                          bool skipMenu) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint32_t sz = readU32BE(&data[pos + 4]);
        size_t cend = pos + 8 + sz;
        if (cend > size) break;

        if (memcmp(&data[pos], "FORM", 4) == 0 && sz >= 4 && memcmp(&data[pos + 8], "GUMP", 4) == 0) {
            WC3Gump gump;
            parseGump(&data[pos + 12], sz - 4, gump);
            if (!(skipMenu && gump.type == WC3Gump::TYPE_MENU))
                out.push_back(gump);
        }

        pos = cend;
        if (sz & 1) pos++;
    }
}

// ---------------------------------------------------------------------------
// Stage loading — VICTORY.IFF (FORM STGE)
// ---------------------------------------------------------------------------

bool WC3Scene::loadStage(const uint8_t* data, size_t size) {
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

                    WC3StageRoom room;
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
                                std::vector<WC3Gump> bgGumps;
                                parseGumps(&data[k + 12], rs - 4, bgGumps);
                                if (!bgGumps.empty()) {
                                    room.bkgd_shape_id = bgGumps[0].scrn_id;
                                    room.bkgd_x = bgGumps[0].x;
                                    room.bkgd_y = bgGumps[0].y;
                                }
                            } else if (strcmp(rft, "ACTR") == 0) {
                                // Keep the stage's own TYPE_MENU actor as a fallback — most
                                // SCEN files don't redefine it, and buildZones() prefers the
                                // last TYPE_MENU actor found, so a SCEN XTRA override (merged
                                // in later, in loadScene()) still takes priority when present.
                                parseGumps(&data[k + 12], rs - 4, room.actors);
                            } else if (strcmp(rft, "ACTV") == 0) {
                                std::vector<WC3Gump> actvGumps;
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

    printf("WC3Scene: loaded stage with %zu rooms\n", stageData.rooms.size());

    // Run the stage's own top-level INIT once, right after parsing — this was
    // parsed into stageData.init_code but never actually executed anywhere,
    // so the scene-global bank-A/B SETs it makes (e.g. VICTORY.IFF's is just
    // three VAR(8) slot/-1 SETs — bank-A 22/23/24 = -1, a "no channel picked
    // yet" sentinel several rooms' own INIT bytecode reads via GET-A) never
    // took effect. That left those slots at their unset default (0), which
    // rooms' own INIT misread as "channel 0 selected" — e.g. room 0001
    // (Flight Deck) hid its default placeholder gump (scrn32) and showed the
    // channel-0 ship (scrn16) instead, even on the very first visit before
    // any channel was ever actually picked. Needs *a* room reference for
    // runBytecode's signature even though this code never touches
    // gump-visibility state — stageData.rooms[0] is just a placeholder.
    if (!stageData.rooms.empty()) {
        runBytecode(stageData.init_code, stageData.rooms[0], /*allowNav=*/false, nullptr, nullptr, nullptr);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Scene loading — SCEN####.IFF (FORM SCNE)
// Merges XTRA gumps and script codes directly into the already-loaded stageData.
// ---------------------------------------------------------------------------

std::string WC3Scene::extractStageName(const uint8_t* data, size_t size) {
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

bool WC3Scene::loadScene(const uint8_t* data, size_t size) {
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
                    WC3StageRoom* stageRoom = nullptr;
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
                                std::vector<WC3Gump> actvGumps;
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

    printf("WC3Scene: applied scene '%s' — %d rooms overridden\n",
           stageName.c_str(), roomsOverridden);
    return true;
}

// ---------------------------------------------------------------------------
// Action execution
// ---------------------------------------------------------------------------

int WC3Scene::scenRoomToStageIndex(int scenRoomIndex) const {
    char id[5];
    snprintf(id, sizeof(id), "%04d", scenRoomIndex);
    for (int i = 0; i < static_cast<int>(stageData.rooms.size()); i++)
        if (stageData.rooms[i].id == id) return i;
    return -1;
}

bool WC3Scene::isGumpVisible(const WC3StageRoom& room, uint32_t gumpId, uint32_t type) const {
    auto it = room.gumpVisible.find(gumpId);
    if (it != room.gumpVisible.end()) return it->second;
    return type != WC3Gump::TYPE_ANIM;
}

// ---------------------------------------------------------------------------
// Bytecode interpreter
//
// WC3's ACTV/INIT/EXIT scripts are a flat stream of 32-bit LE words. Recognized
// opcode words all share the 0xFACE00xx pattern (low byte = opcode number);
// everything else is data. Grammar below was derived from static analysis of
// the shipped GAMEFLOW scripts, cross-checked across dozens of examples:
//
//   <literal>                 push the literal
//   VAR(11) N / VAR(12) N     push variable bank B / bank A slot N
//   VAR(19) <op-or-literal>   push one operand — a literal, or a recognized
//                             0/1-arg "getter" opcode evaluated inline
//   VAR(30)                   pop a; push NOT a
//   VAR(31)                   pop b,a; push (a == b)
//   VAR(34)                   pop b,a; push (a != b)  [lower confidence]
//   VAR(36)                   pop b,a; push (a AND b)
//   VAR(37)                   pop b,a; push (a OR b)
//   VAR(24)                   no-op (syntactic marker; the condition is
//                             already on the stack from the VAR(19)/... chain)
//   VAR(8)/VAR(50)/VAR(57) A B  var[A] = B; also pushes (oldA == B) so a bare
//                             "VAR(8) A B" can itself gate a following
//                             VAR(22) with no separate comparison chain
//   VAR(22)                   pop condition (default true if stack empty);
//                             if false, skip to the matching VAR(25) [else]
//                             or VAR(23) [endif]
//   VAR(23)                   end block; if reached by normal execution (not
//                             via a false-condition skip) and a VAR(25)
//                             immediately follows, skip that else-block too
//   VAR(25)                   else — only ever reached as a skip landing
//                             point; otherwise skipped per VAR(23)'s rule
//   VAR(55)/VAR(56) gumpId    hide / show gumpId in the current room
//   VAR(44) gumpId p1 p2      start gumpId's one-shot triggered animation
//                             (p1,p2 are always literal 1,1 in every sample
//                             seen — consumed but not modeled)
//   VAR(45) N [VAR(47) M]     resolve LOOKMOVI[N] to a movie name, shot M
//                             (default 0); VAR(87)/(48)/(46)/(86) around it
//                             are no-op movie-block markers
//   VAR(6)/(7)/(49)           room-nav / SC_205-shot-or-subscene / branch pak
//                             entry — only recorded into `result` when
//                             allowNav (a real player click), never for
//                             INIT/EXIT background setup code
//
// This is a best-effort reconstruction, not a disassembly: opcodes with no
// bearing on the elevator/visibility work (VAR(13)/(61)/(84) getters, VAR(34))
// are stubbed conservatively (return 0 / treat as NEQ) for lack of ground truth.
// ---------------------------------------------------------------------------

namespace {
constexpr uint32_t kOpMask = 0xFFFFFF00u;
constexpr uint32_t kOpTag  = 0xFACE0000u;
constexpr uint32_t kOp12   = 0xFACE000Cu;
constexpr uint32_t kOp19   = 0xFACE0013u;
constexpr uint32_t kOp22   = 0xFACE0016u;
constexpr uint32_t kOp23   = 0xFACE0017u;
constexpr uint32_t kOp24   = 0xFACE0018u;
constexpr uint32_t kOp25   = 0xFACE0019u;
constexpr uint32_t kOp47   = 0xFACE002Fu;
constexpr uint32_t kOp48   = 0xFACE0030u;
}

void WC3Scene::runBytecode(const std::vector<uint8_t>& code, const WC3StageRoom& room,
                            bool allowNav, ActionResult* result,
                            std::vector<PendingMovie>* movies,
                            std::function<RSImageSet*(int)> getShape,
                            bool dryRun) const {
    if (code.empty()) return;
    size_t n = code.size();
    auto word  = [&](size_t i) -> uint32_t { return readU32LE(&code[i]); };
    auto isOp  = [](uint32_t w) { return (w & kOpMask) == kOpTag; };

    std::vector<int32_t> stack;
    auto pop = [&]() -> int32_t {
        if (stack.empty()) return 0;
        int32_t v = stack.back(); stack.pop_back(); return v;
    };

    // Set by the most recent VAR(45) (movie-macro block start) and referenced
    // by any later bare VAR(47) shot-list in the same bytecode run — see
    // case 45/47 below. Persists across the whole run, not just one block,
    // since conditional dispatch tables (e.g. room 0007's EXIT code) declare
    // the movie once via VAR(45) and then pick its shot list from inside a
    // later if/else chain, not immediately after VAR(45) itself.
    std::string macroMovieName;

    size_t pos = 0;

    // Evaluate one operand at `pos`: a plain literal, or a recognized 0/1-arg
    // getter opcode (consuming its own inline argument if it takes one).
    std::function<int32_t()> evalOperand = [&]() -> int32_t {
        if (pos + 4 > n) return 0;
        uint32_t w = word(pos);
        if (!isOp(w)) { pos += 4; return static_cast<int32_t>(w); }
        uint32_t op = w & 0xFF;
        pos += 4;
        switch (op) {
            case 11: { // VAR(11) N — GET bank B
                int32_t slot = (pos + 4 <= n) ? static_cast<int32_t>(word(pos)) : 0;
                pos += 4;
                auto it = varsB.find(slot);
                return it != varsB.end() ? it->second : 0;
            }
            case 12: { // VAR(12) N — GET bank A
                int32_t slot = (pos + 4 <= n) ? static_cast<int32_t>(word(pos)) : 0;
                pos += 4;
                auto it = varsA.find(slot);
                return it != varsA.end() ? it->second : 0;
            }
            case 61: // unresolved 1-arg getter — consume its arg, stub 0
                if (pos + 4 <= n) pos += 4;
                return 0;
            case 13: case 84: // unresolved 0-arg getters — stub 0
                return 0;
            default:
                // Not a recognized getter — treat the opcode word itself as the
                // value rather than silently desyncing the following scan.
                return static_cast<int32_t>(w);
        }
    };

    // From `p` (pointing at a VAR(22)), scan forward tracking VAR(22)/VAR(23)
    // nesting depth to find this block's own matching VAR(23). If a VAR(25)
    // [else] immediately follows it, returns the position just past the
    // else's own VAR(22) (so its body runs unconditionally); otherwise
    // returns the position just past the matching VAR(23).
    auto findElseOrEnd = [&](size_t p) -> size_t {
        int depth = 0;
        while (p + 4 <= n) {
            uint32_t w = word(p);
            if (w == kOp22) { depth++; p += 4; }
            else if (w == kOp23) {
                depth--; p += 4;
                if (depth == 0) {
                    if (p + 4 <= n && word(p) == kOp25) return p + 8;
                    return p;
                }
            } else {
                p += 4;
            }
        }
        return p;
    };

    // From `p` (pointing at an else-block's VAR(22), reached via normal
    // fallthrough after its matching if-branch was taken), scan forward to
    // just past its own matching VAR(23) so the else-block is skipped whole.
    auto skipBlock = [&](size_t p) -> size_t {
        int depth = 0;
        while (p + 4 <= n) {
            uint32_t w = word(p);
            p += 4;
            if (w == kOp22) depth++;
            else if (w == kOp23) { depth--; if (depth == 0) return p; }
        }
        return p;
    };

    // Returns the position of the VAR(23) that closes the if/else-if block whose
    // body starts at `p` (immediately after its own VAR(22)), without consuming
    // `pos` — pure lookahead, tracking VAR(22)/VAR(23) nesting depth.
    auto findBlockEnd = [&](size_t p) -> size_t {
        int depth = 1;
        while (p + 4 <= n) {
            uint32_t w = word(p);
            if (w == kOp22) depth++;
            else if (w == kOp23) { depth--; if (depth == 0) return p; }
            p += 4;
        }
        return p;
    };

    // Peek: does `p` (immediately after VAR(49)'s own argument) hold the idiom
    // "VAR(19) VAR(12) slot OP24 OP22 <if-body> [OP25 <else-body>] OP23", where
    // each body opens with a bare VAR(47) <literal>? If so, that literal pair is
    // the reaction-shot selector for the branch-pak choice just recorded — see
    // ActionResult::branchTrueShot/branchFalseShot. Pure lookahead; doesn't
    // touch `pos` or `stack`.
    auto peekBranchShots = [&](size_t p, int32_t& trueShot, int32_t& falseShot) {
        trueShot = falseShot = -1;
        if (p + 20 > n) return;
        if (word(p) != kOp19 || word(p + 4) != kOp12) return;
        // p+8 = slot (unused here — the actual condition value comes from the
        // player's choice, not the variable's current value at peek time)
        if (word(p + 12) != kOp24 || word(p + 16) != kOp22) return;
        size_t ifBody = p + 20;
        if (ifBody + 8 <= n && word(ifBody) == kOp47)
            trueShot = static_cast<int32_t>(word(ifBody + 4));
        size_t ifEnd = findBlockEnd(ifBody); // points at the if-block's own VAR(23)
        // Past VAR(23), an else-block is introduced by VAR(25) immediately
        // followed by the else-block's OWN VAR(22) — skip both to reach its body.
        if (ifEnd + 12 <= n && word(ifEnd + 4) == kOp25 && word(ifEnd + 8) == kOp22) {
            size_t elseBody = ifEnd + 12;
            if (elseBody + 8 <= n && word(elseBody) == kOp47)
                falseShot = static_cast<int32_t>(word(elseBody + 4));
        }
    };

    auto doSet = [&](int32_t slot, int32_t val) {
        int32_t oldVal = 0;
        auto it = varsA.find(slot);
        if (it != varsA.end()) oldVal = it->second;
        if (!dryRun) varsA[slot] = val;
        stack.push_back(oldVal == val ? 1 : 0);
    };

    while (pos + 4 <= n) {
        uint32_t w = word(pos);
        if (!isOp(w)) { pos += 4; continue; } // stray literal with no consumer
        uint32_t op = w & 0xFF;
        pos += 4;

        switch (op) {
        case 19: stack.push_back(evalOperand()); break;
        case 30: { int32_t a = pop(); stack.push_back(a == 0 ? 1 : 0); break; }
        case 31: { int32_t b = pop(), a = pop(); stack.push_back(a == b ? 1 : 0); break; }
        case 34: { int32_t b = pop(), a = pop(); stack.push_back(a != b ? 1 : 0); break; }
        case 36: { int32_t b = pop(), a = pop(); stack.push_back((a && b) ? 1 : 0); break; }
        case 37: { int32_t b = pop(), a = pop(); stack.push_back((a || b) ? 1 : 0); break; }
        case 24: break; // syntactic no-op
        // Terminal "commit to flying" signal: found at the end of the Flight
        // Control ready-room's "Go to FLIGHT DECK." door variant (gid 19, the
        // gump VICTORY.IFF's room 0003 INIT swaps in for gid 2 once bank-B
        // slot 155/161 — "wingman selection committed" — is set), always
        // reached unconditionally once that gump is clickable at all. Takes
        // no operand.
        case 1: if (allowNav && result) result->launchMission = true; break;
        // LOOKMISN.IFF mission-index selector — see ActionResult::missionIndex.
        case 5: {
            int32_t midx = evalOperand();
            if (allowNav && result) result->missionIndex = midx;
            break;
        }
        case 8: case 57: {
            int32_t slot = evalOperand();
            int32_t val  = evalOperand();
            doSet(slot, val);
            break;
        }
        // VAR(50) <n> <v>: previously grouped with VAR(8)/VAR(57) as a third
        // bank-A SET, but that collided with real bank-A flags — e.g. the
        // mess hall's (SCEN0006.IFF) own INIT opens with VAR(50) 8 -1, which
        // then permanently defeated its own later "GET-A 8" one-shot check
        // for its lift/barracks arrival videos (see todo.MD). Reverse-
        // engineered instead by comparing every VAR(50) call site across all
        // of VICTORY.IFF's rooms: it's consistently the instruction
        // immediately after a VAR(44) <gumpId> 1 1 (start gumpId's
        // triggered animation) call, using a small slot number that tracks
        // that specific animation/door (never read back anywhere via
        // GET-A/GET-B) and always a fixed literal (0 or -1) — i.e.
        // per-animation bookkeeping, not a persistent narrative flag.
        // Stubbed as a no-op (still consumes its 2 args) until that
        // bookkeeping is itself modeled.
        case 50: {
            evalOperand();
            evalOperand();
            break;
        }
        case 22: {
            bool cond = stack.empty() ? true : (pop() != 0);
            if (!cond) pos = findElseOrEnd(pos - 4);
            break;
        }
        case 23:
            if (pos + 4 <= n && word(pos) == kOp25) pos = skipBlock(pos + 4);
            break;
        case 25: break; // landing point only
        case 55: { uint32_t gid = static_cast<uint32_t>(evalOperand()); if (!dryRun) room.gumpVisible[gid] = false; break; }
        case 56: { uint32_t gid = static_cast<uint32_t>(evalOperand()); if (!dryRun) room.gumpVisible[gid] = true;  break; }
        case 44: {
            uint32_t gid = static_cast<uint32_t>(evalOperand());
            evalOperand(); evalOperand(); // p1, p2 — always literal 1,1; unused
            if (!dryRun) {
                room.gumpVisible[gid] = true;
                uint32_t nowMs = SDL_GetTicks();
                uint32_t durationMs = 1000; // fallback if shape lookup unavailable
                if (getShape) {
                    for (auto& actor : room.actors) {
                        if (actor.id != gid || actor.scrn_id < 0) continue;
                        RSImageSet* img = getShape(actor.scrn_id);
                        if (img && img->GetNumImages() > 0 && actor.anim_fps > 0)
                            durationMs = (uint32_t)img->GetNumImages() * 1000u / (uint32_t)actor.anim_fps;
                        break;
                    }
                }
                room.animStartTick[gid] = nowMs;
                room.animEndTick[gid]   = nowMs + durationMs;
            }
            break;
        }
        case 45: {
            int32_t midx = evalOperand();
            auto mit = lookMovi.find(midx);
            macroMovieName = (mit != lookMovi.end()) ? mit->second : std::string();
            // Simple, immediate form: VAR(45) N VAR(47) shot — a single fixed
            // shot right after the movie index (e.g. a clicked gump's own
            // "play this one clip" macro). The deferred/conditional form
            // (VAR(47) reached later, from inside an if/else dispatch on some
            // other variable) is handled by the standalone case 47 below.
            if (pos + 4 <= n && word(pos) == kOp47) {
                pos += 4;
                int shot = evalOperand();
                if (!macroMovieName.empty()) {
                    if (movies) movies->push_back({macroMovieName, shot});
                    if (allowNav && result && result->movieName.empty() && macroMovieName != "sc_205") {
                        result->movieName = macroMovieName;
                        result->movieShot = shot;
                    }
                }
            }
            break;
        }
        // Deferred VAR(47) <shot1> [<shot2> ...] VAR(48): a variable-length
        // shot list for the movie most recently declared by VAR(45), reached
        // from inside a later if/else dispatch (not immediately after
        // VAR(45) — that immediate form is consumed inline by case 45 and
        // never reaches here). Seen in e.g. room 0007's EXIT code, which
        // VAR(45)-declares "sc_200" once, then per-picked-pilot dispatches to
        // a TWO-shot list: [that pilot's own BRCH group, the shared "Outro"
        // BRCH] — both queued and played back to back.
        case 47: {
            // Decided once, before consuming any shots: whether THIS shot list
            // is the one that wins the result (first VAR45/47 pair in the
            // script — matches the movieName.empty() guard the old per-shot
            // check used, since result->movieName only ever transitions
            // empty -> non-empty once). Captures the FULL list into
            // movieShots, not just the first shot (see ActionResult's own
            // comment on why the old first-shot-only behavior silently
            // dropped a movie's shared closing shot for every gump click).
            bool captureThis = allowNav && result && result->movieName.empty() && macroMovieName != "sc_205";
            bool firstShot = true;
            while (pos + 4 <= n && word(pos) != kOp48) {
                int32_t shot = evalOperand();
                if (!macroMovieName.empty()) {
                    if (movies) movies->push_back({macroMovieName, shot});
                    if (captureThis) {
                        if (firstShot) {
                            result->movieName = macroMovieName;
                            result->movieShot = shot;
                        }
                        result->movieShots.push_back(shot);
                    }
                }
                firstShot = false;
            }
            break;
        }
        case 46: case 48: case 86: case 87: break; // movie-macro no-op markers
        case 6: {
            int32_t target = evalOperand();
            if (allowNav && result) {
                printf("WC3Scene: VAR(6) %d (go to SCEN room %d)\n", target, target);
                // No synthesized fallback shot here: the destination room's own
                // INIT code already has a complete, context-sensitive arrival-shot
                // mechanism (reads bank-A slot 4 — the departing room's own number,
                // set by every room's EXIT code via VAR(8) 4 <self> — to pick the
                // correct shot for wherever the player is arriving FROM). Inventing
                // a shot here duplicated/conflicted with that, playing two videos
                // (or the wrong one) on ordinary room-to-room transitions.
                result->type   = ActionResult::GoRoom;
                result->target = target;
            }
            break;
        }
        case 7: {
            int32_t nval = evalOperand();
            // Optional trailing literal "mode" argument — consume without acting on it.
            if (pos + 4 <= n && !isOp(word(pos))) pos += 4;
            if (allowNav && result) {
                if (nval <= 18) {
                    if (result->transitionShot < 0) result->transitionShot = nval;
                } else if (result->subscene < 0) {
                    result->subscene = nval;
                }
            }
            break;
        }
        case 49: {
            int32_t k = evalOperand();
            if (allowNav && result && result->branchEntry < 0) {
                result->branchEntry = k;
                int32_t trueShot, falseShot;
                peekBranchShots(pos, trueShot, falseShot);
                result->branchTrueShot  = trueShot;
                result->branchFalseShot = falseShot;
                // Everything after a branch-pak call in the same ACTV script is
                // conditioned on which line the player picks (see peekBranchShots'
                // if/else idiom) — a variable we don't know yet. Evaluating it now
                // would read a stale/default condition and record garbage follow-up
                // effects (observed: a stray, unintended SC_205 shot firing right
                // after the reaction clip). Stop here; the reaction shot itself is
                // already fully captured via the peek above.
                return;
            }
            break;
        }
        default:
            break; // unimplemented — consumes no operands (safe: literals never match isOp)
        }
    }
}

WC3Scene::ActionResult WC3Scene::activateGump(uint32_t gump_id, std::function<RSImageSet*(int)> getShape,
                                               bool dryRun) const {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return {};
    auto& room = stageData.rooms[currentRoom];
    // Stage ACTV is pushed first, then SCEN ACTV is appended on top — walk in
    // reverse so a SCEN's override for this gump_id is found before the stage's
    // default, while rooms with no override still fall back to the stage's.
    for (auto it = room.actv_codes.rbegin(); it != room.actv_codes.rend(); ++it) {
        auto& [id, code] = *it;
        if (id != gump_id) continue;
        ActionResult result;
        runBytecode(code, room, /*allowNav=*/true, &result, nullptr, getShape, dryRun);
        if (result.type != ActionResult::None || result.transitionShot >= 0 || result.subscene >= 0 ||
            result.branchEntry >= 0 || !result.movieName.empty())
            return result;
    }
    return {};
}

std::vector<WC3Scene::PendingMovie> WC3Scene::runRoomInit(int roomIndex, std::function<RSImageSet*(int)> getShape) {
    std::vector<PendingMovie> movies;
    if (roomIndex < 0 || roomIndex >= static_cast<int>(stageData.rooms.size())) return movies;
    auto& room = stageData.rooms[roomIndex];
    // Reset to type-based defaults each time the room is (re)entered, then let
    // INIT's own VAR(55)/(56) calls apply overrides on top.
    room.gumpVisible.clear();
    room.animEndTick.clear();
    room.animStartTick.clear();
    room.pinnedFrame.clear();
    room.doorOpening.clear();
    room.doorAnimTick.clear();
    runBytecode(room.init_code, room, /*allowNav=*/false, nullptr, &movies, getShape);
    // setRoom() already built zones once, from type-based-default visibility
    // (before INIT's own VAR(55)/(56) hide/show calls above ran) — rebuild
    // now that visibility reflects INIT's actual output, or hidden actors
    // (e.g. wingman-selection portraits INIT decided not to show) would stay
    // hoverable/clickable via their stale zone entries.
    if (roomIndex == currentRoom) buildZones(getShape);
    return movies;
}

std::vector<WC3Scene::PendingMovie> WC3Scene::runRoomExit(int roomIndex, std::function<RSImageSet*(int)> getShape) {
    std::vector<PendingMovie> movies;
    if (roomIndex < 0 || roomIndex >= static_cast<int>(stageData.rooms.size())) return movies;
    auto& room = stageData.rooms[roomIndex];
    runBytecode(room.exit_code, room, /*allowNav=*/false, nullptr, &movies, getShape);
    return movies;
}


uint32_t WC3Scene::pollFinishedAnimation() {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size())) return 0;
    auto& room = stageData.rooms[currentRoom];
    uint32_t now = SDL_GetTicks();
    for (auto it = room.animEndTick.begin(); it != room.animEndTick.end(); ++it) {
        if (now >= it->second) {
            uint32_t gid = it->first;
            room.animEndTick.erase(it);
            return gid;
        }
    }
    return 0;
}

void WC3Scene::triggerGumpAnim(uint32_t gumpId, std::function<RSImageSet*(int)> getShape,
                                bool armCompletionPoll) {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size())) return;
    auto& room = stageData.rooms[currentRoom];
    room.gumpVisible[gumpId] = true;
    uint32_t nowMs = SDL_GetTicks();
    uint32_t durationMs = 1000; // fallback if shape lookup unavailable — mirrors case 44's own
    if (getShape) {
        for (auto& actor : room.actors) {
            if (actor.id != gumpId || actor.scrn_id < 0) continue;
            RSImageSet* img = getShape(actor.scrn_id);
            if (img && img->GetNumImages() > 0 && actor.anim_fps > 0)
                durationMs = (uint32_t)img->GetNumImages() * 1000u / (uint32_t)actor.anim_fps;
            break;
        }
    }
    room.animStartTick[gumpId] = nowMs;
    if (armCompletionPoll) room.animEndTick[gumpId] = nowMs + durationMs;
    else room.animEndTick.erase(gumpId);
}

void WC3Scene::setGumpPinnedFrame(uint32_t gumpId, int frame) {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size())) return;
    auto& room = stageData.rooms[currentRoom];
    if (frame < 0) room.pinnedFrame.erase(gumpId);
    else room.pinnedFrame[gumpId] = frame;
}

void WC3Scene::setGumpVisible(uint32_t gumpId, bool visible) {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size())) return;
    stageData.rooms[currentRoom].gumpVisible[gumpId] = visible;
}

// ---------------------------------------------------------------------------

void WC3Scene::setRoom(int roomIndex, std::function<RSImageSet*(int)> getShape) {
    if (roomIndex >= 0 && roomIndex < static_cast<int>(stageData.rooms.size())) {
        currentRoom = roomIndex;
        printf("WC3Scene: entering room %d (%s)\n",
               roomIndex, stageData.rooms[roomIndex].id.c_str());
    }
    buildZones(getShape);
}

const std::vector<std::string>& WC3Scene::getRoomMenuItems() const {
    static const std::vector<std::string> empty;
    if (currentRoom < 0 || currentRoom >= (int)stageData.rooms.size())
        return empty;
    return stageData.rooms[currentRoom].menu_items;
}

void WC3Scene::buildZones(std::function<RSImageSet*(int)> getShape) {
    zones.clear();
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return;

    auto& room = stageData.rooms[currentRoom];

    // Populate room menu_items from the room's TYPE_MENU actor. The stage's own
    // ACTR TYPE_MENU is the fallback; if a SCEN's XTRA adds its own TYPE_MENU
    // override, it's merged in after the stage's, so take the LAST match here
    // rather than the first.
    room.menu_items.clear();
    for (auto& actor : room.actors) {
        if (actor.type == WC3Gump::TYPE_MENU && !actor.menu_items.empty()) {
            room.menu_items = actor.menu_items;
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
        if (!isGumpVisible(room, actor.id, actor.type)) continue;
        if (actor.type == WC3Gump::TYPE_NAVPOINT) {
            if (actor.w <= 0 || actor.h <= 0) continue;
            Zone z;
            z.x = actor.x;  z.y = actor.y;
            z.w = actor.w;  z.h = actor.h;
            z.gump_id = actor.id;
            z.type    = actor.type;
            z.titl_id = actor.titl_id;
            z.anim_fps = actor.anim_fps;
            zones.push_back(z);
        } else if (actor.type == WC3Gump::TYPE_BUTTON ||
                   actor.type == WC3Gump::TYPE_CHECKBOX) {
            // Floor selector buttons (e.g. lift): position from CORD, size from sprite.
            // Options-screen checkboxes (TYPE_CHECKBOX) use the identical shape —
            // position/size from CORD or sprite, one clickable Zone — the on/off
            // state itself is tracked by WC3GameFlow via setGumpPinnedFrame(),
            // same mechanism the Lift's own floor buttons already use.
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
        } else if (actor.type == WC3Gump::TYPE_CHARACTER  ||
                   actor.type == WC3Gump::TYPE_CHARACTER2 ||
                   actor.type == WC3Gump::TYPE_MOVIE_FG   ||
                   actor.type == WC3Gump::TYPE_MOVIE_BG2  ||
                   actor.type == WC3Gump::TYPE_PERSON      ||
                   actor.type == WC3Gump::TYPE_MOVIE_BG) {
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
        } else if (actor.type == WC3Gump::TYPE_MENU) {
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

    // Assign labels from the room menu items by titl_id — each gump's own index
    // into room.menu_items — not by position. A gump only shows its label once
    // it actually has a live ACTV action; gumps with no override yet for the
    // current SCEN (i.e. not unlocked by story progression) stay unlabeled.
    const auto& items = room.menu_items;
    if (!items.empty()) {
        for (auto& z : zones) {
            if (z.titl_id < 0 || z.titl_id >= (int)items.size()) continue;
            ActionResult r = activateGump(z.gump_id, getShape, /*dryRun=*/true);
            z.isRoomChange = (r.type == ActionResult::GoRoom);
            if (r.type != ActionResult::None || r.subscene >= 0 || r.branchEntry >= 0 ||
                r.transitionShot >= 0 || !r.movieName.empty()) {
                z.label = items[z.titl_id];
            }
        }
    }
}

int WC3Scene::hitTest(int mx, int my) const {
    // Zones can legitimately overlap (e.g. a large room-navigation navpoint region
    // behind a small character sprite standing in it) — the smallest-area match is
    // the most specific one and should win, rather than whichever was added first.
    int best = -1;
    long bestArea = 0;
    for (int i = 0; i < static_cast<int>(zones.size()); i++) {
        auto& z = zones[i];
        if (mx < z.x || mx >= z.x + z.w || my < z.y || my >= z.y + z.h) continue;
        long area = (long)z.w * (long)z.h;
        if (best < 0 || area < bestArea) { best = i; bestArea = area; }
    }
    return best;
}

std::vector<uint32_t> WC3Scene::getHoveredActorIds(uint32_t hoveredGumpId,
                                                    std::function<RSImageSet*(int)> getShape) const {
    std::vector<uint32_t> result;
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return result;
    auto& room = stageData.rooms[currentRoom];

    const Zone* hoveredZone = nullptr;
    if (hoveredGumpId != 0) {
        for (auto& z : zones)
            if (z.gump_id == hoveredGumpId) { hoveredZone = &z; break; }
    }

    for (auto& actor : room.actors) {
        if (actor.sound_ids.empty()) continue;
        if (actor.type != WC3Gump::TYPE_CHARACTER && actor.type != WC3Gump::TYPE_CHARACTER2 &&
            actor.type != WC3Gump::TYPE_PERSON) continue;

        bool isHovered = (actor.id == hoveredGumpId);
        if (!isHovered && hoveredZone) {
            RSImageSet* img = getShape(actor.scrn_id);
            RLEShape* frame0 = (img && img->GetNumImages() > 0) ? img->GetShape(0) : nullptr;
            int aw = frame0 ? frame0->GetWidth()  : 0;
            int ah = frame0 ? frame0->GetHeight() : 0;
            isHovered = actor.x < hoveredZone->x + hoveredZone->w && actor.x + aw > hoveredZone->x &&
                        actor.y < hoveredZone->y + hoveredZone->h && actor.y + ah > hoveredZone->y;
        }
        if (isHovered) result.push_back(actor.id);
    }
    return result;
}

const std::vector<int>* WC3Scene::getActorSoundIds(uint32_t gumpId) const {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return nullptr;
    auto& room = stageData.rooms[currentRoom];
    for (auto& actor : room.actors)
        if (actor.id == gumpId) return actor.sound_ids.empty() ? nullptr : &actor.sound_ids;
    return nullptr;
}

void WC3Scene::render(FrameBuffer* fb, PakArchive* shapes,
                      std::function<RSImageSet*(int)> getShape, uint32_t hoveredGumpId,
                      int yOffset) {
    if (currentRoom < 0 || currentRoom >= static_cast<int>(stageData.rooms.size()))
        return;

    auto& room = stageData.rooms[currentRoom];

    // Draw background
    if (room.bkgd_shape_id >= 0) {
        RSImageSet* bg = getShape(room.bkgd_shape_id);
        if (bg && bg->GetNumImages() > 0) {
            RLEShape* shape = bg->GetShape(0);
            if (shape) {
                Point2D p = {room.bkgd_x, room.bkgd_y + yOffset};
                shape->SetPosition(&p);
                fb->drawShape(shape);
            }
        }
    }

    uint32_t now = SDL_GetTicks();

    // Draw all actors (stage ACTR + scene XTRA merged). TYPE_MOVIE_BG/BG2 gumps
    // render like any other actor here — despite the name, most are static or
    // looping decorative overlay sprites (e.g. a status-panel insert layered
    // over a full-screen background plate), not placeholders for real FMV.
    //
    // Animation policy:
    //   Gumps with a live VAR(44)-triggered animation (animStartTick) — play once,
    //   frame = elapsed/frameDuration, clamped to the last frame (no looping).
    //   TYPE_BUTTON — static: stays on frame 0 (dark/inactive) until a click
    //   triggers OP44/animStartTick (above), which then plays its frames once.
    //   No standing loop — a button's "frames" are a discrete inactive->active
    //   sequence, not an idle animation.
    //   TYPE_CHARACTER / TYPE_PERSON — hover-only: only animate while the cursor
    //   is over this gump (e.g. a door that plays its open animation on hover).
    //   All other animated types — auto-loop: advance unconditionally.
    // Multi-frame sprites with no ANIM/FPS_ chunk in their source data (fps==0)
    // still need a rate to animate at — most such actors observed in-game are
    // meant to play continuously, so fall back to a fixed default rather than
    // freezing on frame 0.
    constexpr uint32_t kDefaultSpriteFps = 15;
    // The gump the player is hovering (e.g. a large "Go to X" navpoint) and a
    // purely-decorative sprite it should visually animate together with (e.g.
    // the door graphic drawn inside that navpoint's area) are frequently two
    // separate gumps with no explicit link in the data — the navpoint carries
    // the click/label, the sprite carries the animation, and they're simply
    // positioned to overlap. Look up the hovered zone's own rect once so the
    // loop below can treat any hover-only sprite whose bounds overlap it as
    // hovered too, not just an exact gump_id match.
    const Zone* hoveredZone = nullptr;
    if (hoveredGumpId != 0) {
        for (auto& z : zones)
            if (z.gump_id == hoveredGumpId) { hoveredZone = &z; break; }
    }
    for (auto& actor : room.actors) {
        if (actor.scrn_id < 0 || actor.type == WC3Gump::TYPE_MENU) continue;
        if (!isGumpVisible(room, actor.id, actor.type)) continue;
        RSImageSet* img = getShape(actor.scrn_id);
        if (!img || img->GetNumImages() == 0) continue;
        int n = img->GetNumImages();
        uint32_t fps = actor.anim_fps > 0 ? (uint32_t)actor.anim_fps : kDefaultSpriteFps;
        int frame = 0;
        auto pinned = room.pinnedFrame.find(actor.id);
        auto triggered = room.animStartTick.find(actor.id);
        if (pinned != room.pinnedFrame.end()) {
            frame = pinned->second;
        } else if (triggered != room.animStartTick.end() && n > 1) {
            uint32_t elapsed = now - triggered->second;
            uint32_t frameDurationMs = 1000u / fps;
            uint32_t f = frameDurationMs > 0 ? elapsed / frameDurationMs : 0;
            if (actor.type == WC3Gump::TYPE_BUTTON) {
                // Buttons "release" once their press animation has fully
                // played through — back to frame 0 (dark/inactive) — rather
                // than holding on the last (lit) frame forever. Holding is
                // correct for one-shot chained-animation actors (e.g. the
                // elevator, which stays on its "open" frame until a
                // different gump is swapped in), but a button should look
                // pressed only briefly.
                frame = (f >= (uint32_t)(n - 1)) ? 0 : (int)f;
            } else {
                frame = (int)std::min<uint32_t>(f, (uint32_t)n - 1); // one-shot: hold last frame, don't loop
            }
        } else if (n > 1 && actor.type != WC3Gump::TYPE_BUTTON &&
                   actor.type != WC3Gump::TYPE_CHECKBOX) {
            // TYPE_BUTTON is excluded from auto-loop entirely (falls through
            // to frame=0 below) — a button's frames are a discrete
            // dark/inactive -> lit/active sequence meant to be shown once,
            // on click (via the animStartTick branch above), not cycled
            // continuously. Looping it here made every multi-frame button
            // (e.g. the Simulator terminal's, the lift floor selector)
            // flicker between frames every ~1/fps seconds even when idle.
            // TYPE_CHECKBOX is excluded the same way — its on/off state is
            // driven entirely by an explicit setGumpPinnedFrame() call from
            // WC3GameFlow (checked above, before this branch), not animated.
            bool hoverOnly = (actor.type == WC3Gump::TYPE_CHARACTER ||
                               actor.type == WC3Gump::TYPE_CHARACTER2 ||
                               actor.type == WC3Gump::TYPE_PERSON);
            bool isHovered = (actor.id == hoveredGumpId);
            if (hoverOnly && !isHovered && hoveredZone) {
                RLEShape* frame0 = img->GetShape(0);
                int aw = frame0 ? frame0->GetWidth()  : 0;
                int ah = frame0 ? frame0->GetHeight() : 0;
                isHovered = actor.x < hoveredZone->x + hoveredZone->w && actor.x + aw > hoveredZone->x &&
                            actor.y < hoveredZone->y + hoveredZone->h && actor.y + ah > hoveredZone->y;
            }
            if (actor.type == WC3Gump::TYPE_PERSON) {
                // Door sprite sheets pack two independent one-shot sequences
                // into one frame set: frames [0, half) are the opening
                // animation (played once when the cursor enters), frames
                // [half, n) are the closing animation (played once when it
                // leaves) — not a single forward/reverse cycle over all n
                // frames. Each play always starts fresh from its own first
                // frame; never loops.
                int half = n / 2;
                if (half < 1) half = 1;
                uint32_t frameDurationMs = 1000u / fps;
                auto oIt = room.doorOpening.find(actor.id);
                bool haveState = oIt != room.doorOpening.end();
                bool wasHovered = haveState && oIt->second;
                if (isHovered != wasHovered) {
                    room.doorAnimTick[actor.id] = now;
                    room.doorOpening[actor.id] = isHovered;
                    haveState = true;
                }
                if (isHovered) {
                    uint32_t elapsed = now - room.doorAnimTick[actor.id];
                    int delta = frameDurationMs > 0 ? (int)(elapsed / frameDurationMs) : 0;
                    frame = std::min(delta, half - 1);
                } else if (haveState) {
                    uint32_t elapsed = now - room.doorAnimTick[actor.id];
                    int delta = frameDurationMs > 0 ? (int)(elapsed / frameDurationMs) : 0;
                    int closeLen = n - half;
                    if (closeLen < 1) closeLen = 1;
                    frame = half + std::min(delta, closeLen - 1);
                }
                // else: never interacted with — hold resting closed frame 0.
            } else if (!hoverOnly || isHovered) {
                frame = (now / (1000u / fps)) % (uint32_t)n;
            }
        }
        RLEShape* shape = img->GetShape(frame);
        if (shape) {
            Point2D p = {actor.x, actor.y + yOffset};
            shape->SetPosition(&p);
            fb->drawShape(shape);
        }
    }
}
