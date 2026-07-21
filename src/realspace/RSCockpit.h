#pragma once
#include "precomp.h"
#include <vector>
#include <unordered_map>

// WC3 COCK>SHAP shape ids identified by actually rendering and visually
// inspecting each id's decoded frames (against MEDPIT.IFF; every one of
// its 45 ids has been inspected). "VGA"/"SVGA" pairs below are matched by
// identical frame count N plus a roughly-2x size ratio — where a
// same-sized id has a *different* N, it's documented as a separate,
// unrelated gauge instead of guessed into a pairing it doesn't fit (e.g.
// kBarMeterFillA/B below don't pair with the smaller, differently-framed
// bar gauge at ids 49/50).
//
// Cross-checked against all 5 cockpit files (MEDPIT/ARWPIT/EXCPIT/HVYPIT/
// BOMPIT): every id below keeps the same *content* across all 5 (spot-
// checked by rendering, e.g. id 98's canopy-frame silhouette and id 12's
// pilot head-turn both confirmed as the same animation on ARWPIT/HVYPIT
// too), but width/height/frame-count often vary — that's expected
// per-cockpit art (each ship's dashboard is laid out differently), not a
// wrong id mapping, and matters most where N stays exactly equal (the
// strongest same-instrument signal, since N is animation content, not
// dashboard sizing). Two exceptions worth noting: id 12 (kPilotHeadTurn)
// doesn't exist at all in BOMPIT.IFF (bombers apparently don't show a
// head-turn animation), and ids 2/32 exist only in HVYPIT.IFF/BOMPIT.IFF
// (a second background at each of the two background sizes — see the
// ARTP/ARTP_SVGA size-based-matching comment in parseWC3_COCK_SHAP).
// Frame counts for the bar-meter gauges (19/20/49/50) and the AUTO label
// (21/22/51/52) vary more than expected across cockpits (e.g. EXCPIT's
// AUTO label has 3 frames instead of 2) — plausibly genuine per-ship
// differences in gauge precision/extra states, not re-checked further.
namespace WC3CockpitShapeId {
    constexpr uint32_t kBackgroundSVGA = 0;   // 640x480, full hi-res background
    constexpr uint32_t kBackgroundVGA = 30;   // 320x200, full lo-res background
    // 14x14, 4 frames: circle -> crosshair -> circle -> diamond. Reads as a
    // weapon lock-on/targeting reticle animation.
    constexpr uint32_t kTargetingReticle = 6;
    // Same reticle animation at SVGA scale (11x11, same 4-frame sequence).
    constexpr uint32_t kTargetingReticleSVGA = 36;
    // 85x24 / 24x85 (transposed pair): a horizontal/vertical gauge bezel —
    // a gradient bar background frame, not a filled level itself (single
    // static frame each).
    constexpr uint32_t kGaugeBezelHorizontal = 17;
    constexpr uint32_t kGaugeBezelVertical = 18;
    // 22x49 / 23x49, 12 frames each: two side-by-side analog bar-meter
    // fill animations in different color schemes (A: blue/cyan/yellow,
    // B: red/pink) — reads as two separate stat gauges (e.g. fuel/shield
    // vs. hull/damage) rather than one gauge's two halves. Not a digit
    // font, despite the frame count inviting that guess at first —
    // rendering the actual frames rules that out.
    constexpr uint32_t kBarMeterFillA = 19;
    constexpr uint32_t kBarMeterFillB = 20;
    // 12x20, 10 frames each: a second, smaller/differently-framed pair of
    // bar-meter fill gauges — NOT the SVGA scale of kBarMeterFillA/B
    // (frame count doesn't match: 10 vs 12), so most likely a distinct
    // pair of gauges for different stats.
    constexpr uint32_t kBarMeterFillSmallA = 49;
    constexpr uint32_t kBarMeterFillSmallB = 50;
    // 89x81, 23 frames: an animated rectangular green bracket. Confirmed by
    // the user (not just visual guessing, unlike most ids in this file):
    // this is the right MFD's target-scanning animation, played while no
    // target is yet identified — positionally the 6th SHAP entry in the
    // pak (0-based index 5), not literally id 5 or 6. See
    // SCCockpit::RenderMFDSTarget.
    constexpr uint32_t kTargetLockBox = 8;
    constexpr uint32_t kTargetLockBoxSVGA = 38;  // 46x34, 21 frames (close but not exact N match)
    // 100x87, 24 frames: almost entirely transparent except a thin
    // horizontal bar whose color shifts yellow -> orange -> red across the
    // sequence — reads as a color-coded target health/shield status bar,
    // shown alongside kTargetLockBox rather than a smooth animation (the
    // game likely picks a frame by target HP fraction, not by time).
    constexpr uint32_t kTargetHealthBar = 9;
    constexpr uint32_t kTargetHealthBarSVGA = 39;  // 50x36, 24 frames (exact N match)
    // 89x81, 12 frames: NOT a hand animation despite the name (kept for
    // continuity with earlier code/comments that already used it) —
    // user-identified, 2026-07 session, as the Damage MFD's page-2 ship
    // graphic (RenderMFDSDamage): frame 0 is the base/undamaged ship
    // outline; frames 1-11 are 4 position-identical clusters (front 1-3,
    // right 4-6, back 7-9, left 10-11) overlaid on it per quadrant,
    // showing yellow (mild) or red (heavy) damage. kPilotHandAnimation2SVGA
    // below has a different aspect ratio (46x34 vs. this id's 89x81)
    // despite the matching 12-frame count, so it's NOT confidently the
    // same asset at SVGA scale — RenderMFDSDamage currently uses this id
    // unconditionally, not g_ifVGA-branched, pending that being sorted out.
    constexpr uint32_t kPilotHandAnimation2 = 13;
    constexpr uint32_t kPilotHandAnimation2SVGA = 43;  // 46x34, 12 frames (exact N match)
    // 75x77, 38 frames: frame 0 is a helmet/head silhouette outline; the
    // remaining 37 frames are small, visually distinct colored icons (not
    // a smooth animation sequence) — reads as a system/subsystem status
    // icon set for an MFD, one icon selected per system state rather than
    // played back in order.
    constexpr uint32_t kSystemStatusIcons = 14;
    constexpr uint32_t kSystemStatusIconsSVGA = 44;  // 39x34, 38 frames (exact N match)
    // 46x13 / 45x12 (VGA/SVGA pair), 2 frames: the literal text "AUTO"
    // with a small indicator square that's dark red in frame 0 and bright
    // green in frame 1 — an autopilot-available status light/label toggle.
    // id 22/52 were previously (wrongly) assumed to be the same "AUTO"
    // label at a second scale — the id-21/51 vs id-22/52 pairing at the
    // SAME INST mode slot (0xff0d vs 0xff0e) in both the VGA and SVGA FRNT
    // tables (real per-cockpit-file positions, not guessed) instead shows
    // 21 and 51 are one instrument (AUTO) and 22/52 are a *second*, distinct
    // instrument immediately to its right — this is the "LOCK" label
    // visible in the same real screen position as WC3's actual AUTO/LOCK
    // status-light pair, same 2-frame dark/lit toggle format.
    constexpr uint32_t kAutopilotLabel = 21;
    constexpr uint32_t kAutopilotLabelSmall = 51;      // 24x6, same 2-frame toggle, smaller
    constexpr uint32_t kLockLabel = 22;
    constexpr uint32_t kLockLabelSmall = 52;           // 21x4, same 2-frame toggle, smaller
    // 126x109, 3 frames: spiky, scratch-like burst patterns scattered
    // across the frame, increasing in number/density across the 3 frames
    // — reads as a canopy/windscreen damage-crack overlay.
    constexpr uint32_t kCanopyDamageCracks = 25;
    constexpr uint32_t kCanopyDamageCracksSVGA = 55;  // 64x48, 3 frames (exact N match)
    // 154x117, 5 frames — same idea as kCanopyDamageCracks at a different
    // size/frame progression; not confirmed whether this is a third
    // distinct damage-overlay instrument or an alternate resolution of a
    // 5-stage version of the same effect.
    constexpr uint32_t kCanopyDamageCracksAlt = 26;
    constexpr uint32_t kCanopyDamageCracksAltSVGA = 56;  // 77x47, 5 frames (exact N match)
    // 56x56 / 46x46 / 87x87 (and probably others): concentric-ring dial
    // with a 4-way crosshair — reads as a compass/radar/nav dial
    // background, appearing at several different sizes for different
    // instrument panels.
    constexpr uint32_t kCompassDialSmall = 45;
    constexpr uint32_t kCompassDialMedium = 24;
    constexpr uint32_t kCompassDialLarge = 15;
    // 315x21 / 158x9 (VGA/SVGA pair), 1 frame: dark curved silhouettes at
    // each end of a wide, mostly-transparent strip — reads as the top
    // corners of the cockpit canopy/windscreen frame overlay.
    constexpr uint32_t kCanopyFrameTop = 98;
    constexpr uint32_t kCanopyFrameTopSVGA = 97;
    // 15x69, 9 frames: 8 distinct directional arrows (all 8 compass
    // octants) plus what reads as a blank/neutral 9th frame — reads as a
    // threat-warning or heading-pointer directional indicator.
    constexpr uint32_t kDirectionalIndicator = 99;
    constexpr uint32_t kDirectionalIndicatorSVGA = 100;  // confirmed: same 8-arrow set, smaller
    // 190x86, 11 frames: a black gloved hand/fist in a sequence of
    // slightly different positions — reads as a throttle-hand or
    // control-stick-hand animation visible in the cockpit view.
    constexpr uint32_t kPilotHandAnimation = 11;
    // 127x92, 1 frame: a dashboard sub-panel with switch/button rows
    // along its edges and a dark viewport in the middle — reads as a
    // cockpit side-panel piece, distinct from the two full backgrounds.
    constexpr uint32_t kSidePanel = 53;
    // 127x110, 1 frame: a dark red grid (evenly-spaced horizontal/
    // vertical lines) with a small crosshair box at the center — reads as
    // a radar/MFD screen background grid, plausibly what renders inside
    // kSidePanel's dark viewport area.
    constexpr uint32_t kRadarScreenGrid = 29;
    constexpr uint32_t kRadarScreenGridSVGA = 59;  // 63x45, same grid+crosshair
    // 27x27 / 17x17 (VGA/SVGA pair), 1 frame: a small dashed circle with
    // N/S/E/W tick marks and a center dot — reads as a compass/radar
    // center marker, distinct from (and smaller than) kCompassDial*.
    constexpr uint32_t kCenterMarker = 5;
    constexpr uint32_t kCenterMarkerSVGA = 35;
    // 7x13, 8 frames: a small chevron/arrow shape rotating through 8
    // positions at 45-degree increments — reads as a compass/heading
    // needle, likely overlaid on kCompassDialSmall/Medium/Large.
    constexpr uint32_t kHeadingNeedle = 7;
    constexpr uint32_t kHeadingNeedleSVGA = 37;  // 6x8, same 8-frame rotation
    // User-confirmed (2026-07 session, live in-game): this is the
    // throttle-hand pose sequence, not a head-turn animation as first
    // guessed from its rendered frames alone — distinct from
    // kPilotHandAnimation (id 11), which is the joystick hand instead.
    // See SCCockpit::RenderWC3Instruments(SVGA)'s drawShape/drawAt.
    constexpr uint32_t kThrottleHandAnimation = 12;
    // 37x11 / 13x37 (transposed pair), 1 frame: the same horizontal/
    // vertical gradient-bar bezel style as kGaugeBezelHorizontal/Vertical
    // above, but smaller — likely a second, differently-sized gauge
    // bezel instance rather than a VGA/SVGA scale of the same one (no
    // matching larger/smaller pair confirmed).
    constexpr uint32_t kGaugeBezelHorizontalSmall = 47;
    constexpr uint32_t kGaugeBezelVerticalSmall = 48;
    // 37x33, 1 frame: same scratch/burst damage-crack style as
    // kCanopyDamageCracks*, single frame, smaller — likely another
    // damage-overlay stage or instance, not confirmed which.
    constexpr uint32_t kCanopyDamageCracksSmall = 54;
    // Every one of MEDPIT.IFF's 45 ids has now been visually inspected —
    // either identified above, or grouped into a same-style-as-X note
    // where the exact distinct purpose isn't confirmed. None of the
    // above have been wired up to actually render anywhere yet, and none
    // have been cross-checked for id-consistency against the other 4
    // cockpit files (ARWPIT/EXCPIT/HVYPIT/BOMPIT).
}

