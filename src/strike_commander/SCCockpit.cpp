//
//  SCCockpit.cpp
//  libRealSpace
//
//  Created by Rémi LEONARD on 02/09/2024.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//
#include "precomp.h"
#include "../engine/GLBatch.h"
#include "SCCockpit.h"
#include "../commons/GraphicsSettings.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <climits>
#include <unordered_map>
#include <unordered_set>
#include "../engine/gametimer.h"
#include "../wing_commander_3/WC3Globals.h"
#include "../wing_commander_3/WC3Font.h"
#include "../wing_commander_3/WC3MVEStream.h"
#include "../realspace/XtreArchive.h"
#include "../realspace/RSWC3Shape.h"
#include "../commons/XtreDecompressor.h"

// ..\..\DATA\COCKPITS\TARGSHAP.PAK — per-ship-type target-diagram art
// (bracket/lock animation + per-facing shield/hull status), documented by
// the user from the WC3 manual/asset notes, corrected (2026-07 session)
// against a direct byte-level decode of the real file:
//
//   Outer PAK: 39 raw offset-table slots, 36 of them real ship entries
//   (000.SHP-ish, one per ship type/class; the rest are empty/garbage
//   slots past the last real entry), each with outer PakEntry::type 0x40.
//   Each entry is NOT a nested PakArchive and is NOT the raw "1.11" blob
//   either — the 5 leading bytes that looked like a fixed prefix (a
//   varying 4-byte field then a constant 0xE1 marker byte) are actually a
//   4-byte little-endian uncompressed-size header followed immediately by
//   XtreDecompressor::decompressType1-compressed data (the same "type
//   0x40" scheme WC3BranchPak/WC4BranchPak already use for BRANCHn.PAK —
//   0xE1's top 3 bits, 0xE0, are just the first control byte of that
//   compressed stream, not a marker byte at all). Decompressing first
//   yields a real "1.11"-format RSImageSet blob (the same format
//   RSWC3DecodeShapeEntry already decodes for COCK>SHAP bodies and
//   DEATH/EJCT frames) — see TargShapeIndex::GetShapesForIndex. Every one of the 36 real
//   entries decodes to the same 52 frames (indices 0-51, byte-verified,
//   frame layout user-confirmed exact, 2026-07 session — supersedes two
//   earlier wrong guesses):
//     0-5 targeting/lock animation (loops continuously while no target is
//     selected — see RenderMFDSTarget's own no-target branch — then plays
//     0-4 once and holds on 5 once a target is locked, see
//     DrawShipTargetDiagram); frame 5 doubles as "full hull health" (it's
//     the base silhouette itself, x_blit=y_min=0, not a per-facing icon);
//     6/7/8, 9/10/11, 12/13/14, 15/16/17 = top/right/bottom/left shield
//     status, 3 frames each (full/medium/low), clockwise from top;
//     18/19, 20/21, 22/23, 24/25 = top/right/bottom/left hull damage, 2
//     frames each (medium/low — no "full" frame of its own since frame 5
//     already covers that case) — all of the above at SVGA (hi-res) scale;
//     26-51 repeats the same 26-frame layout at VGA (low-res) scale
//     (corrected — the reverse of what was first assumed: 0-25 is SVGA,
//     26-51 is VGA, not the other way around). The engine's own entries
//     this session used only the SVGA-range indices unconditionally
//     regardless of g_ifVGA — see DrawShipFacingDiagram's own
//     facingOffset, fixed alongside that correction. Older per-file notes
//     describing an "11-frame variant" with no per-facing data weren't
//     re-confirmed this round; not contradicted either, just not what the
//     36 entries decoded here happened to be.
//
// Mapping "top/left/bottom/right" (a target diagram, viewed from above) to
// this engine's own front/back/left/right (SCMissionActors::shield_front
// etc.) is an assumed but unconfirmed convention: top=front (nose), bottom=
// back (tail), matching how a top-down ship silhouette is normally drawn.
namespace TargShapeIndex {
    // Ship model filename (MISN_PART::member_name, upper-cased) -> TARGSHAP
    // index. Built from the user's ship-name list cross-referenced against
    // real filenames in DATA\OBJECTS\ (OBJECTS.TRE) — only kept where a
    // filename match was reasonably confident; ships left out simply get no
    // TARGSHAP art (SelectTargShapeSet returns nullptr) rather than risk
    // showing the wrong ship's diagram. "P"-suffixed fighter filenames
    // (HELLCATP/TBOLTP/ARROWP/EXCALP) follow this codebase's own established
    // player-flyable-variant naming convention (see WC3Mission.cpp's
    // buildActorFromPart, HELLCATP donor-matching). KDESD/KCRUD/EXCALD-style
    // "D"-suffixed names elsewhere in OBJECTS.TRE are that same ship's
    // destroyed/debris variant, not a distinct class, so deliberately not
    // used here.
    static const std::unordered_map<std::string, int> kShipNameToIndex = {
        {"ARROWP", 1},
        // Non-"P" filenames (ARROW/HELLCAT/TBOLT/EXCAL.IFF) are the same
        // fighter class flown by AI wingmen — OBJECTS.TRE carries them as
        // separate model files from the player's own "P"-suffixed instance
        // (see buildActorFromPart's HELLCATP donor-matching comment above),
        // but the 2D target-diagram silhouette is keyed per ship class, not
        // per player/AI instance, so alias them to the same index (user-
        // reported 2026-07: wingman ships showed no target icon at all —
        // SelectTargShapeSet's lookup simply had no entry for the plain
        // name).
        {"ARROW", 1},
        {"HELLCATP", 2},
        {"HELLCAT", 2},
        {"TBOLTP", 3},
        {"TBOLT", 3},
        {"EXCALP", 4},
        {"EXCAL", 4},
        {"VICTORY", 5},
        {"SHUTTLE", 7},
        {"BEHEMOTH", 8},
        {"DARKET", 10},
        {"DRALTHI", 11},
        {"VAKTOTH", 12},
        {"BLOODFNG", 13},
        {"STRAKHA", 14},
        {"PAKTAHN", 15},
        {"KCORV", 16},
        {"KCRUISER", 17},
        // Light vs Heavy Destroyer (18/19): guessed from KDESTL's "L" suffix
        // vs plain KDEST — not confirmed against the manual.
        {"KDESTL", 18},
        {"KDEST", 19},
        {"KTRN", 20},
        {"KDREAD", 21},
        {"KCARRIER", 22},
        {"TCRUISER", 24},
        {"TDEST", 25},
        {"TTRANS", 26},
        {"KASBASE", 30},
        {"ASTRFTR", 31},
        {"EKAPSHI", 32},
        {"KTRAND", 33},
        {"SORTHAK", 34},
        {"FAULT", 35},
        {"KSGENR", 36},
    };
    // Lazily loaded/cached, keyed by index — the outer PAK and each nested
    // per-ship SHP only ever need decoding once, not per-cockpit-instance
    // or per-frame.
    static PakArchive* s_outerPak = nullptr;
    static bool s_outerPakLoadAttempted = false;
    static std::unordered_map<int, RSImageSet*> s_shapesByIndex;

    static RSImageSet* GetShapesForIndex(int index) {
        auto cached = s_shapesByIndex.find(index);
        if (cached != s_shapesByIndex.end()) {
            return cached->second;
        }
        if (!s_outerPakLoadAttempted) {
            s_outerPakLoadAttempted = true;
            TreEntry* entry = AssetManager::getInstance().GetEntryByName("..\\..\\DATA\\COCKPITS\\TARGSHAP.PAK");
            if (entry != nullptr) {
                s_outerPak = new PakArchive();
                s_outerPak->InitFromRAM("TARGSHAP.PAK", entry->data, entry->size);
                printf("DEBUG TARGSHAP: loaded, numEntries=%zu\n", s_outerPak->GetNumEntries());
            } else {
                printf("DEBUG TARGSHAP: TreEntry not found\n");
            }
        }
        if (s_outerPak == nullptr || !s_outerPak->IsReady() || (size_t)index >= s_outerPak->GetNumEntries()) {
            return nullptr;
        }
        PakEntry* shipEntry = s_outerPak->GetEntry(index);
        if (shipEntry == nullptr || shipEntry->size == 0) {
            return nullptr;
        }
        // Bug fix (2026-07 session, round 2 — user-reported: only the red
        // grid background shows, target images never appear): the round-1
        // fix above ("skip a fixed 5-byte prefix, decode directly")
        // treated the data as an uncompressed "1.11" blob because the
        // literal bytes "1.11" happen to sit at that offset in every
        // entry — but that's a coincidence of the *compressed* bytes, not
        // real data; RSWC3DecodeShapeEntry's own sub-image offset table
        // came out as garbage (multi-million-byte si_off values) because
        // the bytes after "1.11" are still XtreDecompressor::TYPE1
        // compressed, not raw. Confirmed by testing both of this
        // codebase's other decompressors (LZBuffer LZW / PKWareDecompressor)
        // against every plausible skip offset — neither matched — before
        // finding extractor/shapes_viewer.cpp's independently-reversed
        // decompress_type1 (== XtreDecompressor::decompressType1, already
        // in this codebase and already used for BRANCHn.PAK's own type-0x40
        // entries — see WC3BranchPak::decompressType40) actually is the
        // right scheme: decompressing entry 24 this way reproduced a clean
        // "1.11" blob with a fully in-bounds 52-entry offset table and a
        // sane 89x81 scanning-bracket frame 0, byte-verified.
        constexpr size_t kUncompressedSizePrefix = 4;
        RSImageSet* set;
        if (shipEntry->size > kUncompressedSizePrefix) {
            uint32_t uncompressedSize = shipEntry->data[0] | (shipEntry->data[1] << 8) |
                                         (shipEntry->data[2] << 16) | (shipEntry->data[3] << 24);
            if (uncompressedSize > 0 && uncompressedSize <= 8 * 1024 * 1024) {
                std::vector<uint8_t> decompressed(uncompressedSize);
                size_t got = XtreDecompressor::decompressType1(
                    shipEntry->data + kUncompressedSizePrefix, decompressed.data(),
                    shipEntry->size - kUncompressedSizePrefix);
                set = RSWC3DecodeShapeEntry(decompressed.data(), got);
            } else {
                set = new RSImageSet();
            }
        } else {
            set = new RSImageSet();
        }
        printf("DEBUG TARGSHAP: index=%d decodedFrames=%zu\n", index, set->GetNumImages());
        s_shapesByIndex[index] = set;
        return set;
    }
    // Returns nullptr if this actor's model has no confident TARGSHAP
    // mapping (see kShipNameToIndex) or the archive/entry couldn't be
    // loaded.
    static RSImageSet* SelectTargShapeSet(SCMissionActors* actor) {
        if (actor == nullptr || actor->object == nullptr) {
            return nullptr;
        }
        std::string name = actor->object->member_name;
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        auto it = kShipNameToIndex.find(name);
        if (it == kShipNameToIndex.end()) {
            return nullptr;
        }
        return GetShapesForIndex(it->second);
    }
}

// Capital-ship classification for the Target Radar's cross-vs-dot marker
// (see RenderMFDSTargetRadarWC3). Ship-name-based, same convention/data
// source as TargShapeIndex::kShipNameToIndex above (MISN_PART::member_name,
// upper-cased) — not derived from any engine field since none currently
// distinguishes ship class. Real-world WC3 ship-class knowledge, not
// independently re-verified against the manual: Kilrathi/Terran capitals,
// carriers, transports, and stations are listed; fighters (including
// Kilrathi ones — Dralthi/Vaktoth/Bloodfang/Strakha/Paktahn) are not.
// Finds the palette entry closest (by RGB distance) to the requested color.
// The HUD is drawn entirely through palette-index primitives (color 223 is
// used everywhere else in this file for a fixed green/amber look), so there
// is no direct way to ask for "red" or "blue" — the actual index that looks
// red/blue depends on whatever VGAPalette this cockpit loaded.
static uint8_t ClosestPaletteIndex(const VGAPalette &pal, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t best = 0;
    int bestDist = INT_MAX;
    for (int i = 0; i < 256; i++) {
        int dr = (int)pal.colors[i].r - r;
        int dg = (int)pal.colors[i].g - g;
        int db = (int)pal.colors[i].b - b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = (uint8_t)i;
        }
    }
    return best;
}

// Every TARGSHAP.PAK frame's x_blit/y_min header fields (see
// RSWC3DecodeShapeEntry) are real per-frame placement data — where in the
// shared 89x81 (SVGA scale; VGA is smaller) canvas that frame's content
// should sit, confirmed by decoding a real entry (TCRUISER, 2026-07
// session): frames 6-17 form 4 clusters of 3 IDENTICAL (x_blit,y_min)
// pairs each (one per facing), and 18-25 form 4 clusters of 2 — a real,
// file-driven layout, not something to invent screen offsets for. Bug
// found from the user's report that positioning looked wrong past frame 5:
// DrawShipFacingDiagram was discarding that data entirely, hand-placing
// each facing at a fixed ±14px diamond around `center` instead. Fixed by
// reading each shape's baked-in position (RSWC3DecodeShapeEntry already
// sets shape->position = {x_blit, y_min}) and offsetting it from the same
// canvas origin the base silhouette (frames 0-5, whose own x_blit/y_min
// are always 0,0) is drawn at, via GetOriginalCanvasOffset below —
// necessary because these RLEShape objects are cached and reused every
// frame (TargShapeIndex::s_shapesByIndex), and drawShape's own call
// convention (SetPosition to an absolute screen coordinate, then draw)
// would otherwise clobber the original x_blit/y_min after the first frame
// ever drawn, corrupting every subsequent frame's placement.
//
// Frame layout is user-confirmed exact (2026-07 session, live-tuned
// against the running game — supersedes an earlier, wrong "shield is
// frames 6-13, 2 frames/facing" guess): 6/7/8=top shield full/medium/low,
// 9/10/11=right shield, 12/13/14=bottom shield, 15/16/17=left shield (3
// tiers each, clockwise from top starting at 6, matching the position
// data's own 3-per-facing clustering exactly). Frame 5 (the animation's
// held "lock acquired" frame, x_blit=y_min=0 — the base silhouette, not a
// per-facing icon) doubles as "full hull health"; hull damage only adds a
// per-facing overlay when armor is below max: 18/19=top medium/low,
// 20/21=right, 22/23=bottom, 24/25=left (2 tiers each, no "full" frame of
// its own since frame 5 already covers that case).
static Point2D GetOriginalCanvasOffset(RLEShape *shape) {
    static std::unordered_map<RLEShape *, Point2D> s_originalOffsets;
    auto it = s_originalOffsets.find(shape);
    if (it != s_originalOffsets.end()) {
        return it->second;
    }
    Point2D offset = shape->position;
    s_originalOffsets[shape] = offset;
    return offset;
}

// Draws a ship's TARGSHAP.PAK target diagram (targeting/lock animation
// frames 0-5, plus per-facing shield/armor tiers when the 51-frame variant
// has them) centered at `center`. Shared by RenderTargetWithCam (the
// full-screen HUD-projected target overlay) and RenderMFDSTarget (the
// right MFD's fixed panel) — same art, same layout, different callers'
// idea of where "the target's on-screen position" is.
// Per-facing shield/hull damage indicators — only present in the 51-frame
// TARGSHAP variant (see TargShapeIndex's own layout comment). Top=front/
// Bottom=back/Left=left/Right=right is an assumed, unconfirmed top-down-
// diagram convention. Positioning is read directly from each frame's own
// embedded x_blit/y_min (see GetOriginalCanvasOffset above), not hand-
// placed. Split out from DrawShipTargetDiagram (below) so RenderMFDSShield
// can reuse just this part for the player's own ship — the target-lock
// scan animation DrawShipTargetDiagram also draws makes no sense on a "my
// own ship's shield status" page.
static void DrawShipFacingDiagram(FrameBuffer *fb, Point2D center, SCMissionActors *targetActor, RSImageSet *targShapes) {
    size_t numFrames = targShapes->GetNumImages();
    if (numFrames >= 26 && targetActor != nullptr) {
        // Frames 0-25 are SVGA (hi-res) scale, 26-51 repeat the same
        // layout at VGA (low-res) scale — corrected (2026-07 session),
        // the reverse of what was first assumed. Previously this always
        // used the 0-25 (SVGA) range regardless of g_ifVGA, so a VGA-mode
        // cockpit was drawing hi-res-scaled shield/armor gauge art.
        uint32_t facingOffset = g_ifVGA ? 26 : 0;
        struct FacingGauge {
            float current, maxVal;
            uint32_t shieldFrameBase;  // 3 frames: full/medium/low
            uint32_t armorFrameBase;   // 2 frames: medium/low (full = no overlay, frame 5 covers it)
        };
        // Clockwise from top, matching the position data's own clustering:
        // top(front), right, bottom(back), left.
        FacingGauge facings[4] = {
            { targetActor->shield_front, targetActor->max_shield_front, 6 + facingOffset,  18 + facingOffset },  // top/front
            { targetActor->shield_right, targetActor->max_shield_right,  9 + facingOffset,  20 + facingOffset }, // right
            { targetActor->shield_back,  targetActor->max_shield_back,   12 + facingOffset, 22 + facingOffset }, // bottom/back
            { targetActor->shield_left,  targetActor->max_shield_left,   15 + facingOffset, 24 + facingOffset }, // left
        };
        float armorFacing[4] = { targetActor->armor_front, targetActor->armor_right,
                                  targetActor->armor_back, targetActor->armor_left };
        float armorMaxFacing[4] = { targetActor->max_armor_front, targetActor->max_armor_right,
                                     targetActor->max_armor_back, targetActor->max_armor_left };
        for (int i = 0; i < 4; i++) {
            if (facings[i].maxVal > 0.0f) {
                // 3 frames: full/medium/low.
                float frac = facings[i].current / facings[i].maxVal;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                uint32_t tier = frac > 0.66f ? 0 : (frac > 0.33f ? 1 : 2);
                RLEShape *shieldShape = targShapes->GetShape(facings[i].shieldFrameBase + tier);
                if (shieldShape != nullptr) {
                    Point2D offset = GetOriginalCanvasOffset(shieldShape);
                    Point2D pos = { center.x - shieldShape->GetWidth() / 2 + offset.x,
                                     center.y - shieldShape->GetHeight() / 2 + offset.y };
                    shieldShape->SetPosition(&pos);
                    fb->drawShape(shieldShape);
                }
            }
            // Full hull health draws nothing extra here — frame 5 (the
            // base silhouette, drawn by DrawShipTargetDiagram) already
            // represents it. Only damaged facings get an overlay.
            if (armorMaxFacing[i] > 0.0f && armorFacing[i] < armorMaxFacing[i]) {
                float frac = armorFacing[i] / armorMaxFacing[i];
                if (frac < 0.0f) frac = 0.0f;
                // 2 frames: medium (0) vs low (1).
                uint32_t tier = frac > 0.5f ? 0 : 1;
                RLEShape *armorShape = targShapes->GetShape(facings[i].armorFrameBase + tier);
                if (armorShape != nullptr) {
                    Point2D offset = GetOriginalCanvasOffset(armorShape);
                    Point2D pos = { center.x - armorShape->GetWidth() / 2 + offset.x,
                                     center.y - armorShape->GetHeight() / 2 + offset.y };
                    armorShape->SetPosition(&pos);
                    fb->drawShape(armorShape);
                }
            }
        }
    }
}

// Draws a ship's TARGSHAP.PAK target diagram: targeting/lock animation
// (frames 0-4 play once, then hold on frame 5 — user-confirmed, 2026-07
// session; the old behavior looped 0-5 forever, which doesn't match a
// "lock acquired, stays locked" animation) plus, via DrawShipFacingDiagram,
// per-facing shield/armor tiers when the 51-frame variant has them. Shared
// by RenderTargetWithCam (the full-screen HUD-projected target overlay)
// and RenderMFDSTarget (the right MFD's fixed panel) — animFrame/animTimer/
// lastActor are the caller's own SCCockpit::targetDiagram* fields, passed
// by reference rather than kept as function-local static state, so the
// animation actually resets to frame 0 when the locked target changes
// instead of a single shared counter free-running regardless of target.
static void DrawShipTargetDiagram(FrameBuffer *fb, Point2D center, SCMissionActors *targetActor, RSImageSet *targShapes,
                                   int &animFrame, float &animTimer, SCMissionActors *&lastActor) {
    if (targetActor != lastActor) {
        lastActor = targetActor;
        animFrame = 0;
        animTimer = 0.0f;
    }
    if (animFrame < 5) {
        const float kFrameSeconds = 0.1f;
        animTimer += GameTimer::getInstance().getDeltaTime();
        while (animTimer >= kFrameSeconds && animFrame < 5) {
            animTimer -= kFrameSeconds;
            animFrame++;
        }
    }
    // Same 0-25 SVGA / 26-51 VGA split as DrawShipFacingDiagram's own
    // facingOffset — the lock-on animation is frames 0-5 of whichever
    // half matches the current resolution, not always the SVGA half.
    size_t animShapeIndex = (size_t)animFrame + (g_ifVGA ? 26 : 0);
    RLEShape *animShape = targShapes->GetShape(animShapeIndex);
    if (animShape != nullptr) {
        Point2D animPos = center;
        animPos.x = animPos.x - animShape->GetWidth() / 2;
        animPos.y = animPos.y - animShape->GetHeight() / 2;
        animShape->SetPosition(&animPos);
        fb->drawShape(animShape);
    }
    DrawShipFacingDiagram(fb, center, targetActor, targShapes);
}

static bool projectRealToHUD( Vector3D targetWorld,  Matrix planeFromWorld,  Vector3D eyeLocal,
                              FrameBuffer *fb, int &outX, int &outY) {
    // Transform target into plane local frame (-Z = forward, X = right, Y = up)
    Vector3D targetLocal = targetWorld.transformPoint(planeFromWorld);

    // Direction from eye to target
    Vector3D dir = {
        targetLocal.x - eyeLocal.x,
        targetLocal.y - eyeLocal.y,
        targetLocal.z - eyeLocal.z
    };

    // -Z = devant : dir.z doit être négatif pour que la cible soit devant
    const float forward = -dir.z;
    if (forward <= 0.001f)
        return false;

    // Géométrie du quad HUD dans le repère local (-Z=avant)
    // Le quad est dans le plan Z = -quadDist
    const float quadDist = (5.8f + 6.0f) * 0.5f; // 5.9 unités devant l'œil
    const float quadXmin = -1.22f, quadXmax = 1.35f; // latéral (X_local)
    const float quadYmin = -0.8f,  quadYmax = 2.0f;  // vertical (Y_local)

    // Intersection rayon avec le plan Z = -quadDist
    // eyeLocal.z + t * dir.z = -quadDist  =>  t = (-quadDist - eyeLocal.z) / dir.z
    float t = (-quadDist - eyeLocal.z) / dir.z;

    float xHit = eyeLocal.x + t * dir.x;
    float yHit = eyeLocal.y + t * dir.y;

    // Mapping UV -> pixels HUD
    float u = (xHit - quadXmin) / (quadXmax - quadXmin); // 0=gauche, 1=droite
    float v = (quadYmax - yHit) / (quadYmax - quadYmin); // 0=haut, 1=bas (Y inversé)

    outX = (int)(u * (fb->width  - 1));
    outY = (int)(v * (fb->height - 1));

    return (outX >= 0 && outX < fb->width && outY >= 0 && outY < fb->height);
}

bool SCCockpit::project_to_screen(Vector3D coord, int &Xout, int &Yout) {
    if (this->is_3d_cockpit) {
        Matrix planeFromWorld = this->player_plane->ptw.invertRigidBodyMatrixLocal();
        return projectRealToHUD(coord, planeFromWorld, {0,0,0}, this->hud_framebuffer, Xout, Yout);
    }
    Vector3D campos = this->cockpit_camera.getPosition();
    Vector3DHomogeneous v = {coord.x, coord.y, coord.z, 1.0f};

    Matrix *mproj = this->cockpit_camera.getProjectionMatrix();
    Matrix *mview = this->cockpit_camera.getViewMatrix();

    Vector3DHomogeneous mcombined = mview->multiplyMatrixVector(v);
    Vector3DHomogeneous result = mproj->multiplyMatrixVector(mcombined);
    if (result.z > 0.0f) {
        float x = result.x / result.w;
        float y = result.y / result.w;

        Xout = (int)((x + 1.0f) * 160.0f);
        Yout = (int)((1.0f - y - 0.45f) * 100.0f) - 1;
        return true;
    }
    return false;
}
Point2D SCCockpit::GetWC3MfdSize() {
    if (this->cockpit != nullptr && !this->cockpit->instrumentShapes.empty()) {
        const RSCockpit::WC3VDULayout &vdu = g_ifVGA ? this->cockpit->vgaFrontVDU : this->cockpit->svgaFrontVDU;
        if (vdu.valid) {
            return {vdu.width, vdu.height};
        }
    }
    return {115, 95};
}
void SCCockpit::SetWC3Resolution(bool vga) {
    g_ifVGA = vga;
    VGA.SetCanvasResolution(vga ? 320 : 640, vga ? 200 : 480);
    // target_framebuffer/comm_framebuffer/debug_framebuffer/mfd_*_framebuffer
    // are all sized from g_ifVGA at SCCockpit::init() time and never
    // revisited elsewhere — resize them here now that this is a live,
    // repeatable toggle rather than a one-off settings choice.
    if (this->target_framebuffer != nullptr) {
        delete this->target_framebuffer;
        this->target_framebuffer = new FrameBuffer(vga ? 320 : 640, vga ? 200 : 480);
    }
    if (this->comm_framebuffer != nullptr) {
        delete this->comm_framebuffer;
        this->comm_framebuffer = new FrameBuffer(vga ? 320 : 640, 13);
    }
    if (this->debug_framebuffer != nullptr) {
        delete this->debug_framebuffer;
        this->debug_framebuffer = new FrameBuffer(vga ? 320 : 640, vga ? 200 : 480);
    }
    // GetWC3MfdSize reads g_ifVGA (already updated above) to pick the real
    // per-resolution VDU size.
    Point2D mfdSize = this->GetWC3MfdSize();
    if (this->mfd_right_framebuffer != nullptr) {
        delete this->mfd_right_framebuffer;
        this->mfd_right_framebuffer = new FrameBuffer(mfdSize.x, mfdSize.y);
    }
    if (this->mfd_left_framebuffer != nullptr) {
        delete this->mfd_left_framebuffer;
        this->mfd_left_framebuffer = new FrameBuffer(mfdSize.x, mfdSize.y);
    }
}
// F1: SVGA_COCKPIT -> VGA_COCKPIT -> HUD_ONLY -> SVGA_COCKPIT. Only the two
// *_COCKPIT states are a resolution choice (SetWC3Resolution); HUD_ONLY
// deliberately keeps whichever resolution was already active — it's a
// "hide the cockpit frame" toggle, not a third resolution. This is a
// session-only override (doesn't call saveGraphicsSettings, unlike the
// Options menu's own VGA/SVGA choice) — cycling views in flight shouldn't
// silently rewrite the player's saved default.
void SCCockpit::CycleViewMode() {
    switch (this->cockpit_view_mode) {
    case WC3CockpitViewMode::SVGA_COCKPIT:
        this->cockpit_view_mode = WC3CockpitViewMode::VGA_COCKPIT;
        this->SetWC3Resolution(true);
        break;
    case WC3CockpitViewMode::VGA_COCKPIT:
        this->cockpit_view_mode = WC3CockpitViewMode::HUD_ONLY;
        break;
    case WC3CockpitViewMode::HUD_ONLY:
        this->cockpit_view_mode = WC3CockpitViewMode::SVGA_COCKPIT;
        this->SetWC3Resolution(false);
        break;
    }
}
SCCockpit::SCCockpit() {
    // Matches the current logical canvas aspect ratio (see
    // commons/GraphicsSettings.h/RSVGA::SetCanvasResolution) — 320x200 for
    // VGA, 640x480 for SVGA. Read g_ifVGA directly rather than
    // VGA.GetCanvasWidth()/Height(): this constructor can run before
    // RSVGA::init() has applied the setting, but g_ifVGA itself is set
    // during early startup regardless (see main.cpp's wc3_options.cfg
    // load), so it's always current.
    float aspect = g_ifVGA ? (320.0f / 200.0f) : (640.0f / 480.0f);
    this->cockpit_camera.setPersective(45.0f, aspect, 0.1f, 10000.0f);
}
SCCockpit::~SCCockpit() {
    if (this->hud_framebuffer) {
        delete this->hud_framebuffer;
        this->hud_framebuffer = nullptr;
    }
    if (this->mfd_right_framebuffer) {
        delete this->mfd_right_framebuffer;
        this->mfd_right_framebuffer = nullptr;
    }
    if (this->mfd_left_framebuffer) {
        delete this->mfd_left_framebuffer;
        this->mfd_left_framebuffer = nullptr;
    }
    if (this->commVideoStream) {
        delete this->commVideoStream;
        this->commVideoStream = nullptr;
    }
    if (this->commVideoOwnedData) {
        delete[] this->commVideoOwnedData;
        this->commVideoOwnedData = nullptr;
    }
}
/**
 * SCCockpit::init
 *
 * Initialize the cockpit object from the standard SC cockpit assets.
 *
 * This function reads the palette from the standard SC palette IFF file,
 * initializes an RSPalette object from it, and assigns the result to the
 * SCCockpit::palette member variable.
 *
 * It also initializes the cockpit object from the F16 cockpit IFF file
 * and assigns the result to the SCCockpit::cockpit member variable.
 */
