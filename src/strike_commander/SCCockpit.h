//
//  SCCockpit.h
//  libRealSpace
//
//  Created by Rémi LEONARD on 02/09/2024.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//
#pragma once
#ifndef __libRealSpace__SCCockpit__
#define __libRealSpace__SCCockpit__

#include "precomp.h"


struct HudLine {
    Point2D start;
    Point2D end;
};

class SCCockpit {
private:
    AssetManager &Assets = AssetManager::getInstance();
    RSFontManager &FontManager = RSFontManager::getInstance();
    RSMixer &Mixer = RSMixer::getInstance();
    RSVGA &VGA = RSVGA::getInstance();
    SCMouse &Mouse = SCMouse::getInstance();
    std::vector<HudLine> horizon;
    bool project_to_screen(Vector3D coord, int &Xout, int &Yout);
    // Real WC3 MFD panel size from the cockpit file's own COCK>VDU chunk
    // (vgaFrontVDU/svgaFrontVDU, selected by g_ifVGA — same convention
    // RenderTargetWithCam already uses for targetHudVGA/SVGA), falling back
    // to the old 115x95 screen-edge guess only if the file has no valid VDU
    // chunk (or this isn't a WC3 cockpit at all). Shared by every RenderMFDS*
    // function that needs to center/bound content within the panel, so they
    // all agree on the same size Render()'s pmfd_left/pmfd_right computation
    // and SCCockpit::init()'s framebuffer allocation use.
    Point2D GetWC3MfdSize();
    void IdentifyRAWSContact(SCMissionActors *actor, FrameBuffer *fb, float headingRad, Point2D pmfd_left, Point2D raws_size, bool is_zoomed, int rsize);
    void RenderTargetingReticle(FrameBuffer *fb, CHUD_SHAPE *reticleShape, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter);
    void RenderStraffingReticle(FrameBuffer *fb, CHUD_SHAPE *reticleShape, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter);
    void RenderBombSight(FrameBuffer* fb, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter);
    void RenderMFDS(Point2D mfds, FrameBuffer *fb);
    void RenderMissileHud(Point2D position, FrameBuffer *fb, CHUD *hud, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter);
    void RenderIrTargetHud(Point2D position, FrameBuffer *fb, CHUD *hud, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter);
    void RenderMFDSRadarImplementation(Point2D pmfd_left, float range, const char* mode_name, bool air_mode, FrameBuffer *fb);
    void RenderMFDSRadarSingleTargetImplementation(Point2D pmfd_left, float range, const char *mode_name, bool air_mode, FrameBuffer *fb);
    void BuildPaletteLUT();
    // Lock-on: accumulates lock_progress (elapsed seconds held within
    // target_in_range AND a per-weapon aspect check) each Update() tick.
    // The aspect check is SCMissionActors::ClassifyHitQuadrant, not a flat
    // boresight cone: heatseeker requires the player behind the target
    // (engines visible), every other lock-required weapon just requires the
    // target generally in front of the player. See target_locked/
    // lock_progress's own comments for how this is consumed.
    void updateLockOn();
    void RenderTargetBox(FrameBuffer *fb, Point2D hudTopLeft, Point2D hudBottomRight, Point2D hudCenter);
    uint32_t m_lastLockTicks{0};
    void printTTAG(Point2D pos, HUD_POS &tag, std::string name, FrameBuffer *fb, RSFont *font);
    // WC3's hi-res (640x480) "SVGA" cockpit background, drawn as its own
    // GL-textured fullscreen quad rather than through the 320x200 software
    // FrameBuffer path the rest of Render() uses — see RSCockpit::ARTP_SVGA.
    // Uploaded once and cached since the background art is static.
    Texture *hires_bg_texture{nullptr};
    GLuint hires_bg_gl_id{0};
    bool hires_bg_uploaded{false};
    int hires_bg_w{0};
    int hires_bg_h{0};
    void RenderHighResBackground();
    RSFont *font;
    RSFont *big_font;
    int radio_mission_timer{0};
    std::unordered_map<uint32_t, uint8_t> palette_lut;
    std::unordered_map<std::string, std::string> hud_text_tags;
    bool palette_lut_dirty = true;
    int current_weapon_id{-1};
    Camera cockpit_camera;
    MISN_PART *current_target{nullptr};
    SCMissionActors *current_target_actor{nullptr};
    // WC3 cockpit animation state — time accumulator and frame counter
    // advanced in RenderWC3Instruments each frame to cycle cockpit overlays.
    float wc3_anim_time{0.0f};
    int   wc3_anim_frame{0};
    // useHud selects the cockpit file's COCK>VGA|SVGA>HUD instrument layout
    // instead of its FRNT layout — see WC3CockpitViewMode::HUD_ONLY.
    void RenderWC3Instruments(FrameBuffer* fb, bool useHud = false);
    void RenderWC3InstrumentsSVGA(FrameBuffer* fb, bool useHud = false);
    // True once the player's own front-quadrant shield or armor has taken
    // any damage — user-confirmed (2026-07 session): WC3CockpitShapeId::
    // kCompassDialMedium (id 24) is the real Tactical Display/radar screen
    // (see GetCompassAreaScreenPos), but only meant to be drawn once this
    // is true.
    bool IsPlayerFrontQuadrantDamaged();
    // "The predefined compass area" — user-confirmed (2026-07 session) as
    // the real Tactical Display/radar screen rectangle, independent of
    // both VDU panels. Returns false (rect undefined) if neither a
    // per-cockpit override (see SCCockpit.cpp's own comment) nor a FRNT
    // INST record for WC3CockpitShapeId::kCompassDialMedium exists. Used
    // by RenderMFDSTargetRadarWC3 (radar dial/dots) and
    // RenderVDUDamageOverlay (radar-damage frame position).
    bool GetCompassAreaRect(bool isSVGA, Point2D &topLeft, Point2D &bottomRight);
    // Convenience wrapper returning just topLeft — see GetCompassAreaRect.
    Point2D GetCompassAreaScreenPos(bool isSVGA, bool &found);
    // Draws WC3CockpitShapeId::kCanopyDamageCracks(SVGA)'s 3 frames as a
    // per-component VDU-area damage overlay — user-confirmed (2026-07
    // session): frame 0 = left VDU (ShipComponent::VDU1) damage, frame 1 =
    // right VDU (VDU2), frame 2 = radar/Tactical Display damage (drawn at
    // GetCompassAreaScreenPos), each only drawn once that specific
    // component's damage is nonzero. Previously misidentified as generic
    // "canopy damage cracks" art and not drawn at all (see this
    // function's own .cpp comment).
    void RenderVDUDamageOverlay(FrameBuffer* fb, const RSCockpit::WC3VDULayout& vdu, bool isSVGA);
    // Sets g_ifVGA + the real VGA canvas resolution, then resizes every
    // cockpit-owned, resolution-sized framebuffer to match — these are
    // otherwise only ever sized once in init() from g_ifVGA's value at that
    // moment and never revisited, which was fine while VGA/SVGA was a
    // rarely-touched settings-menu choice but not once CycleViewMode makes
    // it a live, frequently-used in-flight toggle.
    void SetWC3Resolution(bool vga);
public:
    // F1 cycles through these. SVGA_COCKPIT/VGA_COCKPIT are the existing
    // full cockpit rendering (background art + FRNT instrument layout) at
    // each resolution; HUD_ONLY keeps whichever resolution was last active
    // but skips the cockpit background/frame entirely and reads the
    // cockpit file's separate COCK>HUD instrument layout instead of FRNT's
    // — real WC3 cockpit files carry a distinct instrument layout
    // specifically for this (gauges/MFDs with no cockpit frame around
    // them), not just FRNT redrawn without a background.
    enum class WC3CockpitViewMode { SVGA_COCKPIT, VGA_COCKPIT, HUD_ONLY };
    WC3CockpitViewMode cockpit_view_mode{WC3CockpitViewMode::SVGA_COCKPIT};
    void CycleViewMode();
    // Composites an arbitrary 640x480 RLEShape (e.g. a frame from
    // RSCockpit::deathFrames/ejectFrames) over whatever's already been
    // drawn this frame — same hi-res GL-textured-quad coordinate space
    // RenderHighResBackground uses for RSCockpit::ARTP_SVGA, but alpha
    // blended (palette index 255 treated as transparent, same convention
    // RSVGA::vSync uses for the low-res instrument overlay) rather than
    // opaque. deathFrames/ejectFrames are full 640x480 canvases but mostly
    // transparent — only a small region (the ejection seat/pilot art) is
    // actually painted, so this must run *after* the cockpit background is
    // drawn, not instead of it, or the background disappears and the small
    // painted region loses its visual anchor. Rebuilds the texture on every
    // call instead of caching — meant for a handful-of-frames-per-second
    // flip-book, not a hot per-frame path.
    void RenderCockpitOverlayShape(RLEShape *shape);
    VGAPalette palette;
    RSCockpit* cockpit{nullptr};
    RSHud* hud{nullptr};
    FrameBuffer *hud_framebuffer{nullptr};
    FrameBuffer *mfd_right_framebuffer{nullptr};
    FrameBuffer *mfd_left_framebuffer{nullptr};
    FrameBuffer *raws_framebuffer{nullptr};
    FrameBuffer *target_framebuffer{nullptr};
    FrameBuffer *alti_framebuffer{nullptr};
    FrameBuffer *speed_framebuffer{nullptr};
    FrameBuffer *shield_framebuffer{nullptr};
    FrameBuffer *comm_framebuffer{nullptr};
    FrameBuffer *debug_framebuffer{nullptr};
    float pitch{0.0f};
    float roll{0.0f};
    float yaw{0.0f};