/*
    Iff chunk hierarchy:
        CKPT
            INFO    vector<uint8_t>
            ARTP    RSImageSet
            VTMP    RSImageSet
            EJEC    RSImageSet
            GUNF    RSImageSet
            GHUD    RSImageSet
            REAL
                INFO    vector<uint8_t>
                OBJS    RSEntiry
            CHUD
                FILE    string
            MONI
                INFO    vector<uint8_t>
                SPOT    vector<uint8_t>
                SHAP    RLEShape
                DAMG    RLEShape
                MFDS
                    COMM
                        INFO   vector<uint8_t>
                    AARD
                        INFO   vector<uint8_t> 
                        SHAP   RLEShape
                    AGRD
                        INFO    vector<uint8_t> 
                        SHAP    RLEShape
                    GCAM
                        INFO    vector<uint8_t>
                        SHAP    RLEShape
                    WEAP
                        INFO    vector<uint8_t>
                        SHAP    RLEShape
                    DAMG
                        INFO    vector<uint8_t>
                        SHAP    RLEShape    
                INST
                    RAWS
                        INFO    vector<uint8_t>
                        SHAP    RLEShape
                    ALTI
                        INFO    vector<uint8_t>
                        SHAP    RLEShape
                    AIRS
                        INFO    vector<uint8_t>
                        SHAP    RLEShape
                    MWRN
                        INFO    vector<uint8_t>
                        SHAP    RLEShape
            FADE    vector<uint8_t>
*/
struct InfoShape {
    std::vector<uint8_t> INFO;
    uint16_t x{0};
    uint16_t y{0};
    uint16_t width{0};
    uint16_t height{0};
    RLEShape SHAP;
    RSImageSet ARTS;
};
struct InfoRSImageSet {
    std::vector<uint8_t> INFO;
    RSImageSet SHAP;
};