void SCCockpit::init() {
    RSPalette palette;
    TreEntry *entries = (TreEntry *)Assets.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (entries != nullptr) {
        palette.initFromFileRam(entries->data, entries->size);
    } else {
        FileData *f = Assets.GetFileData("PALETTE.IFF");
        if (f != nullptr) {
            palette.initFromFileData(f);
        }
    }
    this->palette = *palette.GetColorPalette();
    cockpit = nullptr;
    TreEntry *cockpit_def = nullptr;
    if (player_plane != nullptr) {
        if (player_plane->object->entity != nullptr) {
            RSEntity *e = player_plane->object->entity;
            cockpit_def = Assets.GetEntryByName("..\\..\\DATA\\OBJECTS\\" + e->cockpit_name + ".IFF");
            // WC3's cockpit IFFs (MEDPIT.IFF, ARWPIT.IFF, etc., from
            // MISSIONS.TRE) live under DATA\COCKPITS\, not DATA\OBJECTS\ (the
            // Strike Commander convention above). Try that path too before
            // falling through to the SC-specific F16/F22 fallbacks below.
            if (cockpit_def == nullptr)
                cockpit_def = Assets.GetEntryByName("..\\..\\DATA\\COCKPITS\\" + e->cockpit_name + ".IFF");
        }
    }
    if (cockpit_def == nullptr) {
        if (player_plane->object->entity->name == "..\\..\\DATA\\OBJECTS\\F-16DES.IFF") {
            cockpit_def = Assets.GetEntryByName("..\\..\\DATA\\OBJECTS\\F16-CKPT.IFF");
            this->is_f16_cockpit = true;
        } else if (player_plane->object->entity->name == "..\\..\\DATA\\OBJECTS\\F-22.IFF") {
            cockpit_def = Assets.GetEntryByName("..\\..\\DATA\\OBJECTS\\F22-CKPT.IFF");
            this->is_f16_cockpit = false;
        } else if (player_plane->object->entity->name == "..\\..\\DATA\\OBJECTS\\F-22B.IFF") {
            cockpit_def = Assets.GetEntryByName("..\\..\\DATA\\OBJECTS\\F22-CKPT.IFF");
            this->is_f16_cockpit = false;
        }
    }
    if (cockpit_def != nullptr) {
        cockpit = new RSCockpit();
        // WC3's cockpit IFFs use a top-level "FORM COCK", entirely unrelated
        // to Strike Commander's "FORM CKPT" — detect it from the tag at
        // byte offset 8 (after the "FORM"/size header) and dispatch to the
        // WC3-specific parser instead.
        bool isWC3Cockpit = cockpit_def->size >= 12 &&
                             memcmp(cockpit_def->data + 8, "COCK", 4) == 0;
        if (isWC3Cockpit) {
            cockpit->InitFromWC3Ram(cockpit_def->data, cockpit_def->size);
        } else {
            cockpit->InitFromRam(cockpit_def->data, cockpit_def->size);
        }
        // Framebuffers/fonts are needed for MFD and instrument rendering
        // regardless of whether SC's HUD.IFF-based analog HUD asset exists —
        // WC3 doesn't ship one (its own HUD is drawn from the cockpit's own
        // sprite atlas instead, not yet wired up), so this used to be gated
        // on hud_def and left every framebuffer/font null for WC3.
        for (int i = 0; i < 36; i++) {
            HudLine line;
            line.start.x = 0;
            line.start.y = 0 + i * 20;
            line.end.x = 70;
            line.end.y = 0 + i * 20;
            horizon.push_back(line);
        }
        hud_framebuffer = new FrameBuffer(150, 128);
        // Sized from the real per-cockpit COCK>VDU chunk when available
        // (InitFromWC3Ram already ran above, so vgaFrontVDU/svgaFrontVDU
        // are populated by now) — falls back to the old 115x95 guess only
        // if the file has no valid VDU chunk, or this is an SC cockpit.
        Point2D mfdSize = this->GetWC3MfdSize();
        mfd_right_framebuffer = new FrameBuffer(mfdSize.x, mfdSize.y);
        mfd_left_framebuffer = new FrameBuffer(mfdSize.x, mfdSize.y);
        Point2D raws_size = {this->cockpit->MONI.INST.RAWS.ZOOM.GetWidth(), this->cockpit->MONI.INST.RAWS.ZOOM.GetHeight()};
        raws_framebuffer = new FrameBuffer((std::max)(raws_size.x, 1), (std::max)(raws_size.y, 1));
        // target_framebuffer spans the whole logical canvas (the targeting
        // overlay's own bounding box, see RSCockpit::targetHudVGA/SVGA, is
        // defined in that same canvas' coordinate space) — sized to match
        // g_ifVGA rather than hardcoded 320x200, so SVGA mode's box
        // ((0,0)-(639,268), a genuinely different 4:3 canvas, not a 2x
        // scale of VGA's) doesn't draw or clip against an undersized
        // buffer. comm_framebuffer's width follows the same canvas width
        // (it's a full-width text banner); its height (13, a font-line
        // height) doesn't scale with resolution.
        target_framebuffer = new FrameBuffer(g_ifVGA ? 320 : 640, g_ifVGA ? 200 : 480);
        alti_framebuffer = new FrameBuffer(33, 29);
        speed_framebuffer = new FrameBuffer(36, 29);
        // 22x49 matches WC3CockpitShapeId::kBarMeterFillA's own frame size
        // (MEDPIT.IFF) — only meaningful for WC3 cockpits, see
        // RenderShieldGauge.
        shield_framebuffer = new FrameBuffer(22, 49);
        comm_framebuffer = new FrameBuffer(g_ifVGA ? 320 : 640, 13);
        this->font = FontManager.GetFont("..\\..\\DATA\\FONTS\\SHUDFONT.SHP");
        this->big_font = FontManager.GetFont("..\\..\\DATA\\FONTS\\HUDFONT.SHP");

        TreEntry *hud_def = Assets.GetEntryByName("..\\..\\DATA\\OBJECTS\\HUD.IFF");
        if (hud_def != nullptr) {
            hud = new RSHud();
            hud->InitFromRam(hud_def->data, hud_def->size);
        }
    }
    debug_framebuffer = new FrameBuffer(g_ifVGA ? 320 : 640, g_ifVGA ? 200 : 480);
}
void SCCockpit::RenderMFDS(Point2D mfds, FrameBuffer *fb = nullptr, bool clearBackground) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    // Clear the panel first. Not every MFD page draws an opaque
    // full-panel background of its own, and RLE shape drawing only paints
    // non-transparent pixels — without an explicit clear here, switching
    // pages (e.g. weapons -> power) left the previous page's pixels
    // visible underneath/around the new page's content instead of being
    // replaced by it. This used to early-return before ever reaching here
    // for WC3 cockpits (no MONI.SHAP casing — that's an SC-only asset),
    // which is exactly the case that was missing this clear.
    //
    // RenderMFDSWeapon passes clearBackground=false (user-requested,
    // 2026-07 session: "get rid of the clear to black on the weapons
    // MFD") — it has no confirmed real backdrop art of its own, so the
    // black clear just left every icon/gauge sitting on a flat black
    // panel instead. A follow-up attempt drew shape 53 (kSidePanel, 0x35)
    // here as a substitute background, but the user then asked for the
    // weapon MFD to not draw 0x35 either (it's the real Comm-page
    // backdrop — see RenderMFDSCommWC3 — not a generic one; drawing it on
    // Weapon was a guess, now retracted). So: no clear, no substitute
    // background, just skip straight to the caller's own icons/gauges
    // drawing over whatever was already on screen. Accepting the
    // page-switch residue risk this clear existed to prevent, same as
    // the original request.
    if (!clearBackground) {
        return;
    }
    Point2D mfdSize = this->GetWC3MfdSize();
    for (int row = 0; row < mfdSize.y; row++) {
        fb->line(mfds.x, mfds.y + row, mfds.x + mfdSize.x - 1, mfds.y + row, 0);
    }
    if (this->cockpit->MONI.SHAP.data == nullptr) {
        return;
    }
    this->cockpit->MONI.SHAP.SetPosition(&mfds);
    for (int i = 0; i < this->cockpit->MONI.SHAP.GetHeight(); i++) {
        fb->line(mfds.x, mfds.y + i, mfds.x + this->cockpit->MONI.SHAP.GetWidth(), mfds.y + i, 2);
    }
    fb->drawShape(&this->cockpit->MONI.SHAP);
}
// Right MFD panel, per the user's spec: TARGSHAP.PAK's own frames 0-5
// looping continuously while no target is selected (user-corrected,
// 2026-07 session — previously this drew the unrelated generic COCK>SHAP
// kTargetLockBox scanning animation instead), then the per-ship diagram
// (DrawShipTargetDiagram, shared with RenderTargetWithCam's full-screen
// HUD overlay) once current_target_actor is set. Always called for the
// right MFD (see RenderHUD's dispatch) — unlike the left MFD, not gated
// behind show_radars/show_weapons/etc.
void SCCockpit::RenderMFDSTarget(Point2D pmfd, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    this->RenderMFDS(pmfd, fb);
    // Real WC3 MFD panel size from the cockpit file's own COCK>VDU chunk
    // (GetWC3MfdSize) rather than MONI.SHAP.GetWidth/Height — that field is
    // SC-only (never populated for WC3 cockpits) and RLEShape's dist fields
    // have no default initializer, so it reads uninitialized memory here
    // rather than reliably 0.
    Point2D mfdSize = this->GetWC3MfdSize();
    Point2D center = {pmfd.x + mfdSize.x / 2, pmfd.y + mfdSize.y / 2};
    // Background grid — user-confirmed (2026-07 session, from the same
    // id-by-id contact sheet used to identify the shield gauge): id 29
    // (kRadarScreenGrid)/59 (its SVGA pair) is specifically the shield
    // and targeting displays' background, not a generic panel backdrop
    // reused elsewhere. RenderMFDSShield already draws it indirectly
    // (its mode=0x00 INST record resolves to this same id/pair per
    // cockpit file); this panel has no such per-mode INST record of its
    // own (it's the fixed right MFD, not one of the paged left-MFD
    // modes), so it's looked up directly here instead.
    {
        auto bgIt = this->cockpit->instrumentShapes.find(g_ifVGA ? 59 : 29);
        if (bgIt != this->cockpit->instrumentShapes.end() && bgIt->second != nullptr && bgIt->second->GetNumImages() > 0) {
            RLEShape *bgShape = bgIt->second->GetShape(0);
            if (bgShape != nullptr) {
                Point2D bgPos = pmfd;
                bgShape->SetPosition(&bgPos);
                fb->drawShape(bgShape);
            }
        }
    }
    if (this->current_target_actor == nullptr) {
        // Index 0 is a real TARGSHAP entry (same decompressed layout as
        // every ship-specific one) that no ship name maps to in
        // TargShapeIndex::kShipNameToIndex — used here as the generic,
        // ship-independent source for the frames 0-5 lock/scan loop, since
        // that animation is presumed identical UI chrome across entries
        // (see the TARGSHAP.PAK header comment above).
        RSImageSet *loopShapes = TargShapeIndex::GetShapesForIndex(0);
        if (loopShapes == nullptr || loopShapes->GetNumImages() == 0) {
            return;
        }
        static float loopTimer = 0.0f;
        static int loopFrame = 0;
        const float kFrameSeconds = 0.1f;
        loopTimer += GameTimer::getInstance().getDeltaTime();
        while (loopTimer >= kFrameSeconds) {
            loopTimer -= kFrameSeconds;
            loopFrame = (loopFrame + 1) % 6;
        }
        size_t shapeIndex = (size_t)loopFrame + (g_ifVGA ? 26 : 0);
        RLEShape *shape = loopShapes->GetShape(shapeIndex);
        if (shape == nullptr) {
            return;
        }
        Point2D pos = center;
        pos.x = pos.x - shape->GetWidth() / 2;
        pos.y = pos.y - shape->GetHeight() / 2;
        shape->SetPosition(&pos);
        fb->drawShape(shape);
        return;
    }
    RSImageSet *targShapes = TargShapeIndex::SelectTargShapeSet(this->current_target_actor);
    if (targShapes == nullptr || targShapes->GetNumImages() == 0) {
        return;
    }
    DrawShipTargetDiagram(fb, center, this->current_target_actor, targShapes, this->targetDiagramAnimFrame,
                           this->targetDiagramAnimTimer, this->targetDiagramLastActor);
}
// Left MFD, 'p' key: 4 power meters. See RenderMFDSPower's own body comment
// for the real COCK>FRNT>INST mode=0x060a group this draws from. The
// fill/level bars and E/W/S/D letters are a simulated overlay (see
// SCCockpit::power_alloc/CyclePowerOrShield/AdjustSelectedPower) — no chunk
// anywhere records real per-meter fill art or a fill/level value, so the
// bars are drawn as plain solid rectangles rather than fabricated shape
// ids (see this function's history for why kBarMeterFillSmallA/B were
// dropped as a mislabeled guess). SC-format cockpits have no equivalent
// concept at all, so this is a no-op there (just the shared MFD
// background/border).
void SCCockpit::RenderMFDSPower(Point2D pmfd, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    this->RenderMFDS(pmfd, fb);
    if (this->cockpit->instrumentShapes.empty()) {
        return;
    }
    // Real per-file layout: COCK>FRNT>INST's only repeating MFD-relative
    // group in the whole table (every other record is a singleton) is
    // mode=0x060a, 4 records, shape id 47 (kGaugeBezelHorizontalSmall) —
    // confirmed universal, byte-identical relative x/y across all 6 real
    // cockpit files checked (5 of 6 share x=16,y=6/15/24/33; MEDPIT alone
    // is 1px off at y=5/14/23/32, unexplained). This is the real "4 power
    // meters" layout; file order top-to-bottom is user-confirmed as
    // Engines/Weapons/Shields/Damage-repair (E/W/S/D).
    // Hand-rolled 5x7 bitmap glyphs, not RSFont/WC3Font: RSFont (this->font/
    // big_font) is an SC-only asset — WC3 cockpits never populate it (see
    // WC3NavMap.h's "WC3 has no equivalent asset for at all" comment,
    // confirmed live: this->big_font null-derefs here for a real WC3
    // cockpit). The real WC3 text path (WC3Font/WC3Globals) lives in the
    // wing_commander_3/ layer, which strike_commander/ (this file) can't
    // depend on without the same virtual-hook-override machinery
    // SCNavMap/WC3NavMap needed for its own text overlay — out of scope
    // for 4 static letters, so they're drawn directly instead.
    static const char *const kGlyphE[7] = {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
    static const char *const kGlyphW[7] = {"10001", "10001", "10001", "10101", "10101", "11011", "10001"};
    static const char *const kGlyphS[7] = {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
    static const char *const kGlyphD[7] = {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
    static const char *const *const kGaugeGlyphs[4] = {kGlyphE, kGlyphW, kGlyphS, kGlyphD};
    auto drawGlyph = [&](const char *const *glyph, int x, int y, uint8_t color) {
        for (int row = 0; row < 7; row++) {
            const char *line = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (line[col] == '1') {
                    fb->plot_pixel(x + col, y + row, color);
                }
            }
        }
    };
    uint8_t colorRed = ClosestPaletteIndex(this->palette, 255, 0, 0);
    uint8_t colorGreen = ClosestPaletteIndex(this->palette, 0, 255, 0);
    auto &layouts = g_ifVGA ? this->cockpit->vgaFrontInstruments : this->cockpit->svgaFrontInstruments;
    int gaugeIndex = 0;
    for (auto &rec : layouts) {
        if (rec.mode != 0x060a) continue;
        auto it = this->cockpit->instrumentShapes.find(rec.shapeId);
        if (it == this->cockpit->instrumentShapes.end() || it->second == nullptr || it->second->GetNumImages() == 0) {
            continue;
        }
        RLEShape *shape = it->second->GetShape(0);
        if (shape == nullptr) {
            continue;
        }
        Point2D pos = {pmfd.x + rec.x, pmfd.y + rec.y};
        shape->SetPosition(&pos);
        fb->drawShape(shape);

        if (gaugeIndex < 4) {
            // "Fill" is how much of the real gauge graphic's own left edge
            // is shown — user-confirmed real behavior ("only the first
            // quarter of each gauge graphic is visible" by default, i.e.
            // the graphic itself is cropped, not a bezel plus a
            // separately-colored overlay bar). Draw the whole shape, then
            // paint over the unlit portion (from the fill point to its
            // right edge) with black, rather than clipping the draw call
            // itself. Fraction is this subsystem's share of the 100-unit
            // power pool: default 25/100 -> exactly a quarter shown.
            float frac = this->power_alloc[gaugeIndex] / kTotalPowerUnits;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            int visibleW = (int)(shape->GetWidth() * frac + 0.5f);
            int maskX = pos.x + visibleW;
            // Inset 7px off the top/bottom and 1px off the right edge so
            // the mask covers the gauge's interior track only, leaving its
            // bezel border visible on those three sides.
            int maskW = shape->GetWidth() - visibleW - 1;
            int maskTop = 7;
            int maskBottom = shape->GetHeight() - 7;
            if (maskW > 0) {
                for (int row = maskTop; row < maskBottom; row++) {
                    fb->line(maskX, pos.y + row, maskX + maskW - 1, pos.y + row, 0);
                }
            }

            bool selected = (gaugeIndex == this->selected_power_gauge);
            drawGlyph(kGaugeGlyphs[gaugeIndex], pmfd.x + rec.x - 10, pos.y + (shape->GetHeight() - 7) / 2,
                      selected ? colorGreen : colorRed);
        }
        gaugeIndex++;
    }
}

// MDFS_POWER ('p') handler — user-confirmed real behavior (2026-07
// session): rotates through 5 states rather than a plain open/close
// toggle — gauge E highlighted, W, S, D, then the shield/hull MFD, then
// back to gauge E. There is no "closed" state reachable via 'p' itself;
// the panel only goes away by switching to a different MFD page (radar/
// weapons/comm/damage/cam), same as every other show_* flag. Direct 's'
// (MDFS_SHIELD) still independently toggles the shield page on its own,
// unrelated to this cycle.
void SCCockpit::CyclePowerOrShield() {
    if (this->show_power) {
        this->selected_power_gauge++;
        if (this->selected_power_gauge > 3) {
            this->selected_power_gauge = 0;
            this->show_power = false;
            this->show_shield = true;
        }
    } else if (this->show_shield) {
        this->show_shield = false;
        this->show_power = true;
        this->selected_power_gauge = 0;
    } else {
        this->show_power = true;
        this->selected_power_gauge = 0;
    }
}

// Power-allocation MFD ('[' decreases, ']' increases the selected gauge —
// see SCStrike's context-sensitive RADAR_ZOOM_IN/OUT handler). delta is
// signed whole power units to move into (positive) or out of (negative)
// the currently selected gauge; the 4 gauges always sum to
// kTotalPowerUnits, so whatever's applied here is taken from/returned to
// the other three, split proportionally to their current share (falls
// back to an even split if they're all at 0, e.g. after repeatedly maxing
// one gauge out).
void SCCockpit::AdjustSelectedPower(float delta) {
    int sel = this->selected_power_gauge;
    if (sel < 0 || sel > 3) return;
    float otherSum = 0.0f;
    for (int i = 0; i < 4; i++) {
        if (i != sel) otherSum += this->power_alloc[i];
    }
    float applied = delta;
    if (applied > 0.0f) {
        applied = (std::min)(applied, otherSum);
        applied = (std::min)(applied, kTotalPowerUnits - this->power_alloc[sel]);
    } else if (applied < 0.0f) {
        applied = -(std::min)(-applied, this->power_alloc[sel]);
    }
    if (applied == 0.0f) return;
    this->power_alloc[sel] += applied;
    float remaining = -applied;
    for (int i = 0; i < 4; i++) {
        if (i == sel) continue;
        float share = (otherSum > 0.0f) ? (this->power_alloc[i] / otherSum) : (1.0f / 3.0f);
        this->power_alloc[i] += remaining * share;
        if (this->power_alloc[i] < 0.0f) this->power_alloc[i] = 0.0f;
    }
}

// Left MFD, 's' key: shield status — user-confirmed real page. COCK>FRNT>
// INST mode=0x00 is the real per-file backdrop for this page: a single
// record, shape id 59 (kGaugeBezelHorizontalSmall's sibling
// kRadarScreenGridSVGA — a generic grid-style panel background, not
// exclusively "radar"; reused here since SYS's own slot 0 is the real
// "Shields" subsystem id, matching this page number).
//
// The per-facing shield/hull diagram itself does NOT come from
// TARGSHAP.PAK (that's for displaying OTHER ships once targeted/scanned —
// see RenderMFDSTarget/DrawShipTargetDiagram). User-confirmed (2026-07
// session, identified live from a full id-by-id contact sheet dumped
// from this cockpit's own COCK>SHAP, cross-checked against a real
// screenshot): it's this cockpit's own shape id 9, 24 frames, real
// colors confirmed via the cockpit's own PALETTE.IFF — each facing gets
// its own dedicated 3-frame block (3/2/1 lines-or-tiers, frame = base +
// (3-tier)) —
//   top(front)=0-2, right=3-5, bottom(back)=6-8, left=9-11: shield ticks (yellow)
//   top(front)=12-14, right=15-17, bottom(back)=18-20, left=21-23: hull frame (orange/red)
// Driven by the same real per-facing simulation data
// (shield_front/back/left/right + max_shield_*, armor_front/back/
// left/right + max_armor_*) RenderShieldGauge/the old TARGSHAP path
// read, just for the player's own ship instead of the current target.
void SCCockpit::RenderMFDSShield(Point2D pmfd, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    this->RenderMFDS(pmfd, fb);
    if (this->cockpit->instrumentShapes.empty()) {
        return;
    }
    Point2D mfdSize = this->GetWC3MfdSize();
    Point2D center = {pmfd.x + mfdSize.x / 2, pmfd.y + mfdSize.y / 2};

    auto &layouts = g_ifVGA ? this->cockpit->vgaFrontInstruments : this->cockpit->svgaFrontInstruments;
    for (auto &rec : layouts) {
        if ((rec.mode >> 8) != 0x00) continue;
        // Shape 9 is the per-facing shield/hull gauge set drawn explicitly
        // below via drawTier(), keyed off real shield/armor values. This
        // page-0 INST record for it just points at the same shape's frame
        // 0 (front-facing, full-shield tick) with no value-driven logic —
        // drawing it here duplicated that gauge as an always-on sprite
        // near the panel's top-left corner, on top of the real one.
        if (rec.shapeId == 9) continue;
        auto it = this->cockpit->instrumentShapes.find(rec.shapeId);
        if (it == this->cockpit->instrumentShapes.end() || it->second == nullptr || it->second->GetNumImages() == 0) {
            continue;
        }
        RLEShape *shape = it->second->GetShape(0);
        if (shape == nullptr) continue;
        Point2D pos = {pmfd.x + rec.x, pmfd.y + rec.y};
        shape->SetPosition(&pos);
        fb->drawShape(shape);
    }

    if (this->player_plane == nullptr || this->player_plane->pilot == nullptr) {
        return;
    }
    auto gaugeIt = this->cockpit->instrumentShapes.find(9);
    if (gaugeIt == this->cockpit->instrumentShapes.end() || gaugeIt->second == nullptr ||
        gaugeIt->second->GetNumImages() < 18) {
        return;
    }
    RSImageSet *gaugeSet = gaugeIt->second;
    SCMissionActors *pilot = this->player_plane->pilot;

    auto tierFor = [](float cur, float mx) -> int {
        if (mx <= 0.0f) return 0;
        float frac = cur / mx;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        if (frac > 0.66f) return 3;
        if (frac > 0.33f) return 2;
        if (frac > 0.0f) return 1;
        return 0;
    };
    // Position fix (2026-07 session, user-prompted: "player shield
    // strength should have similar position coordinates in the IFF
    // files" — same root cause as the TARGSHAP facing-diagram bug fixed
    // earlier this session): shape 9's own frame headers carry real
    // x_blit/y_min placement data within a shared 100x87 canvas (decoded
    // and byte-verified against MEDPIT.IFF's real COCK>SHAP id 9 — 24
    // frames: 0-2/3-5/6-8/9-11 = shield front/right/back/left, 3 tiers
    // each; 12-14/15-17/18-20/21-23 = hull front/right/back/left, 3 tiers
    // each), exactly like every other RSWC3DecodeShapeEntry-decoded
    // frame. The hand-placed kShieldOfs/kHullOfs/kVerticalFacingUpOfs/etc.
    // constants this replaced were compensating for discarding that real
    // data and re-deriving an approximate layout by eye instead.
    auto drawTier = [&](int tier, uint32_t facingBase) {
        if (tier <= 0) return;
        RLEShape *shape = gaugeSet->GetShape(facingBase + (3 - tier));
        if (shape == nullptr) return;
        Point2D offset = GetOriginalCanvasOffset(shape);
        Point2D pos = { center.x - shape->GetWidth() / 2 + offset.x,
                         center.y - shape->GetHeight() / 2 + offset.y };
        shape->SetPosition(&pos);
        fb->drawShape(shape);
    };

    // Facing-to-frame-base mapping is user-confirmed (2026-07 session):
    // top=front, right, bottom=back, left, each its own dedicated 3-frame
    // block.
    drawTier(tierFor(pilot->shield_front, pilot->max_shield_front), 0);
    drawTier(tierFor(pilot->shield_right, pilot->max_shield_right), 3);
    drawTier(tierFor(pilot->shield_back, pilot->max_shield_back), 6);
    drawTier(tierFor(pilot->shield_left, pilot->max_shield_left), 9);

    drawTier(tierFor(pilot->armor_front, pilot->max_armor_front), 12);
    drawTier(tierFor(pilot->armor_right, pilot->max_armor_right), 15);
    drawTier(tierFor(pilot->armor_back, pilot->max_armor_back), 18);
    drawTier(tierFor(pilot->armor_left, pilot->max_armor_left), 21);
}
void SCCockpit::RenderTargetWithCam(Point2D top_left = {126, 5}, FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    // Real per-cockpit layout (COCK>VGA|SVGA>FRNT>TARG — see
    // RSCockpit::targetHudVGA/SVGA) when available: VGA is (0,0)-(319,112),
    // SVGA is (0,0)-(639,268) — byte-identical across every cockpit file
    // checked (MEDPIT/HVYPIT/ARWPIT), so this is a universal WC3 layout,
    // not something that needs per-ship lookup. Picks whichever matches
    // the current g_ifVGA setting (see commons/GraphicsSettings.h) since
    // target_framebuffer is now sized to match too. Falls back to the old
    // hardcoded 68x85 box for SC-format cockpits (F16/F22), which never
    // populate either.
    Point2D hud_size = {68, 85};
    if (this->cockpit != nullptr) {
        RSCockpit::TargetHudBox &targetHud = g_ifVGA ? this->cockpit->targetHudVGA : this->cockpit->targetHudSVGA;
        if (targetHud.valid) {
            top_left = {targetHud.xMin, targetHud.yMin};
            hud_size = {targetHud.xMax - targetHud.xMin,
                        targetHud.yMax - targetHud.yMin};
        }
    }
    Point2D bottom_right = {top_left.x + hud_size.x, top_left.y + hud_size.y};

    if (this->current_target != nullptr) {
        int Xhud, Yhud;
        if (project_to_screen(this->current_target->position, Xhud, Yhud)) {
            // Check if the target is within HUD boundaries. This used to
            // get immediately overwritten by a second, buggy check here
            // that compared hudX against fb->height instead of hudY
            // (copy-paste typo) — on top of the box itself being a
            // hardcoded 68x85 guess instead of the real ~319x112 region
            // above, targets past screen x=200 (of 320) were rejected
            // outright regardless of the box. Between the two, the
            // targeting display essentially never appeared. Confirmed by
            // decoding the real COCK>VGA>FRNT>TARG chunk (see
            // RSCockpit::targetHudVGA) directly from 3 different cockpit
            // files.
            bool isInHud = (Xhud >= top_left.x && Xhud < bottom_right.x && Yhud >= top_left.y && Yhud < bottom_right.y);
            int hudX = Xhud - top_left.x;
            int hudY = Yhud - top_left.y;
            // If the target is within the HUD boundaries, draw a targeting indicator
            if (isInHud) {
                Point2D targetPoint = {hudX + top_left.x, hudY + top_left.y};
                if (this->hud != nullptr) {
                    // SC-format path (F16/F22 cockpits, which ship HUD.IFF).
                    RLEShape *targetShape = this->hud->small_hud->TARG->SHAPSET->GetShape(1);
                    Point2D reticlePos = targetPoint;
                    reticlePos.x = reticlePos.x - targetShape->GetWidth() / 2;
                    reticlePos.y = reticlePos.y - targetShape->GetHeight() / 2;
                    targetShape->SetPosition(&reticlePos);
                    fb->drawShape(targetShape);
                    if (this->target_in_range && this->current_weapon_id != weapon_ids::ID_20MM) {
                        RLEShape *inRangeShape = this->hud->small_hud->MISD->SHAP;
                        Point2D inRangePos = targetPoint;
                        inRangePos.x = inRangePos.x - inRangeShape->GetWidth() / 2;
                        inRangePos.y = inRangePos.y - inRangeShape->GetHeight() / 2;
                        inRangeShape->SetPosition(&inRangePos);
                        fb->drawShape(inRangeShape);
                    }
                } else if (this->cockpit != nullptr) {
                    // WC3 cockpits never ship HUD.IFF (this->hud stays
                    // null) — the SC-format path above used to dereference
                    // it unconditionally here, crashing the moment a target
                    // was locked and in view. Use TARGSHAP.PAK's per-ship
                    // target diagram art instead (see TargShapeIndex above) —
                    // the correct, purpose-built resource for this, rather
                    // than the cockpit's own generic instrumentShapes
                    // (kTargetLockBox/kTargetHealthBar), which this used to
                    // fall back to before TARGSHAP.PAK's existence/layout
                    // was documented.
                    RSImageSet *targShapes = TargShapeIndex::SelectTargShapeSet(this->current_target_actor);
                    if (targShapes != nullptr && targShapes->GetNumImages() > 0) {
                        DrawShipTargetDiagram(fb, targetPoint, this->current_target_actor, targShapes,
                                               this->targetDiagramAnimFrame, this->targetDiagramAnimTimer,
                                               this->targetDiagramLastActor);
                    }
                }
            }
        }
    }
}

static bool projectLocalAnglesToHud(const Vector3D &vLocal, FrameBuffer *fb, int &outX, int &outY) {
    // Repère avion/cockpit:
    // -Z = devant
    // +X = droite (supposé)
    // +Y = haut (supposé)
    const float forward = -vLocal.z;
    if (forward <= 0.001f)
        return false; // derrière ou trop proche du plan

    // Angles (radians)
    // az > 0 => à droite si +X est à droite
    const float az = atan2f(vLocal.x, forward);

    // el > 0 => vers le haut si +Y est vers le haut
    // (si ton Y est inversé, enlève le '-' ci-dessous)
    const float el = atan2f(vLocal.y, forward);

    // A TUNER: FOV angulaire du HUD (collimation approximée)
    const float hudFovX = 30.0f * (float)M_PI / 180.0f; // total
    const float hudFovY = 25.0f * (float)M_PI / 180.0f; // total

    const float nx = az / (hudFovX * 0.5f); // [-1..1]
    const float ny = el / (hudFovY * 0.5f); // [-1..1]

    const float cx = (fb->width - 1) * 0.5f;
    const float cy = (fb->height - 1) * 0.5f;

    outX = (int)(cx + nx * cx);
    outY = (int)(cy - ny * cy);
    return (outX >= 0 && outX < fb->width && outY >= 0 && outY < fb->height);
}

void SCCockpit::RenderTargetingReticle(FrameBuffer *fb, CHUD_SHAPE *reticleShape, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter) {
    int hud_width = hudBottomRight.x - hudTopLeft.x;
    int hud_height = hudBottomRight.y - hudTopLeft.y;
    int hud_center_x = hud_width / 2;
    int hud_center_y = hud_height / 2;
    if (reticleShape == nullptr) {
        reticleShape = this->hud->small_hud->LCOS;
    }
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (!this->player_plane) {
        return;
    }
    float target_distance = 0.0f;
    const float dt = (this->player_plane->tps > 0) ? (1.0f / (float)this->player_plane->tps) : (1.0f / 60.0f);

    Vector3D avion_pos {
        this->player_plane->x,
        this->player_plane->y,
        this->player_plane->z
    };
    Vector3D planeVelWorld = {
        (this->player_plane->x - this->player_plane->last_px),
        (this->player_plane->y - this->player_plane->last_py),
        (this->player_plane->z - this->player_plane->last_pz)
    };
    
    float timeOfFlight = 0.5f;
    float debutTimeOfFlight = timeOfFlight;
    
    float projectile_speed = 1000.0f;
    
    Vector3D initial_trust{0, 0, 0};

    initial_trust = this->player_plane->getWeaponIntialVector(projectile_speed);
    float projectile_speed_world = initial_trust.Length();
    // === LEAD ANGLE: vitesse de la cible ===
    Vector3D targetVelocityWorld = {0.0f, 0.0f, 0.0f};
    Vector3D predictedTargetPos = (this->current_target != nullptr) 
        ? this->current_target->position 
        : avion_pos;
    float newDist = 0.0f;
    if (this->current_target != nullptr) {
        Vector3D toTarget = {
            this->current_target->position.x - avion_pos.x,
            this->current_target->position.y - avion_pos.y,
            this->current_target->position.z - avion_pos.z
        };
        target_distance = toTarget.Length();
        
        // Temps de vol vers la cible courante
        float tof = target_distance / projectile_speed_world;
        int nbsteps_predict = tof * this->player_plane->tps;
        timeOfFlight = tof;
        debutTimeOfFlight = timeOfFlight;
        // Récupérer la vitesse de la cible depuis l'acteur mission
        // On cherche l'acteur correspondant à this->current_target
        SCMissionActors *targetActor = this->current_target_actor;

        if (targetActor != nullptr && targetActor->plane != nullptr) {
            
            /**/
            Vector3D targetVelocityWorld = {
                (targetActor->plane->x - targetActor->plane->last_px) / dt,
                (targetActor->plane->y - targetActor->plane->last_py) / dt,
                (targetActor->plane->z - targetActor->plane->last_pz) / dt
            };
        }

        // Position prédite de la cible à l'instant d'impact (lead)
        // On itère: la position prédite change le ToF, qui change la position prédite
        // 2-3 itérations suffisent pour converger
        for (int iter = 0; iter < 3; iter++) {
            predictedTargetPos = {
                this->current_target->position.x + targetVelocityWorld.x * tof,
                this->current_target->position.y + targetVelocityWorld.y * tof,
                this->current_target->position.z + targetVelocityWorld.z * tof
            };
            // Recalcul du ToF avec la nouvelle distance prédite
            Vector3D toPredict = {
                predictedTargetPos.x - avion_pos.x,
                predictedTargetPos.y - avion_pos.y,
                predictedTargetPos.z - avion_pos.z
            };
            newDist = toPredict.Length();
            tof = (projectile_speed_world > 0.0f) ? (newDist / projectile_speed_world) : 0.0f;
            nbsteps_predict = tof * this->player_plane->tps;
        }
        
        timeOfFlight = tof;
    }

    int nbsteps = timeOfFlight * this->player_plane->tps;
    GunSimulatedObject *weap = new GunSimulatedObject();
    
    Vector3D omegaLocal = this->player_plane->angular_velocity;
    
    Matrix &ptw = this->player_plane->ptw;
    Vector3D omegaStep = {
        ptw.v[0][0] * omegaLocal.x + ptw.v[1][0] * omegaLocal.y + ptw.v[2][0] * omegaLocal.z,
        ptw.v[0][1] * omegaLocal.x + ptw.v[1][1] * omegaLocal.y + ptw.v[2][1] * omegaLocal.z,
        ptw.v[0][2] * omegaLocal.x + ptw.v[1][2] * omegaLocal.y + ptw.v[2][2] * omegaLocal.z
    };

    initial_trust = this->player_plane->getWeaponIntialVector(projectile_speed);
    Vector3D planeDispAccum{0, 0, 0};
    
    weap->obj = this->player_plane->weaps_load[0]->objct;
    weap->x = this->player_plane->x + planeDispAccum.x;
    weap->y = this->player_plane->y + planeDispAccum.y;
    weap->z = this->player_plane->z + planeDispAccum.z;
    weap->vx = initial_trust.x;
    weap->vy = initial_trust.y;
    weap->vz = initial_trust.z;
    weap->weight = this->player_plane->weaps_load[0]->objct->weight_in_kg * 2.205f;
    weap->azimuthf = this->player_plane->azimuthf;
    weap->elevationf = this->player_plane->elevationf;
    weap->target = nullptr;

    Vector3D impact{0, 0, 0};
    Vector3D velo{0, 0, 0};
    bool firstIter = true;
    float bulletSpeedAfterShot = 0.0f;
    for (int i = 0; i < nbsteps; i++) {
        // 1. Accumuler le déplacement avion
        planeDispAccum = planeDispAccum + planeVelWorld;
        planeVelWorld = planeVelWorld.rotateByAxis(omegaStep);

        std::tie(impact, velo) = weap->ComputeTrajectory(this->player_plane->tps);
        
        Vector3D finalPos = {
            impact.x + planeDispAccum.x,
            impact.y + planeDispAccum.y,
            impact.z + planeDispAccum.z
        };

        weap->x = impact.x;
        weap->y = impact.y;
        weap->z = impact.z;
        
        weap->vx = velo.x;
        weap->vy = velo.y;
        weap->vz = velo.z;

        if (firstIter) {
            bulletSpeedAfterShot = velo.Length();
            firstIter = false;
        }
    }

    const Vector3D impactWorld = {
        impact.x + planeDispAccum.x,
        impact.y + planeDispAccum.y,
        impact.z + planeDispAccum.z
    };
    this->targetImpactPointWorld = impactWorld;

    

    int Xdraw = 0, Ydraw = 0;
    bool onHud = false;
    //if () {
    if (project_to_screen(impactWorld, Xdraw, Ydraw)) {
        if (this->is_3d_cockpit == false) {
            Xdraw = Xdraw - hudCenter.x + hud_center_x + hudTopLeft.x;
            Ydraw = Ydraw - hudCenter.y + hud_center_y + hudTopLeft.y;
        }
        Point2D gun_piper= {Xdraw, Ydraw};
        RLEShape *s = reticleShape->SHAPSET->GetShape(0);
        int shapeWidth = s->GetWidth();
        int shapeHeight = s->GetHeight();
        gun_piper.x -= (1+shapeWidth / 2);
        gun_piper.y -= (-1+shapeHeight / 2);
        s->SetPosition(&gun_piper);
        fb->drawShapeWithBox(s, hudTopLeft.x, hudBottomRight.x, hudTopLeft.y, hudBottomRight.y);
        int distance_to_target_index = target_distance*3.2808399f / 1000;
        if (distance_to_target_index > 12 ) {
            distance_to_target_index = 12;
        }
        std::unordered_map<int, int> distance_to_target_shape = {
            {0, 0},
            {1, 1},
            {2, 2},
            {3, 3},
            {4, 4},
            {5, 5},
            {6, 6},
            {7, 7},
            {8, 8},
            {9, 10},
            {10, 11},
            {11, 12},
            {12, 9},
            {20, 0},
            {21, 1},
            {22, 2},
            {23, 3},
            {24, 6},
            {25, 4},
            {26, 5},
            {27, 7},
            {28, 9},
            {29, 12},
            {30, 10},
            {31, 11},
            {32, 8}
        };
        
        int max_d_width = reticleShape->SHAPSET->GetShape(9)->GetWidth();
        int max_d_height = reticleShape->SHAPSET->GetShape(9)->GetHeight();
        distance_to_target_index += max_d_width == 13 ? 20 : 0;
        int offset_y = shapeHeight / 2 - ((shapeHeight - max_d_height)/2);
        if (distance_to_target_index != 0 && distance_to_target_index != 20) {
            RLEShape *distance = reticleShape->SHAPSET->GetShape(distance_to_target_shape[distance_to_target_index]);
            Point2D anchor = {Xdraw, Ydraw};
            anchor.x -= distance->leftDist+1;
            anchor.y -= offset_y-1;

            distance->SetPosition(&anchor);
            fb->drawShapeWithBox(distance, hudTopLeft.x, hudBottomRight.x, hudTopLeft.y, hudBottomRight.y);
        }
    }

    // === Affichage du LEAD DIAMOND sur la cible prédite ===
    // On projette la position future de la cible (avec lead)
    if (this->current_target != nullptr) {
        int Xlead = 0, Ylead = 0;
        if (project_to_screen(predictedTargetPos, Xlead, Ylead)) {
            if (this->is_3d_cockpit == false) {
                Xlead = Xlead - hudCenter.x + hud_center_x + hudTopLeft.x;
                Ylead = Ylead - hudCenter.y + hud_center_y + hudTopLeft.y;
            }
            // Dessine un losange/diamond autour de la position prédite de la cible
            int diamond_size = 4;
            if (this->face == CP_BIG) {
                diamond_size = 6;
            }
            fb->lineWithBox(Xlead, Ylead - diamond_size, Xlead + diamond_size, Ylead, 223, 0, fb->width, 0, fb->height);
            fb->lineWithBox(Xlead + diamond_size, Ylead, Xlead, Ylead + diamond_size, 223, 0, fb->width, 0, fb->height);
            fb->lineWithBox(Xlead, Ylead + diamond_size, Xlead - diamond_size, Ylead, 223, 0, fb->width, 0, fb->height);
            fb->lineWithBox(Xlead - diamond_size, Ylead, Xlead, Ylead - diamond_size, 223, 0, fb->width, 0, fb->height);
        }

        int Xtarget = 0, Ytarget = 0;
        if (project_to_screen(this->current_target->position, Xtarget, Ytarget)) {
            if (this->is_3d_cockpit == false) {
                Xtarget = Xtarget - hudCenter.x + hud_center_x + hudTopLeft.x;
                Ytarget = Ytarget - hudCenter.y + hud_center_y + hudTopLeft.y;
            }
            // Dessine un carré autour de la position actuelle de la cible
            int box_size = 3;
            if (this->face == CP_BIG) {
                box_size = 5;
            }
            fb->lineWithBox(Xtarget - box_size, Ytarget - box_size, Xtarget + box_size, Ytarget - box_size, 223, 0, fb->width, 0, fb->height);
            fb->lineWithBox(Xtarget + box_size, Ytarget - box_size, Xtarget + box_size, Ytarget + box_size, 223, 0, fb->width, 0, fb->height);
            fb->lineWithBox(Xtarget + box_size, Ytarget + box_size, Xtarget - box_size, Ytarget + box_size, 223, 0, fb->width, 0, fb->height);
            fb->lineWithBox(Xtarget - box_size, Ytarget + box_size, Xtarget - box_size, Ytarget - box_size, 223, 0, fb->width, 0, fb->height);
        }
    }

    if (debug_print) {
        Point2D debugPos = {5, 10};
        std::string debugTxt5 = "Angular speed: " + std::to_string(this->player_plane->angular_velocity.x) + ", " + std::to_string(this->player_plane->angular_velocity.y) + ", " + std::to_string(this->player_plane->angular_velocity.z);
        debug_framebuffer->printText(this->big_font, &debugPos, (char *)debugTxt5.c_str(), 0, 0, (uint32_t)debugTxt5.length(), 2, 2);

        std::string debugTxt = "TOF: " + std::to_string(timeOfFlight) + "s nbsteps: " + std::to_string(nbsteps);
        debugPos.x = 5;
        debugPos.y += 8;
        debug_framebuffer->printText(this->big_font, &debugPos, (char *)debugTxt.c_str(), 0, 0, (uint32_t)debugTxt.length(), 2, 2);
        std::string debugTxt2 = "Dist: " + std::to_string((int)target_distance) + "m predict: " + std::to_string((int)newDist) + "m";
        debugPos.x = 5;
        debugPos.y += 8;
        debug_framebuffer->printText(this->big_font, &debugPos, (char *)debugTxt2.c_str(), 0, 0, (uint32_t)debugTxt2.length(), 2, 2);
        std::string debugTxt3 = "Debug TOF: " + std::to_string(debutTimeOfFlight) + "s";
        debugPos.x = 5;
        debugPos.y += 8;
        debug_framebuffer->printText(this->big_font, &debugPos, (char *)debugTxt3.c_str(), 0, 0, (uint32_t)debugTxt3.length(), 2, 2);
        float predictedImpactDistance = (this->targetImpactPointWorld - avion_pos).Length();
        std::string debugTxt4 = "Predicted Impact Dist: " + std::to_string((int)predictedImpactDistance) + "m bullet speed: " + std::to_string((int)projectile_speed_world) + " m/s after shot: " + std::to_string((int)bulletSpeedAfterShot) + " m/s";
        debugPos.x = 5;
        debugPos.y += 8;
        debug_framebuffer->printText(this->big_font, &debugPos, (char *)debugTxt4.c_str(), 0, 0, (uint32_t)debugTxt4.length(), 2, 2);
    }
    delete weap;
}
void SCCockpit::RenderStraffingReticle(FrameBuffer *fb, CHUD_SHAPE *reticleShape, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter) {
    int hud_width = hudBottomRight.x - hudTopLeft.x;
    int hud_height = hudBottomRight.y - hudTopLeft.y;
    int hud_center_x = hud_width / 2;
    int hud_center_y = hud_height / 2;
    if (reticleShape == nullptr) {
        reticleShape = this->hud->small_hud->LCOS;
    }
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (!this->player_plane) {
        return;
    }
    float target_distance = 0.0f;
    const float dt = (this->player_plane->tps > 0) ? (1.0f / (float)this->player_plane->tps) : (1.0f / 60.0f);

    Vector3D avion_pos {
        this->player_plane->x,
        this->player_plane->y,
        this->player_plane->z
    };
    Vector3D planeVelWorld = {
        (this->player_plane->x - this->player_plane->last_px),
        (this->player_plane->y - this->player_plane->last_py),
        (this->player_plane->z - this->player_plane->last_pz)
    };
    
    float timeOfFlight = 0.5f;
    float debutTimeOfFlight = timeOfFlight;
    
    float projectile_speed = 1000.0f / this->player_plane->tps;
    
    Vector3D initial_trust{0, 0, 0};

    initial_trust = this->player_plane->getWeaponIntialVector(projectile_speed);
    float projectile_speed_world = initial_trust.Length();
    // === LEAD ANGLE: vitesse de la cible ===
    Vector3D targetVelocityWorld = {0.0f, 0.0f, 0.0f};
    Vector3D predictedTargetPos = (this->current_target != nullptr) 
        ? this->current_target->position 
        : avion_pos;
    float newDist = 0.0f;

    int nbsteps = timeOfFlight * this->player_plane->tps;
    GunSimulatedObject *weap = new GunSimulatedObject();
    
    Vector3D omegaLocal = this->player_plane->angular_velocity;
    
    Matrix &ptw = this->player_plane->ptw;
    Vector3D omegaStep = {
        ptw.v[0][0] * omegaLocal.x + ptw.v[1][0] * omegaLocal.y + ptw.v[2][0] * omegaLocal.z,
        ptw.v[0][1] * omegaLocal.x + ptw.v[1][1] * omegaLocal.y + ptw.v[2][1] * omegaLocal.z,
        ptw.v[0][2] * omegaLocal.x + ptw.v[1][2] * omegaLocal.y + ptw.v[2][2] * omegaLocal.z
    };

    initial_trust = this->player_plane->getWeaponIntialVector(projectile_speed);
    Vector3D planeDispAccum{0, 0, 0};
    
    weap->obj = this->player_plane->weaps_load[0]->objct;
    weap->x = this->player_plane->x + planeDispAccum.x;
    weap->y = this->player_plane->y + planeDispAccum.y;
    weap->z = this->player_plane->z + planeDispAccum.z;
    weap->vx = initial_trust.x;
    weap->vy = initial_trust.y;
    weap->vz = initial_trust.z;
    weap->weight = this->player_plane->weaps_load[0]->objct->weight_in_kg * 2.205f;
    weap->azimuthf = this->player_plane->azimuthf;
    weap->elevationf = this->player_plane->elevationf;
    weap->target = nullptr;

    Vector3D impact{0, 0, 0};
    Vector3D velo{0, 0, 0};
    bool firstIter = true;
    float bulletSpeedAfterShot = 0.0f;
    for (int i = 0; i < nbsteps; i++) {
        // 1. Accumuler le déplacement avion
        planeDispAccum = planeDispAccum + planeVelWorld;
        planeVelWorld = planeVelWorld.rotateByAxis(omegaStep);

        std::tie(impact, velo) = weap->ComputeTrajectory(this->player_plane->tps);
        
        Vector3D finalPos = {
            impact.x + planeDispAccum.x,
            impact.y + planeDispAccum.y,
            impact.z + planeDispAccum.z
        };

        weap->x = impact.x;
        weap->y = impact.y;
        weap->z = impact.z;
        
        weap->vx = velo.x;
        weap->vy = velo.y;
        weap->vz = velo.z;

        if (firstIter) {
            bulletSpeedAfterShot = velo.Length();
            firstIter = false;
        }
    }

    const Vector3D impactWorld = {
        impact.x + planeDispAccum.x,
        impact.y + planeDispAccum.y,
        impact.z + planeDispAccum.z
    };
    this->targetImpactPointWorld = impactWorld;

    int Xdraw = 0, Ydraw = 0;
    bool onHud = false;
    if (project_to_screen(impactWorld, Xdraw, Ydraw)) {
        if (this->is_3d_cockpit == false) {
            Xdraw = Xdraw - hudCenter.x + hud_center_x + hudTopLeft.x;
            Ydraw = Ydraw - hudCenter.y + hud_center_y + hudTopLeft.y;
        }
        Point2D gun_piper= {Xdraw, Ydraw};
        RLEShape *s = reticleShape->SHAP;
        int shapeWidth = s->GetWidth();
        int shapeHeight = s->GetHeight();
        gun_piper.x -= (1+shapeWidth / 2);
        gun_piper.y -= (-1+shapeHeight / 2);
        s->SetPosition(&gun_piper);
        fb->drawShapeWithBox(s, hudTopLeft.x, hudBottomRight.x, hudTopLeft.y, hudBottomRight.y);
    }

    delete weap;
}

void SCCockpit::RenderBombSight(FrameBuffer *fb, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter) {
    int hud_width = hudBottomRight.x - hudTopLeft.x;
    int hud_height = hudBottomRight.y - hudTopLeft.y;
    int hud_center_x = hud_width / 2;
    int hud_center_y = hud_height / 2;
    // Par défaut, on dessine dans la texture HUD si elle existe
    if (!fb) {
        fb = (this->hud_framebuffer != nullptr) ? this->hud_framebuffer : VGA.getFrameBuffer();
    }
    if (!this->player_plane) {
        return;
    }

    // Zone de visée dans le HUD
    const Point2D center{fb->width / 2, fb->height / 2};
    const int width = 50;
    const int height = 60;

    const int bx1 = center.x - width / 2;
    const int bx2 = center.x + width / 2;
    const int by1 = center.y - height / 2;
    const int by2 = center.y + height / 2;

    GunSimulatedObject *weap = new GunSimulatedObject();

    
    float thrustMagnitude = 1.0f; // comme avant

    Vector3D initial_trust{0, 0, 0};
    initial_trust = this->player_plane->getWeaponIntialVector(thrustMagnitude);
    weap->obj = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct;

    weap->x = this->player_plane->x;
    weap->y = this->player_plane->y;
    weap->z = this->player_plane->z;
    weap->vx = initial_trust.x;
    weap->vy = initial_trust.y;
    weap->vz = initial_trust.z;

    weap->weight = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->weight_in_kg * 2.205f;
    weap->azimuthf = this->player_plane->yaw;
    weap->elevationf = this->player_plane->pitch;
    weap->target = nullptr;
    weap->mission = this->current_mission;

    Vector3D impact{0, 0, 0};
    Vector3D velo{0, 0, 0};
    std::tie(impact, velo) = weap->ComputeTrajectoryUntilGround(this->player_plane->tps);

    Vector3D impactWorld = {impact.x, impact.y, impact.z};
    this->targetImpactPointWorld = impactWorld;
    const Matrix planeFromWorld = this->player_plane->ptw.invertRigidBodyMatrixLocal();

    const Vector2D bombAngularOffset = {0.0f, 0.0f};

    int Xhud = 0, Yhud = 0;
    if (project_to_screen(impactWorld, Xhud, Yhud)) {
        if (this->is_3d_cockpit == false) {
            Xhud = Xhud - hudCenter.x + hud_center_x + hudTopLeft.x;
            Yhud = Yhud - hudCenter.y + hud_center_y + hudTopLeft.y;
        }
        fb->plot_pixel(center.x, center.y, 223);
        fb->lineWithBox(center.x, center.y, Xhud, Yhud, 223, bx1, bx2, by1, by2);
        fb->plot_pixel(Xhud, Yhud, 223);
        fb->circle_slow(Xhud, Yhud, 6, 90);
    }

    delete weap;
}
void SCCockpit::RenderMFDSWeapon(Point2D pmfd_right, FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (!this->cockpit->instrumentShapes.empty()) {
        // WC3 ('w' key): id 14's frame 0 is this page's real background
        // diagram (user-corrected, 2026-07 session: "0x0D is wrong, 0x0E
        // is the background diagram" — id 13/kPilotHandAnimation2's own
        // frame 0, drawn here in an earlier pass, was a wrong guess;
        // reverted), drawn once, never cycled; frames 1-37 are the actual
        // per-mount status icons overlaid on it, one per armed hardpoint
        // (real frame-to-weapon-type mapping unconfirmed, so hardpoints
        // just get sequential icons rather than a specific per-weapon-type
        // one). id 20 (kBarMeterFillB) is the "Weapon's gauge" — reuses
        // the gun energy pool (SCPlane::gun_energy_current/capacity) for
        // gun-category weapons since that's the one real per-weapon
        // "readiness" fraction this codebase already tracks;
        // missiles/torpedoes have no such pool so show a full gauge.
        //
        // clearBackground=false (user-requested, 2026-07 session): with no
        // solid-color backdrop, RenderMFDS's own anti-residue black clear
        // just left every icon/gauge on a flat black panel — see its own
        // comment for the page-switch-residue tradeoff this accepts.
        this->RenderMFDS(pmfd_right, fb, false);
        Point2D mfdSize = this->GetWC3MfdSize();
        int mfdW = mfdSize.x;
        int mfdH = mfdSize.y;
        auto mountIt = this->cockpit->instrumentShapes.find(14);
        // Real screen position of the ship-diagram canvas's own top-left
        // corner (frame 0, drawn below) — defaults to the panel's own
        // top-left if id 14 isn't available, so the WEAP-anchor fallback
        // path still has a sane base.
        Point2D diagramOrigin = pmfd_right;
        if (mountIt != this->cockpit->instrumentShapes.end() && mountIt->second != nullptr &&
            mountIt->second->GetNumImages() > 0) {
            RLEShape *bgShape = mountIt->second->GetShape(0);
            if (bgShape != nullptr) {
                Point2D center = {pmfd_right.x + mfdW / 2, pmfd_right.y + mfdH / 2};
                Point2D offset = GetOriginalCanvasOffset(bgShape);
                Point2D bgPos = {center.x - bgShape->GetWidth() / 2 + offset.x,
                                  center.y - bgShape->GetHeight() / 2 + offset.y};
                diagramOrigin = bgPos;
                bgShape->SetPosition(&bgPos);
                fb->drawShape(bgShape);
            }
        }
        if (mountIt != this->cockpit->instrumentShapes.end() && mountIt->second != nullptr &&
            mountIt->second->GetNumImages() > 1 && this->player_plane != nullptr) {
            size_t numMountFrames = mountIt->second->GetNumImages() - 1;  // frame 0 excluded (background diagram, drawn above)
            // Real per-hardpoint icon anchors from the cockpit file's own
            // WEAP chunk (group A = gun-category hardpoints, group B =
            // missile/secondary — inferred from MEDPIT's known loadout
            // matching countA/countB, not independently confirmed).
            // Anchored to diagramOrigin (the ship diagram's own canvas
            // top-left), not pmfd_right (the raw panel top-left) — user-
            // reported, 2026-07 session: icons were in the wrong position
            // relative to the (now correctly shown) ship backdrop. The
            // diagram is centered within the panel with real margins on
            // every side (its own embedded x_blit/y_min plus the panel-
            // vs-canvas size difference), so anchors authored relative to
            // the diagram itself land wrong if measured from the panel
            // edge instead. Falls back to the old generic 6-column grid
            // (still panel-relative — no diagram to align to in that
            // case) for any hardpoint past what the file records anchors
            // for, or entirely if the cockpit's WEAP chunk didn't parse.
            const RSCockpit::WC3WeaponLayout &weap = g_ifVGA ? this->cockpit->vgaFrontWeap : this->cockpit->svgaFrontWeap;
            // Which gun-type is currently selected (mirrors SCStrike.cpp's
            // collectDistinctGunTypes + FIRE_PRIMARY's own selection-
            // resolution logic — duplicated here rather than shared since
            // that's a file-static helper in a different translation
            // unit). selected_gun_group == gunTypes.size() is the "fire
            // every gun together" state (G's final cycle step), matching
            // every hardpoint of any type.
            std::vector<int> gunTypesSeen;
            // Which missile *hardpoint* (bank) is currently selected —
            // mirrors SCStrike.cpp's collectMissileHardpointIndices.
            // User-corrected, 2026-07 session: "only one missile bank is
            // selected at a time, regardless if it has the same type of
            // missiles as another bank" — banks are identified by
            // hardpoint index, not weapon type, so two same-type banks no
            // longer both highlight together. selected_missile_group < 0
            // is B's "select all hardpoints" state, matching every
            // missile-category hardpoint regardless of bank.
            std::vector<int> missileBankIndices;
            for (size_t hi = 0; hi < this->player_plane->weaps_load.size(); hi++) {
                auto *hpt = this->player_plane->weaps_load[hi];
                if (hpt == nullptr || hpt->objct == nullptr || hpt->objct->wdat == nullptr) continue;
                if (hpt->objct->wdat->weapon_category == 0) {
                    int wid = hpt->objct->wdat->weapon_id;
                    if (std::find(gunTypesSeen.begin(), gunTypesSeen.end(), wid) == gunTypesSeen.end())
                        gunTypesSeen.push_back(wid);
                } else {
                    missileBankIndices.push_back((int)hi);
                }
            }
            bool fireAllGuns = !gunTypesSeen.empty() && this->player_plane->selected_gun_group >= (int)gunTypesSeen.size();
            int selectedGunId = (!fireAllGuns && !gunTypesSeen.empty())
                                     ? gunTypesSeen[this->player_plane->selected_gun_group % gunTypesSeen.size()] : -1;
            bool fireAllMissiles = this->player_plane->selected_missile_group < 0;
            int selectedMissileBankIndex = (!fireAllMissiles && !missileBankIndices.empty())
                                                ? missileBankIndices[this->player_plane->selected_missile_group % missileBankIndices.size()] : -1;
            // Real per-weapon-type (deselected, selected) icon frame pairs
            // (user-confirmed, 2026-07 session, live-read off id 14 itself):
            // each weapon type gets its own two adjacent frames rather than
            // a separately-drawn highlight box — the "selected" look is
            // baked into the sprite itself. T-bomb's pair (37/38) only
            // exists in EXCPITT's own copy of id 14 — the >=GetNumImages()
            // bounds check right after this table's lookup already falls
            // back safely to the old sequential frame for any cockpit
            // file whose id 14 is smaller, no special-casing needed here.
            // Every WC3 gun/missile/torpedo/mine type now has a confirmed
            // pair — kGenericMissileFrame/the sequential-by-draw-order
            // fallback below only matter for an unresolved/unknown
            // weapon_id now (e.g. a non-WC3 SC1 hardpoint).
            static const std::unordered_map<int, std::pair<size_t, size_t>> kWeaponIconFrames = {
                {weapon_ids::ID_NEUTGUN, {6, 7}},
                {weapon_ids::ID_IONGUN, {8, 9}},
                {weapon_ids::ID_PHOTGUN, {2, 3}},
                {weapon_ids::ID_RLASER_WC3, {4, 5}},
                {weapon_ids::ID_MASSGUN, {10, 11}},
                {weapon_ids::ID_REAPGUN, {12, 13}},
                {weapon_ids::ID_TACHGUN, {20, 21}},
                {weapon_ids::ID_PLASGUN, {14, 15}},
                {weapon_ids::ID_MESOGUN, {18, 19}},
                {weapon_ids::ID_IRMISS, {22, 23}},
                {weapon_ids::ID_HSMISS, {24, 25}},
                {weapon_ids::ID_FFMISS, {26, 27}},
                {weapon_ids::ID_TORKMISS, {28, 29}},
                {weapon_ids::ID_DFMISS, {30, 31}},
                {weapon_ids::ID_LEECHMISS, {32, 33}},
                {weapon_ids::ID_MINEMISS, {35, 36}},
                {weapon_ids::ID_TEMBMISS, {37, 38}},
            };
            constexpr size_t kGenericMissileFrame = 1;
            int drawnA = 0, drawnB = 0, drawnFallback = 0, drawn = 0;
            for (size_t i = 0; i < this->player_plane->weaps_load.size(); i++) {
                if (this->player_plane->weaps_load[i] == nullptr) {
                    continue;
                }
                auto *wdat = this->player_plane->weaps_load[i]->objct->wdat;
                bool isGun = (wdat != nullptr && wdat->weapon_category == 0);
                Point2D iconPos;
                bool havePos = false;
                if (weap.valid) {
                    if (isGun && drawnA < (int)weap.groupA.size()) {
                        iconPos = {diagramOrigin.x + weap.groupA[drawnA].x, diagramOrigin.y + weap.groupA[drawnA].y};
                        drawnA++;
                        havePos = true;
                    } else if (!isGun && drawnB < (int)weap.groupB.size()) {
                        iconPos = {diagramOrigin.x + weap.groupB[drawnB].x, diagramOrigin.y + weap.groupB[drawnB].y};
                        drawnB++;
                        havePos = true;
                    }
                }
                if (!havePos) {
                    iconPos = {pmfd_right.x + 8 + (drawnFallback % 6) * 16,
                               pmfd_right.y + 8 + (drawnFallback / 6) * 16};
                    drawnFallback++;
                }
                bool selected = wdat != nullptr &&
                    ((isGun && (fireAllGuns || wdat->weapon_id == selectedGunId)) ||
                     (!isGun && (fireAllMissiles || (int)i == selectedMissileBankIndex)));
                size_t frame = 1 + (drawn % numMountFrames);
                if (wdat != nullptr) {
                    auto pairIt = kWeaponIconFrames.find(wdat->weapon_id);
                    if (pairIt != kWeaponIconFrames.end()) {
                        frame = selected ? pairIt->second.second : pairIt->second.first;
                    } else if (!isGun) {
                        frame = kGenericMissileFrame;
                    }
                }
                if (frame >= mountIt->second->GetNumImages()) {
                    frame = 1 + (drawn % numMountFrames);
                }
                Point2D anchorPos = iconPos;
                RLEShape *icon = mountIt->second->GetShape(frame);
                if (icon != nullptr) {
                    // Same fix as the shield/hull gauges (2026-07 session,
                    // user-prompted): each icon frame's own embedded
                    // x_blit/y_min is a real registration-point offset
                    // (cursor-hotspot-style, small and usually negative —
                    // MEDPIT's real id-14 frames range roughly -2..-31),
                    // not something to discard in favor of drawing every
                    // icon's raw top-left straight at the WEAP anchor.
                    // One real frame (34) decodes an obviously-corrupt
                    // offset (x_scan_max in the tens of thousands) — capped
                    // so a single bad frame can't fling an icon off-panel.
                    Point2D offset = GetOriginalCanvasOffset(icon);
                    if (offset.x > -100 && offset.x < 100 && offset.y > -100 && offset.y < 100) {
                        iconPos.x += offset.x;
                        iconPos.y += offset.y;
                    }
                    icon->SetPosition(&iconPos);
                    fb->drawShape(icon);
                }
                // User-requested (2026-07 session): "only one missile bank
                // can be chosen at a time except when B is pressed" — a
                // bank is one specific hardpoint (selectedMissileBankIndex
                // above), not every hardpoint sharing its missile type
                // (guns are the type-grouped case; missiles are per-bank —
                // see selected_missile_group's own comment) — "when
                // selecting a missile bank draw frame 1 on top of the
                // currently selected missile bank": an extra generic
                // selection marker overlaid on the type-specific icon, not
                // a replacement for it.
                if (!isGun && selected) {
                    RLEShape *marker = mountIt->second->GetShape(kGenericMissileFrame);
                    if (marker != nullptr) {
                        Point2D markerPos = anchorPos;
                        Point2D markerOffset = GetOriginalCanvasOffset(marker);
                        if (markerOffset.x > -100 && markerOffset.x < 100 && markerOffset.y > -100 && markerOffset.y < 100) {
                            markerPos.x += markerOffset.x;
                            markerPos.y += markerOffset.y;
                        }
                        marker->SetPosition(&markerPos);
                        fb->drawShape(marker);
                    }
                }
                drawn++;
            }
            // Real text labels (user-described, 2026-07 session): COCK>
            // SVGA|VGA>HUD>TEXT mode==3 records are this page's own status
            // text — id 17 "D%d" (decoy count), id 10 "Full Guns" (or the
            // specific selected gun's name when not in the "fire all guns"
            // state), id 3 "No Missile" (or the selected bank's weapon
            // name + remaining count). Byte-verified against MEDPIT.IFF —
            // same 3 records, same ids/positions, in both its FRNT and HUD
            // copies of TEXT.
            auto &hudText = g_ifVGA ? this->cockpit->vgaHudText : this->cockpit->svgaHudText;
            WC3Font *statusFont = WC3Globals::getInstance().getFont("SLRG");
            if (statusFont != nullptr && statusFont->isLoaded()) {
                uint8_t green = ClosestPaletteIndex(this->palette, 0, 200, 50);
                for (auto &te : hudText) {
                    if (te.mode != 3) continue;
                    std::string text;
                    if (te.id == 17) {
                        uint32_t decoys = (this->player_plane->object != nullptr && this->player_plane->object->entity != nullptr)
                                               ? this->player_plane->object->entity->decoy_count : 0;
                        char buf[32];
                        snprintf(buf, sizeof(buf), te.text, decoys);
                        text = buf;
                    } else if (te.id == 10) {
                        if (fireAllGuns || gunTypesSeen.empty()) {
                            text = te.text;
                        } else {
                            auto nameIt = weapon_names.find(static_cast<weapon_ids>(selectedGunId));
                            text = (nameIt != weapon_names.end()) ? nameIt->second : te.text;
                        }
                    } else if (te.id == 3) {
                        if (fireAllMissiles && !missileBankIndices.empty()) {
                            // No real file text confirmed for this state
                            // (only "No Missile" and a single-bank name+
                            // count were found) — best-effort label.
                            text = "ALL MISSILES";
                        } else if (selectedMissileBankIndex >= 0 &&
                                   (size_t)selectedMissileBankIndex < this->player_plane->weaps_load.size() &&
                                   this->player_plane->weaps_load[selectedMissileBankIndex] != nullptr &&
                                   this->player_plane->weaps_load[selectedMissileBankIndex]->objct != nullptr &&
                                   this->player_plane->weaps_load[selectedMissileBankIndex]->objct->wdat != nullptr) {
                            auto *bank = this->player_plane->weaps_load[selectedMissileBankIndex];
                            auto nameIt = weapon_names.find(static_cast<weapon_ids>(bank->objct->wdat->weapon_id));
                            std::string name = (nameIt != weapon_names.end()) ? nameIt->second : "MISSILE";
                            text = name + " " + std::to_string(bank->nb_weap);
                        } else {
                            text = te.text;
                        }
                    } else {
                        continue;
                    }
                    Point2D textPos = {pmfd_right.x + te.x, pmfd_right.y + te.y};
                    statusFont->drawTextColored(fb, text, textPos.x, textPos.y, green);
                }
            }
        }
        // The id-20 (kBarMeterFillB) gun-energy gauge that used to draw
        // here has been removed (user-requested, 2026-07 session: "get
        // rid of the weapons gauge from the weapons MFD").
        return;
    }
    if (this->cockpit->MONI.SHAP.data == nullptr) {
        return;
    }
    std::string txt;
    this->RenderMFDS(pmfd_right, fb);
    Point2D pmfd_right_center = {pmfd_right.x + this->cockpit->MONI.SHAP.GetWidth() / 2,
                                 pmfd_right.y + this->cockpit->MONI.SHAP.GetHeight() / 2};
    Point2D pmfd_right_weapon = {pmfd_right_center.x - this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->GetWidth() / 2,
                                 pmfd_right_center.y - 10 -
                                     this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->GetHeight() / 2};
    this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->SetPosition(&pmfd_right_weapon);
    fb->drawShape(this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0));
    std::unordered_map<int, int> weapons_shape = {
        {1, 1},
        {2, 1},
        {3, 5},
        {4, 7},
        {5, 9},
        {6, 11},
        {7, 13},    
        {8, 15},
        {9, 3}
    };
    if (this->player_plane->object->entity->hpts.size() > 1) {
        for (int indice = 1; indice < this->player_plane->object->entity->hpts.size() / 2 + 1; indice++) {
            if (this->player_plane->weaps_load[indice] == nullptr) {
                continue;
            }
            int weapon_id = this->player_plane->weaps_load[indice]->objct->wdat->weapon_id;
            int nb_weap = this->player_plane->weaps_load[indice]->nb_weap;
            int i = 4 - indice;
            int selected = indice == this->player_plane->selected_weapon;
            RLEShape *shape = this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(weapons_shape[weapon_id] + selected);
            int32_t s_width = shape->GetWidth();
            int32_t s_height = shape->GetHeight();

            if (nb_weap > 0) {
                Point2D pmfd_right_weapon_hp = {pmfd_right_center.x - 15 - s_width / 2 - i * 9,
                                                pmfd_right_center.y - 18 - s_height / 2 + i * 9};
                shape->SetPosition(&pmfd_right_weapon_hp);
                fb->drawShape(shape);
                txt = std::to_string(nb_weap);
                Point2D pmfd_right_weapon_hp_text = {pmfd_right_weapon_hp.x + s_width / 2 + 1,
                                                     pmfd_right_weapon_hp.y + s_height + 6};
                fb->printText(this->big_font, &pmfd_right_weapon_hp_text, (char *)txt.c_str(), 0, 0,
                              (uint32_t)txt.length(), 2, 2);
            }
            if (this->player_plane->weaps_load[this->player_plane->object->entity->hpts.size() - indice] == nullptr) {
                continue;
            }
            nb_weap = this->player_plane->weaps_load[this->player_plane->object->entity->hpts.size() - indice]->nb_weap;
            selected = 9 - indice == this->player_plane->selected_weapon;
            shape = this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(weapons_shape[weapon_id] + selected);
            if (nb_weap > 0) {
                Point2D pmfd_right_weapon_hp_left = {pmfd_right_center.x + 13 - s_width / 2 + i * 9,
                                                     pmfd_right_center.y - 18 - s_height / 2 + i * 9};
                shape->SetPosition(&pmfd_right_weapon_hp_left);
                fb->drawShape(shape);
                txt = std::to_string(nb_weap);
                Point2D pmfd_right_weapon_hp_text_left = {pmfd_right_weapon_hp_left.x + s_width / 2 - 1,
                                                          pmfd_right_weapon_hp_left.y + s_height + 6};
                fb->printText(this->big_font, &pmfd_right_weapon_hp_text_left, (char *)txt.c_str(), 0, 0,
                              (uint32_t)txt.length(), 2, 2);
            }
        }
    }

    int sel_weapon_id = 0;
    int sel_nb_weap = 0;
    if (this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
        sel_weapon_id = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->weapon_id;
        for (auto weap : this->player_plane->weaps_load) {
            if (weap != nullptr && weap->objct->wdat->weapon_id == sel_weapon_id) {
                sel_nb_weap += weap->nb_weap;
            }
        }
    }

    Point2D pmfd_right_weapon_gun{pmfd_right_weapon.x - 8 +
                                      this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->GetWidth() / 2,
                                  pmfd_right_weapon.y + 6};
    txt = std::to_string(sel_nb_weap);
    fb->printText(this->big_font, &pmfd_right_weapon_gun, (char *)txt.c_str(), 0, 0, (uint32_t)txt.length(), 2, 2);

    Point2D pmfd_right_weapon_radar{pmfd_right_weapon.x, pmfd_right_weapon.y + 5};
    fb->printText(this->big_font, &pmfd_right_weapon_radar, const_cast<char *>("NORM"), 0, 0, 4, 2, 2);

    Point2D pmfd_right_weapon_selected{pmfd_right_weapon.x + 12 +
                                           this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->GetWidth() / 2,
                                       pmfd_right_weapon.y + 5};
    fb->printText(this->big_font, &pmfd_right_weapon_selected, (char *)weapon_names[static_cast<weapon_ids>(sel_weapon_id)].c_str(), 0, 0,
                  (uint32_t)weapon_names[static_cast<weapon_ids>(sel_weapon_id)].length(), 2, 2);

    Point2D pmfd_right_weapon_chaff{pmfd_right_weapon.x - 7 +
                                        this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->GetWidth() / 2,
                                    pmfd_right_weapon.y + 4 * 9};
    fb->printText(this->big_font, &pmfd_right_weapon_chaff, const_cast<char *>("C:30"), 0, 0, 4, 2, 2);

    Point2D pmfd_right_weapon_flare{pmfd_right_weapon.x - 7 +
                                        this->cockpit->MONI.MFDS.WEAP.ARTS.GetShape(0)->GetWidth() / 2,
                                    pmfd_right_weapon.y + 5 * 9};
    fb->printText(this->big_font, &pmfd_right_weapon_flare, const_cast<char *>("F:30"), 0, 0, 4, 2, 2);
}

// WC3's left-MFD "Target Radar": COCK>SHAP id 15 background, contacts
// plotted by ANGLE OFF BORESIGHT rather than a top-down world-space map —
// dead ahead lands at the panel center, directly behind lands on the outer
// ring, with bearing (up/down/left/right around the player) mapped to
// angular position around the circle. Confirmed by the user. One unified
// display, no AARD/AGRD/ASST-style mode split (unlike the SC-format
// implementations below, which this replaces for WC3 — those read
// this->cockpit->MONI.MFDS.*.ARTS, an SC-only field WC3 cockpits never
// populate, so they never worked here regardless).
// pmfd is unused for WC3 cockpits — position/size now always come from
// GetCompassAreaRect (see its own comment), not the caller's panel origin.
// User-confirmed (2026-07 session): WC3CockpitShapeId::kCompassDialLarge
// (id 15), previously drawn here as a background dial behind the dots, is
// not wanted at all — removed, along with the RenderMFDS() panel clear
// (that cleared a GetWC3MfdSize()-sized rectangle, sized for the old left-
// VDU-panel placement; wrong for the small compass-area circle this now
// draws into, and there's no dial background to clear underneath anymore
// anyway — the dots plot directly over the cockpit's own printed art).
void SCCockpit::RenderMFDSTargetRadarWC3(Point2D pmfd, float range, FrameBuffer *fb) {
    Point2D topLeft{}, bottomRight{};
    bool haveRect = this->GetCompassAreaRect(!g_ifVGA, topLeft, bottomRight);
    if (!haveRect) {
        // No confirmed or fallback rect at all — nothing sane to draw into.
        return;
    }
    int mfdW = bottomRight.x - topLeft.x;
    int mfdH = bottomRight.y - topLeft.y;
    Point2D center = {topLeft.x + mfdW / 2, topLeft.y + mfdH / 2};
    // Half the smaller dimension, so the dot ring stays inside the
    // confirmed rect on both axes even when it isn't perfectly square
    // (e.g. BOMPIT's 82x81).
    const int ringRadius = (std::min)(mfdW, mfdH) / 2;
    if (this->player_plane == nullptr || this->current_mission == nullptr) {
        return;
    }
    Matrix planeFromWorld = this->player_plane->ptw.invertRigidBodyMatrixLocal();
    // User-confirmed exact radar colors (2026-07 session) — supersede the
    // earlier approximate values (plain red/blue/orange/yellow).
    uint8_t colorRed = ClosestPaletteIndex(this->palette, 239, 56, 24);      // enemy fighters
    uint8_t colorBlue = ClosestPaletteIndex(this->palette, 24, 73, 235);     // friendly fighters
    uint8_t colorOrange = ClosestPaletteIndex(this->palette, 239, 130, 48);  // enemy capital ships
    uint8_t colorWhite = ClosestPaletteIndex(this->palette, 255, 255, 255);
    // Friendly capital ships/stations (IsCapitalShipName) and supply
    // structures (jump buoys, asteroid depots — IsFriendlySupplyStructureName)
    // get a distinct color from ordinary friendly fighters/wingmen.
    uint8_t colorLightBlue = ClosestPaletteIndex(this->palette, 93, 134, 239);
    uint8_t colorYellow = ClosestPaletteIndex(this->palette, 239, 203, 69);  // missiles/mines

    auto plotContact = [&](Vector3D worldPos, uint8_t color, bool cross) {
        Vector3D local = worldPos.transformPoint(planeFromWorld);
        float len = sqrtf(local.x * local.x + local.y * local.y + local.z * local.z);
        if (len < 1.0f || len > range) {
            return;
        }
        // 0 = dead ahead (-Z, this engine's local-forward convention — see
        // project_to_screen's projectRealToHUD comment), pi = directly behind.
        float angleFromForward = acosf((std::max)(-1.0f, (std::min)(1.0f, -local.z / len)));
        float radiusFrac = angleFromForward / (float)M_PI;
        // Bearing around the circle: local Y (up) = 12 o'clock, local X
        // (right) = 3 o'clock. Not independently confirmed against a real
        // reference image.
        float bearingAngle = atan2f(local.x, local.y);
        Point2D dot = {
            center.x + (int)(radiusFrac * ringRadius * sinf(bearingAngle)),
            center.y - (int)(radiusFrac * ringRadius * cosf(bearingAngle))
        };
        if (dot.x < topLeft.x || dot.x >= bottomRight.x || dot.y < topLeft.y || dot.y >= bottomRight.y) {
            return;
        }
        // User-confirmed (2026-07 session) marker sizes: plain contacts are
        // a 2x2 pixel block; the selected-target cross is two overlapping
        // filled bars, 6x2 (horizontal) and 2x6 (vertical), both centered
        // on the plotted position.
        auto fillRect = [&](int x0, int y0, int w, int h) {
            for (int yy = 0; yy < h; yy++) {
                for (int xx = 0; xx < w; xx++) {
                    fb->plot_pixel(x0 + xx, y0 + yy, color);
                }
            }
        };
        if (cross) {
            fillRect(dot.x - 3, dot.y - 1, 6, 2);
            fillRect(dot.x - 1, dot.y - 3, 2, 6);
        } else {
            fillRect(dot.x, dot.y, 2, 2);
        }
    };

    // User-confirmed (2026-07 session): capital ships and fighters are
    // both plotted as plain dots — only the currently-selected target
    // (current_target_actor) is drawn as a cross, regardless of what kind
    // of contact it is. Previously every capital ship was unconditionally
    // a cross; that was wrong.
    for (auto actor : this->current_mission->enemies) {
        if (actor == nullptr || !actor->is_active || actor->is_destroyed || actor->object == nullptr) {
            continue;
        }
        // User-confirmed (2026-07 session): cloaked enemy contacts (e.g.
        // Strakha fighters, Skipper missiles — Kilrathi cloaking-capable
        // types) disappear from radar entirely. Reuses SCPlane::cloaked
        // (SCPlane.h), the same field WC3Strike::updateCloakEffect already
        // drives for the player's own Excalibur cloak — generic, not
        // hardcoded to a specific ship name, so it applies to any actor
        // whose plane gets cloaked, not just Strakha specifically. No
        // enemy AI currently ever sets this true (only the player-cloak
        // path does), so this check is a no-op until enemy cloak-toggling
        // behavior exists — see this session's own memory note for the
        // "Skipper missile" gap (not found anywhere in the codebase/data,
        // so its radar entry couldn't be wired up at all).
        if (actor->plane != nullptr && actor->plane->cloaked) {
            continue;
        }
        std::string name = actor->object->member_name;
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        bool capital = SCMissionActors::IsCapitalShipName(name);
        bool isSelectedTarget = (actor == this->current_target_actor);
        plotContact(actor->object->position, capital ? colorOrange : colorRed, isSelectedTarget);
    }
    for (auto actor : this->current_mission->friendlies) {
        if (actor == nullptr || actor == this->current_mission->player || !actor->is_active ||
            actor->is_destroyed || actor->object == nullptr) {
            continue;
        }
        std::string name = actor->object->member_name;
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        bool capital = SCMissionActors::IsCapitalShipName(name);
        bool lightBlue = capital || SCMissionActors::IsFriendlySupplyStructureName(name);
        bool isSelectedTarget = (actor == this->current_target_actor);
        plotContact(actor->object->position, lightBlue ? colorLightBlue : colorBlue, isSelectedTarget);
    }
    for (auto wp : this->current_mission->waypoints) {
        if (wp == nullptr || wp->spot == nullptr) {
            continue;
        }
        // Take-off/landing nav points don't appear on radar (user-
        // confirmed, 2026-07 session) — only real navigation destinations
        // do.
        if (SCMissionWaypoint::IsTakeoffOrLanding(wp)) {
            continue;
        }
        // Nav points: always a white "+", per the user's own confirmation
        // — unlike capital ships/fighters, not conditional on being the
        // selected target.
        plotContact(wp->spot->position, colorWhite, true);
    }

    // Missiles/torpedoes in flight — user-confirmed real behavior: yellow
    // dots. weapon_category 2=missile, 3=torpedo (SCenums.h's own
    // convention); category 0 (guns) is a hitscan/short-lived tracer, not
    // meaningfully trackable on a radar sweep, so it's excluded. No mine
    // weapon type exists anywhere in this codebase yet (SCenums.h's
    // weapon_ids has no mine entry, and nothing spawns/simulates one) —
    // that's a separate, larger gap than a radar-color tweak.
    auto plotWeapons = [&](const std::vector<SCSimulatedObject *> &weapons) {
        for (auto sim_obj : weapons) {
            if (sim_obj == nullptr || !sim_obj->alive || sim_obj->obj == nullptr || sim_obj->obj->wdat == nullptr) {
                continue;
            }
            uint8_t cat = sim_obj->obj->wdat->weapon_category;
            if (cat != 2 && cat != 3) continue;
            plotContact({sim_obj->x, sim_obj->y, sim_obj->z}, colorYellow, false);
        }
    };
    plotWeapons(this->player_plane->weaps_object);
    for (auto actor : this->current_mission->actors) {
        if (actor == nullptr || actor->plane == nullptr || actor == this->current_mission->player) {
            continue;
        }
        plotWeapons(actor->plane->weaps_object);
    }
}
// See header comment: dead-ahead boresight. Not the framebuffer's raw
// geometric center (user-confirmed live in-game: that showed up off to
// the right and low in the cockpit frame), and not project_to_screen's
// projection either (also tried, same session — its own asymmetric
// baked-in offset didn't land on the real boresight point). User-
// confirmed real calibration for MEDPIT instead: (320, 176) at SVGA's
// 640x480 reference canvas, scaled proportionally to whatever the
// current canvas actually is (VGA included). Other cockpit files may
// need their own calibration once tested — this is the only one
// confirmed so far.
Point2D SCCockpit::GetWC3BoresightScreenPos(FrameBuffer *fb) {
    const float kSvgaBoresightX = 320.0f;
    const float kSvgaBoresightY = 176.0f;
    return {(int16_t)(kSvgaBoresightX * (fb->width / 640.0f)), (int16_t)(kSvgaBoresightY * (fb->height / 480.0f))};
}
// See header comment. Same idea as RenderTargetingReticle's own inline
// calc (predictedTargetPos/timeOfFlight, further down in this file) but
// that one's gated behind this->hud (SC's HUD.IFF, never populated for
// WC3) and has a real bug worth not copying here: its own per-tick target
// velocity is computed into a locally-shadowed variable that immediately
// goes out of scope, so its lead solution silently always uses zero
// target velocity. This version divides the per-tick position delta by
// dt properly before using it.
Vector3D SCCockpit::ComputeTargetLeadPoint() {
    if (this->player_plane == nullptr) {
        return {0.0f, 0.0f, 0.0f};
    }
    Vector3D avionPos = {this->player_plane->x, this->player_plane->y, this->player_plane->z};
    if (this->current_target_actor == nullptr || this->current_target_actor->object == nullptr) {
        return avionPos;
    }
    Vector3D targetPos = this->current_target_actor->object->position;
    float dt = (this->player_plane->tps > 0) ? (1.0f / (float)this->player_plane->tps) : (1.0f / 60.0f);
    Vector3D targetVel = {0.0f, 0.0f, 0.0f};
    if (this->current_target_actor->plane != nullptr && dt > 0.0f) {
        targetVel = {
            (this->current_target_actor->plane->x - this->current_target_actor->plane->last_px) / dt,
            (this->current_target_actor->plane->y - this->current_target_actor->plane->last_py) / dt,
            (this->current_target_actor->plane->z - this->current_target_actor->plane->last_pz) / dt
        };
    }
    float projectileSpeed = this->player_plane->getWeaponIntialVector(1000.0f).Length();
    if (projectileSpeed <= 0.0f) {
        return targetPos;
    }
    // 3 iterations to converge time-of-flight against the moving
    // predicted point — same iteration count RenderTargetingReticle uses.
    Vector3D predicted = targetPos;
    for (int iter = 0; iter < 3; iter++) {
        Vector3D toPredicted = {predicted.x - avionPos.x, predicted.y - avionPos.y, predicted.z - avionPos.z};
        float tof = toPredicted.Length() / projectileSpeed;
        predicted = {
            targetPos.x + targetVel.x * tof,
            targetPos.y + targetVel.y * tof,
            targetPos.z + targetVel.z * tof
        };
    }
    return predicted;
}
// See header comment for the real per-frame meaning of each of id 6's 4
// frames (user-confirmed, 2026-07 session). project_to_screen's own math
// is calibrated to a fixed 320x200 reference canvas regardless of the
// cockpit's real VGA/SVGA resolution (see its own body — the +1)*160/
// *100 constants), so its raw output is rescaled here to the actual
// framebuffer size rather than assumed to already match it.
void SCCockpit::RenderWC3TargetingReticle(FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (this->cockpit == nullptr) {
        return;
    }
    // Bug fix (2026-07 session, user-reported: "target frame isn't drawing
    // around ships"): this always looked up the VGA-numbered shape id (6),
    // even in SVGA mode. kTargetingReticleSVGA (36) was declared in
    // RSCockpit.h but never actually used anywhere — a cockpit whose SHAP
    // PAK only carries the SVGA-numbered reticle art (no id-6 duplicate)
    // made this whole function a silent no-op under SVGA, the common case.
    // Falls back to the VGA id if the SVGA one isn't present, matching this
    // file's other g_ifVGA-conditional lookups' own fallback style.
    auto it = this->cockpit->instrumentShapes.find(
        g_ifVGA ? WC3CockpitShapeId::kTargetingReticle : WC3CockpitShapeId::kTargetingReticleSVGA);
    if (it == this->cockpit->instrumentShapes.end()) {
        it = this->cockpit->instrumentShapes.find(WC3CockpitShapeId::kTargetingReticle);
    }
    if (it == this->cockpit->instrumentShapes.end() || it->second == nullptr || it->second->GetNumImages() < 4) {
        return;
    }
    RSImageSet *reticleSet = it->second;
    auto drawFrame = [&](uint32_t frameIdx, Point2D at) {
        RLEShape *shape = reticleSet->GetShape(frameIdx);
        if (shape == nullptr) return;
        Point2D pos = at;
        pos.x -= shape->GetWidth() / 2;
        pos.y -= shape->GetHeight() / 2;
        shape->SetPosition(&pos);
        fb->drawShape(shape);
    };
    auto projectScaled = [&](Vector3D world, Point2D &outPos) -> bool {
        int x, y;
        if (!this->project_to_screen(world, x, y)) return false;
        outPos.x = (int)(x * (fb->width / 320.0f));
        outPos.y = (int)(y * (fb->height / 200.0f));
        return outPos.x >= 0 && outPos.x < fb->width && outPos.y >= 0 && outPos.y < fb->height;
    };

    // Frame 0 (on-screen lead marker) / frame 2 (off-screen edge tracker):
    // mutually exclusive, only while a target is locked.
    if (this->current_target_actor != nullptr && this->player_plane != nullptr) {
        Vector3D leadPoint = this->ComputeTargetLeadPoint();
        Point2D leadPos;
        if (projectScaled(leadPoint, leadPos)) {
            drawFrame(0, leadPos);
        } else {
            // Off-screen: clamp to the screen edge along the real 3D
            // bearing to the lead point, boresight-relative — same
            // local-space transform + atan2 bearing RenderMFDSTargetRadarWC3
            // uses for its own off-boresight contacts, but mapped onto a
            // rectangular screen edge instead of a circular MFD ring.
            Matrix planeFromWorld = this->player_plane->ptw.invertRigidBodyMatrixLocal();
            Vector3D local = leadPoint.transformPoint(planeFromWorld);
            float len = sqrtf(local.x * local.x + local.y * local.y + local.z * local.z);
            if (len > 1.0f) {
                float bearingAngle = atan2f(local.x, local.y);
                float dirX = sinf(bearingAngle);
                float dirY = -cosf(bearingAngle);
                int marginX = fb->width / 8;
                int marginY = fb->height / 8;
                int cx = fb->width / 2;
                int cy = fb->height / 2;
                float halfW = (float)(fb->width / 2 - marginX);
                float halfH = (float)(fb->height / 2 - marginY);
                float scale = (std::min)((fabsf(dirX) > 0.0001f) ? halfW / fabsf(dirX) : 1e9f,
                                          (fabsf(dirY) > 0.0001f) ? halfH / fabsf(dirY) : 1e9f);
                Point2D edgePos = {(int16_t)(cx + (int)(dirX * scale)), (int16_t)(cy + (int)(dirY * scale))};
                drawFrame(2, edgePos);
            }
        }
    }

    // Frame 1: next waypoint marker, only while it projects on-screen.
    if (this->current_mission != nullptr && this->nav_point_id != nullptr &&
        !this->current_mission->waypoints.empty() &&
        (size_t)*this->nav_point_id < this->current_mission->waypoints.size() &&
        this->current_mission->waypoints[*this->nav_point_id]->spot != nullptr) {
        Vector3D wpPos = this->current_mission->waypoints[*this->nav_point_id]->spot->position;
        Point2D wpScreenPos;
        if (projectScaled(wpPos, wpScreenPos)) {
            drawFrame(1, wpScreenPos);
        }
    }

    // Frame 3: mouse cursor, only while flying with the mouse.
    if (this->mouse_control) {
        drawFrame(3, this->Mouse.position);
    }
}
// Bug fix (2026-07 session, user-reported: the target box tracked the
// target-lead-indicator marker instead of the ship's real on-screen
// position): SCCockpit::project_to_screen's non-VR branch always projects
// through this->cockpit_camera, which is rotated to match the player
// ship's own nose orientation only (see its update site: rotate(-elevationf,
// -azimuthf, -twist)) — it never tracks whichever camera is actually
// rendering the 3D scene this frame. That's harmless for HUD elements only
// ever meaningful in the default forward cockpit view, but SCStrike's
// camera_mode (TARGET/CHASE/FOLLOW/etc.) can point the real render camera
// (SCRenderer::camera, repositioned per mode before the scene draws) in a
// completely different direction — in which case project_to_screen instead
// answers "where would this be if I were looking straight out the nose,"
// which is roughly where a boresight-relative lead-indicator marker sits,
// not where the target actually renders on screen. Used only by
// RenderWC3TargetBrackets, which needs to track the real rendered ship.
static bool ProjectToScreenViaActiveCamera(Vector3D coord, int &Xout, int &Yout) {
    SCRenderer &renderer = SCRenderer::getInstance();
    Vector3DHomogeneous v = {coord.x, coord.y, coord.z, 1.0f};
    Matrix *mproj = renderer.camera.getProjectionMatrix();
    Matrix *mview = renderer.camera.getViewMatrix();
    Vector3DHomogeneous mcombined = mview->multiplyMatrixVector(v);
    Vector3DHomogeneous result = mproj->multiplyMatrixVector(mcombined);
    if (result.z <= 0.0f || result.w == 0.0f) {
        return false;
    }
    float x = result.x / result.w;
    float y = result.y / result.w;
    // Same fixed 320x200 reference-canvas mapping project_to_screen itself
    // uses, so callers can keep rescaling by fb->width/320.0f etc.
    Xout = (int)((x + 1.0f) * 160.0f);
    Yout = (int)((1.0f - y - 0.45f) * 100.0f) - 1;
    return true;
}
// Corner-anchored bracket (each edge's middle half removed) when !full, a
// complete 4-sided box when full. x1<x2/y1<y2 assumed — every call site
// below builds its box from a min/max pair, so this never has to defend
// against a flipped rectangle.
static void DrawTargetBracket(FrameBuffer *fb, int x1, int y1, int x2, int y2, uint8_t colorIdx, bool full,
                               int thickness) {
    if (full) {
        fb->lineThick(x1, y1, x2, y1, colorIdx, thickness);
        fb->lineThick(x1, y2, x2, y2, colorIdx, thickness);
        fb->lineThick(x1, y1, x1, y2, colorIdx, thickness);
        fb->lineThick(x2, y1, x2, y2, colorIdx, thickness);
        return;
    }
    int qw = (x2 - x1) / 4;
    int qh = (y2 - y1) / 4;
    fb->lineThick(x1, y1, x1 + qw, y1, colorIdx, thickness);
    fb->lineThick(x2 - qw, y1, x2, y1, colorIdx, thickness);
    fb->lineThick(x1, y2, x1 + qw, y2, colorIdx, thickness);
    fb->lineThick(x2 - qw, y2, x2, y2, colorIdx, thickness);
    fb->lineThick(x1, y1, x1, y1 + qh, colorIdx, thickness);
    fb->lineThick(x1, y2 - qh, x1, y2, colorIdx, thickness);
    fb->lineThick(x2, y1, x2, y1 + qh, colorIdx, thickness);
    fb->lineThick(x2, y2 - qh, x2, y2, colorIdx, thickness);
}
int SCCockpit::GetCurrentTargetTurretCount() const {
    if (this->current_target_actor == nullptr || this->current_target_actor->object == nullptr ||
        this->current_target_actor->object->entity == nullptr) {
        return 0;
    }
    return (int)this->current_target_actor->object->entity->turrets.size();
}
bool SCCockpit::GetTurretWorldPosition(SCMissionActors *actor, int turretIndex, Vector3D &outPos) {
    if (actor == nullptr || actor->object == nullptr || actor->object->entity == nullptr) {
        return false;
    }
    auto &turrets = actor->object->entity->turrets;
    if (turretIndex < 0 || (size_t)turretIndex >= turrets.size() || turrets[turretIndex] == nullptr) {
        return false;
    }
    // RSEntity::TURRET is a private nested type (the struct is declared
    // before RSEntity's own "public:" label, even though the `turrets`
    // vector itself is public) — `auto` accesses its public fields through
    // the already-public vector without needing to name the private type.
    auto *turret = turrets[turretIndex];
    // Same orientation triple and Y->Z->X glRotatef order the render path
    // uses for this exact ship (SCRenderer.cpp drawModel; SCStrike.cpp's
    // own actor_orientation build) — must match exactly or this marker
    // drifts away from the turret model actually drawn on screen.
    Vector3D orientation = {(360.0f - static_cast<float>(actor->object->azymuth) + 90.0f),
                             static_cast<float>(actor->object->pitch), -static_cast<float>(actor->object->roll)};
    Matrix rot;
    rot.Clear();
    rot.Identity();
    rot.rotateM(orientation.x * DEG_TO_RAD, 0, 1, 0);
    rot.rotateM(orientation.y * DEG_TO_RAD, 0, 0, 1);
    rot.rotateM(orientation.z * DEG_TO_RAD, 1, 0, 0);
    // Raw turret x/y/z -> render-space (y,z,x) permutation, matching the
    // GL turret glTranslatef(turret->y, turret->z, turret->x) call.
    Vector3DHomogeneous localOffset;
    localOffset.x = turret->y;
    localOffset.y = turret->z;
    localOffset.z = turret->x;
    localOffset.w = 0.0f;
    Vector3DHomogeneous worldOffset = rot.multiplyMatrixVector(localOffset);
    outPos = {actor->object->position.x + worldOffset.x, actor->object->position.y + worldOffset.y,
              actor->object->position.z + worldOffset.z};
    return true;
}
void SCCockpit::RenderWC3TargetBrackets(FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (this->current_target_actor == nullptr || this->current_target_actor->object == nullptr ||
        this->current_mission == nullptr) {
        return;
    }
    SCMissionActors *actor = this->current_target_actor;
    RSEntity *entity = actor->object->entity;

    bool isEnemy = std::find(this->current_mission->enemies.begin(), this->current_mission->enemies.end(), actor) !=
                   this->current_mission->enemies.end();

    // Same nearest-palette-index lookup pattern as the KPS:/SET: cockpit-
    // green color above, cached per color rather than recomputed every frame.
    static uint8_t blueIdx = 0, redIdx = 0, whiteIdx = 0;
    static bool colorsCached = false;
    if (!colorsCached) {
        colorsCached = true;
        auto nearest = [this](uint8_t r, uint8_t g, uint8_t b) -> uint8_t {
            int best = 1 << 30;
            uint8_t bestIdx = 0;
            for (int i = 0; i < 256; i++) {
                const Texel *c = this->palette.GetRGBColor(i);
                int dr = (int)c->r - r, dg = (int)c->g - g, db = (int)c->b - b;
                int d = dr * dr + dg * dg + db * db;
                if (d < best) {
                    best = d;
                    bestIdx = (uint8_t)i;
                }
            }
            return bestIdx;
        };
        blueIdx = nearest(40, 120, 255);
        redIdx = nearest(255, 40, 40);
        whiteIdx = nearest(255, 255, 255);
    }
    uint8_t shipColor = isEnemy ? redIdx : blueIdx;

    // Center is always the object's own projected 3D position — not the
    // AABB corners' own min/max midpoint (which can sit off-center from
    // the object's actual position/reference point if the mesh's local
    // origin isn't its geometric center) and not the lead-indicator point
    // ComputeTargetLeadPoint()/RenderWC3TargetingReticle uses (a velocity-
    // extrapolated aim point, not the ship's real current position).
    int shipSx, shipSy;
    if (!ProjectToScreenViaActiveCamera(actor->object->position, shipSx, shipSy)) {
        return;
    }
    int cx = (int)(shipSx * (fb->width / 320.0f));
    int cy = (int)(shipSy * (fb->height / 200.0f));

    // Project the world AABB's 8 corners (local mesh-space bounds rotated/
    // translated by the same ship orientation GetTurretWorldPosition uses)
    // to get an on-screen size estimate only — correctly accounts for
    // perspective without needing a camera right/up vector, same principle
    // as SCStrike.cpp's own bounding-box-diagonal camera-framing calc. The
    // resulting spread is used purely for sizing the square below; its own
    // min/max midpoint is discarded in favor of the object-position center
    // computed above.
    int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
    bool anyCornerVisible = false;
    if (entity != nullptr) {
        BoudingBox *bb = entity->GetBoudingBpx();
        if (bb != nullptr) {
            Vector3D orientation = {(360.0f - static_cast<float>(actor->object->azymuth) + 90.0f),
                                     static_cast<float>(actor->object->pitch),
                                     -static_cast<float>(actor->object->roll)};
            Matrix rot;
            rot.Clear();
            rot.Identity();
            rot.rotateM(orientation.x * DEG_TO_RAD, 0, 1, 0);
            rot.rotateM(orientation.y * DEG_TO_RAD, 0, 0, 1);
            rot.rotateM(orientation.z * DEG_TO_RAD, 1, 0, 0);
            for (int i = 0; i < 8; i++) {
                Vector3DHomogeneous corner;
                corner.x = (i & 1) ? bb->max.x : bb->min.x;
                corner.y = (i & 2) ? bb->max.y : bb->min.y;
                corner.z = (i & 4) ? bb->max.z : bb->min.z;
                corner.w = 0.0f;
                Vector3DHomogeneous worldOffset = rot.multiplyMatrixVector(corner);
                Vector3D worldCorner = {actor->object->position.x + worldOffset.x,
                                         actor->object->position.y + worldOffset.y,
                                         actor->object->position.z + worldOffset.z};
                int sx, sy;
                if (ProjectToScreenViaActiveCamera(worldCorner, sx, sy)) {
                    anyCornerVisible = true;
                    int scaledX = (int)(sx * (fb->width / 320.0f));
                    int scaledY = (int)(sy * (fb->height / 200.0f));
                    minX = (std::min)(minX, scaledX);
                    maxX = (std::max)(maxX, scaledX);
                    minY = (std::min)(minY, scaledY);
                    maxY = (std::max)(maxY, scaledY);
                }
            }
        }
    }
    // Square, not rectangular: use the larger of the two projected spans
    // for both sides. Falls back to a small fixed size with no mesh/
    // bounding-box data at all rather than not drawing anything.
    int halfSize = anyCornerVisible ? (std::max)(maxX - minX, maxY - minY) / 2 : 10;
    halfSize = (std::max)(halfSize, 6); // stays legible at long range

    DrawTargetBracket(fb, cx - halfSize, cy - halfSize, cx + halfSize, cy + halfSize, shipColor,
                       /*full=*/this->target_hard_locked, /*thickness=*/1);

    // Selected turret marker — always bracket-style regardless of lock
    // state (only the ship-level box distinguishes locked vs unlocked).
    if (this->targeted_turret_index >= 0) {
        Vector3D turretWorldPos;
        if (this->GetTurretWorldPosition(actor, this->targeted_turret_index, turretWorldPos)) {
            int tsx, tsy;
            if (ProjectToScreenViaActiveCamera(turretWorldPos, tsx, tsy)) {
                int tcx = (int)(tsx * (fb->width / 320.0f));
                int tcy = (int)(tsy * (fb->height / 200.0f));
                const int kTurretHalfSize = 8;
                DrawTargetBracket(fb, tcx - kTurretHalfSize, tcy - kTurretHalfSize, tcx + kTurretHalfSize,
                                   tcy + kTurretHalfSize, whiteIdx, /*full=*/false, /*thickness=*/1);
            }
        }
    }
}
void SCCockpit::RenderMFDSRadar(Point2D pmfd_left, float range, int mode, FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (!this->cockpit->instrumentShapes.empty()) {
        this->RenderMFDSTargetRadarWC3(pmfd_left, range, fb);
        return;
    }
    switch (mode) {
    case RadarMode::AARD:
        this->RenderMFDSRadarImplementation(pmfd_left, range, "AIR", true, fb);
        break;
    case RadarMode::AGRD:
        this->RenderMFDSRadarImplementation(pmfd_left, range, "GROUND", false, fb);
        break;
    case RadarMode::ASST:
        this->RenderMFDSRadarSingleTargetImplementation(pmfd_left, range, "SINGLE", false, fb);
        break;
    case RadarMode::AFRD:
        break;
    default:
        break;
    }
}

void SCCockpit::RenderMFDSRadarSingleTargetImplementation(Point2D pmfd_left, float range, const char *mode_name, bool air_mode,
                                              FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    // Calcul des positions et dimensions
    Point2D pmfd_center = {
        pmfd_left.x + this->cockpit->MONI.SHAP.GetWidth() / 2,
        pmfd_left.y + this->cockpit->MONI.SHAP.GetHeight() / 2,
    };
    this->RenderMFDS(pmfd_left, fb);

    // Dimensions du radar
    Point2D radar_size = {100, 80};
    Point2D bottom_right = {pmfd_left.x + radar_size.x, pmfd_left.y + radar_size.y};

    // Choisir les ressources en fonction du mode
    auto &radar_arts = this->cockpit->MONI.MFDS.AARD.ARTS;
    int bg_shape_index = 3;

    // Position du fond du radar
    Point2D radar_bg_pos = pmfd_left;

    
    radar_bg_pos.x += this->cockpit->MONI.SHAP.GetWidth() / 2 - radar_arts.GetShape(bg_shape_index)->GetWidth() / 2;
    
    radar_bg_pos.y += this->cockpit->MONI.SHAP.GetHeight() / 2 - radar_arts.GetShape(bg_shape_index)->GetHeight() / 2;

    // Dessine le fond du radar
    radar_arts.GetShape(bg_shape_index)->SetPosition(&radar_bg_pos);
    fb->drawShape(radar_arts.GetShape(bg_shape_index));

    // Affiche les infos textuelles
    Point2D pmfd_text = {radar_bg_pos.x + 10, radar_bg_pos.y + 2};
    std::string rzoom_str = "10";
    switch (this->radar_zoom) {
    case 1:
        rzoom_str = "10";
        break;
    case 2:
        rzoom_str = "20";
        break;
    case 3:
        rzoom_str = "40";
        break;
    case 4:
        rzoom_str = "80";
        break;
    default:
        rzoom_str = "10";
        break;
    }
    fb->printText(this->big_font, &pmfd_text, (char *)rzoom_str.c_str(), 0, 0, 2, 2, 2);

    std::string mode_text = " " + std::string(mode_name) + " ";
    fb->printText(this->big_font, &pmfd_text, const_cast<char *>(mode_text.c_str()), 0, 0, (int)mode_text.length(), 2,
                  2);

    // Position du joueur comme centre du radar
    Vector2D center = {this->player->position.x, this->player->position.z};

    // Préparation pour la rotation
    int heading = (int)this->heading;
    heading = (heading) % 360;
    float headingRad = heading / 180.0f * (float)M_PI;

    // Fonction pour dessiner un contact sur le radar
    auto drawContact = [&](SCMissionActors *object, bool isFriendly, bool isDestroyed) {
        // Vérifier si l'entité correspond au mode actuel
        bool valid_entity;
        if (!object->is_active)
            return;

        Vector2D objPos = {object->object->position.x, object->object->position.z};

        // Rotation selon le heading du joueur
        Vector2D rotatedPos = objPos.rotateAroundPoint(center, -headingRad);
        Vector2D relativePos = {rotatedPos.x - center.x, rotatedPos.y - center.y};

        // Vérification de la distance
        float distance = sqrtf(relativePos.x * relativePos.x + relativePos.y * relativePos.y);
        if (distance >= range)
            return;

        // Échelle et position sur l'écran
        float scale = 60.0f / range;
        Point2D screenPos = {pmfd_center.x + (int)(relativePos.x * scale),
                             pmfd_center.y + (int)(relativePos.y * scale)};

        // Vérifie si le contact est dans les limites du MFD
        if (screenPos.x <= pmfd_left.x || screenPos.x >= pmfd_left.x + this->cockpit->MONI.SHAP.GetWidth() ||
            screenPos.y <= pmfd_left.y || screenPos.y >= pmfd_left.y + this->cockpit->MONI.SHAP.GetHeight())
            return;

        // Sélectionne l'icône appropriée
        int iconIndex;
        
        iconIndex = 10;
    
        // Dessine l'icône
        radar_arts.GetShape(iconIndex)->SetPosition(&screenPos);
        fb->drawShapeWithBox(radar_arts.GetShape(iconIndex), pmfd_left.x, bottom_right.x, pmfd_left.y, bottom_right.y);
        Point2D targetPos = {screenPos.x - 2, screenPos.y - 1};
        std::string altitude_str = std::to_string((int)object->object->position.y);
        std::string heading_str = std::to_string((int)object->object->azymuth);
        std::string speed_str = std::to_string(object->plane != nullptr ? (int)object->plane->airspeed: 0);
        Point2D textPos = {targetPos.x + 10, targetPos.y};
        fb->printText(this->big_font, &textPos, (char *)altitude_str.c_str(), 0, 0, (uint32_t)altitude_str.length(), 1, 1);
        textPos.x = targetPos.x + 10;
        textPos.y = textPos.y + 6;
        fb->printText(this->big_font, &textPos, (char *)heading_str.c_str(), 0, 0, (uint32_t)heading_str.length(), 1, 1);
        textPos.x = targetPos.x + 10;
        textPos.y = textPos.y + 6;
        fb->printText(this->big_font, &textPos, (char *)speed_str.c_str(), 0, 0, (uint32_t)speed_str.length(), 1, 1);
    };

    // Dessine les ennemis 
    for (auto actor : this->current_mission->enemies) {
        if (actor->object == this->current_target) {
            drawContact(actor, false, actor->is_destroyed);
        }
    }

    // Dessine les alliés (sauf le joueur)
    for (auto actor : this->current_mission->friendlies) {
        if (actor->object == this->current_target) {
            drawContact(actor, true, actor->is_destroyed);
        }
    }
}

void SCCockpit::RenderMFDSRadarImplementation(Point2D pmfd_left, float range, const char *mode_name, bool air_mode,
                                              FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    // Calcul des positions et dimensions
    Point2D pmfd_center = {
        pmfd_left.x + this->cockpit->MONI.SHAP.GetWidth() / 2,
        pmfd_left.y + this->cockpit->MONI.SHAP.GetHeight() / 2,
    };
    this->RenderMFDS(pmfd_left, fb);

    // Dimensions du radar
    Point2D radar_size = {100, 80};
    Point2D bottom_right = {pmfd_left.x + radar_size.x, pmfd_left.y + radar_size.y};

    // Choisir les ressources en fonction du mode
    auto &radar_arts = air_mode ? this->cockpit->MONI.MFDS.AARD.ARTS : this->cockpit->MONI.MFDS.AGRD.ARTS;
    int bg_shape_index = air_mode ? 4 : 1;

    // Position du fond du radar
    Point2D radar_bg_pos = pmfd_left;

    if (air_mode) {
        radar_bg_pos.x +=
            -5 + this->cockpit->MONI.SHAP.GetWidth() / 2 - radar_arts.GetShape(bg_shape_index)->GetWidth() / 2;
    } else {
        radar_bg_pos.x += this->cockpit->MONI.SHAP.GetWidth() / 2 - radar_arts.GetShape(bg_shape_index)->GetWidth();
    }

    radar_bg_pos.y += this->cockpit->MONI.SHAP.GetHeight() / 2 - radar_arts.GetShape(bg_shape_index)->GetHeight() / 2;

    // Dessine le fond du radar
    radar_arts.GetShape(bg_shape_index)->SetPosition(&radar_bg_pos);
    fb->drawShape(radar_arts.GetShape(bg_shape_index));

    // Affiche les infos textuelles
    Point2D pmfd_text = {radar_bg_pos.x + 10, radar_bg_pos.y + 2};
    std::string rzoom_str = "10";
    switch (this->radar_zoom) {
    case 1:
        rzoom_str = "10";
        break;
    case 2:
        rzoom_str = "20";
        break;
    case 3:
        rzoom_str = "40";
        break;
    case 4:
        rzoom_str = "80";
        break;
    default:
        rzoom_str = "10";
        break;
    }
    fb->printText(this->big_font, &pmfd_text, (char *)rzoom_str.c_str(), 0, 0, 2, 2, 2);

    std::string mode_text = " " + std::string(mode_name) + " ";
    fb->printText(this->big_font, &pmfd_text, const_cast<char *>(mode_text.c_str()), 0, 0, (int)mode_text.length(), 2,
                  2);

    // Affiche le "360" uniquement en mode air
    if (air_mode) {
        fb->printText(this->big_font, &pmfd_text, const_cast<char *>("360"), 0, 0, 3, 2, 2);
    }

    // Position du joueur comme centre du radar
    Vector2D center = {this->player->position.x, this->player->position.z};

    // Préparation pour la rotation
    int heading = (int)this->heading;
    heading = (heading + 360) % 360;
    float headingRad = heading / 180.0f * (float)M_PI;

    // Fonction pour dessiner un contact sur le radar
    auto drawContact = [&](SCMissionActors *object, bool isFriendly, bool isDestroyed) {
        // Vérifier si l'entité correspond au mode actuel
        bool valid_entity;
        if (!object->is_active)
            return;
        if (air_mode) {
            valid_entity = (object->object->entity->entity_type == EntityType::jet);
        } else {
            valid_entity = (object->object->entity->entity_type == EntityType::ground ||
                            object->object->entity->entity_type == EntityType::ornt ||
                            object->object->entity->entity_type == EntityType::swpn);
        }

        if (!valid_entity)
            return;

        Vector2D objPos = {object->object->position.x, object->object->position.z};

        // Rotation selon le heading du joueur
        Vector2D rotatedPos = objPos.rotateAroundPoint(center, -headingRad);
        Vector2D relativePos = {rotatedPos.x - center.x, rotatedPos.y - center.y};

        // Vérification de la distance
        float distance = sqrtf(relativePos.x * relativePos.x + relativePos.y * relativePos.y);
        if (distance >= range)
            return;

        // Échelle et position sur l'écran
        float scale = 60.0f / range;
        Point2D screenPos = {pmfd_center.x + (int)(relativePos.x * scale),
                             pmfd_center.y + (int)(relativePos.y * scale)};

        // Vérifie si le contact est dans les limites du MFD
        if (screenPos.x <= pmfd_left.x || screenPos.x >= pmfd_left.x + this->cockpit->MONI.SHAP.GetWidth() ||
            screenPos.y <= pmfd_left.y || screenPos.y >= pmfd_left.y + this->cockpit->MONI.SHAP.GetHeight())
            return;

        // Sélectionne l'icône appropriée
        int iconIndex;
        if (air_mode) {
            if (isFriendly) {
                iconIndex = isDestroyed ? 8 : 9;
            } else {
                iconIndex = isDestroyed ? 5 : 0;
            }
        } else {
            // En mode sol, tous les icônes sont les mêmes (index 3)
            iconIndex = 3;
        }

        // Dessine l'icône
        radar_arts.GetShape(iconIndex)->SetPosition(&screenPos);
        fb->drawShapeWithBox(radar_arts.GetShape(iconIndex), pmfd_left.x, bottom_right.x, pmfd_left.y, bottom_right.y);

        // Si c'est la cible actuelle, dessine le marqueur de cible
        if (object->object == this->current_target) {
            Point2D targetPos = {screenPos.x - 2, screenPos.y - 1};
            // Utilise toujours l'icône de cible du mode air
            this->cockpit->MONI.MFDS.AARD.ARTS.GetShape(2)->SetPosition(&targetPos);
            fb->drawShapeWithBox(this->cockpit->MONI.MFDS.AARD.ARTS.GetShape(2), pmfd_left.x, bottom_right.x,
                                 pmfd_left.y, bottom_right.y);
        }
    };

    // Dessine les ennemis 
    for (auto actor : this->current_mission->enemies) {
        drawContact(actor, false, actor->is_destroyed);
    }

    // Dessine les alliés (sauf le joueur)
    for (auto actor : this->current_mission->friendlies) {
        if (actor != this->current_mission->player) {
            drawContact(actor, true, actor->is_destroyed);
        }
    }
}
void SCCockpit::RenderRAWSBig(Point2D pmfd_left = {84, 112}, FrameBuffer *fb = nullptr) {
    if (this->cockpit->MONI.INST.RAWS.ZOOM.data == nullptr) {
        return;
    }
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    Point2D raws_size = {this->cockpit->MONI.INST.RAWS.ZOOM.GetWidth(), this->cockpit->MONI.INST.RAWS.ZOOM.GetHeight()};
    if (this->cockpit->MONI.INST.RAWS.zoom_x != 0 && this->cockpit->MONI.INST.RAWS.zoom_y != 0) {
        if (pmfd_left.x != 0 && pmfd_left.y != 0) {
            pmfd_left.x = this->cockpit->MONI.INST.RAWS.zoom_x;
            pmfd_left.y = this->cockpit->MONI.INST.RAWS.zoom_y;
        }
    }
    Point2D bottom_right = {pmfd_left.x + raws_size.x, pmfd_left.y + raws_size.y};
    this->cockpit->MONI.INST.RAWS.ZOOM.SetPosition(&pmfd_left);
    for (int y = pmfd_left.y; y < bottom_right.y; y++) {
        fb->line(pmfd_left.x, y, bottom_right.x, y, 0);
    }
    
    
    int heading = (int)this->heading;
    heading = (heading) % 360;
    float headingRad = heading / 180.0f * (float)M_PI;
    int rsize = this->cockpit->MONI.INST.RAWS.ZOOM.GetWidth() / 2;
    for (auto contact : this->current_mission->actors) {
        this->IdentifyRAWSContact(contact, fb, headingRad, pmfd_left, raws_size, true, rsize);
    }
    fb->drawShape(&this->cockpit->MONI.INST.RAWS.ZOOM);
}
void SCCockpit::IdentifyRAWSContact(SCMissionActors *contact, FrameBuffer *fb = nullptr, float headingRad = 0.0f, Point2D pmfd_left = {84, 112}, Point2D raws_size = {0, 0}, bool is_zoomed = false, int rsize = 10) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (contact->is_active && contact->object->entity->radar_signature != nullptr) {
        Vector2D contact_pos = {
            contact->object->position.x,
            contact->object->position.z
        };
        Vector2D center = {
            this->player->position.x,
            this->player->position.z
        };
        Vector2D roa_dir = {
            contact_pos.x - center.x,
            contact_pos.y - center.y
        };

        float distance = roa_dir.Length();
        roa_dir.Normalize();
        const float max_range = 30000.0f; // 30 km
        int radar_index = 29; // Index de l'icône de contact radar
        static std::unordered_map<std::string, int> radar_signature_to_index = {
            {"747", 29},
            {"A-10", 29},
            {"AWACS", 29},
            {"BOATX", 19},
            {"C130DES", 29},
            {"C130GRN", 29},
            {"CARRIERW", 19},
            {"DESTROYS", 23},
            {"F-15", 29},
            {"F-16DES", 29},
            {"F-16GRAY", 29},
            {"F-18", 29},
            {"F-22", 29},
            {"F-4", 29},
            {"LEARJET", 29},
            {"MIG21", 29},
            {"MIG29", 29},
            {"MIRAGE", 29},
            {"MOBSAMLN", 23},
            {"RDRSTL2", 21},
            {"STASAMLN", 23},
            {"SU27", 29},
            {"TORNCG", 29},
            {"TU-20", 29},
            {"YF23", 29},
            {"ZSU-23", 19},
        };
        static std::unordered_map<std::string, int> radar_signature_to_index_zoom = {
            {"747", 11},
            {"A-10", 13},
            {"AWACS", 11},
            {"BOATX", 3},
            {"C130DES", 11},
            {"C130GRN", 11},
            {"CARRIERW", 3},
            {"DESTROYS", 5},
            {"F-15", 17},
            {"F-16DES", 17},
            {"F-16GRAY", 17},
            {"F-18", 17},
            {"F-22", 17},
            {"F-4", 13},
            {"LEARJET", 11},
            {"MIG21", 13},
            {"MIG29", 17},
            {"MIRAGE", 15},
            {"MOBSAMLN", 5},
            {"RDRSTL2", 9},
            {"STASAMLN", 7},
            {"SU27", 17},
            {"TORNCG", 17},
            {"TU-20", 11},
            {"YF23", 17},
            {"ZSU-23", 3},
        };
        if (is_zoomed) {
            if (radar_signature_to_index_zoom.find(contact->object->member_name) != radar_signature_to_index_zoom.end()) {
                radar_index = radar_signature_to_index_zoom[contact->object->member_name];
            }
        } else {
            if (radar_signature_to_index.find(contact->object->member_name) != radar_signature_to_index.end()) {
                radar_index = radar_signature_to_index[contact->object->member_name];
            }
        }
        if (distance < max_range) {
            float scale = 10.0f;
            scale = (distance / max_range) * rsize;
            Point2D p = {(int)(roa_dir.x * scale), (int)(roa_dir.y * scale)};
            Point2D rotatedPos = p.rotateAroundPoint({0, 0}, headingRad);
            Point2D raw_pos = {
                pmfd_left.x + (raws_size.x / 2) + rotatedPos.x,
                pmfd_left.y + (raws_size.y / 2) + rotatedPos.y
            };
            this->cockpit->MONI.INST.RAWS.SYMB.GetShape(radar_index)->SetPosition(&raw_pos);
            fb->drawShape(this->cockpit->MONI.INST.RAWS.SYMB.GetShape(radar_index));
        }
    }
    
}
void SCCockpit::RenderRAWS(Point2D pmfd_left = {84, 112}, FrameBuffer *fb = nullptr) {
    if (this->cockpit->MONI.INST.RAWS.NORM.data == nullptr) {
        return;
    }
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    Point2D raws_size = {this->cockpit->MONI.INST.RAWS.NORM.GetWidth(), this->cockpit->MONI.INST.RAWS.NORM.GetHeight()};
    if (this->cockpit->MONI.INST.RAWS.x != 0 && this->cockpit->MONI.INST.RAWS.y != 0) {
        if (pmfd_left.x != 0 && pmfd_left.y != 0) {
            pmfd_left.x = this->cockpit->MONI.INST.RAWS.x;
            pmfd_left.y = this->cockpit->MONI.INST.RAWS.y;
        }
    }
    Point2D bottom_right = {pmfd_left.x + raws_size.x, pmfd_left.y + raws_size.y};
    this->cockpit->MONI.INST.RAWS.NORM.SetPosition(&pmfd_left);
    for (int y = pmfd_left.y; y < bottom_right.y; y++) {
        fb->line(pmfd_left.x, y, bottom_right.x, y, 0);
    }
    
    int heading = (int)this->heading;
    heading = (heading + 360) % 360;
    float headingRad = heading / 180.0f * (float)M_PI;
    int rsize = this->cockpit->MONI.INST.RAWS.NORM.GetWidth() / 2;
    for (auto contact : this->current_mission->actors) {
        this->IdentifyRAWSContact(contact, fb, headingRad, pmfd_left, raws_size, false, rsize);
    }
    fb->drawShape(&this->cockpit->MONI.INST.RAWS.NORM);
}