    float speed{0.0f};
    float mach{0.0f};
    float altitude{0.0f};
    float heading{0.0f};

    float g_limit{0.0f};
    float g_load{0.0f};
    float pitch_speed{0.0f};
    float yaw_speed{0.0f};
    float roll_speed{0.0f};

    bool gear{false};
    bool flaps{false};
    bool airbrake{false};
    bool mouse_control{false};
    bool show_radars{false};
    bool show_weapons{false};
    bool show_damage{false};
    bool show_comm{false};
    bool show_cam{false};
    bool show_power{false};
    // User-confirmed real MFD page (2026-07 session). Maps to
    // COCK>FRNT>INST mode=0x00 — the same page id as SYS's own slot 0,
    // whose subsystem id (17) is the real "Shields" entry — two
    // independent chunks agreeing. See RenderMFDSShield.
    bool show_shield{false};
    // Power-allocation MFD state (user-confirmed real behavior, 2026-07
    // session): MDFS_POWER ('p') rotates through 5 states — the 4 gauges
    // (each highlighted in turn) then the shield/hull MFD, back to the
    // first gauge — via CyclePowerOrShield(). Order top-to-bottom/index
    // 0-3 is Engines/Weapons/Shields/Damage-repair (the E/W/S/D letters
    // drawn beside each gauge), matching the real per-file mode=0x060a
    // bezel group's file order. The 4 values always sum to
    // kTotalPowerUnits; AdjustSelectedPower moves power between the
    // selected gauge and the other three (see its own comment).
    int selected_power_gauge{0};
    float power_alloc[4]{25.0f, 25.0f, 25.0f, 25.0f};
    static constexpr float kTotalPowerUnits = 100.0f;
    void CyclePowerOrShield();
    void AdjustSelectedPower(float delta);
    bool target_in_range{false};
    // Lock-on state, computed each Update() tick by updateLockOn() (see
    // its own comment). lock_progress is elapsed seconds held within
    // range+boresight-cone, UNCAPPED (not normalized 0..1) — resets to 0
    // the instant either condition breaks. kLockThresholdSeconds/
    // target_locked are a generic "locked enough" indicator for HUD use;
    // SCPlane::Shoot() itself no longer reads this constant for its own
    // guided-vs-dumbfire/lock-required decisions — every WC3 missile/
    // torpedo/T-bomb (weapon_category 2 or 3) instead compares
    // lock_progress directly against its own real, per-weapon
    // wdat->lock_time_required_seconds (RSEntity::parseREAL_OBJT_MISL_
    // DATA), since that genuinely varies per weapon (e.g. Friend-or-Foe
    // needs 0s, Image-Recognition needs 1s — confirmed real data).
    static constexpr float kLockThresholdSeconds = 1.5f;
    bool target_locked{false};
    float lock_progress{0.0f};
    // Set every frame by SCStrike (mirrors SCStrike::CanEngageAutopilot(),
    // which SCCockpit has no direct access to call itself) — drives the
    // WC3CockpitShapeId::kAutopilotLabel status light. True = the AUTOPILOT
    // key would actually engage right now, not that autopilot is currently
    // active (there's no "autopilot in progress" state to show — engaging
    // is an instant teleport, see SCStrike::autopilotCompute).
    bool autopilot_available{false};
    // Sticky target-selection lock, toggled by LOCK_TARGET (L). Independent
    // of target_locked/lock_progress above: this only controls which actor
    // stays selected as current_target (persists even if the player looks
    // away) and which HUD target-box style is drawn (solid square vs.
    // broken bracket) — it does NOT by itself satisfy any weapon's aspect
    // requirement. See SCStrike::updateAutoTarget for how this gates
    // auto-targeting, and updateLockOn's own comment for why lock_progress
    // still requires real-time correct aspect regardless of this flag.
    bool target_hard_locked{false};
    RadarMode radar_mode{RadarMode::AARD};
    int radar_zoom{1};
    int throttle{0};
    int comm_target{0};
    SCMissionActors *comm_actor{nullptr};
    float way_az{0};
    MISN_PART *target{nullptr};
    MISN_PART *player{nullptr};
    RSProf *player_prof{nullptr};
    std::vector<MISN_PART *> parts;
    std::vector<SCAiPlane *> ai_planes;
    Camera *cam;
    CockpitFace face{CockpitFace::CP_FRONT};
    Vector2D weapoint_coords;
    SCPlane *player_plane;
    SCMission *current_mission;
    uint8_t *nav_point_id{nullptr};
    Hud_weapon_mode weapon_mode{Hud_weapon_mode::WM_HUD_NONE};
    Vector3D hud_eye_world = {0.0f, 0.0f, 0.0f};
    bool has_hud_eye_world = false;
    bool debug_print{false};
    bool big_cockpit{false};
    bool is_3d_cockpit{false};
    bool is_f16_cockpit{true};
    bool is_shooting{false};
    