struct RAWSShape {
    std::vector<uint8_t> INFO;
    uint16_t x{0};
    uint16_t y{0};
    uint16_t zoom_x{0};
    uint16_t zoom_y{0};
    uint16_t width{0};
    uint16_t height{0};
    RSImageSet SYMB;
    RLEShape ZOOM;
    RLEShape NORM;
};

struct RealObjs {
    std::vector<uint8_t> INFO;
    // No default previously — left as garbage from `new RSCockpit()` for
    // any cockpit whose parse never reaches REAL>OBJS (every WC3 cockpit:
    // RSCockpit::InitFromWC3Ram/parseWC3_COCK only ever registers SHAP/
    // DETH, never REAL, so parseREAL_OBJS — the only setter — never runs).
    // SCStrike::renderVirtualF16/F22Cockpit dereferenced this
    // unconditionally once this->cockpit->cockpit was non-null, crashing
    // on the garbage pointer (confirmed live: SIGSEGV in
    // SCRenderer::drawModel with a clearly-uninitialized object pointer).
    RSEntity *OBJS{nullptr};
};

/**
 * @struct Moni
 * @brief Struct that represents the Moni hierarchy
 *
 * This struct represents the Moni hierarchy from the RealSpace game.
 * It contains the following fields:
 * - INFO: A vector of bytes containing the INFO data
 * - SPOT: A vector of bytes containing the SPOT data
 * - SHAP: An RLEShape object representing the shape of the cockpit
 * - DAMG: An RLEShape object representing the damaged shape of the cockpit
 * - MFDS: A struct containing the MFDs of the cockpit
 * - INST: A struct containing the instruments of the cockpit
 */