void SCCockpit::RenderMFDSComm(Point2D pmfd_left, int mode, FrameBuffer *fb = nullptr) {
    // Real WC3 comm page (2026-07 session, user-requested): the underlying
    // data model (RSProf::radi.opts/asks/msgs/sond, SCMissionActors::
    // setMessage/respondToRadioMessage, already wired to keys 1-8 in
    // SCStrike.cpp) was already fully functional for WC3 profiles — this
    // function's own MONI.SHAP-gated early return below was the only thing
    // actually blocking it from ever being drawn, since WC3 cockpits never
    // populate that SC-only field.
    if (!this->cockpit->instrumentShapes.empty()) {
        this->RenderMFDSCommWC3(pmfd_left, mode, fb);
        return;
    }
    if (this->cockpit->MONI.SHAP.data == nullptr) {
        return;
    }
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    for (int i = 0; i < this->cockpit->MONI.SHAP.GetHeight(); i++) {
        fb->line(pmfd_left.x, pmfd_left.y + i, pmfd_left.x + this->cockpit->MONI.SHAP.GetWidth(), pmfd_left.y + i, 2);
    }
    Vector2D center = {this->player->position.x, this->player->position.y};
    Point2D pmfd_text = {pmfd_left.x + 20, pmfd_left.y + 20};
    Point2D pfmd_title = {pmfd_text.x + 12, pmfd_text.y};
    fb->printText(this->big_font, &pfmd_title, const_cast<char *>("Comm mode:"), 120, 0, 10, 2, 2);
    pfmd_title.y += 10;
    pfmd_title.x = pmfd_left.x + 20;
    fb->printText(this->big_font, &pfmd_title, const_cast<char *>("Select frequency"), 0, 0, 16, 2, 2);
    pmfd_text.y += 20;
    if (mode == 0) {
        int cpt = 1;
        for (auto ai : this->current_mission->friendlies) {
            Vector2D ai_position = {ai->object->position.x, ai->object->position.z};
            Vector2D roa_dir = {ai_position.x - center.x, ai_position.y - center.y};

            float distance = sqrtf((float)(roa_dir.x * roa_dir.x) + (float)(roa_dir.y * roa_dir.y));
            if (ai->actor_name != "PLAYER" && ai->is_active && ai->profile != nullptr) {
                std::string name_str = std::to_string(cpt) + ". " + ai->actor_name;
                Point2D pfmd_entry = {pmfd_text.x, pmfd_text.y};
                fb->printText(this->big_font, &pfmd_entry, (char *)name_str.c_str(), 120, 0,
                              (uint32_t)name_str.length(), 2, 2);
                pmfd_text.y += 10;
                cpt++;
            }
        }
        if (cpt == 1) {
            pfmd_title.y += 25;
            pfmd_title.x = pmfd_left.x + 32;
            fb->printText(this->big_font, &pfmd_title, const_cast<char *>("NO RECIVER"), 120, 0, 10, 2, 2);
        }
    } else if (mode > 0) {
        int cpt = 1;
        if (this->comm_actor != nullptr && this->comm_actor->profile != nullptr) {
            auto ai = this->comm_actor;
            int cpt_message = 1;
            for (auto asks : ai->profile->radi.opts) {
                std::string toask = std::string("") + asks;
                std::string request = this->current_mission->player->profile->radi.asks.at(toask);
                Point2D pfmd_entry = {pmfd_text.x, pmfd_text.y};
                std::string asks_str = std::to_string(cpt_message) + ". " + request;
                fb->printText(this->big_font, &pfmd_entry, (char *)asks_str.c_str(), 124, 0,
                              (uint32_t)asks_str.length(), 2, 2);
                pmfd_text.y += 6;
                cpt_message++;
            }
        }
    }
    this->cockpit->MONI.SHAP.SetPosition(&pmfd_left);
    fb->drawShape(&this->cockpit->MONI.SHAP);
}