    // Offset angulaire pour le viseur cannon (en radians)
    // x = azimut, y = élévation
    // En 2D: {0, 0}, en 3D: ajuster selon la géométrie
    Vector2D cannonAngularOffset = {0.0f, 0.0f};
    Vector3D targetImpactPointWorld = {0.0f, 0.0f, 0.0f};
    SCCockpit();
    ~SCCockpit();
    void init( );
    void Update();
    void Render(CockpitFace face);
    void RenderHUD();
    void RenderHUD(Point2D position, FrameBuffer *fb);
    void RenderMFDSWeapon(Point2D pmfd_right, FrameBuffer *fb);
    void RenderMFDSRadar(Point2D pmfd_left, float range, int mode, FrameBuffer *fb);
    void RenderMFDSComm(Point2D pmfd_left, int mode, FrameBuffer *fb);
    void RenderMFDSDamage(Point2D pmfd_left, FrameBuffer *fb);
    // Right MFD, always on regardless of show_radars/show_weapons/etc (see
    // RenderHUD's dispatch): the generic COCK>SHAP scanning animation while
    // no target is selected, then TARGSHAP.PAK's per-ship diagram once one
    // is (see TargShapeIndex in SCCockpit.cpp).
    void RenderMFDSTarget(Point2D pmfd, FrameBuffer *fb);
    // WC3-specific left MFD "Target Radar" (COCK>SHAP id 15 background):
    // contacts plotted by angle-off-boresight rather than a top-down world
    // map — dead ahead lands at the panel center, directly behind lands on
    // the outer ring. See RenderMFDSRadar's dispatch for how this and the
    // SC-format (F16/F22) radar implementations are selected.
    void RenderMFDSTargetRadarWC3(Point2D pmfd, float range, FrameBuffer *fb);
    // Predicted (lead) intercept point in world space for the player's
    // own gun against current_target_actor — reusable version of
    // RenderTargetingReticle's own inline calc (that one's SC-only,
    // gated behind HUD.IFF/this->hud) for RenderWC3TargetingReticle's
    // frame-0 lead marker. Returns the player's own position if there's
    // no player/target to aim at.
    Vector3D ComputeTargetLeadPoint();
    // Screen position of the aircraft's own dead-ahead boresight —
    // NOT simply fb->width/2,fb->height/2: project_to_screen's own
    // projection math has an asymmetric baked-in offset (see its own
    // body), so the true "straight ahead" point doesn't land at the
    // framebuffer's geometric center. kCenterMarker (id 5, the boresight
    // crosshair) and RenderWC3TargetingReticle's lead marker (id 6,
    // frame 0) both need to agree on the same point for "align the
    // crosshair with the lead marker to hit" to be geometrically true,
    // so both go through this instead of independently guessing.
    Point2D GetWC3BoresightScreenPos(FrameBuffer *fb);
    // WC3's own targeting-reticle shape (kTargetingReticle, id 6) —
    // user-confirmed real per-frame meaning (2026-07 session): frame 0
    // lead marker, frame 1 next-waypoint cross, frame 2 off-screen
    // target edge tracker, frame 3 mouse cursor. See its own body
    // comment for details; not the generic looping instrument every
    // other unhandled id gets via RenderWC3Instruments(SVGA).
    void RenderWC3TargetingReticle(FrameBuffer *fb);
    void RenderMFDSPower(Point2D pmfd, FrameBuffer *fb);
    void RenderMFDSShield(Point2D pmfd, FrameBuffer *fb);
    void RenderRAWS(Point2D pmfd_left, FrameBuffer *fb);
    void RenderRAWSBig(Point2D pmfd_left, FrameBuffer *fb);
    void RenderTargetWithCam(Point2D top_left, FrameBuffer *fb);
    // Player's own shield/hull status, driven by SCMissionActors::health
    // (now decremented on hit — see hasBeenHit) against the ship's max
    // health (RSEntity::health). Only meaningful for genuine WC3 cockpits:
    // reads RSCockpit::instrumentShapes[WC3CockpitShapeId::kBarMeterFillA],
    // which SC-format cockpits (F16/F22) never populate. No-op if that
    // shape id isn't present.
    void RenderShieldGauge(Point2D top_left, FrameBuffer *fb);
    void RenderAlti(Point2D alti_pos, FrameBuffer *fb);
    void RenderSpeedOmetter(Point2D speed_top_left, FrameBuffer *fb);
    bool RenderCommMessages(Point2D pmfd_text, FrameBuffer *fb);
    void RenderMFDSCamera(Point2D pmfd_left, FrameBuffer *fb);
    void SetCommActorTarget(int target);
    void RenderTextTags(Point2D position, FrameBuffer *fb, CHUD *hud, RSFont *font);
    void RenderAltiBandRoll(Point2D alti_top_left, FrameBuffer *fb, RSFont *font, CHUD_SHAPE *alti_band_roll);
    void RenderSpeedBandRoll(Point2D speed_top_left, FrameBuffer *fb, RSFont *sfont, CHUD_SHAPE *speed_band);
    void RenderHeadingCompas(Point2D heading_top_left, FrameBuffer *fb, RSFont *sfont, CHUD_SHAPE *heading_compas);
    void RenderPitchLadder(Point2D center, Point2D clip_size, FrameBuffer *fb, SLADD *ladd, RSFont *ft);
};
#endif