struct Moni {
    std::vector<uint8_t> INFO;
    std::vector<uint8_t> SPOT;
    RLEShape SHAP;
    RLEShape DAMG;
    struct Mfds {
        InfoShape COMM;
        InfoShape AARD;
        InfoShape AGRD;
        InfoShape GCAM;
        InfoShape WEAP;
        InfoShape DAMG;
    } MFDS;
    struct Inst {
        RAWSShape RAWS;
        InfoShape ALTI;
        InfoShape AIRS;
        InfoShape MWRN;
    } INST;
    std::unordered_map<std::string, bool> instruments_present;
};

/**
 * @class RSCockpit
 * @brief Class that represents a RealSpace cockpit
 *
 * This class represents a RealSpace cockpit and is used to parse the data
 * from the RealSpace game. The data is parsed from a blob of memory and the
 * resulting data is stored in this class.
 *
 * @warning This class is still in development and is not fully tested.
 *
 * @see RSCockpit.cpp
 */
class RSCockpit {

private:
    AssetManager *asset_manager;
    std::vector<uint8_t> INFO;
    
    struct Chud {
        std::string FILE;
    } CHUD;
    
    std::vector<uint8_t> FADE;

    void parseCKPT(uint8_t* data, size_t size);
    void parseINFO(uint8_t* data, size_t size);
    void parseARTP(uint8_t* data, size_t size);
    void parseVTMP(uint8_t* data, size_t size);
    void parseEJEC(uint8_t* data, size_t size);
    void parseGUNF(uint8_t* data, size_t size);
    void parseGHUD(uint8_t* data, size_t size);
    void parseREAL(uint8_t* data, size_t size);
    void parseCHUD(uint8_t* data, size_t size);
    void parseMONI(uint8_t* data, size_t size);
    void parseFADE(uint8_t* data, size_t size);
    void parseREAL_INFO(uint8_t* data, size_t size);
    void parseREAL_OBJS(uint8_t* data, size_t size);
    void parseCHUD_FILE(uint8_t* data, size_t size);