// Real WC3 comm page (2026-07 session, user-requested). Reuses this->
// RenderMFDS's shared panel clear (already WC3-safe) plus shape 53/
// kSidePanel as the backdrop ("a dashboard sub-panel with switch/button
// rows... dark viewport in the middle" — WC3CockpitShapeId's own comment;
// page 4's real confirmation is the SYS chunk's own subsystemId=16
// "Communication" slot, byte-identical across all 6 real cockpit files).
// mode==0 lists the mission's other active friendlies by their real
// profile callsign (radi.info.callsign — see the PROF>RADI>INFO fixed-
// stride parsing fix earlier this session, which is what makes this
// readable instead of empty); mode>0 lists the selected contact's own
// radi.opts question keys, each looked up through the PLAYER's own
// profile (radi.asks) for human-readable question text. Choosing a
// numbered option is unchanged — SCMissionActors::respondToRadioMessage
// (already wired to keys 1-8 in SCStrike.cpp) reads this exact same
// opts/asks data, and setMessage() already renders the reply as text
// (RenderCommMessages) and plays it as audio (Mixer.playSoundVoc, via
// either SC's spch+SPEECH.PAK indirection or WC3's own inline SOND PCM) —
// none of that needed to change, only this page's own rendering was
// missing.
// Starts/advances/stops the comm-reply video stream to track whatever the
// current radio message is. Same file-loading convention as WC3GameFlow::
// playMovie (preloaded archive first, then an on-demand CD-archive
// fallback) but non-blocking — this is called every frame the comm page
// is on-screen, not once up front.
void SCCockpit::UpdateCommVideo() {
    RadioMessages *activeMsg =
        this->current_mission->radio_messages.empty() ? nullptr : this->current_mission->radio_messages[0];
    bool wantsVideo = activeMsg != nullptr && !activeMsg->fmvFilename.empty();
    if (!wantsVideo) {
        if (this->commVideoStream != nullptr) {
            delete this->commVideoStream;
            this->commVideoStream = nullptr;
            if (this->commVideoOwnedData != nullptr) {
                delete[] this->commVideoOwnedData;
                this->commVideoOwnedData = nullptr;
            }
        }
        return;
    }
    if (this->commVideoStream == nullptr) {
        char assetPath[128];
        snprintf(assetPath, sizeof(assetPath), "..\\..\\DATA\\MOVIES\\%s", activeMsg->fmvFilename.c_str());
        uint8_t *data = nullptr;
        size_t size = 0;
        bool owned = false;
        TreEntry *entry = Assets.GetEntryByName(assetPath);
        if (entry != nullptr) {
            data = entry->data;
            size = entry->size;
        } else {
            static const char *cdTres[] = {"cd1movie.tre", "cd2movie.tre", "cd3movie.tre", "cd4movie.tre", nullptr};
            for (const char **tre = cdTres; *tre && data == nullptr; tre++) {
                TreEntry *e = XtreArchive::LoadSingleEntry(*tre, assetPath);
                if (e != nullptr) {
                    data = e->data;
                    size = e->size;
                    owned = true;
                    delete e;
                }
            }
        }
        if (data != nullptr) {
            WC3MVEStream *stream = new WC3MVEStream();
            if (stream->load(data, size)) {
                this->commVideoStream = stream;
                this->commVideoOwnedData = owned ? data : nullptr;
            } else {
                delete stream;
                if (owned) {
                    delete[] data;
                }
                // Leave commVideoStream null — RenderMFDSCommWC3 falls
                // through to the normal contact/question list, same as
                // if this reply had no video at all.
                activeMsg->fmvFilename.clear();
            }
        } else {
            printf("SCCockpit: comm reply movie not found: %s\n", activeMsg->fmvFilename.c_str());
            activeMsg->fmvFilename.clear();
        }
    }
    if (this->commVideoStream != nullptr) {
        this->commVideoStream->advance(GameTimer::getInstance().getDeltaTime());
    }
}
void SCCockpit::RenderMFDSCommWC3(Point2D pmfd, int mode, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    this->RenderMFDS(pmfd, fb);
    this->UpdateCommVideo();
    if (this->commVideoStream != nullptr && !this->commVideoStream->isFinished()) {
        // Fills the whole VDU panel, replacing the contact/question list
        // entirely — per user spec, not a small inset next to the text.
        if (!this->commVideoPaletteLutBuilt) {
            for (int r = 0; r < 256; r += 8) {
                for (int g = 0; g < 256; g += 8) {
                    for (int b = 0; b < 256; b += 8) {
                        uint8_t idx = ClosestPaletteIndex(this->palette, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                        uint32_t key = (((uint32_t)r >> 3) << 10) | (((uint32_t)g >> 3) << 5) | ((uint32_t)b >> 3);
                        this->commVideoPaletteLut[key] = idx;
                    }
                }
            }
            this->commVideoPaletteLutBuilt = true;
        }
        Point2D mfdSize = this->GetWC3MfdSize();
        int w = mfdSize.x;
        int h = mfdSize.y;
        if (w > 0 && h > 0) {
            const uint32_t *src = this->commVideoStream->getPixels();
            std::vector<uint8_t> indexed((size_t)w * h);
            for (int yy = 0; yy < h; yy++) {
                int sy = yy * 200 / h;
                for (int xx = 0; xx < w; xx++) {
                    int sx = xx * 320 / w;
                    uint32_t v = src[sy * 320 + sx];
                    uint8_t r = (uint8_t)(v & 0xFF);
                    uint8_t g = (uint8_t)((v >> 8) & 0xFF);
                    uint8_t b = (uint8_t)((v >> 16) & 0xFF);
                    uint32_t key = (((uint32_t)r >> 3) << 10) | (((uint32_t)g >> 3) << 5) | ((uint32_t)b >> 3);
                    indexed[(size_t)yy * w + xx] = this->commVideoPaletteLut[key];
                }
            }
            fb->blit(indexed.data(), pmfd.x, pmfd.y, w, h);
        }
        return;
    }
    auto bgIt = this->cockpit->instrumentShapes.find(WC3CockpitShapeId::kSidePanel);
    if (bgIt != this->cockpit->instrumentShapes.end() && bgIt->second != nullptr && bgIt->second->GetNumImages() > 0) {
        RLEShape *bgShape = bgIt->second->GetShape(0);
        if (bgShape != nullptr) {
            Point2D bgPos = pmfd;
            bgShape->SetPosition(&bgPos);
            fb->drawShape(bgShape);
        }
    }
    WC3Font *font = WC3Globals::getInstance().getFont("SLRG");
    if (font == nullptr || !font->isLoaded()) {
        return;
    }
    uint8_t green = ClosestPaletteIndex(this->palette, 0, 200, 50);
    int lineHeight = font->getHeight() + 2;
    int x = pmfd.x + 4;
    int y = pmfd.y + 4;
    if (mode == 0) {
        font->drawTextColored(fb, "COMMUNICATION", x, y, green);
        y += lineHeight;
        font->drawTextColored(fb, "FREQUENCY:", x, y, green);
        y += lineHeight;
        int cpt = 1;
        for (auto ai : this->current_mission->friendlies) {
            if (ai->actor_name == "PLAYER" || !ai->is_active || ai->profile == nullptr) {
                continue;
            }
            std::string label = !ai->profile->radi.info.callsign.empty() ? ai->profile->radi.info.callsign
                                                                           : ai->actor_name;
            std::string entry = std::to_string(cpt) + ". " + label;
            font->drawTextColored(fb, entry, x, y, green);
            y += lineHeight;
            cpt++;
        }
        if (cpt == 1) {
            font->drawTextColored(fb, "NO RECEIVER", x, y, green);
        }
    } else {
        font->drawTextColored(fb, "SELECT MESSAGE", x, y, green);
        y += lineHeight * 2;
        if (this->comm_actor != nullptr && this->comm_actor->profile != nullptr &&
            this->current_mission->player != nullptr && this->current_mission->player->profile != nullptr) {
            auto &playerAsks = this->current_mission->player->profile->radi.asks;
            int cpt = 1;
            // Numbering must match SCMissionActors::respondToRadioMessage's
            // own loop (SCMissionActors.cpp), which increments its cpt for
            // every opts entry unconditionally — cpt here can't skip an
            // entry just because the player's own asks text is missing, or
            // the number the player presses would select the wrong option.
            for (char key : this->comm_actor->profile->radi.opts) {
                auto it = playerAsks.find(std::string(1, key));
                std::string text = (it != playerAsks.end()) ? it->second : std::string(1, key);
                std::string entry = std::to_string(cpt) + ". " + text;
                font->drawTextColored(fb, entry, x, y, green);
                y += lineHeight;
                cpt++;
            }
        }
    }
}

// See header comment. Same letterboxed-viewport/immediate-mode-quad GL code
// as RenderHighResBackground below, factored out and generalized to any
// shape+palette instead of just the cached ARTP_SVGA background — but with
// an overlay-only palette (index 255 forced transparent) instead of
// this->palette directly, and no glClear, so it composites onto whatever
// this frame already drew instead of replacing it.
void SCCockpit::RenderCockpitOverlayShape(RLEShape *shape) {
    if (shape == nullptr) {
        return;
    }
    int w = shape->GetWidth();
    int h = shape->GetHeight();
    if (w <= 0 || h <= 0) {
        return;
    }
    std::vector<uint8_t> indexBuf((size_t)w * h, 255);
    shape->buffer_size.x = w;
    shape->buffer_size.y = h;
    size_t byteRead = 0;
    shape->Expand(indexBuf.data(), &byteRead);

    // Local copy, not this->palette directly: index 255 means "transparent,
    // show what's underneath" for this overlay (same convention
    // RSVGA::vSync uses for the low-res instrument buffer), but that's not
    // true of every palette user — e.g. RenderHighResBackground's own
    // opaque background may legitimately paint with color 255. Mutating
    // this->palette in place would leak the override into those unrelated
    // draws.
    VGAPalette overlayPalette = this->palette;
    overlayPalette.colors[255].a = 0;

    RSImage img;
    img.width = w;
    img.height = h;
    img.data = indexBuf.data();
    img.palette = &overlayPalette;
    img.flags = 0;

    Texture tex;
    tex.set(&img);
    tex.updateContent(&img);
    // Same detach as RenderHighResBackground: img.data merely points into
    // indexBuf's vector-owned buffer, never something RSImage owns.
    img.data = nullptr;

    int winW = VGA.GetWindowWidth();
    int winH = VGA.GetWindowHeight();
    int vw = (int)((float)winH * (4.0f / 3.0f));
    int x = (winW - vw) / 2;
    glViewport(x, 0, vw, winH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLuint glId = 0;
    glGenTextures(1, &glId);
    glBindTexture(GL_TEXTURE_2D, glId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    gb.color4f(1.0f, 1.0f, 1.0f, 1.0f);

    gb.begin(GL_QUADS);
    gb.texCoord2f(0, 1);
    gb.vertex2d(0, 0);
    gb.texCoord2f(1, 1);
    gb.vertex2d(w, 0);
    gb.texCoord2f(1, 0);
    gb.vertex2d(w, h);
    gb.texCoord2f(0, 0);
    gb.vertex2d(0, h);
    gb.end();

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDeleteTextures(1, &glId);
}

bool SCCockpit::IsPlayerFrontQuadrantDamaged() {
    if (this->player_plane == nullptr || this->player_plane->pilot == nullptr) {
        return false;
    }
    SCMissionActors *pilot = this->player_plane->pilot;
    return pilot->shield_front < pilot->max_shield_front ||
           pilot->armor_front < pilot->max_armor_front;
}
// WC3CockpitShapeId::kCompassDialMedium's real screen position — "the
// predefined compass area", near screen center, independent of both VDU
// panels. Used both to anchor RenderMFDSRadar's output there instead of
// the left VDU, and as the position for RenderVDUDamageOverlay's
// radar-damage frame. found is false (position undefined) if neither a
// per-cockpit override nor a FRNT INST record for it exists.
//
// User-confirmed (2026-07 session) per-cockpit overrides — none of these
// come from the cockpit files' own data (checked for MEDPIT specifically:
// searched the raw file for its circle's coordinates, as both plain int16
// and the VDU chunk's own ×256 fixed-point encoding — no match at all;
// MEDPIT.IFF's own FRNT INST record for id 24 gives (220,130) in SVGA
// space, confirmed simply wrong for this purpose by the mismatch with the
// user's real coordinates). Position is each circle's top-left corner
// (RLEShape::SetPosition is top-left-registered like everywhere else in
// this renderer) — e.g. MEDPIT's 80x80 circle from (280,280) to (360,360)
// stores as {280,280}; EXCPIT's 75x75 circle from (282,310) to (357,385)
// (real cockpit_name confirmed via MISNB002.IFF's own COCK chunk, the
// mission that actually flies the Excalibur — NOT "excalpit") stores as
// {282,310}. Keyed by cockpit name (RSEntity::cockpit_name, already
// upper-cased at parse time) until every cockpit's real position is
// known; falls through to the (demonstrated unreliable for MEDPIT, but
// only other option available) raw INST-record lookup for any cockpit
// without a confirmed override yet.
struct CompassAreaRect { Point2D topLeft; Point2D bottomRight; };
static const std::unordered_map<std::string, CompassAreaRect> kCompassAreaOverrideByCockpit = {
    {"MEDPIT", {{280, 280}, {360, 360}}},
    {"EXCPIT", {{282, 310}, {357, 385}}},
    {"ARWPIT", {{283, 323}, {357, 396}}},  // Arrow
    {"HVYPIT", {{284, 268}, {356, 338}}},  // Thunderbolt
    {"BOMPIT", {{45, 329}, {127, 410}}},   // Longbow — notably off-center
};
// Real screen rectangle for "the predefined compass area" — where the
// radar dial/dots actually live, near screen center (except BOMPIT — see
// its own map comment), independent of both VDU panels. found is false
// (rect undefined) if neither a per-cockpit override nor a FRNT INST
// record for WC3CockpitShapeId::kCompassDialMedium exists.
//
// User-confirmed (2026-07 session) per-cockpit overrides — none of these
// come from the cockpit files' own data (checked for MEDPIT specifically:
// searched the raw file for its circle's coordinates, as both plain int16
// and the VDU chunk's own ×256 fixed-point encoding — no match at all;
// MEDPIT.IFF's own FRNT INST record for id 24 gives (220,130) in SVGA
// space, confirmed simply wrong for this purpose by the mismatch with the
// user's real coordinates). EXCPIT's real cockpit_name was confirmed via
// MISNB002.IFF's own COCK chunk (the mission that actually flies the
// Excalibur) — NOT "excalpit". Keyed by cockpit name (RSEntity::
// cockpit_name, already upper-cased at parse time); falls through to the
// (demonstrated unreliable for MEDPIT, but only other option available)
// raw INST-record lookup only if a 6th cockpit variant with no override
// ever turns up — all 5 known WC3 cockpits now have confirmed overrides.
bool SCCockpit::GetCompassAreaRect(bool isSVGA, Point2D &topLeft, Point2D &bottomRight) {
    if (this->player_plane != nullptr && this->player_plane->object != nullptr &&
        this->player_plane->object->entity != nullptr) {
        auto overrideIt = kCompassAreaOverrideByCockpit.find(this->player_plane->object->entity->cockpit_name);
        if (overrideIt != kCompassAreaOverrideByCockpit.end()) {
            topLeft = overrideIt->second.topLeft;
            bottomRight = overrideIt->second.bottomRight;
            return true;
        }
    }
    const auto& layouts = isSVGA ? this->cockpit->svgaFrontInstruments : this->cockpit->vgaFrontInstruments;
    const RSCockpit::WC3VDULayout& vdu = isSVGA ? this->cockpit->svgaFrontVDU : this->cockpit->vgaFrontVDU;
    for (auto& rec : layouts) {
        if (rec.shapeId != WC3CockpitShapeId::kCompassDialMedium) {
            continue;
        }
        Point2D pos;
        if ((rec.mode >> 8) == 0xff) {
            pos = {rec.x, rec.y};
        } else if (vdu.valid) {
            pos = {(int16_t)(vdu.leftX + rec.x), (int16_t)(vdu.leftY + rec.y)};
        } else {
            continue;
        }
        // No real size known for the raw-INST fallback case — reuse the
        // generic MFD panel size as a best-effort guess.
        Point2D mfdSize = this->GetWC3MfdSize();
        topLeft = pos;
        bottomRight = {(int16_t)(pos.x + mfdSize.x), (int16_t)(pos.y + mfdSize.y)};
        return true;
    }
    return false;
}
Point2D SCCockpit::GetCompassAreaScreenPos(bool isSVGA, bool &found) {
    Point2D topLeft{}, bottomRight{};
    found = this->GetCompassAreaRect(isSVGA, topLeft, bottomRight);
    return topLeft;
}
// See this method's own header comment (SCCockpit.h) for the frame->
// component mapping.
void SCCockpit::RenderVDUDamageOverlay(FrameBuffer* fb, const RSCockpit::WC3VDULayout& vdu, bool isSVGA) {
    if (this->player_plane == nullptr || this->player_plane->pilot == nullptr) {
        return;
    }
    SCMissionActors *pilot = this->player_plane->pilot;
    uint32_t shapeId = isSVGA ? WC3CockpitShapeId::kCanopyDamageCracksSVGA
                               : WC3CockpitShapeId::kCanopyDamageCracks;
    auto it = this->cockpit->instrumentShapes.find(shapeId);
    if (it == this->cockpit->instrumentShapes.end() || it->second == nullptr) {
        return;
    }
    RSImageSet *imgset = it->second;
    auto drawFrame = [&](int frame, Point2D pos) {
        if ((size_t)frame >= imgset->GetNumImages()) {
            return;
        }
        RLEShape *shape = imgset->GetShape(frame);
        if (shape == nullptr) {
            return;
        }
        shape->SetPosition(&pos);
        fb->drawShape(shape);
    };
    if (vdu.valid && pilot->component_damage[(size_t)ShipComponent::VDU1] > 0.0f) {
        drawFrame(0, {vdu.leftX, vdu.leftY});
    }
    if (vdu.valid && pilot->component_damage[(size_t)ShipComponent::VDU2] > 0.0f) {
        Point2D rightOrigin = vdu.GetRightOrigin(fb->width);
        drawFrame(1, rightOrigin);
    }
    if (pilot->component_damage[(size_t)ShipComponent::TacticalDisplay] > 0.0f) {
        bool found = false;
        Point2D compassPos = this->GetCompassAreaScreenPos(isSVGA, found);
        if (found) {
            drawFrame(2, compassPos);
        }
    }
}
void SCCockpit::RenderWC3InstrumentsSVGA(FrameBuffer* fb, bool useHud) {
    // In SVGA mode the framebuffer is 640x480, so SVGA INST positions (also
    // in 640x480 space) map directly — no scaling or GL quad path needed.
    //
    // Mode high byte 0xff = screen-absolute cockpit overlay; draw at INST (x,y).
    // Other mode high bytes (0x00–0x06) are MFD-relative: their (x,y) values
    // are offsets within the VDU sub-panel, not the main framebuffer.

    // kBarMeterFillA (19/0x13) = fuel gauge, kBarMeterFillB (20/0x14) =
    // weapon/gun energy gauge — user-confirmed identities (2026-07
    // session; RenderShieldGauge's own earlier "kBarMeterFillA = shield"
    // assumption was wrong) and direction: the LAST frame is the full
    // gauge, not frame 0 as originally guessed. kBarMeterFillSmallA/B are
    // a separate, still-unconfirmed pair (not a VGA/SVGA twin of A/B —
    // see their own declaration comment), left pinned to frame 0.
    //
    // kAutopilotLabel/kAutopilotLabelSmall (AUTO) and kLockLabel/
    // kLockLabelSmall (LOCK) are 2-frame dark/lit status-light toggles —
    // same "was being time-cycled instead of state-driven" bug, this time
    // reported directly (both lights blinking constantly rather than
    // reflecting real autopilot-available/missile-lock state).
    auto selectStateFrame = [&](uint32_t shapeId) -> int {
        using namespace WC3CockpitShapeId;
        if (shapeId == kBarMeterFillA || shapeId == kBarMeterFillB) {
            auto gIt = this->cockpit->instrumentShapes.find(shapeId);
            size_t numFrames = (gIt != this->cockpit->instrumentShapes.end() && gIt->second != nullptr)
                                    ? gIt->second->GetNumImages() : 0;
            if (numFrames == 0) return 0;
            float fraction = 1.0f;
            if (shapeId == kBarMeterFillA) {
                // GetFuelCapacity(), not a hardcoded 12800 (user-reported,
                // 2026-07 session: gauge didn't start full) — WC3 planes
                // are constructed with a real per-ship capacity (RSEntity::
                // jdyn->FUEL), not the fixed value the old SCPlane::fuel
                // field comment describes; see SCPlane::fuel_max's own
                // comment.
                if (this->player_plane != nullptr && this->player_plane->GetFuelCapacity() > 0.0f) {
                    fraction = this->player_plane->GetFuel() / this->player_plane->GetFuelCapacity();
                }
            } else if (this->player_plane != nullptr && !this->player_plane->weaps_load.empty() &&
                       this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
                auto *wdat = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat;
                if (wdat != nullptr && wdat->weapon_category == 0 && this->player_plane->object->entity->gun_energy_capacity > 0.0f) {
                    fraction = this->player_plane->GetCurrentGunEnergy() / this->player_plane->object->entity->gun_energy_capacity;
                }
            }
            if (fraction < 0.0f) fraction = 0.0f;
            if (fraction > 1.0f) fraction = 1.0f;
            return (int)(fraction * (float)(numFrames - 1) + 0.5f);
        }
        if (shapeId == kBarMeterFillSmallA || shapeId == kBarMeterFillSmallB) {
            return 0;
        }
        if (shapeId == kAutopilotLabel || shapeId == kAutopilotLabelSmall) {
            return this->autopilot_available ? 1 : 0;
        }
        if (shapeId == kLockLabel || shapeId == kLockLabelSmall) {
            return this->target_locked ? 1 : 0;
        }
        return -1;
    };
    auto drawShape = [&](uint32_t shapeId, RSImageSet* imgset, Point2D pos) {
        if (imgset == nullptr || imgset->GetNumImages() == 0) return;
        // Throttle hand — user-confirmed (2026-07 session): kThrottleHandAnimation
        // (id 12, NOT kPilotHandAnimation id 11/the joystick hand, which
        // moves with joystick input instead) represents the throttle
        // position, not a real looping animation. Frame-select by
        // this->throttle (0-100) instead of wc3_anim_frame, and pin the
        // shape's own bottom edge (position.y + botDist, same
        // registration-point convention every other per-frame-sized shape
        // this session uses) to the framebuffer's bottom-middle —
        // otherwise different-height throttle poses shift the hand
        // up/down as the frame changes instead of keeping its base
        // planted, and rec.x/rec.y (tuned for one specific frame) don't
        // track that.
        if (shapeId == WC3CockpitShapeId::kThrottleHandAnimation) {
            size_t n = imgset->GetNumImages();
            float frac = (std::max)(0.0f, (std::min)(1.0f, this->throttle / 100.0f));
            int frame = (int)(frac * (float)(n > 1 ? n - 1 : 0) + 0.5f);
            RLEShape* shape = imgset->GetShape(frame);
            if (shape == nullptr) return;
            Point2D handPos = {(int16_t)(fb->width / 2), (int16_t)(fb->height - shape->botDist)};
            shape->SetPosition(&handPos);
            fb->drawShape(shape);
            return;
        }
        // Joystick hand (id 11, kPilotHandAnimation) — user-confirmed
        // (2026-07 session) position is correct via the generic path's
        // GetOriginalCanvasOffset fix, but frame selection needs to track
        // real joystick/keyboard input instead of looping via
        // wc3_anim_frame. MEDPIT.IFF's real 11 frames (byte-verified):
        // frames 0-6 form a clean, monotonic-in-y_min sweep at a roughly
        // constant x (a pitch axis), while frames 7/10 sit well right/left
        // of that cluster's x range (the two most extreme, unambiguous
        // roll poses). Frames 8-9 fall between those without a confident
        // reading of what they represent, so they're deliberately not
        // used here rather than guessed. control_stick_x/y (SCPlane) are
        // the real raw stick axes read by SCJdynPlane::processInput,
        // whose own kStickFullDeflection=200 is reused here as the "full
        // deflection" normalization basis; direction sign (which end of
        // the pitch sweep is "stick back" vs "stick forward") is an
        // unconfirmed guess pending live-visual verification.
        if (shapeId == WC3CockpitShapeId::kPilotHandAnimation) {
            size_t n = imgset->GetNumImages();
            int frame = 0;
            if (n >= 11 && this->player_plane != nullptr) {
                constexpr float kStickFullDeflection = 200.0f;
                float normPitch = (std::max)(-1.0f, (std::min)(1.0f, this->player_plane->control_stick_y / kStickFullDeflection));
                float normRoll = (std::max)(-1.0f, (std::min)(1.0f, this->player_plane->control_stick_x / kStickFullDeflection));
                if (fabsf(normRoll) > 0.5f && fabsf(normRoll) > fabsf(normPitch)) {
                    frame = normRoll > 0.0f ? 7 : 10;
                } else {
                    frame = (int)((normPitch + 1.0f) * 0.5f * 6.0f + 0.5f);
                    if (frame < 0) frame = 0;
                    if (frame > 6) frame = 6;
                }
            }
            RLEShape* shape = imgset->GetShape(frame);
            if (shape == nullptr) return;
            Point2D offset = GetOriginalCanvasOffset(shape);
            Point2D handPos = {(int16_t)(pos.x + offset.x), (int16_t)(pos.y + offset.y)};
            shape->SetPosition(&handPos);
            fb->drawShape(shape);
            return;
        }
        // kCenterMarker (id 5) — user-confirmed (2026-07 session): should
        // sit on the real dead-ahead boresight (see GetWC3BoresightScreenPos's
        // own comment for why that's not simply the framebuffer's raw
        // center), same reference point as kTargetingReticle's (id 6)
        // lead marker, not at whatever position its own file INST record
        // supplies (off-center in practice).
        if (shapeId == WC3CockpitShapeId::kCenterMarker) {
            RLEShape* shape = imgset->GetShape(wc3_anim_frame % imgset->GetNumImages());
            if (shape == nullptr) return;
            Point2D centerPos = this->GetWC3BoresightScreenPos(fb);
            shape->SetPosition(&centerPos);
            fb->drawShape(shape);
            return;
        }
        // kTargetingReticle/kTargetingReticleSVGA (id 6/36) — drawn by
        // RenderWC3TargetingReticle instead (its 4 frames are 4 distinct,
        // conditionally-shown indicators, not a real animation — see that
        // function's own comment), so skip it here rather than also
        // cycling it via wc3_anim_frame like every other unhandled id. Was
        // comparing only against the VGA id (6) even in this SVGA-only
        // path, so a cockpit whose SVGA INST records reference id 36
        // (bug fix, 2026-07 session, same root cause as
        // RenderWC3TargetingReticle's own lookup fix) fell through to the
        // generic static-instrument draw below instead of being skipped —
        // a stray reticle frozen at its authored layout position, on top
        // of (or instead of) the dynamic one.
        if (shapeId == WC3CockpitShapeId::kTargetingReticle || shapeId == WC3CockpitShapeId::kTargetingReticleSVGA) {
            return;
        }
        // kSidePanel (id 53) — user-reported (2026-07 session): "extra
        // 0x35 on the comms display". RenderMFDSCommWC3 already draws this
        // exact shape explicitly as its own backdrop (real per-frame size,
        // bounded RenderMFDS clear immediately before it), so also letting
        // it through this generic per-mode loop (mode 0x04 = Comm, this
        // shape's own real INST record) draws it a second time first —
        // and if kSidePanel's real footprint is larger than the MFD
        // panel's own declared size, RenderMFDSCommWC3's own clear+redraw
        // doesn't fully cover that first draw, leaving a visible duplicate
        // fringe around the edges. Skip it here; the dedicated function
        // owns it.
        if (shapeId == WC3CockpitShapeId::kSidePanel) {
            return;
        }
        // kPilotHandAnimation2 (id 13) — user-reported (2026-07 session):
        // "shape 0x0D is being shown and is cycling locations... It should
        // not." This generic per-mode loop was cycling it via
        // wc3_anim_frame like a real animation, but it's actually
        // RenderMFDSDamage's page-2 ship-damage graphic (see its own
        // comment) — state-driven by real armor data, not time. Skip it
        // here; the dedicated function owns it.
        if (shapeId == WC3CockpitShapeId::kPilotHandAnimation2) {
            return;
        }
        // kSystemStatusIcons (id 14) — user-reported (2026-07 session):
        // "one more MFD ... looping through the different weapon MFD
        // icons." RenderMFDSWeapon already draws this explicitly, one
        // real icon per armed hardpoint at its own real WEAP-chunk anchor
        // (see its own comment) — this generic per-mode loop was also
        // cycling the whole 38-frame set via wc3_anim_frame wherever its
        // own INST record placed it, on some other page entirely. Skip it
        // here; the dedicated function owns it.
        if (shapeId == WC3CockpitShapeId::kSystemStatusIcons) {
            return;
        }
        // kCompassDialMedium (id 24) — user-confirmed (2026-07 session):
        // not always-on background art (its "compass/radar dial" look was
        // an unconfirmed visual guess), but a front-quadrant-damage
        // indicator, only meant to appear once the player's own front
        // shield/armor has taken damage. Its own INST position is also
        // reused as the Tactical Display/radar-scope screen location for
        // RenderVDUDamageOverlay's frame-2 (radar damage) overlay — see
        // that function's own header comment.
        if (shapeId == WC3CockpitShapeId::kCompassDialMedium && !this->IsPlayerFrontQuadrantDamaged()) {
            return;
        }
        int stateFrame = selectStateFrame(shapeId);
        int frame = stateFrame >= 0 ? stateFrame : (wc3_anim_frame % imgset->GetNumImages());
        RLEShape* shape = imgset->GetShape(frame);
        if (shape == nullptr) return;
        // kPilotHandAnimation (id 11, the joystick hand — 2026-07 session,
        // user-prompted "fix throttle and joystick hand, same issue"):
        // its real frames (byte-verified against MEDPIT.IFF) each carry a
        // genuine x_blit/y_min offset that varies non-monotonically both
        // horizontally and vertically frame to frame — the hand visibly
        // shifting around the stick as it grips different tilt positions,
        // not noise — same "real per-frame placement data, not optional"
        // finding as every other shape fixed this session. The old
        // `bx<0&&by<0`-only guard here also read shape->position directly
        // without the GetOriginalCanvasOffset cache, so after the first
        // frame it was reading back an already-absolute screen position
        // from the previous draw instead of the original file offset —
        // broken for any repeatedly-drawn shape on this generic path, not
        // just id 11.
        Point2D offset = GetOriginalCanvasOffset(shape);
        pos.x += offset.x;
        pos.y += offset.y;
        shape->SetPosition(&pos);
        fb->drawShape(shape);
    };

    // vdu.leftShapeId/rightShapeId are NOT VDU screen background art — live
    // testing found the left MFD showing a correctly-positioned red radar
    // grid + bar gauges (built from the MFD-relative INST records handled
    // below) with no visible "background shape" at all, so this was
    // dumped and checked: for MEDPIT.IFF, leftShapeId=25/rightShapeId=26
    // exactly match WC3CockpitShapeId::kCanopyDamageCracks (126x109, 3
    // frames) / kCanopyDamageCracksAlt (154x117, 5 frames) — confirmed by
    // frame count and per-frame width/height, not just id number. These
    // two fields are the canopy-damage-crack shape ids, not a VDU
    // background pair; the leftX/leftY they're drawn at is coincidentally
    // the same coordinate the MFD panel also uses. Previously this drew
    // id 25 there unconditionally (every frame, regardless of actual
    // canopy damage) and time-cycled it — a real contributor to the
    // "gauges cycling constantly" report. Not drawing canopy damage
    // anywhere yet is a real gap (DETH/EJCT's sibling "show cracks as the
    // canopy takes damage" feature), but drawing crack art mislabeled as
    // an MFD background was actively wrong, so removed rather than left in
    // place. vdu.combinedShapeId is left alone below — always -1 on every
    // cockpit file checked so far, never actually exercised, but unclear
    // whether the same mislabeling applies there too.
    const RSCockpit::WC3VDULayout& vdu = useHud ? cockpit->svgaHudVDU : cockpit->svgaFrontVDU;
    if (vdu.valid && vdu.combinedShapeId >= 0) {
        auto it = cockpit->instrumentShapes.find((uint32_t)vdu.combinedShapeId);
        if (it != cockpit->instrumentShapes.end()) {
            drawShape(vdu.combinedShapeId, it->second, {vdu.leftX, vdu.leftY});
        }
    }

    // MFD-relative records' mode high byte is a real page/group id (0x00-
    // 0x06, 0x08 — 0x07 never appears; SYS's own slot-order byte uses this
    // exact same 8-value set {0,1,2,3,4,5,6,8}, strong cross-chunk evidence
    // it's one shared "page" concept, not two coincidentally-similar
    // encodings). Page->key mapping, by confidence:
    //   0x00 -> Shields: user-confirmed real page. Shape 59
    //     (kRadarScreenGridSVGA — a generic grid-style panel background, not
    //     exclusively "radar" as its name implies) is the real backdrop;
    //     corroborated by SYS's own slot 0 being the real "Shields"
    //     subsystem id (17) at the same page number. See RenderMFDSShield.
    //   0x02 -> Radar: shares the same shape 59 grid backdrop as Shields,
    //     plus shape 38 (close match for kTargetLockBoxSVGA), consistent
    //     with a target-lock overlay drawn only in single-target radar
    //     submode.
    //   0x04 -> Comm: shape 53 (kSidePanel). Corroborated independently by
    //     SYS's own page-4 slot being the real "Comm" subsystem id (16) —
    //     two unrelated chunks agreeing. High confidence.
    //   0x06 -> Power: confirmed separately, see RenderMFDSPower.
    //   0x03 (shape 44, kSystemStatusIconsSVGA, 38 frames) and 0x05 (shape
    //     43, kPilotHandAnimation2SVGA, 12 frames) have no corroborating
    //     second source and no clearly-matching show_* page — left in the
    //     idle/default bucket (drawn only when no dedicated page is
    //     selected) rather than guessed onto Damage/Weapon/Camera and risk
    //     showing plausible-looking but wrong art on the wrong page.
    // This loop used to draw every MFD-relative record unconditionally,
    // every frame, regardless of which (if any) page was active — so
    // pressing e.g. 'p' got the real power bezels from RenderMFDSPower PLUS
    // every other page's instruments all stacked in the same small panel.
    // That's the reported "MFD sprites drawn over each other" bug.
    bool leftMfdPageActive = this->show_radars || this->show_weapons || this->show_damage ||
                              this->show_comm || this->show_cam || this->show_power || this->show_shield;
    auto pageVisible = [&](uint16_t mode) -> bool {
        uint8_t page = (uint8_t)(mode >> 8);
        if (page == 0x00) return this->show_shield;
        // Radar page content no longer draws on the left VDU at all — see
        // the show_radars call site's own comment (RenderMFDSRadar now
        // draws at GetCompassAreaScreenPos instead). leftMfdPageActive
        // still includes show_radars so the idle/default bucket below
        // correctly stays hidden while radar mode is active.
        if (page == 0x02) return false;
        if (page == 0x04) return this->show_comm;
        if (page == 0x06) return this->show_power;
        return !leftMfdPageActive;
    };
    auto& layouts = useHud ? cockpit->svgaHudInstruments : cockpit->svgaFrontInstruments;
    for (auto& rec : layouts) {
        auto it = cockpit->instrumentShapes.find(rec.shapeId);
        if (it == cockpit->instrumentShapes.end()) continue;
        if ((rec.mode >> 8) == 0xff) {
            // Screen-absolute — draw directly at INST (x,y). Always on:
            // these are fixed cockpit overlays (status lights, KPS/SET
            // readouts, ...), not left-MFD page content.
            Point2D pos = {rec.x, rec.y};
            drawShape(rec.shapeId, it->second, pos);
        } else if (vdu.valid && pageVisible(rec.mode)) {
            Point2D leftPos = {(int16_t)(vdu.leftX + rec.x),
                                (int16_t)(vdu.leftY + rec.y)};
            drawShape(rec.shapeId, it->second, leftPos);
        }
    }
    this->RenderVDUDamageOverlay(fb, vdu, /*isSVGA=*/true);

    // Draw KPS (current speed) and SET (throttle setting) text labels in
    // cockpit green. TEXT chunk records with id=0x01 (KPS) and id=0x02 (SET)
    // at mode=0xff carry the format string and screen position.
    // SET = target speed in KPS: 420 at 100% throttle (Hellcat), 1200 at
    // afterburner; sourced from RSEntity::thrust_in_newton / weight_in_kg
    // (despite those field names — see SCJdynPlane::computeThrust's comment).
    auto& textEntries = useHud ? cockpit->svgaHudText : cockpit->svgaFrontText;
    if (player_plane != nullptr && !textEntries.empty()) {
        // GFSM is a general chunky UI-menu font (also used for options/
        // quit-confirm/menu text elsewhere) — too tall/bold for a cockpit
        // digit readout and visibly the wrong style. globals.iff's FONT
        // form carries 9 fonts total; SLRG is used here (and for the
        // target-name/distance readout below) per user selection.
        WC3Font* font = WC3Globals::getInstance().getFont("SLRG");
        if (font != nullptr && font->isLoaded()) {
            // Find nearest palette index to WC3 cockpit green (R=0, G=200, B=50).
            static uint8_t cockpitGreenIdx = 0;
            static bool    cockpitGreenCached = false;
            if (!cockpitGreenCached) {
                cockpitGreenCached = true;
                int best = 1 << 30;
                for (int i = 0; i < 256; i++) {
                    const Texel* c = this->palette.GetRGBColor(i);
                    int dr = (int)c->r - 0, dg = (int)c->g - 200, db = (int)c->b - 50;
                    int d = dr*dr + dg*dg + db*db;
                    if (d < best) { best = d; cockpitGreenIdx = (uint8_t)i; }
                }
            }

            int kps_val = (int)(-player_plane->vz);
            if (kps_val < 0) kps_val = 0;

            int set_val = 0;
            if (player_plane->afterburner_engaged &&
                player_plane->object != nullptr &&
                player_plane->object->entity != nullptr) {
                set_val = player_plane->object->entity->weight_in_kg;
            } else {
                // max_throttle is always 100 (see SCPlane::init).
                set_val = (int)(player_plane->Mthrust * player_plane->GetThrottle() / 100);
            }

            char buf[32];
            for (const auto& entry : textEntries) {
                if (entry.mode != 0xff) continue;
                if (entry.id != 0x01 && entry.id != 0x02) continue;
                int val = (entry.id == 0x01) ? kps_val : set_val;
                snprintf(buf, sizeof(buf), entry.text, (long)val);
                font->drawTextColored(fb, buf, entry.x, entry.y, cockpitGreenIdx);
            }

            // Target name + distance readout. Real MEDPIT.IFF TEXT records:
            // id 0x08/0x0f ("%s", identical offset — only 0x08 is drawn, 0x0f
            // would just overlap it) and id 0x07 ("%03ld"), all mode=0x02 —
            // the same page value the FRNT>INST page-mapping comment above
            // identifies as "Radar" (shape 38/kTargetLockBoxSVGA, a
            // target-lock overlay). Never previously rendered: the mode!=0xff
            // filter above silently dropped every MFD-relative TEXT record.
            // Anchor is the right VDU's mirrored origin (GetRightOrigin), not
            // the left one the offsets are stored relative to — confirmed
            // against a live screenshot (computed (458,270)/(458,279) vs.
            // measured (463,271)/(463,280) for a targeted Darket at 4000
            // units), which also happens to put it on the opposite side of
            // the cockpit from the KPS:/SET: readout above.
            if (this->current_target_actor != nullptr && this->current_target != nullptr && vdu.valid) {
                // Real ship/pilot name comes from the target's own profile
                // (PROFILE\<cast_actor>.IFF, already loaded into
                // actor->profile by WC3Mission::buildActorFromPart for every
                // actor, not just named pilots) — PROF>RADI>INFO is a fixed
                // id(2)+name(15)+callsign(15) record, and the callsign (the
                // second entry) is the one actually used as a ship's proper
                // name: confirmed against real profiles, e.g.
                // PROFILE\TCRUISER.IFF is name="Bruiser"/callsign="Ajax" and
                // PROFILE\VICA1.IFF is name="Victory"/callsign="Victory".
                // Falls back to the class name/actor_name derivation below
                // only if no profile was loaded (LoadProfile can return
                // nullptr — not every cast actor has a profile file).
                std::string name;
                if (this->current_target_actor->profile != nullptr &&
                    !this->current_target_actor->profile->radi.info.callsign.empty()) {
                    name = this->current_target_actor->profile->radi.info.callsign;
                } else {
                    bool isEnemy = std::find(this->current_mission->enemies.begin(),
                                              this->current_mission->enemies.end(),
                                              this->current_target_actor) != this->current_mission->enemies.end();
                    name = (isEnemy && this->current_target_actor->object != nullptr)
                        ? this->current_target_actor->object->member_name
                        : this->current_target_actor->actor_name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (!name.empty()) {
                        name[0] = (char)::toupper((unsigned char)name[0]);
                    }
                }
                int distance = (int)(this->current_target->position - this->player_plane->position).Length();
                Point2D rightOrigin = vdu.GetRightOrigin(fb->width);
                for (const auto& entry : textEntries) {
                    if (entry.mode != 0x02) continue;
                    Point2D p = {rightOrigin.x + entry.x, rightOrigin.y + entry.y};
                    if (entry.id == 0x08) {
                        font->drawTextColored(fb, name.c_str(), p.x, p.y, cockpitGreenIdx);
                    } else if (entry.id == 0x07) {
                        snprintf(buf, sizeof(buf), entry.text, (long)distance);
                        font->drawTextColored(fb, buf, p.x, p.y, cockpitGreenIdx);
                    }
                }
            }
        }
    }
}

void SCCockpit::RenderWC3Instruments(FrameBuffer* fb, bool useHud) {
    constexpr float kAnimFPS = 10.0f;
    wc3_anim_time += GameTimer::getInstance().getDeltaTime();
    if (wc3_anim_time >= 1.0f / kAnimFPS) {
        wc3_anim_time -= 1.0f / kAnimFPS;
        wc3_anim_frame++;
    }

    const auto& svgaInstruments = useHud ? cockpit->svgaHudInstruments : cockpit->svgaFrontInstruments;
    if (!g_ifVGA && !svgaInstruments.empty()) {
        RenderWC3InstrumentsSVGA(fb, useHud);
        return;
    }

    auto& layouts = useHud ? cockpit->vgaHudInstruments : cockpit->vgaFrontInstruments;
    if (!layouts.empty()) {
        // Previously this loop always treated rec.x/rec.y as screen-
        // absolute, even for MFD-relative (non-0xff mode) records — those
        // are small local offsets (e.g. 6,8 or 16,32) meant to be added to
        // the VDU panel's own origin (see vgaFrontVDU, and
        // RenderWC3InstrumentsSVGA's matching comment), so they used to
        // render bunched up near the screen's top-left corner instead of
        // on the instrument panel. MFD-relative instruments draw on the
        // left panel only — see RenderWC3InstrumentsSVGA's matching comment
        // for why (right panel is dedicated to RenderMFDSTarget).
        const RSCockpit::WC3VDULayout& vdu = useHud ? cockpit->vgaHudVDU : cockpit->vgaFrontVDU;
        // See RenderWC3InstrumentsSVGA's selectStateFrame comment.
        auto selectStateFrame = [&](uint32_t shapeId) -> int {
            using namespace WC3CockpitShapeId;
            if (shapeId == kBarMeterFillA || shapeId == kBarMeterFillB) {
                auto gIt = this->cockpit->instrumentShapes.find(shapeId);
                size_t numFrames = (gIt != this->cockpit->instrumentShapes.end() && gIt->second != nullptr)
                                        ? gIt->second->GetNumImages() : 0;
                if (numFrames == 0) return 0;
                float fraction = 1.0f;
                if (shapeId == kBarMeterFillA) {
                    // See RenderWC3InstrumentsSVGA's matching comment.
                    if (this->player_plane != nullptr && this->player_plane->GetFuelCapacity() > 0.0f) {
                        fraction = this->player_plane->GetFuel() / this->player_plane->GetFuelCapacity();
                    }
                } else if (this->player_plane != nullptr && !this->player_plane->weaps_load.empty() &&
                           this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
                    auto *wdat = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat;
                    if (wdat != nullptr && wdat->weapon_category == 0 && this->player_plane->object->entity->gun_energy_capacity > 0.0f) {
                        fraction = this->player_plane->GetCurrentGunEnergy() / this->player_plane->object->entity->gun_energy_capacity;
                    }
                }
                if (fraction < 0.0f) fraction = 0.0f;
                if (fraction > 1.0f) fraction = 1.0f;
                return (int)(fraction * (float)(numFrames - 1) + 0.5f);
            }
            if (shapeId == kBarMeterFillSmallA || shapeId == kBarMeterFillSmallB) {
                return 0;
            }
            if (shapeId == kAutopilotLabel || shapeId == kAutopilotLabelSmall) {
                return this->autopilot_available ? 1 : 0;
            }
            if (shapeId == kLockLabel || shapeId == kLockLabelSmall) {
                return this->target_locked ? 1 : 0;
            }
            return -1;
        };
        // See RenderWC3InstrumentsSVGA's matching drawShape comment (id 11,
        // the joystick hand): real per-frame x_blit/y_min offsets, and the
        // old bx<0&&by<0 guard read shape->position directly, so it was
        // reading back an already-absolute screen position after the first
        // draw instead of the file's original offset.
        auto drawAt = [&](RLEShape* shape, int16_t x, int16_t y) {
            Point2D offset = GetOriginalCanvasOffset(shape);
            Point2D pos = {(int16_t)(x + offset.x), (int16_t)(y + offset.y)};
            shape->SetPosition(&pos);
            fb->drawShape(shape);
        };
        // MFD-relative records (mode high byte != 0xff) belong to a real
        // page/group — see RenderWC3InstrumentsSVGA's matching comment for
        // the cross-chunk evidence and the confirmed/best-effort/unmapped
        // page->key assignments (0x00->Shields, 0x02->Radar, 0x04->Comm,
        // 0x06->Power, 0x03/0x05 left in the idle/default bucket). Drawing
        // all of them unconditionally, every frame, on top of whichever
        // dedicated page renderer was also active is what caused every
        // left-MFD sprite to visibly stack.
        bool leftMfdPageActive = this->show_radars || this->show_weapons || this->show_damage ||
                                  this->show_comm || this->show_cam || this->show_power || this->show_shield;
        auto pageVisible = [&](uint16_t mode) -> bool {
            uint8_t page = (uint8_t)(mode >> 8);
            if (page == 0x00) return this->show_shield;
            // Radar page content no longer draws on the left VDU at all — see
        // the show_radars call site's own comment (RenderMFDSRadar now
        // draws at GetCompassAreaScreenPos instead). leftMfdPageActive
        // still includes show_radars so the idle/default bucket below
        // correctly stays hidden while radar mode is active.
        if (page == 0x02) return false;
            if (page == 0x04) return this->show_comm;
            if (page == 0x06) return this->show_power;
            return !leftMfdPageActive;
        };
        for (auto& rec : layouts) {
            auto it = cockpit->instrumentShapes.find(rec.shapeId);
            if (it == cockpit->instrumentShapes.end()) continue;
            RSImageSet* imgset = it->second;
            if (imgset == nullptr || imgset->GetNumImages() == 0) continue;
            bool isAbsolute = (rec.mode >> 8) == 0xff || !vdu.valid;
            if (!isAbsolute && !pageVisible(rec.mode)) continue;
            // Throttle hand — see the SVGA path's matching comment
            // (RenderWC3InstrumentsSVGA's drawShape): kThrottleHandAnimation
            // (id 12, NOT kPilotHandAnimation/id 11 — that's the joystick
            // hand, driven by joystick input instead). Frame-select by
            // this->throttle instead of looping, anchor to the
            // framebuffer's bottom-middle via the shape's own botDist
            // instead of the file's fixed rec.x/rec.y.
            if (rec.shapeId == WC3CockpitShapeId::kThrottleHandAnimation) {
                size_t n = imgset->GetNumImages();
                float frac = (std::max)(0.0f, (std::min)(1.0f, this->throttle / 100.0f));
                int frame = (int)(frac * (float)(n > 1 ? n - 1 : 0) + 0.5f);
                RLEShape* shape = imgset->GetShape(frame);
                if (shape == nullptr) continue;
                Point2D handPos = {(int16_t)(fb->width / 2), (int16_t)(fb->height - shape->botDist)};
                shape->SetPosition(&handPos);
                fb->drawShape(shape);
                continue;
            }
            // kCenterMarker (id 5) — see the SVGA path's matching comment:
            // should sit on the real dead-ahead boresight, same reference
            // point as kTargetingReticle's (id 6) lead marker, not at
            // whatever position its own file INST record supplies
            // (off-center in practice).
            if (rec.shapeId == WC3CockpitShapeId::kCenterMarker) {
                RLEShape* shape = imgset->GetShape(wc3_anim_frame % imgset->GetNumImages());
                if (shape == nullptr) continue;
                Point2D centerPos = this->GetWC3BoresightScreenPos(fb);
                shape->SetPosition(&centerPos);
                fb->drawShape(shape);
                continue;
            }
            // kTargetingReticle (id 6) — see the SVGA path's matching
            // comment: drawn by RenderWC3TargetingReticle instead, skip
            // it here.
            if (rec.shapeId == WC3CockpitShapeId::kTargetingReticle) {
                continue;
            }
            // kSidePanel (id 53) — see the SVGA path's matching comment:
            // RenderMFDSCommWC3 already draws this as its own Comm-page
            // backdrop; skip it here to avoid the reported duplicate.
            if (rec.shapeId == WC3CockpitShapeId::kSidePanel) {
                continue;
            }
            // kPilotHandAnimation2 (id 13) — see the SVGA path's matching
            // comment: RenderMFDSDamage's page-2 ship-damage graphic owns
            // this shape now, not a looping animation.
            if (rec.shapeId == WC3CockpitShapeId::kPilotHandAnimation2) {
                continue;
            }
            // kSystemStatusIcons (id 14) — see the SVGA path's matching
            // comment: RenderMFDSWeapon owns this shape (one real icon
            // per armed hardpoint), not a looping animation.
            if (rec.shapeId == WC3CockpitShapeId::kSystemStatusIcons) {
                continue;
            }
            // kCompassDialMedium (id 24) — see the SVGA path's matching
            // comment (RenderWC3InstrumentsSVGA's drawShape): a front-
            // quadrant-damage indicator, not always-on background art.
            if (rec.shapeId == WC3CockpitShapeId::kCompassDialMedium && !this->IsPlayerFrontQuadrantDamaged()) {
                continue;
            }
            int stateFrame = selectStateFrame(rec.shapeId);
            int frame = stateFrame >= 0 ? stateFrame : (wc3_anim_frame % imgset->GetNumImages());
            RLEShape* shape = imgset->GetShape(frame);
            if (shape == nullptr) continue;
            if (isAbsolute) {
                drawAt(shape, rec.x, rec.y);
            } else {
                drawAt(shape, (int16_t)(vdu.leftX + rec.x), (int16_t)(vdu.leftY + rec.y));
            }
        }
        this->RenderVDUDamageOverlay(fb, vdu, /*isSVGA=*/false);
    } else {
        for (auto& [id, imgset] : cockpit->instrumentShapes) {
            if (imgset == nullptr || imgset->GetNumImages() == 0) continue;
            RLEShape* shape0 = imgset->GetShape(0);
            if (shape0 == nullptr) continue;
            int w = shape0->GetWidth(), h = shape0->GetHeight();
            if ((w == 320 && h == 200) || (w == 640 && h == 480)) continue;
            int frame = wc3_anim_frame % imgset->GetNumImages();
            fb->drawShape(imgset->GetShape(frame));
        }
    }
}

void SCCockpit::RenderHighResBackground() {
    RLEShape *shape = this->cockpit->ARTP_SVGA.GetShape(0);
    if (shape == nullptr) {
        return;
    }
    if (hires_bg_texture == nullptr) {
        int w = shape->GetWidth();
        int h = shape->GetHeight();
        std::vector<uint8_t> indexBuf((size_t)w * h, 255);
        // Same convention FrameBuffer::drawShape uses to pull raw indexed
        // pixels out of an RLEShape built via InitFromPixels.
        shape->buffer_size.x = w;
        shape->buffer_size.y = h;
        size_t byteRead = 0;
        shape->Expand(indexBuf.data(), &byteRead);

        // Local copy, not this->palette directly (same reasoning as
        // RenderCockpitOverlayShape): the windshield area of this art is
        // painted with palette index 255, meant as a transparent cutout
        // that reveals the real-time 3D scene rendered underneath it this
        // same frame (renderScene() draws the world/actors before calling
        // cockpit->Render(), which reaches here) — same convention
        // RSVGA::vSync uses for the low-res instrument overlay. Without
        // this override, index 255 rendered fully opaque (this->palette's
        // colors are loaded with alpha=255 — see VGAPalette::ReadPatch in
        // Texture.cpp), painting over the whole 3D world with a solid
        // color and leaving only the cockpit and static background
        // visible, confirmed by live testing.
        VGAPalette bgPalette = this->palette;
        bgPalette.colors[255].a = 0;

        RSImage img;
        img.width = w;
        img.height = h;
        img.data = indexBuf.data();
        img.palette = &bgPalette;
        img.flags = 0;

        hires_bg_texture = new Texture();
        hires_bg_texture->set(&img);
        hires_bg_texture->updateContent(&img);
        // RSImage::~RSImage() frees `data`, but img.data merely points into
        // indexBuf's vector-owned buffer — never something RSImage owns.
        // Detach it here so ~RSImage() is a no-op instead of a double free
        // when indexBuf's own destructor releases the same pointer.
        img.data = nullptr;
        hires_bg_w = w;
        hires_bg_h = h;
    }

    // Same 4:3-letterboxed viewport convention RSVGA::displayBuffer uses for
    // the 320x200 overlay, so this hi-res background aligns with the
    // HUD/MFD/instrument layer composited on top of it afterward.
    int winW = VGA.GetWindowWidth();
    int winH = VGA.GetWindowHeight();
    int w = (int)((float)winH * (4.0f / 3.0f));
    int x = (winW - w) / 2;
    glViewport(x, 0, w, winH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, hires_bg_w, 0, hires_bg_h, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (!hires_bg_uploaded) {
        glGenTextures(1, &hires_bg_gl_id);
        glBindTexture(GL_TEXTURE_2D, hires_bg_gl_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hires_bg_w, hires_bg_h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     hires_bg_texture->data);
        hires_bg_uploaded = true;
    } else {
        glBindTexture(GL_TEXTURE_2D, hires_bg_gl_id);
    }

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    gb.color4f(1.0f, 1.0f, 1.0f, 1.0f);

    gb.begin(GL_QUADS);
    gb.texCoord2f(0, 1);
    gb.vertex2d(0, 0);
    gb.texCoord2f(1, 1);
    gb.vertex2d(hires_bg_w, 0);
    gb.texCoord2f(1, 0);
    gb.vertex2d(hires_bg_w, hires_bg_h);
    gb.texCoord2f(0, 0);
    gb.vertex2d(0, hires_bg_h);
    gb.end();

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void SCCockpit::RenderMFDSDamage(Point2D pmfd_left, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    this->RenderMFDS(pmfd_left, fb);
    if (this->cockpit->instrumentShapes.empty()) {
        Point2D damage_pos = {pmfd_left.x +25, pmfd_left.y +15};
        RLEShape *damage_shape = this->cockpit->MONI.MFDS.DAMG.ARTS.GetShape(0);
        damage_shape->SetPosition(&damage_pos);
        fb->drawShape(damage_shape);
        return;
    }
    if (this->damage_page == 0) {
        // Page 0: per-subsystem text list (user-described, 2026-07
        // session). COCK>FRNT>SYS gives the real on-screen slot order +
        // subsystem id per row (see RSCockpit::WC3DamageSubsystemSlot for
        // what's confirmed vs. still unconfirmed about its 10-byte record
        // layout), cross-referenced against COCK>FRNT>DAMG>TEXT for the
        // real subsystem name per id. Row *positions* here are computed
        // (evenly spaced bottom-up), not file-derived — no chunk records
        // individual row coordinates, only the label list and the slot
        // order. No per-subsystem damage *state* is tracked anywhere in
        // the simulation either (SCMissionActors only has aggregate
        // shield_front/back/left/right + overall health, no per-subsystem
        // hit points) — so this lists the real subsystem names in their
        // real order but can't yet highlight which one is damaged.
        const auto &slots = g_ifVGA ? this->cockpit->vgaFrontSys : this->cockpit->svgaFrontSys;
        const auto &labels = g_ifVGA ? this->cockpit->vgaFrontDamageLabels : this->cockpit->svgaFrontDamageLabels;
        if (!slots.empty() && !labels.empty() && this->font != nullptr) {
            std::vector<RSCockpit::WC3DamageSubsystemSlot> ordered(slots.begin(), slots.end());
            std::sort(ordered.begin(), ordered.end(), [](const RSCockpit::WC3DamageSubsystemSlot &a, const RSCockpit::WC3DamageSubsystemSlot &b) {
                return a.slotOrder < b.slotOrder;
            });
            Point2D mfdSize = this->GetWC3MfdSize();
            const int rowHeight = 8;
            int startY = pmfd_left.y + mfdSize.y - (int)(ordered.size() * rowHeight) - 4;
            if (startY < pmfd_left.y + 20) startY = pmfd_left.y + 20;
            int row = 0;
            for (const auto &slot : ordered) {
                if (slot.subsystemId == 0xFF) continue;
                const char *name = nullptr;
                for (const auto &label : labels) {
                    if (label.id == slot.subsystemId) {
                        name = label.label;
                        break;
                    }
                }
                if (name == nullptr) continue;
                Point2D textPos = {pmfd_left.x + 4, startY + row * rowHeight};
                fb->printText(this->font, textPos, std::string(name), 0);
                row++;
            }
        }
        return;
    }

    // Page 1: top-down ship graphic with per-quadrant yellow/red damage
    // highlighting (user-described, 2026-07 session). COCK>SHAP id 13
    // (WC3CockpitShapeId::kPilotHandAnimation2 — a misnomer, not a hand
    // animation at all: byte-verified against MEDPIT.IFF, its 12 frames
    // share one 89x81 canvas, same as TARGSHAP's ship-diagram canvas).
    // Frame 0 alone spans a large chunk of the canvas at a position
    // distinct from every other frame — the base/undamaged ship outline,
    // always drawn (user-confirmed: "the first frame is full health").
    // Frames 1-11 form 4 position-identical clusters (front 1-3, right
    // 4-6, back 7-9, left 10-11 — asymmetric in the real data, not a
    // parsing bug) that only differ in pixel content, i.e. baked color,
    // not position — consistent with per-quadrant damage overlays layered
    // on top of frame 0. Only 2 real states were described (yellow/mild,
    // red/heavy), so this uses each cluster's first two frames as
    // mild/heavy and leaves any 3rd frame unused — an unconfirmed guess
    // pending live-visual verification, same as every other
    // frame-semantics call this session flagged as best-effort.
    auto shapeIt = this->cockpit->instrumentShapes.find(WC3CockpitShapeId::kPilotHandAnimation2);
    if (shapeIt == this->cockpit->instrumentShapes.end() || shapeIt->second == nullptr ||
        shapeIt->second->GetNumImages() == 0) {
        return;
    }
    RSImageSet *diagram = shapeIt->second;
    Point2D mfdSize = this->GetWC3MfdSize();
    Point2D center = {pmfd_left.x + mfdSize.x / 2, pmfd_left.y + mfdSize.y / 2};
    RLEShape *baseShape = diagram->GetShape(0);
    if (baseShape == nullptr) {
        return;
    }
    Point2D baseOffset = GetOriginalCanvasOffset(baseShape);
    Point2D basePos = {center.x - baseShape->GetWidth() / 2 + baseOffset.x,
                        center.y - baseShape->GetHeight() / 2 + baseOffset.y};
    baseShape->SetPosition(&basePos);
    fb->drawShape(baseShape);
    if (this->player_plane == nullptr || this->player_plane->pilot == nullptr) {
        return;
    }
    SCMissionActors *pilot = this->player_plane->pilot;
    struct QuadrantDamage {
        float current, maxVal;
        uint32_t frameBase;
    };
    QuadrantDamage quadrants[4] = {
        {pilot->armor_front, pilot->max_armor_front, 1},  // top/front
        {pilot->armor_right, pilot->max_armor_right, 4},  // right
        {pilot->armor_back, pilot->max_armor_back, 7},    // bottom/back
        {pilot->armor_left, pilot->max_armor_left, 10},   // left
    };
    for (int i = 0; i < 4; i++) {
        if (quadrants[i].maxVal <= 0.0f || quadrants[i].current >= quadrants[i].maxVal) {
            continue;
        }
        float frac = quadrants[i].current / quadrants[i].maxVal;
        if (frac < 0.0f) frac = 0.0f;
        // 0 = mild/yellow, 1 = heavy/red.
        uint32_t tier = frac > 0.5f ? 0 : 1;
        RLEShape *quadShape = diagram->GetShape(quadrants[i].frameBase + tier);
        if (quadShape == nullptr) {
            continue;
        }
        Point2D offset = GetOriginalCanvasOffset(quadShape);
        Point2D pos = {center.x - quadShape->GetWidth() / 2 + offset.x,
                        center.y - quadShape->GetHeight() / 2 + offset.y};
        quadShape->SetPosition(&pos);
        fb->drawShape(quadShape);
    }
}

/**
 * Render the cockpit in its current state.
 *
 * If the face number is non-negative, renders the cockpit in its 2D
 * representation using the VGA graphics mode. Otherwise, renders the
 * cockpit in its 3D representation using the OpenGL graphics mode.
 *
 * @param face The face number of the cockpit to render, or -1 to render
 * in 3D.
 */
void SCCockpit::Render(CockpitFace face) {
    this->face = face;
    FrameBuffer *fb{nullptr};
    bool upscale = false;

    VGA.activate();
    VGA.setPalette(&this->palette);
    upscale = VGA.upscale;
    VGA.upscale = false;
    static int frame_count = 0;
    fb = VGA.getFrameBuffer();
    fb->clear();
    if (debug_print) {
        debug_framebuffer->clear();
    }
    this->cannonAngularOffset = {0.0f, 0.0f};
    if (cockpit != nullptr) {
        if (this->face  == CockpitFace::CP_FRONT) {
            if (this->hud != nullptr) {
                this->hud_framebuffer->clear();
                this->RenderHUD({this->hud_framebuffer->width/2,this->hud_framebuffer->height/2}, this->hud_framebuffer);
                Point2D hud_pos = {
                    this->hud->small_hud->HINF->center_x - this->hud_framebuffer->width / 2,
                    this->hud->small_hud->HINF->center_y - this->hud_framebuffer->height / 2
                };
                fb->blit(this->hud_framebuffer->framebuffer, hud_pos.x, hud_pos.y , this->hud_framebuffer->width,
                            this->hud_framebuffer->height);
            }

        }
        if (this->face == CockpitFace::CP_BIG) {
            if (this->hud != nullptr) {
                this->hud_framebuffer->clear();
                this->RenderHUD({this->hud_framebuffer->width/2,this->hud_framebuffer->height/2}, this->hud_framebuffer);
                Point2D hud_pos = {
                    this->hud->large_hud->HINF->center_x - this->hud_framebuffer->width / 2,
                    this->hud->large_hud->HINF->center_y - this->hud_framebuffer->height / 2
                };
                fb->blit(this->hud_framebuffer->framebuffer, hud_pos.x, hud_pos.y , this->hud_framebuffer->width,
                            this->hud_framebuffer->height);
            }
        }
        // Used to draw the SVGA background whenever that art existed at
        // all, regardless of the player's actual VGA/SVGA preference — now
        // gated on g_ifVGA (see commons/GraphicsSettings.h) so VGA mode
        // genuinely renders at VGA scale instead of always preferring the
        // higher-res art when present.
        //
        // HUD_ONLY (see WC3CockpitViewMode) skips this entirely — no
        // cockpit background/frame at all, just whatever RenderWC3Instruments
        // draws below (using the cockpit file's own separate HUD instrument
        // layout, not FRNT's).
        if (this->cockpit_view_mode != WC3CockpitViewMode::HUD_ONLY) {
            if (!g_ifVGA && this->face == CockpitFace::CP_FRONT && this->cockpit->ARTP_SVGA.GetNumImages() > 0) {
                // Draw the hi-res background via its own GL path instead of the
                // low-res 320x200 software one; the rest of Render() below still
                // composites HUD/MFD/instruments into the (alpha-transparent
                // where unpainted) 320x200 fb layered on top of it.
                this->RenderHighResBackground();
            } else {
                fb->drawShape(this->cockpit->ARTP.GetShape(this->face));
            }
        }

        if (this->face  == CockpitFace::CP_FRONT || this->face  == CockpitFace::CP_BIG) {
            bool selected_is_20mm = this->player_plane->weaps_load.size() > 0 &&
                this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr &&
                this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->weapon_id == ID_20MM;
            if (this->is_shooting && selected_is_20mm) {
                RSImageSet *muzzle_flash_set = nullptr;
                if (this->face == CockpitFace::CP_FRONT) {
                    muzzle_flash_set = &this->cockpit->GUNF;
                } else if (this->face == CockpitFace::CP_BIG) {
                    muzzle_flash_set = &this->cockpit->GHUD;
                }
                if (muzzle_flash_set != nullptr) {
                    int muzzle_index = frame_count % muzzle_flash_set->GetNumImages();
                    static float acumulated_time = 0.0f;
                    float time_per_frame = 1.0f / 12.0f;

                    acumulated_time += GameTimer::getInstance().getDeltaTime();
                    if (acumulated_time >= time_per_frame) {
                        acumulated_time = 0.0f;
                        frame_count++;
                    }
                    RLEShape *muzzle_shape = muzzle_flash_set->GetShape(muzzle_index);
                    
                    fb->drawShape(muzzle_shape);
                }
                    
            }
            if (this->player_plane->weaps_load.size() > 0 &&
                this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
                if (this->radar_mode != RadarMode::ASST) {
                    switch (
                        this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->weapon_id) {
                    case ID_20MM:
                        this->radar_mode = RadarMode::AARD;
                        if (this->weapon_mode == Hud_weapon_mode::WM_HUD_STRAF) {
                            this->radar_mode = RadarMode::AGRD;
                        }
                        break;
                    case ID_AIM9J:
                    case ID_AIM9M:
                    case ID_AIM120:
                        this->radar_mode = RadarMode::AARD;
                        break;
                    case ID_MK20:
                    case ID_MK82:
                    case ID_DURANDAL:
                        this->radar_mode = RadarMode::AGRD;
                        break;
                    case ID_AGM65D:
                    case ID_GBU15:
                        this->radar_mode = RadarMode::AGRD;
                        break;
                    case ID_LAU3:
                        this->radar_mode = RadarMode::AGRD;
                        break;
                    }
                }
            }
            // MONI.* (RAWS/ALTI/AIRS) is an SC-format field, only ever
            // populated by parseMONI_SHAP-family parsers — WC3 cockpits
            // (InitFromWC3Ram/parseWC3_COCK) never touch it, so RenderRAWS/
            // RenderAlti/RenderSpeedOmetter/RenderRAWSBig all early-return
            // unconditionally for a real WC3 cockpit file: pure no-op calls
            // every frame. RenderSpeedOmetter's job is already done by the
            // real KPS text readout RenderWC3InstrumentsSVGA/
            // RenderWC3Instruments draw from the file's own TEXT chunk;
            // RenderAlti/RenderRAWS/RenderRAWSBig have no WC3-native
            // equivalent yet (a real altitude gauge / proximity-warning
            // display is a genuine deferred gap, not solved here) — but
            // calling their dead SC bodies doesn't fill that gap either, so
            // there's nothing lost by no longer calling them for WC3.
            bool isWC3Cockpit = !this->cockpit->instrumentShapes.empty();
            if (this->face == CockpitFace::CP_FRONT) {
                if (isWC3Cockpit) {
                    this->RenderWC3Instruments(fb, this->cockpit_view_mode == WC3CockpitViewMode::HUD_ONLY);
                    this->RenderWC3TargetingReticle(fb);
                    this->RenderWC3TargetBrackets(fb);
                } else {
                    this->RenderRAWS({84, 112}, fb);
                    this->RenderAlti({161, 166}, fb);
                    this->RenderSpeedOmetter({125, 166}, fb);
                }
            } else if (this->face == CockpitFace::CP_BIG) {
                if (!isWC3Cockpit) {
                    this->RenderRAWSBig({84, 112}, fb);
                }
            }
            // Was pmfd_right=screen-x0/pmfd_left=screen-(width-...) — the
            // names were swapped from their actual screen position (whoever
            // wrote this mixed up which side is "left"). Renamed to match
            // reality while restructuring this block for the right MFD's
            // new dedicated purpose below.
            //
            // MFD panel origin/size: previously computed from
            // MONI.SHAP.GetWidth()/GetHeight() for SC cockpits (a real,
            // populated field there) but read as uninitialized memory for
            // WC3 (RLEShape's dist fields have no default member
            // initializer, and MONI.SHAP is never populated by
            // InitFromWC3Ram), then later "fixed" to a hardcoded 115x95
            // guess — neither used the real per-cockpit COCK>VDU chunk,
            // which RenderWC3Instruments/RenderWC3InstrumentsSVGA already
            // read for their own instrument-icon positioning. Using a
            // different, disagreeing origin here meant every RenderMFDS*
            // panel's background/gauges could be drawn misaligned with the
            // cockpit art's actual VDU cutouts. Now sourced from the same
            // real vgaFrontVDU/svgaFrontVDU data (selected the same way
            // RenderTargetWithCam already picks targetHudVGA/SVGA), falling
            // back to the old 115x95 screen-edge guess only if the file has
            // no valid VDU chunk at all.
            Point2D mfdSize = this->GetWC3MfdSize();
            int mfdW = mfdSize.x;
            int mfdH = mfdSize.y;
            Point2D pmfd_left, pmfd_right;
            const RSCockpit::WC3VDULayout &vdu = g_ifVGA ? this->cockpit->vgaFrontVDU : this->cockpit->svgaFrontVDU;
            if (isWC3Cockpit && vdu.valid) {
                pmfd_left = {vdu.leftX, vdu.leftY};
                pmfd_right = vdu.GetRightOrigin(g_ifVGA ? 320 : 640);
            } else {
                if (!isWC3Cockpit) {
                    mfdW = this->cockpit->MONI.SHAP.GetWidth();
                    mfdH = this->cockpit->MONI.SHAP.GetHeight();
                }
                pmfd_left = {0, fb->height - mfdH};
                pmfd_right = {fb->width - mfdW - 1, fb->height - mfdH};
            }
            // Right MFD is dedicated to the target scan/ID display now —
            // always rendered, independent of show_weapons/etc, which used
            // to double up onto both panels (the left MFD keeps that
            // rotation below, but only ever draws to the left slot).
            this->RenderMFDSTarget(pmfd_right, fb);
            auto renderRadar = [&]() {
                float range = 0.0f;
                switch (this->radar_zoom) {
                case 1:
                    range = 18520.0f;
                    break;
                case 2:
                    range = 18520.0f * 2.0f;
                case 3:
                    range = 18520.0f * 4.0f;
                    break;
                case 4:
                    range = 18520.0f * 8.0f;
                    break;
                }
                this->RenderMFDSRadar(pmfd_left, range, this->radar_mode);
            };
            // User-confirmed (2026-07 session), WC3 only: there is no
            // toggle key for the radar — it always draws, at "the
            // predefined compass area" (WC3CockpitShapeId::
            // kCompassDialMedium's real position, self-computed inside
            // RenderMFDSTargetRadarWC3 via GetCompassAreaRect),
            // independent of the left-MFD page rotation below.
            // show_radars/MDFS_RADAR's key press (SCStrike.cpp) is now a
            // no-op for WC3 — kept, not removed, since SCStrike.cpp is
            // shared and Strike Commander's own (non-WC3) radar page is
            // unaffected: it keeps its original show_radars-gated
            // left-MFD-panel behavior as the first branch of the rotation
            // below, exactly as before this change — no equivalent user
            // confirmation exists for SC.
            if (isWC3Cockpit) {
                renderRadar();
            }
            if (!isWC3Cockpit && this->show_radars) {
                renderRadar();
            } else if (this->show_weapons) {
                this->RenderMFDSWeapon(pmfd_left);
            } else if (this->show_comm) {
                this->RenderMFDSComm(pmfd_left, this->comm_target);
            } else if (this->show_damage) {
                this->RenderMFDSDamage(pmfd_left, fb);
            } else if (this->show_cam) {
                this->RenderMFDSCamera(pmfd_left, fb);
            } else if (this->show_power) {
                this->RenderMFDSPower(pmfd_left, fb);
            } else if (this->show_shield) {
                this->RenderMFDSShield(pmfd_left, fb);
            } else {
                // User-reported (2026-07 session): pressing 's' (MDFS_
                // SHIELD) twice — toggling show_shield back off — left the
                // shield page's last-drawn content (background grid,
                // gauges, ...) sitting on screen instead of clearing. None
                // of the RenderMFDS* branches above run when every show_*
                // flag is false, so nothing was ever clearing the panel in
                // that state; each RenderMFDS* function's own black clear
                // (RenderMFDS) only runs when it's actually called. Clear
                // it directly here instead.
                this->RenderMFDS(pmfd_left, fb);
            }
        }
        this->RenderCommMessages({0,200}, fb);
    }
    if (this->mouse_control) {
        Mouse.draw();
    }
    if (debug_print) {
        VGA.getFrameBuffer()->blitWithMask(debug_framebuffer->framebuffer, 0, 0, debug_framebuffer->width, debug_framebuffer->height,255);
    }
    VGA.vSync();
    VGA.upscale = upscale;
}
void SCCockpit::Update() {
    this->yaw_speed = this->yaw - (this->player_plane->azimuthf / 10.0f);
    this->pitch_speed = this->pitch - (this->player_plane->elevationf / 10.0f);
    this->roll_speed = this->roll - (this->player_plane->twist / 10.0f);
    this->pitch = this->player_plane->elevationf / 10.0f;
    this->roll = this->player_plane->twist / 10.0f;
    this->yaw = this->player_plane->azimuthf / 10.0f;
    this->speed = (float)this->player_plane->airspeed;
    this->mach = this->player_plane->mach;
    this->g_limit = this->player_plane->Lmax;
    this->g_load = this->player_plane->g_load;
    this->throttle = this->player_plane->GetThrottle();
    this->altitude = this->player_plane->y;
    this->heading = 360 - (this->player_plane->azimuthf / 10.0f);
    this->gear = this->player_plane->GetWheel();
    this->flaps = this->player_plane->GetFlaps() > 0;
    this->airbrake = this->player_plane->GetSpoilers() > 0;
    this->player = this->player_plane->object;
    // Waypoints are added dynamically as the mission's own scripted programs
    // run (SCMissionActors::flyToWaypoint() etc., via OP_SET_OBJ_FLY_TO_WP/
    // OP_SET_OBJ_FLY_TO_AREA) — the list can legitimately still be empty
    // here, e.g. before any such command has fired yet. No nav-point target
    // is a valid state, not a crash: fall back to the player's own position
    // (zero direction/azimuth) instead of indexing out of bounds.
    if (!this->current_mission->waypoints.empty() &&
        *this->nav_point_id < this->current_mission->waypoints.size()) {
        this->weapoint_coords.x = this->current_mission->waypoints[*this->nav_point_id]->spot->position.x;
        this->weapoint_coords.y = this->current_mission->waypoints[*this->nav_point_id]->spot->position.z;
    } else {
        this->weapoint_coords.x = this->player_plane->x;
        this->weapoint_coords.y = this->player_plane->z;
    }

    Vector2D weapoint_direction = {this->weapoint_coords.x - this->player->position.x,
                                   this->weapoint_coords.y - this->player->position.z};
    float weapoint_azimut = (atan2f(weapoint_direction.y, weapoint_direction.x) * 180.0f / (float)M_PI);

    this->current_target = nullptr;
    
    if (this->target != nullptr) {
        switch (this->radar_mode) {
        case RadarMode::AFRD:
        case RadarMode::ASST:
        case RadarMode::AARD:
            if (this->target->entity->entity_type == EntityType::jet) {
                this->current_target = this->target;
            }
            break;
        case RadarMode::AGRD:
            if (this->target->entity->entity_type == EntityType::ground || this->target->entity->entity_type == EntityType::ornt || this->target->entity->entity_type == EntityType::swpn) {
                this->current_target = this->target;
            }
            break;
        }
    }
    
    weapoint_azimut -= 360;
    weapoint_azimut += 90;
    if (weapoint_azimut > 360) {
        weapoint_azimut -= 360;
    }
    while (weapoint_azimut < 0) {
        weapoint_azimut += 360;
    }
    this->way_az = weapoint_azimut;
    Vector2D player_position = {this->player->position.x, this->player->position.z};
    float distance_to_waypoint = (this->weapoint_coords - player_position).Length();
    float radar_altitude = (this->player_plane->y - this->player_plane->groundlevel) * 3.28084f;
    std::ostringstream oss;
    
    
    
    std::string txt;
    int weapons_count = 0;
    if (this->player_plane->weaps_load.size() == 0) {
        txt = "NO WEAP";
    } else {
        if (this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
            int weapon_id = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->weapon_id;
            for (auto weap : this->player_plane->weaps_load) {
                if (weap != nullptr && weap->objct->wdat->weapon_id == weapon_id) {
                    if (weap->objct->weaps.size() == 0) {
                        weapons_count += weap->nb_weap;
                    } else {
                        for (auto w : weap->objct->weaps) {
                            if (w != nullptr) {
                                weapons_count += w->nb_weap;
                            }
                        }
                    }
                    
                } 
            }
            if (this->current_target != nullptr) {
                uint32_t weap_range =
                    this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->effective_range;
                Vector3D dist_to_target = this->current_target->position - this->player_plane->object->position;
                float distance = dist_to_target.Length();
                // Was missing the else branch entirely -- target_in_range
                // only ever got SET true here and reset on current_target
                // going null (below), so once true it stayed sticky-true
                // even after the target moved back out of range.
                this->target_in_range = (distance <= weap_range);
            } else {
                this->target_in_range = false;
            }
            this->updateLockOn();
            if (weapon_names.find(static_cast<weapon_ids>(weapon_id)) != weapon_names.end()) {
                txt = weapon_names[static_cast<weapon_ids>(weapon_id)];
            } else {
                txt = "UNKNOWN";
            }
            current_weapon_id = weapon_id;
        } else {
            txt = "NO WEAP";
        }
    }
    this->hud_text_tags["NUMW"]=std::to_string(weapons_count) + " "+txt;
    this->hud_text_tags["HUDM"]=hud_weapon_mode_names[this->weapon_mode];
    
    if (this->current_target != nullptr) {
        Vector3D dist_to_target = this->current_target->position - this->player_plane->position;
        float distance = dist_to_target.Length();

        oss.str("");
        oss << std::setw(3) << std::setfill('0') << std::fixed << std::setprecision(2) << distance / 1000.0f;
        this->hud_text_tags["TARG"]= "R "+oss.str();
        SCMissionActors *targetActor = nullptr;
        for (auto actor : this->current_mission->enemies) {
            if (actor->object == this->current_target && actor->is_active) {
                targetActor = actor;
                break;
            }
        }
        if (targetActor == nullptr) {
            for (auto actor : this->current_mission->friendlies) {
                if (actor->object == this->current_target && actor->is_active) {
                    targetActor = actor;
                    break;
                }
            }
        }
        if (targetActor != nullptr && targetActor->is_destroyed) {
            targetActor = nullptr;
            this->target = nullptr;
            this->current_target = nullptr;
        }
        this->current_target_actor = targetActor;
        // Default-select the first turret whenever the locked target itself
        // changes to a new capital ship (or becomes one) — tracked via
        // last_turret_cycle_actor rather than re-deriving "is this a new
        // target" some other way, so a turret pick made by the 'r' cycle key
        // (SCStrike.cpp) survives frame-to-frame Update() calls as long as
        // current_target_actor itself hasn't changed.
        if (targetActor != this->last_turret_cycle_actor) {
            this->last_turret_cycle_actor = targetActor;
            this->targeted_turret_index = -1;
            if (targetActor != nullptr && targetActor->object != nullptr && targetActor->object->entity != nullptr) {
                std::string upperName = targetActor->object->member_name;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                if (SCMissionActors::IsCapitalShipName(upperName) &&
                    !targetActor->object->entity->turrets.empty()) {
                    this->targeted_turret_index = 0;
                }
            }
        }
        float target_velocity = targetActor != nullptr ? targetActor->plane != nullptr ? targetActor->plane->airspeed : 0.0f : 0.0f;
        oss.str("");
        oss << std::setw(3) << std::setfill('0') << std::fixed << std::setprecision(2) << target_velocity;
        this->hud_text_tags["CLSR"]= "C "+oss.str();
        if (this->target_in_range) {
            this->hud_text_tags["IRNG"]= "IN RNG";
        } else {
            this->hud_text_tags["IRNG"]= "";
        }
    } else {
        this->hud_text_tags["TARG"] = "";
        this->hud_text_tags["CLSR"]="";
        this->hud_text_tags["IRNG"]= "";
        
    }
    oss.str("");
    oss << std::setw(2) << std::fixed << std::setprecision(1) << this->g_load;
    this->hud_text_tags["GFRC"]= oss.str()+"G";
    oss.str("");
    oss << std::setw(2) << std::fixed << std::setprecision(1) << (float)this->player_plane->object->entity->jdyn->MAX_G;
    this->hud_text_tags["MAXG"]= oss.str()+"G";
    oss.str("");
    oss << std::setw(3) << std::fixed << std::setprecision(2) << this->mach;
    std::string speed_buffer = oss.str();
    this->hud_text_tags["MACH"]= oss.str()+"M";
    oss.str("");
    oss.clear();
    oss << std::setw(4) << std::setfill('0') << std::fixed << std::setprecision(1) << distance_to_waypoint / 1000.0f;
    this->hud_text_tags["WAYP"]= "D"+oss.str();
    oss.str("");
    oss.clear();
    oss << std::setw(4) << std::setfill('0') << std::fixed << std::setprecision(1) << radar_altitude / 1000.0f;
    this->hud_text_tags["RALT"]= "R"+oss.str();

    this->hud_text_tags["LNDG"]= this->player_plane->GetWheel() > 0 ? "GEAR" : "";
    this->hud_text_tags["FLAP"]= this->player_plane->GetFlaps() > 0 ? "FLAP" : "";
    this->hud_text_tags["SPDB"]= this->player_plane->GetSpoilers() > 0 ? "BREAK" : "";
    this->hud_text_tags["THRO"]= std::to_string(this->player_plane->GetThrottle());
    this->hud_text_tags["CALA"]= "T";
    Vector3D camera_position = this->player->position;
    this->cockpit_camera.SetPosition(&camera_position);
    this->cockpit_camera.resetRotate();
    this->cockpit_camera.rotate(
        -tenthOfDegreeToRad(this->player_plane->elevationf),
        -tenthOfDegreeToRad(this->player_plane->azimuthf),
        -tenthOfDegreeToRad(this->player_plane->twist)
    );
    if (this->face == CockpitFace::CP_FRONT) {
        this->cockpit_camera.fovy = 45.0f;
    } else if (this->face == CockpitFace::CP_BIG) {
        this->cockpit_camera.fovy = 30.0f;
    }
    this->cockpit_camera.update();

}
// See declaration comment (SCCockpit.h) and target_locked/lock_progress's
// own comments for the full design. Called once per Update() tick, only
// while a weapon is selected and target_in_range has just been computed
// for it (see call site). Does its own current_target -> SCMissionActors*
// lookup rather than reading current_target_actor, since that member isn't
// refreshed for this frame until later in Update().
void SCCockpit::updateLockOn() {
    uint32_t nowTicks = SDL_GetTicks();
    float dt = (this->m_lastLockTicks != 0) ? (float)(nowTicks - this->m_lastLockTicks) / 1000.0f : 0.0f;
    this->m_lastLockTicks = nowTicks;

    bool inCone = false;
    if (this->target_in_range && this->current_target != nullptr &&
        this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
        int weaponId = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->weapon_id;
        SCMissionActors *targetActor = nullptr;
        for (auto actor : this->current_mission->enemies) {
            if (actor->object == this->current_target && actor->is_active) {
                targetActor = actor;
                break;
            }
        }
        if (targetActor == nullptr) {
            for (auto actor : this->current_mission->friendlies) {
                if (actor->object == this->current_target && actor->is_active) {
                    targetActor = actor;
                    break;
                }
            }
        }
        if (targetActor != nullptr) {
            if (weaponId == weapon_ids::ID_HSMISS) {
                // Heatseeker: only locks while the target's engines face the
                // player, i.e. the player is behind the target.
                inCone = (SCMissionActors::ClassifyHitQuadrant(targetActor, this->player->position) == HitQuadrant::Back);
            } else {
                // IR/FF/Torpedo/T-bomb (and anything else lock-gated): just
                // requires the target to be generally in front of the player.
                inCone = (SCMissionActors::ClassifyHitQuadrant(this->current_mission->player, this->current_target->position) == HitQuadrant::Front);
            }
        }
    }

    if (inCone) {
        this->lock_progress += dt;
    } else {
        this->lock_progress = 0.0f;
    }
    this->target_locked = (this->lock_progress >= kLockThresholdSeconds);
}
void SCCockpit::RenderHUD() {
    FrameBuffer *hud = this->hud_framebuffer;
    if (hud == nullptr) {
        return;
    }
    hud->fillWithColor(255);
    Point2D hud_center = {hud->width / 2, hud->height / 2};
    this->RenderHUD(hud_center, hud);
    
}

void SCCockpit::RenderAlti(Point2D pmfd_left = {177, 179}, FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (this->cockpit->MONI.INST.ALTI.ARTS.GetNumImages() == 0) {
        return;
    }
    if (this->cockpit->MONI.INST.ALTI.x != 0 && this->cockpit->MONI.INST.ALTI.y != 0) {
        if (pmfd_left.x != 0 && pmfd_left.y != 0) {
            pmfd_left.x = this->cockpit->MONI.INST.ALTI.x;
            pmfd_left.y = this->cockpit->MONI.INST.ALTI.y;
        }
    }
    RLEShape *shape = this->cockpit->MONI.INST.ALTI.ARTS.GetShape(0);
    Point2D raws_size = {shape->GetWidth(), shape->GetHeight()};
    Point2D bottom_right = {pmfd_left.x + raws_size.x, pmfd_left.y + raws_size.y};
    shape->SetPosition(&pmfd_left);
    fb->drawShape(shape);
    // Calculate altitude in feet
    float altiInFeet = this->altitude * 3.28084f;

    // Calculate angles for each needle
    // 1000s needle (full circle = 10,000 feet)
    float thousandsAngle = (altiInFeet / 10000.0f) * 2.0f * (float)M_PI;
    // 100s needle (full circle = 1,000 feet)
    float hundredsAngle = (fmodf(altiInFeet, 1000.0f) / 1000.0f) * 2.0f * (float)M_PI;

    // Calculate center position of the altimeter
    Point2D center = {pmfd_left.x + raws_size.x / 2, pmfd_left.y + raws_size.y / 2};

    // Calculate needle lengths
    int thousandsLength = raws_size.x / 2 - 5;
    int hundredsLength = raws_size.x / 2 - 6;

    // Calculate needle endpoints
    Point2D thousandsEnd = {center.x + (int)(thousandsLength * sinf(thousandsAngle)),
                            center.y - (int)(thousandsLength * cosf(thousandsAngle))};

    Point2D hundredsEnd = {center.x + (int)(hundredsLength * sinf(hundredsAngle)),
                           center.y - (int)(hundredsLength * cosf(hundredsAngle))};

    // Draw needles
    fb->line(center.x, center.y, thousandsEnd.x, thousandsEnd.y, 223); // Thousands needle
    fb->line(center.x, center.y, hundredsEnd.x, hundredsEnd.y, 90);    // Hundreds needle (different color)
}
void SCCockpit::RenderShieldGauge(Point2D top_left, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (this->cockpit == nullptr) {
        return;
    }
    auto it = this->cockpit->instrumentShapes.find(WC3CockpitShapeId::kBarMeterFillA);
    if (it == this->cockpit->instrumentShapes.end() || it->second == nullptr) {
        return;
    }
    RSImageSet *fillSet = it->second;
    size_t numFrames = fillSet->GetNumImages();
    if (numFrames == 0) {
        return;
    }
    if (this->player_plane == nullptr || this->player_plane->pilot == nullptr) {
        return;
    }
    SCMissionActors *pilot = this->player_plane->pilot;
    if (pilot->object == nullptr || pilot->object->entity == nullptr) {
        return;
    }
    // kBarMeterFillA (id 19/0x13) is the fuel gauge, not shield (user-
    // corrected, 2026-07 session — this function's original "front-facing
    // shield" framing was wrong; also currently unreachable dead code, no
    // call site anywhere in this file, kept fixed for correctness in case
    // it's wired up later). GetFuelCapacity(), not a hardcoded 12800 — see
    // SCPlane::fuel_max's own comment.
    float fraction = (this->player_plane->GetFuelCapacity() > 0.0f)
                          ? this->player_plane->GetFuel() / this->player_plane->GetFuelCapacity()
                          : 1.0f;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    // Frame-select-by-fraction, not time. Last frame = full gauge, not
    // frame 0 (user-corrected, 2026-07 session).
    size_t frameIdx = (size_t)(fraction * (float)(numFrames - 1) + 0.5f);
    if (frameIdx >= numFrames) frameIdx = numFrames - 1;
    RLEShape *shape = fillSet->GetShape(frameIdx);
    if (shape == nullptr) {
        return;
    }
    shape->SetPosition(&top_left);
    fb->drawShape(shape);
}
void SCCockpit::RenderSpeedOmetter(Point2D pmfd_left = {125, 166}, FrameBuffer *fb = nullptr) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    if (this->cockpit->MONI.INST.AIRS.ARTS.GetNumImages() == 0) {
        return;
    }
    if (this->cockpit->MONI.INST.AIRS.x != 0 && this->cockpit->MONI.INST.AIRS.y != 0) {
        if (pmfd_left.x != 0 && pmfd_left.y != 0) {
            pmfd_left.x = this->cockpit->MONI.INST.AIRS.x;
            pmfd_left.y = this->cockpit->MONI.INST.AIRS.y;
        }
    }
    RLEShape *shape = this->cockpit->MONI.INST.AIRS.ARTS.GetShape(0);
    Point2D raws_size = {shape->GetWidth(), shape->GetHeight()};
    Point2D bottom_right = {pmfd_left.x + raws_size.x, pmfd_left.y + raws_size.y};
    shape->SetPosition(&pmfd_left);
    fb->drawShape(shape);
    // Calculate speed in knots
    float speedInKnots = this->speed;
    // Calculate angle for the needle
    float speedAngle = (speedInKnots / 1500.0f) * 2.0f * (float)M_PI; // Assuming max speed is 600 knots
    // Calculate center position of the speedometer
    Point2D center = {pmfd_left.x + raws_size.x / 2, pmfd_left.y + raws_size.y / 2};
    // Calculate needle length
    int needleLength = raws_size.x / 2 - 5; // Adjusted for the size of the speedometer
    // Calculate needle endpoint
    Point2D needleEnd = {center.x + (int)(needleLength * sinf(speedAngle)),
                         center.y - (int)(needleLength * cosf(speedAngle))};
    // Draw the needle
    fb->line(center.x, center.y, needleEnd.x, needleEnd.y, 223); // Draw the speed needle
}

bool SCCockpit::RenderCommMessages(Point2D pmfd_text, FrameBuffer *fb) {
    bool hasMessage = false;
    if (fb == nullptr) {
        fb = VGA.getFrameBuffer();
    }
    if (this->current_mission->radio_messages.size() > 0) {
        hasMessage = true;
        if (this->radio_mission_timer == 0) {
            if (this->current_mission->radio_messages[0]->sound != nullptr) {
                Mixer.playSoundVoc(this->current_mission->radio_messages[0]->sound->data,
                                   this->current_mission->radio_messages[0]->sound->size);
            }
            this->radio_mission_timer = 400;
        }

        // WC3 cockpits never load PLAQUES.IFF (RSCockpit::InitFromWC3Ram
        // has no equivalent of InitFromRam's PLAQUES.IFF load), so
        // PLAQ.shapes/.fonts stay empty — GetShape(2) below would return
        // nullptr and the very next line's ->GetHeight() would crash the
        // instant any WC3 comm reply actually fired (2026-07 session,
        // found while wiring up the real WC3 comm MFD page). No fabricated
        // banner shape id exists to reuse for WC3 (same "don't fabricate
        // shape ids" policy as the power gauges' plain-rectangle fill), so
        // this draws a plain outlined bar instead of PLAQ's real art.
        if (!this->cockpit->instrumentShapes.empty()) {
            WC3Font *font = WC3Globals::getInstance().getFont("SLRG");
            if (font != nullptr && font->isLoaded()) {
                uint8_t green = ClosestPaletteIndex(this->palette, 0, 200, 50);
                int barHeight = font->getHeight() + 4;
                Point2D barPos = {pmfd_text.x, pmfd_text.y - barHeight - 1};
                fb->line(barPos.x, barPos.y, barPos.x + fb->width - 1, barPos.y, green);
                fb->line(barPos.x, barPos.y + barHeight, barPos.x + fb->width - 1, barPos.y + barHeight, green);
                std::string radio_message = this->current_mission->radio_messages[0]->message;
                std::transform(radio_message.begin(), radio_message.end(), radio_message.begin(), ::toupper);
                font->drawTextColored(fb, radio_message, barPos.x + 4, barPos.y + 2, green);
            }
        } else {
            RLEShape *background_message = this->cockpit->PLAQ.shapes.GetShape(2);
            if (background_message != nullptr) {
                Point2D b_message_pos = {pmfd_text.x, pmfd_text.y - background_message->GetHeight() - 1};
                background_message->SetPosition(&b_message_pos);
                Point2D radio_text = {4, b_message_pos.y + 9};
                fb->drawShape(background_message);
                // Was a hardcoded fonts[1] (crashed on any cockpit whose
                // PLAQ only carries one FONT sub-chunk, e.g. Longbow/
                // BOMPIT, live-tested 2026-07 session) — fonts isn't a
                // fixed tiny/wide pair, its size matches however many
                // FONT chunks that cockpit's file actually has, indexed
                // in parse order, not by PlaqueFontSize. Look up the wide
                // font by its real size tag instead, falling back to
                // whatever font exists if no wide one was parsed.
                RSFont *fnt = nullptr;
                for (auto &plaqueFont : this->cockpit->PLAQ.fonts) {
                    if (plaqueFont.size == PlaqueFontSize::wide) {
                        fnt = plaqueFont.font;
                        break;
                    }
                }
                if (fnt == nullptr && !this->cockpit->PLAQ.fonts.empty()) {
                    fnt = this->cockpit->PLAQ.fonts[0].font;
                }
                if (fnt != nullptr) {
                    std::string radio_message = this->current_mission->radio_messages[0]->message;
                    std::transform(radio_message.begin(), radio_message.end(), radio_message.begin(), ::toupper);
                    fb->printText(fnt, &radio_text, (char *)radio_message.c_str(), 0, 0,
                                  (uint32_t)radio_message.size(), 2, 2);
                }
            }
        }
        if (this->radio_mission_timer > 0) {
            this->radio_mission_timer--;
        }
        if (this->radio_mission_timer <= 0) {
            this->current_mission->radio_messages.erase(this->current_mission->radio_messages.begin());
            this->radio_mission_timer = 0;
        }
    }
    return hasMessage;
}

void SCCockpit::RenderMFDSCamera(Point2D pmfd_left, FrameBuffer *fb) {
    // No WC3-native camera MFD page exists yet (no chunk in the cockpit
    // file carries camera-page layout data to build one from) — but this
    // function's SC-only body below reads MONI.SHAP.GetWidth/Height(),
    // uninitialized memory for a WC3 cockpit (RLEShape's dist fields have
    // no default initializer, and MONI is never populated by
    // InitFromWC3Ram), to size a buffer allocation. Bail out before that
    // rather than risk a garbage-sized allocation/crash — show_cam is a
    // real, reachable keybinding.
    if (!this->cockpit->instrumentShapes.empty()) {
        return;
    }
    SCRenderer &renderer = SCRenderer::getInstance();

    if (!fb) {
        fb = VGA.getFrameBuffer();
    }


    // Récupérer les dimensions de la texture
    int texWidth  = 107;
    int texHeight = 75;

    int mdfs_height = this->cockpit->MONI.SHAP.GetHeight()-20;
    int mdfs_width = this->cockpit->MONI.SHAP.GetWidth()-8;
    if (texWidth <= 0 || texHeight <= 0) {
        return;
    }

    // Lire les pixels RGBA depuis la texture OpenGL
    std::vector<uint8_t> rgbaPixels(texWidth * texHeight * 4);
    
    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);


    // Construire la LUT si la palette a changé
    if (this->palette_lut_dirty) {
        this->BuildPaletteLUT();
    }

    // Convertir le buffer RGBA redimensionné en buffer indexé palette 8 bits via LUT
    std::vector<uint8_t> indexedBuffer(mdfs_width * mdfs_height);
    int buffer_size = (mdfs_width * mdfs_height)-1;
    for (int i = 0; i < mdfs_width * mdfs_height; i++) {
        uint8_t r = rgbaPixels[i * 4 + 0];
        uint8_t g = rgbaPixels[i * 4 + 1];
        uint8_t b = rgbaPixels[i * 4 + 2];

        // Quantifier sur 5 bits et former la clé
        uint32_t key = (((uint32_t)r >> 3) << 10) | (((uint32_t)g >> 3) << 5) | ((uint32_t)b >> 3);
        indexedBuffer[buffer_size-i] = this->palette_lut[key];
    }

    // Blitter le buffer indexé dans le FrameBuffer à la position pmfd_left
    int decalage_x = 8;
    fb->blit(indexedBuffer.data(), pmfd_left.x+decalage_x, pmfd_left.y+11, mdfs_width, mdfs_height);
    this->cockpit->MONI.SHAP.SetPosition(&pmfd_left);
    fb->drawShape(&this->cockpit->MONI.SHAP);
    Point2D center = {pmfd_left.x + mdfs_width / 2, pmfd_left.y + mdfs_height / 2};
    this->cockpit->MONI.MFDS.GCAM.ARTS.GetShape(0)->SetPosition(&center);
    fb->drawShape(this->cockpit->MONI.MFDS.GCAM.ARTS.GetShape(0));
}
void SCCockpit::BuildPaletteLUT() {
    VGAPalette &pal = this->palette;
    this->palette_lut.clear();
    // Précalcule pour chaque combinaison r,g,b réduite (5 bits par composante = 32768 entrées)
    for (int r = 0; r < 256; r += 8) {
        for (int g = 0; g < 256; g += 8) {
            for (int b = 0; b < 256; b += 8) {
                int bestIndex = 128; // Index de départ dans la plage verte
                int bestDist  = INT_MAX;

                // Convertir RGB en luminosité
                float luminance = 0.299f * r + 0.587f * g + 0.114f * b;

                // Chercher uniquement dans la plage [128-159] (dégradé de vert)
                for (int c = 128; c <= 159; c++) {
                    int dr = r - (int)pal.colors[c].r;
                    int dg = g - (int)pal.colors[c].g;
                    int db = b - (int)pal.colors[c].b;
                    int dist = dr * dr + dg * dg + db * db;
                    if (dist < bestDist) {
                        bestDist  = dist;
                        bestIndex = c;
                        if (dist == 0) break;
                    }
                }
                // Clé = r5g5b5 packed sur 15 bits
                uint32_t key = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                this->palette_lut[key] = (uint8_t)bestIndex;
            }
        }
    }
    this->palette_lut_dirty = false;
}
void SCCockpit::SetCommActorTarget(int target) {
    if (target == 0) {
        this->comm_actor = nullptr;
    } else {
        int cpt = 1;
        for (auto ai : this->current_mission->friendlies) {
            if (ai->actor_name != "PLAYER" && ai->is_active) {
                if (cpt == target) {
                    this->comm_actor = ai;
                    this->comm_target = cpt;
                    break;
                }
                cpt++;
            }
        }
    }
}
void SCCockpit::printTTAG(Point2D pos, HUD_POS &tag, std::string name, FrameBuffer *fb, RSFont *font) {
    Point2D tag_pos = {pos.x+tag.x, pos.y+tag.y+font->GetShapeForChar('0')->GetHeight()};
    if (tag.z == 0) {
        return;
    }
    std::string value = this->hud_text_tags[name];
    fb->printText(font, &tag_pos, (char *)value.c_str(), 0, 0, (uint32_t)value.size(), 2, 2);
}
void SCCockpit::RenderMissileHud(Point2D position, FrameBuffer *fb, CHUD *hud, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    static float msd_x = position.x;
    static float msd_y = position.y;
    int circle_size = hud->CIRC->radius;
    fb->circle_slow(position.x, position.y, circle_size, 223);
    static float x_sign = 0.5f;
    static float y_sign = 0.1f;
    if (this->current_target == nullptr) {
        msd_x += x_sign;
        if (x_sign == 0.5f && msd_x > circle_size+position.x) {
            x_sign = -0.5f;
        } else if (x_sign == -0.5f && msd_x < (position.x-circle_size)) {
            x_sign = 0.5f;
        }
        if (y_sign == 0.1f && msd_y > circle_size+position.y) {
            y_sign = -0.1f;
        } else if (y_sign == -0.1f && msd_y < (position.y-circle_size)) {
            y_sign = 0.1f;
        }
        msd_y += y_sign;
        Point2D msd_pos = {(int)msd_x, (int)msd_y};
        hud->MISD->SHAP->SetPosition(&msd_pos);
        fb->drawShape(hud->MISD->SHAP);
    } else {
        int target_screen_x, target_screen_y;
        int hud_width = hudBottomRight.x - hudTopLeft.x;
        int hud_height = hudBottomRight.y - hudTopLeft.y;
        int hud_center_x = hud_width / 2;
        int hud_center_y = hud_height / 2;
        float target_angle_vs_player = 180-(target->azymuth - this->heading);
        // Angle en radians (attention : target_angle_vs_player semble en degrés ici)
        float angle_rad = target_angle_vs_player * M_PI / 180.0f;

        // Direction radiale (du centre vers px,py) = même direction que x_offset/y_offset
        float radial_x = sinf(angle_rad);
        float radial_y = -cosf(angle_rad);

        // Direction tangentielle (perpendiculaire au rayon)
        float tang_x =  cosf(angle_rad);
        float tang_y =  sinf(angle_rad);
        const int S = 4;

        // Tip : sur le cercle
        int tip_x = (int)(position.x + radial_x * circle_size);
        int tip_y = (int)(position.y + radial_y * circle_size);


        // Base : à l'extérieur du cercle, écartée tangentiellement
        int base1_x = tip_x + (int)(radial_x * S + tang_x * S);
        int base1_y = tip_y + (int)(radial_y * S + tang_y * S);
        int base2_x = tip_x + (int)(radial_x * S - tang_x * S);
        int base2_y = tip_y + (int)(radial_y * S - tang_y * S);

        fb->line(tip_x, tip_y, base1_x, base1_y, 223);
        fb->line(tip_x, tip_y, base2_x, base2_y, 223);
        fb->line(base1_x, base1_y, base2_x, base2_y, 223);
                
        if (project_to_screen(this->current_target->position, target_screen_x, target_screen_y)) {
            if (this->is_3d_cockpit == false) {
                target_screen_x = target_screen_x - hudCenter.x + hud_center_x + hudTopLeft.x;
                target_screen_y = target_screen_y - hudCenter.y + hud_center_y + hudTopLeft.y;
            }
            RLEShape *tg = hud->TARG->SHAPSET->GetShape(1);
            int shape_width = tg->GetWidth();
            int shape_height = tg->GetHeight();
            Point2D target_pos = {target_screen_x, target_screen_y};
            target_pos.x -= shape_width / 2;
            target_pos.y -= shape_height / 2;
            tg->SetPosition(&target_pos);
            fb->drawShapeWithBox(tg, 0,fb->width,0, fb->height);
            if (this->target_in_range) {
                Point2D tmsd_pos = {target_screen_x, target_screen_y};
                tmsd_pos.x -= hud->MISD->SHAP->GetWidth() / 2;
                tmsd_pos.y -= hud->MISD->SHAP->GetHeight() / 2;
                hud->MISD->SHAP->SetPosition(&tmsd_pos);
                fb->drawShapeWithBox(hud->MISD->SHAP, 0,fb->width,0, fb->height);
            }
        }
    }
}
void SCCockpit::RenderIrTargetHud(Point2D position, FrameBuffer *fb, CHUD *hud, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    static float msd_x = position.x;
    static float msd_y = position.y;
    
    if (this->current_target != nullptr) {
        int target_screen_x, target_screen_y;
        int hud_width = hudBottomRight.x - hudTopLeft.x;
        int hud_height = hudBottomRight.y - hudTopLeft.y;
        int hud_center_x = hud_width / 2;
        int hud_center_y = hud_height / 2;
                
        if (project_to_screen(this->current_target->position, target_screen_x, target_screen_y)) {
            if (this->is_3d_cockpit == false) {
                target_screen_x = target_screen_x - hudCenter.x + hud_center_x + hudTopLeft.x;
                target_screen_y = target_screen_y - hudCenter.y + hud_center_y + hudTopLeft.y;
            }
            RLEShape *tg = hud->CROS->SHAP;
            int shape_width = tg->GetWidth();
            int shape_height = tg->GetHeight();
            Point2D target_pos = {target_screen_x, target_screen_y};
            target_pos.x -= shape_width / 2;
            target_pos.y -= shape_height / 2;
            tg->SetPosition(&target_pos);
            fb->drawShapeWithBox(tg, 0,fb->width,0, fb->height);
        }
    }
}
// Primary target indicator, drawn unconditionally whenever current_target
// is set (unlike RenderTargetingReticle's own target square, which only
// ever draws under WM_HUD_LCOS). Complete square = target_hard_locked
// (sticky, press-L lock); broken corner-bracket = auto-targeted only, per
// the user's spec: each edge keeps its outer quarter on both ends, middle
// half removed. Color: red for an enemy target, blue for a friendly one —
// resolved via current_target_actor's team_id vs. the player's, same
// enemies+friendlies scan Update() already does (SCCockpit.cpp:~2450).
void SCCockpit::RenderTargetBox(FrameBuffer *fb, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter) {
    if (this->current_target == nullptr) {
        return;
    }
    int hud_width = hudBottomRight.x - hudTopLeft.x;
    int hud_height = hudBottomRight.y - hudTopLeft.y;
    int hud_center_x = hud_width / 2;
    int hud_center_y = hud_height / 2;
    int Xtarget = 0, Ytarget = 0;
    if (!project_to_screen(this->current_target->position, Xtarget, Ytarget)) {
        return;
    }
    if (this->is_3d_cockpit == false) {
        Xtarget = Xtarget - hudCenter.x + hud_center_x + hudTopLeft.x;
        Ytarget = Ytarget - hudCenter.y + hud_center_y + hudTopLeft.y;
    }
    bool isFriendly = (this->current_target_actor != nullptr && this->current_mission != nullptr &&
                        this->current_mission->player != nullptr &&
                        this->current_target_actor->team_id == this->current_mission->player->team_id);
    uint8_t color = isFriendly ? ClosestPaletteIndex(this->palette, 0, 0, 255)
                                : ClosestPaletteIndex(this->palette, 255, 0, 0);
    int box_size = (this->face == CockpitFace::CP_BIG) ? 10 : 8;
    if (this->target_hard_locked) {
        fb->lineWithBox(Xtarget - box_size, Ytarget - box_size, Xtarget + box_size, Ytarget - box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget + box_size, Ytarget - box_size, Xtarget + box_size, Ytarget + box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget + box_size, Ytarget + box_size, Xtarget - box_size, Ytarget + box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget - box_size, Ytarget + box_size, Xtarget - box_size, Ytarget - box_size, color, 0, fb->width, 0, fb->height);
    } else {
        int half = box_size / 2;
        fb->lineWithBox(Xtarget - box_size, Ytarget - box_size, Xtarget - half, Ytarget - box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget + half, Ytarget - box_size, Xtarget + box_size, Ytarget - box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget - box_size, Ytarget + box_size, Xtarget - half, Ytarget + box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget + half, Ytarget + box_size, Xtarget + box_size, Ytarget + box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget - box_size, Ytarget - box_size, Xtarget - box_size, Ytarget - half, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget - box_size, Ytarget + half, Xtarget - box_size, Ytarget + box_size, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget + box_size, Ytarget - box_size, Xtarget + box_size, Ytarget - half, color, 0, fb->width, 0, fb->height);
        fb->lineWithBox(Xtarget + box_size, Ytarget + half, Xtarget + box_size, Ytarget + box_size, color, 0, fb->width, 0, fb->height);
    }
}
void SCCockpit::RenderHUD(Point2D position, FrameBuffer *fb) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    Point2D hud_top_left;
    Point2D hud_bottom_right;
    Point2D hud_center;
    CHUD *hud = nullptr;
    switch (this->face) {
        case CockpitFace::CP_FRONT:
            this->RenderTextTags(position, fb, this->hud->small_hud, this->font);
            hud_top_left = {
                position.x + this->hud->small_hud->HINF->left,
                position.y + this->hud->small_hud->HINF->top
            };
            hud_bottom_right = {
                position.x + this->hud->small_hud->HINF->right,
                position.y + this->hud->small_hud->HINF->bottom
            };
            hud_center = {
                this->hud->small_hud->HINF->center_x,
                this->hud->small_hud->HINF->center_y
            };
            hud = this->hud->small_hud;
        break;
        case CockpitFace::CP_BIG:
            this->RenderTextTags(position, fb, this->hud->large_hud, this->big_font);
            hud_top_left = {
                position.x + this->hud->large_hud->HINF->left,
                position.y + this->hud->large_hud->HINF->top
            };
            hud_bottom_right = {
                position.x + this->hud->large_hud->HINF->right,
                position.y + this->hud->large_hud->HINF->bottom
            };
            hud_center = {
                this->hud->large_hud->HINF->center_x,
                this->hud->large_hud->HINF->center_y
            };
            hud = this->hud->large_hud;
        default:
        break;
    }
    if (this->player_plane->weaps_load.size() > 0 &&
        this->player_plane->weaps_load[this->player_plane->selected_weapon] != nullptr) {
        int weapon_id = this->player_plane->weaps_load[this->player_plane->selected_weapon]->objct->wdat->weapon_id;
        CHUD_SHAPE *lcos = this->hud->small_hud->LCOS;
        CHUD_SHAPE *strf = this->hud->small_hud->STRF;
        if (this->face == CockpitFace::CP_BIG) {
            lcos = this->hud->large_hud->LCOS;
            strf = this->hud->large_hud->STRF;
        }
        switch (this->weapon_mode) {
            case Hud_weapon_mode::WM_HUD_LCOS:
                this->RenderTargetingReticle(fb, lcos, hud_top_left, hud_bottom_right, hud_center);
                break;
            case Hud_weapon_mode::WM_HUD_CCIP:
                this->RenderBombSight(fb,hud_top_left, hud_bottom_right, hud_center);
                break;
            case Hud_weapon_mode::WM_HUD_CCRP:
                // Non implémenté : viseur de type CCRP (pour les bombes guidées)
                break;
            case Hud_weapon_mode::WM_HUD_IRST:
                this->RenderIrTargetHud(position, fb, hud, hud_top_left, hud_bottom_right, hud_center);
                break;
            case Hud_weapon_mode::WM_HUD_SRM:
            case Hud_weapon_mode::WM_HUD_LRM:
                this->RenderMissileHud(position, fb, hud, hud_top_left, hud_bottom_right, hud_center);
                break;
            case Hud_weapon_mode::WM_HUD_STRAF:
                this->RenderStraffingReticle(fb, strf, hud_top_left, hud_bottom_right, hud_center);
                break;
            case Hud_weapon_mode::WM_HUD_NONE:
                break;
            default:
                break;
        }
    }
    this->RenderTargetBox(fb, hud_top_left, hud_bottom_right, hud_center);
}
void SCCockpit::RenderTextTags(Point2D position, FrameBuffer *fb, CHUD *hud, RSFont *font) {
    if (!fb) {
        fb = VGA.getFrameBuffer();
    }
    Point2D alti{0,0};

    printTTAG(position, hud->TTAG->CLSR, "CLSR", fb, font);
    printTTAG(position, hud->TTAG->TARG, "TARG", fb, font);
    printTTAG(position, hud->TTAG->NUMW, "NUMW", fb, font);
    printTTAG(position, hud->TTAG->HUDM, "HUDM", fb, font);
    printTTAG(position, hud->TTAG->IRNG, "IRNG", fb, font);
    printTTAG(position, hud->TTAG->GFRC, "GFRC", fb, font);
    printTTAG(position, hud->TTAG->MAXG, "MAXG", fb, font);
    printTTAG(position, hud->TTAG->MACH, "MACH", fb, font);
    printTTAG(position, hud->TTAG->WAYP, "WAYP", fb, font);
    printTTAG(position, hud->TTAG->RALT, "RALT", fb, font);
    printTTAG(position, hud->TTAG->LNDG, "LNDG", fb, font);
    printTTAG(position, hud->TTAG->FLAP, "FLAP", fb, font);
    printTTAG(position, hud->TTAG->SPDB, "SPDB", fb, font);
    printTTAG(position, hud->TTAG->THRO, "THRO", fb, font);
    printTTAG(position, hud->TTAG->CALA, "CALA", fb, font);
    alti = {position.x+hud->ALTI->x, position.y+hud->ALTI->y};
    
    this->RenderAltiBandRoll(alti, fb, font, hud->ALTI);
    alti = {position.x+hud->ASPD->x, position.y+hud->ASPD->y};
    
    this->RenderSpeedBandRoll(alti, fb, font, hud->ASPD);
    alti = {position.x-(hud->HEAD->width/2), position.y+hud->HEAD->y};
    this->RenderHeadingCompas(alti, fb, font,hud->HEAD);
    Point2D pitch_ladder_size = {
        (hud->LADD->ladd_half_width + hud->LADD->ladd_text_spacer)*2+hud->LADD->ladd_space_in_line+20,
        hud->LADD->ladd_height * 5
    };
    this->RenderPitchLadder(position, pitch_ladder_size, fb, hud->LADD, font);    
}

void SCCockpit::RenderAltiBandRoll(Point2D alti_top_left, FrameBuffer *fb, RSFont *sfont, CHUD_SHAPE *alti_band) {
    if (!fb) fb = VGA.getFrameBuffer();

    float alti_in_feet = this->altitude * 3.28084f;
    Point2D alti_size = {alti_band->width, alti_band->height};
    Point2D bottom_right = {alti_top_left.x + alti_size.x, alti_top_left.y + alti_size.y};
    int center_y = alti_top_left.y + alti_size.y / 2;

    // Plage d'altitude visible (en pieds) depuis le haut et bas de la fenêtre
    float half_view_feet = ((float)alti_size.y / 2.0f) * (500.0f / alti_band->step);

    // Première graduation en dessous de la zone visible, arrondie à 500 pieds
    int first_grad = (int)floorf((alti_in_feet - half_view_feet) / 500.0f) * 500;

    for (int grad_feet = first_grad; grad_feet <= (int)(alti_in_feet + half_view_feet) + 500; grad_feet += 500) {
        // Conversion altitude → Y pixel
        float y = center_y - (grad_feet - alti_in_feet) / 500.0f * alti_band->step;
        int iy = (int)y;

        if (iy < alti_top_left.y || iy > bottom_right.y+alti_band->step) continue;

        // Tick mark
        Point2D tick = {alti_top_left.x, iy-alti_band->SHAP->GetHeight()};
        alti_band->SHAP->SetPosition(&tick);
        fb->drawShapeWithBox(alti_band->SHAP, alti_top_left.x, bottom_right.x,
                             alti_top_left.y-alti_band->step, bottom_right.y-alti_band->step);
        // Label : valeur en milliers de pieds, 1 décimale (ex: "-02.5")
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (grad_feet / 1000.0f);
        std::string txt = oss.str();
        Point2D label = {alti_top_left.x + 6, iy};
        if (iy >= alti_top_left.y && iy <= bottom_right.y) {
            fb->printText(sfont, &label, (char *)txt.c_str(), 0, 0, (uint32_t)txt.length(), 2, 2);
        }
    }
    // Flèche centrale (altitude courante)
    Point2D alti_arrow = {alti_top_left.x - 4, center_y};
    fb->line(alti_arrow.x, alti_arrow.y, alti_arrow.x +3, alti_arrow.y, 223);
}

void SCCockpit::RenderSpeedBandRoll(Point2D speed_top_left, FrameBuffer *fb, RSFont *sfont, CHUD_SHAPE *speed_band) {
    if (!fb) fb = VGA.getFrameBuffer();

    float speed_in_knots = this->speed;
    Point2D speed_size = {speed_band->width, speed_band->height};
    Point2D bottom_right = {speed_top_left.x + speed_size.x, speed_top_left.y + speed_size.y};
    int center_y = speed_top_left.y + speed_size.y / 2;

    float half_view_knots = ((float)speed_size.y / 2.0f) * (50.0f / speed_band->step);

    int first_grad = (int)floorf((speed_in_knots - half_view_knots) / 50.0f) * 50;
    if (first_grad < 0) first_grad = 0;
    int txt_width = sfont->GetShapeForChar('0')->GetWidth() * 2 + 4;
    for (int grad_knots = first_grad; grad_knots <= (int)(speed_in_knots + half_view_knots) + 50; grad_knots += 50) {
        float y = center_y - (grad_knots - speed_in_knots) / 50.0f * speed_band->step;
        int iy = (int)y;

        if (iy < speed_top_left.y || iy > bottom_right.y + speed_band->step) continue;

        Point2D tick = {speed_top_left.x+txt_width, iy - speed_band->SHAP->GetHeight()};
        speed_band->SHAP->SetPosition(&tick);
        fb->drawShapeWithBox(speed_band->SHAP, speed_top_left.x, bottom_right.x,
                             speed_top_left.y - speed_band->step, bottom_right.y - speed_band->step);

        std::string txt = std::to_string(grad_knots/10);
        Point2D label = {speed_top_left.x, iy};
        if (iy >= speed_top_left.y && iy <= bottom_right.y) {
            fb->printText(sfont, &label, (char *)txt.c_str(), 0, 0, (uint32_t)txt.length(), 2, 2);
        }
    }

    Point2D speed_arrow = {speed_top_left.x+txt_width+2, center_y};
    fb->line(speed_arrow.x, speed_arrow.y, speed_arrow.x + (txt_width/2), speed_arrow.y, 223);
}
void SCCockpit::RenderHeadingCompas(Point2D heading_top_left, FrameBuffer *fb, RSFont *sfont, CHUD_SHAPE *heading_band) {
    if (!fb) fb = VGA.getFrameBuffer();

    float current_heading = this->heading; // en degrés [0, 360[
    Point2D heading_size = {heading_band->width, heading_band->height};
    Point2D bottom_right = {heading_top_left.x + heading_size.x, heading_top_left.y + heading_size.y};
    int center_x = heading_top_left.x + heading_size.x / 2;

    // Plage de degrés visible depuis le centre jusqu'au bord
    float half_view_deg = ((float)heading_size.x / 2.0f) * (10.0f / heading_band->step);
    int txt_height = sfont->GetShapeForChar('0')->GetHeight();
    int first_grad = (int)ceilf((current_heading + half_view_deg) / 10.0f) * 10;

    for (int grad_deg = first_grad; grad_deg >= (int)(current_heading - half_view_deg) - 10; grad_deg -= 10) {
        int display_deg = ((grad_deg % 360) + 360) % 360;

        float x = center_x + (grad_deg - current_heading) / 10.0f * heading_band->step;
        int ix = (int)x;

        if (ix < heading_top_left.x || ix > bottom_right.x + heading_band->step) continue;

        Point2D tick = {ix - heading_band->SHAP->GetWidth() / 2, heading_top_left.y};
        heading_band->SHAP->SetPosition(&tick);
        fb->drawShapeWithBox(heading_band->SHAP, heading_top_left.x, bottom_right.x,
                             heading_top_left.y, bottom_right.y);

        std::string txt = std::to_string(display_deg/10);
        
        Point2D label = {ix - (int)(txt.length()) * 2, heading_top_left.y + heading_band->SHAP->GetHeight() + txt_height};
        if (ix >= heading_top_left.x && ix <= bottom_right.x) {
            fb->printText(sfont, &label, (char *)txt.c_str(), 0, 0, (uint32_t)txt.length(), 2, 2);
        }
    }

    // Flèche centrale (cap courant)
    Point2D heading_arrow = {center_x, heading_top_left.y - 3};
    fb->line(heading_arrow.x, heading_arrow.y, heading_arrow.x, heading_arrow.y + 6, 223);

    // Différence angulaire normalisée [-180, 180]
    float way_diff = fmodf(this->way_az - current_heading + 540.0f, 360.0f) - 180.0f;
    float way_x = center_x + way_diff / 10.0f * heading_band->step;
    int iway_x = (int)way_x;

    // Dessiner l'indicateur seulement s'il est dans la zone visible
    if (iway_x <= heading_top_left.x) {
        iway_x = heading_top_left.x;
    } else if (iway_x >= bottom_right.x) {
        iway_x = bottom_right.x;
    }
    Point2D way_arrow = {iway_x+(heading_band->SHP2->GetWidth() / 2), heading_top_left.y - heading_band->SHP2->GetHeight()};
    heading_band->SHP2->SetPosition(&way_arrow);
    fb->drawShape(heading_band->SHP2);
    //fb->rect_slow(heading_top_left.x, heading_top_left.y, bottom_right.x, bottom_right.y, 223);
}
void SCCockpit::RenderPitchLadder(Point2D center, Point2D clip_size, FrameBuffer *fb, SLADD *ladd, RSFont *ft) {
    if (!fb)   fb = VGA.getFrameBuffer();
    if (!ladd) return;

    const int bx1 = center.x - clip_size.x / 2;
    const int bx2 = center.x + clip_size.x / 2;
    const int by1 = center.y - clip_size.y / 2;
    const int by2 = center.y + clip_size.y / 2;

    const float rollRad   = this->roll * (float)M_PI / 180.0f;
    const float pixPerDeg = (float)ladd->ladd_height / 5.0f;
    const int   halfGap   = ladd->ladd_space_in_line / 2;
    const int   color     = 223;

    std::string txt;

    for (int angle = 90; angle >= -90; angle -= 5) {
        const int lineY = center.y + (int)((this->pitch - (float)angle) * pixPerDeg);

        if (angle == 0) {
            // Horizon line: solid, slightly wider than the other bars
            Point2D hStart = { center.x - ladd->ladd_half_width - 5, lineY };
            Point2D hEnd   = { center.x + ladd->ladd_half_width + 5, lineY };
            hStart = hStart.rotateAroundPoint(center, rollRad);
            hEnd   = hEnd.rotateAroundPoint(center, rollRad);
            fb->lineWithBox(hStart.x, hStart.y, hEnd.x, hEnd.y, color, bx1, bx2, by1, by2);
            continue;
        }

        // Left and right bar segments (pre-rotation)
        Point2D lStart = { center.x - ladd->ladd_half_width-halfGap, lineY };
        Point2D lEnd   = { center.x - halfGap,               lineY };
        Point2D rStart = { center.x + halfGap,               lineY };
        Point2D rEnd   = { center.x + ladd->ladd_half_width+halfGap, lineY };

        // Tick endpoints (perpendicular, pointing toward horizon)
        const int tickDir = (angle > 0) ? ladd->ladd_tick_height : -ladd->ladd_tick_height;
        Point2D ltTick = { lStart.x+1, lineY + tickDir };
        Point2D rtTick = { rEnd.x,   lineY + tickDir };

        // Apply roll rotation to all points
        lStart = lStart.rotateAroundPoint(center, rollRad);
        lEnd   = lEnd.rotateAroundPoint(center, rollRad);
        rStart = rStart.rotateAroundPoint(center, rollRad);
        rEnd   = rEnd.rotateAroundPoint(center, rollRad);
        ltTick = ltTick.rotateAroundPoint(center, rollRad);
        rtTick = rtTick.rotateAroundPoint(center, rollRad);

        // Below horizon → dashed lines
        const int skip = (angle < 0) ? 2 : 1;
        fb->lineWithBoxWithSkip(lStart.x, lStart.y, lEnd.x, lEnd.y, color, bx1, bx2, by1, by2, skip);
        fb->lineWithBoxWithSkip(rStart.x, rStart.y, rEnd.x, rEnd.y, color, bx1, bx2, by1, by2, skip);

        // Tick marks at outer ends
        if (ladd->ladd_tick_height != 0) {
            fb->lineWithBox(lStart.x+1, lStart.y, ltTick.x, ltTick.y, color, bx1, bx2, by1, by2);
            fb->lineWithBox(rEnd.x,   rEnd.y,   rtTick.x, rtTick.y, color, bx1, bx2, by1, by2);
        }

        
        txt = std::to_string(std::abs(angle));
        int txtLen = (int)txt.length();
        int spacer = ladd->ladd_text_spacer;
        float dx = rEnd.x - lStart.x;
        float dy = rEnd.y - lStart.y;
        float len = sqrtf(dx*dx + dy*dy);
        float dirX = dx / len, dirY = dy / len;

        int char_w = ft->GetShapeForChar('0')->GetWidth();
        int char_h = ft->GetShapeForChar('0')->GetHeight();
        int txt_w  = char_w * txtLen;

        // Label gauche : direction sortante = -dir
        if (lStart.x > bx1 && lStart.x < bx2 && lStart.y > by1 && lStart.y < by2) {
            float outX = -dirX, outY = -dirY;
            Point2D p = {
                (int)(lStart.x + outX * (spacer + txt_w * 0.5f) - txt_w * 0.5f),
                (int)(lStart.y + outY * (spacer + char_h * 0.5f) - char_h * 0.5f)
            };
            fb->printText(ft, &p, (char *)txt.c_str(), 0, 0, (uint32_t)txt.length(), 2, 2);
        }
        // Label droit : direction sortante = +dir
        if (rEnd.x > bx1 && rEnd.x < bx2 && rEnd.y > by1 && rEnd.y < by2) {
            float outX = dirX, outY = dirY;
            Point2D p = {
                (int)(rEnd.x + outX * (spacer + txt_w * 0.5f) - txt_w * 0.5f),
                (int)(rEnd.y + outY * (spacer + char_h * 0.5f) - char_h * 0.5f)
            };
            fb->printText(ft, &p, (char *)txt.c_str(), 0, 0, (uint32_t)txt.length(), 2, 2);
        }
        
    }
}