    void parseMONI_INFO(uint8_t* data, size_t size);
    void parseMONI_SPOT(uint8_t* data, size_t size);
    void parseMONI_SHAP(uint8_t* data, size_t size);
    void parseMONI_DAMG(uint8_t* data, size_t size);
    void parseMONI_MFDS(uint8_t* data, size_t size);
    void parseMONI_MFDS_COMM(uint8_t* data, size_t size);
    void parseMONI_MFDS_COMM_INFO(uint8_t* data, size_t size);
    void parseMONI_MFDS_AARD(uint8_t* data, size_t size);
    void parseMONI_MFDS_AARD_INFO(uint8_t* data, size_t size);
    void parseMONI_MFDS_AARD_SHAP(uint8_t* data, size_t size);
    void parseMONI_MFDS_AGRD(uint8_t* data, size_t size);
    void parseMONI_MFDS_AGRD_INFO(uint8_t* data, size_t size);
    void parseMONI_MFDS_AGRD_SHAP(uint8_t* data, size_t size);
    void parseMONI_MFDS_GCAM(uint8_t* data, size_t size);
    void parseMONI_MFDS_GCAM_INFO(uint8_t* data, size_t size);
    void parseMONI_MFDS_GCAM_SHAP(uint8_t* data, size_t size);
    void parseMONI_MFDS_WEAP(uint8_t* data, size_t size);
    void parseMONI_MFDS_WEAP_INFO(uint8_t* data, size_t size);
    void parseMONI_MFDS_WEAP_SHAP(uint8_t* data, size_t size);
    void parseMONI_MFDS_DAMG(uint8_t* data, size_t size);
    void parseMONI_MFDS_DAMG_INFO(uint8_t* data, size_t size);
    void parseMONI_MFDS_DAMG_SHAP(uint8_t* data, size_t size);
    void parseMONI_INST(uint8_t* data, size_t size);
    void parseMONI_INST_RAWS_INFO(uint8_t* data, size_t size);
    void parseMONI_INST_RAWS_SHAP(uint8_t* data, size_t size);
    void parseMONI_INST_RAWS_SHAP_SYMB(uint8_t *data, size_t size);
    void parseMONI_INST_RAWS_SHAP_ZOOM(uint8_t *data, size_t size);
    void parseMONI_INST_RAWS_SHAP_NORM(uint8_t *data, size_t size);
    void parseMONI_INST_RAWS(uint8_t *data, size_t size);
    void parseMONI_INST_ALTI(uint8_t* data, size_t size);
    void parseMONI_INST_ALTI_INFO(uint8_t* data, size_t size);
    void parseMONI_INST_ALTI_SHAP(uint8_t* data, size_t size);
    void parseMONI_INST_AIRS(uint8_t* data, size_t size);
    void parseMONI_INST_AIRS_INFO(uint8_t* data, size_t size);
    void parseMONI_INST_AIRS_SHAP(uint8_t* data, size_t size);
    void parseMONI_INST_MWRN(uint8_t* data, size_t size);
    void parseMONI_INST_MWRN_INFO(uint8_t* data, size_t size);
    void parseMONI_INST_MWRN_SHAP(uint8_t* data, size_t size);
    void parseWC3_COCK(uint8_t* data, size_t size);
    void parseWC3_COCK_SHAP(uint8_t* data, size_t size);
    void parseWC3_COCK_DETH(uint8_t* data, size_t size);
    // COCK>VGA/SVGA>FRNT|LEFT|RGHT|BACK|HUD — a per-resolution, per-view
    // instrument/HUD layout tree that parseWC3_COCK previously never even
    // registered a handler for (only SHAP/DETH were read), so it was
    // silently skipped by IFFSaxLexer entirely. FRNT>TARG/INST/VDU/TEXT/WEAP/
    // SYS/DAMG are now decoded (see their own struct comments below). VPRT
    // (16 bytes, 4x int32) is a 3D-view viewport crop box — user-confirmed:
    // it defines the screen region the 3D space view renders into, cropping
    // off the rest for the cockpit frame. Deliberately left unparsed since
    // this engine composites the 3D view via its own OpenGL viewport/camera
    // setup rather than a software-rendered crop rectangle — nothing to
    // extract that applies here. FRNT and HUD are both parsed (HUD is
    // the user-confirmed "cockpit frame off, gauges/MFDs only" layout,
    // real per-cockpit-file data, not a synthesized/hidden-background
    // variant of FRNT); LEFT/RGHT/BACK still carry their own unparsed copy
    // of this whole chunk set.
    void parseWC3_COCK_RES(uint8_t* data, size_t size, bool isSVGA);
    void parseWC3_COCK_RES_FRNT(uint8_t* data, size_t size, bool isSVGA);
    // Registers the same INST/VDU/TEXT/WEAP handlers as FRNT, with
    // isHud=true — TARG is deliberately not registered here: HUD>TARG's box
    // is (-1,-1,-1,-1) on every file checked (an "unbounded" sentinel for
    // the always-on HUD overlay), so there's nothing useful to extract.
    void parseWC3_COCK_RES_HUD(uint8_t* data, size_t size, bool isSVGA);
    void parseWC3_COCK_RES_FRNT_TARG(uint8_t* data, size_t size, bool isSVGA);
    void parseWC3_COCK_RES_FRNT_INST(uint8_t* data, size_t size, bool isSVGA, bool isHud);
    void parseWC3_COCK_RES_FRNT_VDU(uint8_t* data, size_t size, bool isSVGA, bool isHud);
    void parseWC3_COCK_RES_FRNT_TEXT(uint8_t* data, size_t size, bool isSVGA, bool isHud);
    void parseWC3_COCK_RES_FRNT_WEAP(uint8_t* data, size_t size, bool isSVGA, bool isHud);
    // HUD>SYS/DAMG are real, present chunks too (confirmed by direct probe —
    // an earlier assumption that the damage report was FRNT-only was wrong).
    void parseWC3_COCK_RES_FRNT_SYS(uint8_t* data, size_t size, bool isSVGA, bool isHud);
    void parseWC3_COCK_RES_FRNT_DAMG(uint8_t* data, size_t size, bool isSVGA, bool isHud);
    void parseWC3_COCK_RES_FRNT_DAMG_TEXT(uint8_t* data, size_t size, bool isSVGA, bool isHud);


public:

    RLEShape VTMP;
    RSImageSet EJEC;
    RSImageSet GUNF;
    RSImageSet GHUD;
    RSImageSet ARTP;
    // WC3's hi-res "SVGA" front-view background (shape id 0, 640x480,
    // confirmed identical size/id across MEDPIT/ARWPIT/EXCPIT) — kept
    // separate from ARTP (the 320x200 "VGA" version) because it's rendered
    // through its own dedicated GL path; see SCCockpit::RenderHighResBackground.
    RSImageSet ARTP_SVGA;
    // WC3's top-level COCK>DETH is a plain 13-byte filename string (e.g.
    // "meddeath.iff\0", not a shape — see parseWC3_COCK) pointing at a
    // second cockpit-family IFF (DATA\COCKPITS\<name>.IFF) whose own
    // FORM DETH wraps two full-screen (640x480) "1.11" animation
    // sequences: a DETH chunk (pilot-death flip-book, 7 frames for
    // MEDDEATH.IFF) and an EJCT chunk (ejection flip-book, 13 frames).
    // Playback triggering isn't wired up yet — this only resolves and
    // decodes the frame data.
    std::string deathAnimName;
    RSImageSet deathFrames;
    RSImageSet ejectFrames;
    RealObjs REAL;
    Moni MONI;
    RSPlaque PLAQ;

    RSCockpit();
    ~RSCockpit();
    void InitFromRam(uint8_t* data, size_t size);

    // WC3's cockpit IFFs (DATA\COCKPITS\*.IFF) use a top-level "FORM COCK"
    // entirely unrelated to Strike Commander's "FORM CKPT" above — a single
    // SHAP chunk bundles background art and every instrument/gauge sprite as
    // a flat id-keyed shape-pak (see RSWC3Shape.h). The 320x200 "front"
    // background (shape id 30, confirmed identical across MEDPIT/ARWPIT/
    // EXCPIT) populates ARTP[0] so the existing SCCockpit::Render(CP_FRONT)
    // path (fb->drawShape(ARTP.GetShape(face))) displays it unchanged; the
    // 640x480 SVGA background (id 0) populates ARTP_SVGA the same way.
    // Every other id (instruments/gauges — previously decoded then
    // discarded) is now kept in instrumentShapes, keyed by its raw id.
    // Wiring individual gauges up to render at the right screen position
    // is still follow-up work — see WC3ShapeId below for what's been
    // visually identified (by rendering and inspecting each id's frames)
    // so far, out of MEDPIT.IFF's 45 total ids.
    void InitFromWC3Ram(uint8_t* data, size_t size);
    // Every decoded id from COCK>SHAP, including the two backgrounds
    // (which are also duplicated into ARTP/ARTP_SVGA above for the
    // existing render path). Not owned by unique_ptr — matches this
    // codebase's general raw-pointer-ownership convention elsewhere
    // (e.g. RSEntity's own vectors of heap pointers).
    std::unordered_map<uint32_t, RSImageSet*> instrumentShapes;

    // COCK>VGA|SVGA>FRNT>TARG — the real targeting-HUD bounding box and
    // reticle shape ids, decoded from a 24-byte record: 4x int32 (xMin,
    // yMin, xMax, yMax) then 2x uint32 (a primary + secondary shape id into
    // instrumentShapes). Byte-identical across all 3 cockpit files checked
    // (MEDPIT/HVYPIT/ARWPIT) — this is a universal layout, not per-ship.
    // VGA: (0,0,319,112), ids (36,37). SVGA: (0,0,639,268), ids (6,7) — note
    // the VGA view references the ids this file's own WC3CockpitShapeId
    // comments had guessed were "SVGA scale" (36/37) and vice versa (6/7)
    // for SVGA; that earlier VGA/SVGA labeling for ids 6/7/36/37 was a
    // frame-count/size heuristic, not confirmed, and this real reference
    // table contradicts it — flagged here rather than silently "fixed",
    // since which one is actually correct hasn't been re-verified by
    // rendering.
    //
    // Previously, SCCockpit::RenderTargetWithCam used a hardcoded 68x85
    // "is target in HUD view" box instead of this — for a 320-wide screen,
    // that's less than a quarter of the real 319-wide box here, so a
    // target's on-screen position (typically well past x=68 unless dead
    // center-left) would almost always fail the bounds check and never
    // draw anything. Root cause of the targeting display never appearing.
    struct TargetHudBox {
        bool valid{false};
        int xMin{0}, yMin{0}, xMax{0}, yMax{0};
        uint32_t primaryShapeId{0};
        uint32_t secondaryShapeId{0};
    };
    TargetHudBox targetHudVGA;
    TargetHudBox targetHudSVGA;

    // One entry per valid record from COCK>VGA|SVGA>FRNT>INST.
    // Format: 42-byte records; x at offset 2, y at offset 6, shapeId at
    // offset 38 (int16; -1 = disabled/skip). Multiple records may reference
    // the same shapeId at different positions (e.g. four stacked gauge bezels).
    // VGA entries are used for the 320x200 software framebuffer in both VGA
    // and SVGA render modes (the SVGA background is a GL quad underneath;
    // instruments are always drawn on the 320x200 overlay).
    struct WC3InstrumentLayout {
        uint32_t shapeId;
        int16_t  x, y;
        uint16_t mode;
    };
    std::vector<WC3InstrumentLayout> vgaFrontInstruments;
    std::vector<WC3InstrumentLayout> svgaFrontInstruments;
    // Same record format, from COCK>VGA|SVGA>HUD>INST instead of FRNT>INST —
    // the "cockpit frame off" instrument layout (see SCCockpit::
    // WC3CockpitViewMode::HUD_ONLY). A real, separate per-cockpit-file
    // layout, not FRNT reused.
    std::vector<WC3InstrumentLayout> vgaHudInstruments;
    std::vector<WC3InstrumentLayout> svgaHudInstruments;

    // COCK>VGA|SVGA>FRNT>VDU — positions and shape IDs for the left and right
    // VDU (MFD) panel backgrounds. Coordinate fields are LE int32 / 256
    // (8-bit fractional fixed-point, fractional part always 0 in practice).
    // combinedShapeId is -1 on every file checked; leftShapeId/rightShapeId
    // are the SHAP pak IDs to draw as the panel background.
    // leftX/leftY and rightX/rightY are the screen top-left of each panel;
    // in MEDPIT/ARWPIT both panels share the same y and nearly the same x.
    struct WC3VDULayout {
        bool valid{false};
        int32_t combinedShapeId{-1};
        int32_t leftShapeId{-1};
        int32_t rightShapeId{-1};
        int16_t leftX{0}, leftY{0};
        int16_t rightX{0}, rightY{0};
        int16_t width{0}, height{0};

        // rightX/rightY above are byte-identical to leftX/leftY on every
        // cockpit file checked — the file only ever records one physical
        // VDU position, not two. The right panel's actual on-screen
        // position is a workaround: mirror the left origin across the
        // canvas centerline. Confirmed workaround, not a second real
        // coordinate from the file — kept here (rather than inline at each
        // call site) so that caveat travels with the one place this
        // mirroring happens.
        Point2D GetRightOrigin(int16_t canvasWidth) const {
            return {canvasWidth - leftX - width, leftY};
        }
    };
    WC3VDULayout vgaFrontVDU;
    WC3VDULayout svgaFrontVDU;
    // Same format, from COCK>VGA|SVGA>HUD>VDU — see vgaHudInstruments.
    WC3VDULayout vgaHudVDU;
    WC3VDULayout svgaHudVDU;

    // COCK>VGA|SVGA>FRNT>TEXT — cockpit text overlays (speed, throttle, etc.).
    // 34-byte records: [0]=id, [1]=mode (0xff=screen-absolute), [6:26]=text,
    // [26:30]=x LE int32, [30:34]=y LE int32.
    // id 0x01 = KPS (current speed), id 0x02 = SET (throttle setting).
    struct WC3TextEntry {
        uint8_t id;
        uint8_t mode;
        char    text[21];  // 20 bytes from file + null
        int32_t x, y;
    };
    std::vector<WC3TextEntry> vgaFrontText;
    std::vector<WC3TextEntry> svgaFrontText;
    // Same format, from COCK>VGA|SVGA>HUD>TEXT — see vgaHudInstruments.
    std::vector<WC3TextEntry> vgaHudText;
    std::vector<WC3TextEntry> svgaHudText;

    // COCK>VGA|SVGA>FRNT>WEAP — per-hardpoint weapon-icon anchor positions,
    // real per-ship data (unlike VDU/TARG, which are universal across
    // cockpit files). Flat LE int32 stream, no fixed record size:
    // [countA, countA*(x,y)][countB, countB*(x,y)]. Confirmed against real
    // bytes: MEDPIT VGA FRNT WEAP is 56 bytes (14 int32s) = countA=4 (gun-
    // port anchors, evenly spaced) then countB=2 (missile/secondary
    // anchors); ARWPIT VGA FRNT WEAP is 72 bytes (18 int32s) = countA=4,
    // countB=4 with asymmetric mirrored-pair coordinates, consistent with a
    // heavier hardpoint loadout. Group A/B's real meaning (guns vs.
    // missiles, by position in the file) is inferred from MEDPIT's known
    // loadout matching countA/countB — not independently confirmed against
    // a third data point.
    struct WC3WeaponIconAnchor {
        int16_t x, y;
    };
    struct WC3WeaponLayout {
        bool valid{false};
        std::vector<WC3WeaponIconAnchor> groupA;  // primary/gun-port anchors
        std::vector<WC3WeaponIconAnchor> groupB;  // secondary/missile anchors
    };
    WC3WeaponLayout vgaFrontWeap;
    WC3WeaponLayout svgaFrontWeap;
    // Same format, from COCK>VGA|SVGA>HUD>WEAP — see vgaHudInstruments.
    WC3WeaponLayout vgaHudWeap;
    WC3WeaponLayout svgaHudWeap;

    // COCK>VGA|SVGA>FRNT>SYS — the subsystem-damage-report slot table used
    // by the 'd' key: which subsystems are shown, in what order, when the
    // ship has taken damage. Byte-identical across every cockpit file
    // checked (MEDPIT/ARWPIT/BOMPIT/EXCPIT/HVYPIT/EXCPITT, including both
    // copies of EXCPITT.IFF) — a fixed, universal table, not per-ship.
    // 8x10-byte records:
    //   [0]    slot/display-order index — not sequential 0-7 in file order
    //          (observed values 0,4,5,3,8,6,2,1); the "8" instead of an
    //          expected 7 is unexplained.
    //   [1]    subsystem id — matches real FRNT>DAMG>TEXT ids by exact byte
    //          value (16=Comm, 17=Shields, 33=Power, 48=Target, 49=Guns
    //          confirmed); 0xFF = disabled/unused slot (2 of 8 records).
    //          One record's id byte (2) doesn't match any real DAMG id and
    //          its meaning is still unknown; Repair(18)/Engines(34) never
    //          appear as an id byte anywhere in the table.
    //   [2:10] unknown — always 0 except a trailing flag byte (offset 6)
    //          that's 1 on 3 of the 8 records (slot-order 0, 2, and the
    //          unexplained id=2 record) and 0 on the rest; no confirmed
    //          meaning yet.
    struct WC3DamageSubsystemSlot {
        uint8_t slotOrder{0};
        uint8_t subsystemId{0xFF};
        bool flagged{false};  // offset-6 byte; meaning unconfirmed
    };
    std::vector<WC3DamageSubsystemSlot> vgaFrontSys;
    std::vector<WC3DamageSubsystemSlot> svgaFrontSys;
    // Same format, from COCK>VGA|SVGA>HUD>SYS — see vgaHudInstruments.
    std::vector<WC3DamageSubsystemSlot> vgaHudSys;
    std::vector<WC3DamageSubsystemSlot> svgaHudSys;

    // COCK>VGA|SVGA>FRNT>DAMG>TEXT — the subsystem name labels shown by the
    // 'd' key's damage report, keyed by the same subsystem id referenced
    // from SYS above. 16-byte records: [0]=id (uint8), [1:16]=null-
    // terminated ASCII label. Confirmed real: 16=Comm, 17=Shields,
    // 18=Repair, 32=Afterburner, 33=Power, 34=Engines, 48=Target, 49=Guns.
    struct WC3DamageLabel {
        uint8_t id{0};
        char label[16]{};
    };
    std::vector<WC3DamageLabel> vgaFrontDamageLabels;
    std::vector<WC3DamageLabel> svgaFrontDamageLabels;
    // Same format, from COCK>VGA|SVGA>HUD>DAMG>TEXT — see vgaHudInstruments.
    std::vector<WC3DamageLabel> vgaHudDamageLabels;
    std::vector<WC3DamageLabel> svgaHudDamageLabels;
};
