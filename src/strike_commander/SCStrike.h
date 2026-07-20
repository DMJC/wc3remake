#pragma once
//
//  SCStrike.h
//  libRealSpace
//
//  Created by fabien sanglard on 1/28/2014.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//
#include "precomp.h"

#ifndef __libRealSpace__SCStrike__
#define __libRealSpace__SCStrike__

#define SCSTRIKE_MAX_MISSIONS 47
static const char *mission_list[] = {
    "MISN-1A.IFF",  "MISN-1B.IFF",  "MISN-1C.IFF",  "MISN-2A.IFF",  "MISN-3A.IFF",  "MISN-3B.IFF",  "MISN-3C.IFF",
    "MISN-4A.IFF",  "MISN-4B.IFF",  "MISN-4C.IFF",  "MISN-5A.IFF",  "MISN-5B.IFF",  "MISN-5BB.IFF", "MISN-5C.IFF",
    "MISN-6A.IFF",  "MISN-6B.IFF",  "MISN-6C.IFF",  "MISN-6D.IFF",  "MISN-7A.IFF",  "MISN-7B.IFF",  "MISN-7C.IFF",
    "MISN-7D.IFF",  "MISN-8A.IFF",  "MISN-8B.IFF",  "MISN-8C.IFF",  "MISN-9AA.IFF", "MISN-9A.IFF",  "MISN-9B.IFF",
    "MISN-10A.IFF", "MISN-10B.IFF", "MISN-10C.IFF", "MISN-10D.IFF", "MISN-11A.IFF", "MISN-11B.IFF", "MISN-11C.IFF",
    "MISN-12A.IFF", "MISN-12B.IFF", "MISN-13A.IFF", "MISN-13B.IFF", "MISN-14A.IFF", "MISN-14B.IFF", "MISN-15A.IFF",
    "MISN-6X.IFF",  "MISN-8X.IFF",  "MISN-10X.IFF", "MISN-11X.IFF", "TEMPLATE.IFF"
};
static int mission_idx = 0;

/**
 * @class SCStrike
 * @brief Implements the main game logic for Strike Commander
 *
 * This class implements the main game logic for Strike Commander. It handles
 * mission selection, keyboard input, game simulation, and rendering.
 */
class SCStrike : public IActivity {
protected:

    bool air_weapons_mode{false};
    /**
     * @brief Path to current mission file
     */
    std::string miss_file_name;
    /**
     * @brief Current camera mode
     */
    uint8_t camera_mode;
    /**
     * @brief Whether to control the plane with the mouse
     */
    bool mouse_control;
    /**
     * @brief Whether to pause the simulation
     */
    bool pause_simu{false};
    /**
     * @brief The main game camera
     */
    Camera *camera;
    /**
     * @brief The current position of the camera
     */
    Point3D camera_pos;
    /**
     * @brief The target position of the camera
     */
    Point3D target_camera_pos;
    // World-space anchor for View::TRACK ("Track Camera", F10) — set once
    // when the mode is engaged (see checkKeyboard's VIEW_TRACK handler),
    // then held fixed while the camera continuously re-aims at the player
    // each frame, so flying away from it reads as a real tracking shot
    // instead of a second chase cam. See View::TRACK's own comment
    // (SCenums.h).
    Vector3D track_camera_anchor{0, 0, 0};
    Point3D light{4.0f, 4.0f, 4.0f};
    /**
     * @brief The yaw of the camera
     */
    float yaw;
    /**
     * @brief The new position of the plane
     */
    Point3D new_position;
    /**
     * @brief The current lookat position of the pilot
     */
    Point2D pilote_lookat;
    int eye_y{3};
    /**
     * @brief The new orientation of the plane
     */
    Quaternion new_orientation;
    /**
     * @brief The current area
     */
    RSArea *area;
    /**
     * @brief The navigation screen
     */
    SCNavMap *nav_screen;
    /**
     * @brief The player plane
     */
    SCPlane *player_plane;

    /**
     * @brief The cockpit
     */
    SCCockpit *cockpit;

    /**
     * @brief Whether to use autopilot
     */
    bool autopilot{false};
    int autopilot_timeout{0};
    float autopilot_target_azimuth{0};
    // Which scripted autopilot sequence (if any) is currently driving
    // camera_mode==View::AUTO_PILOT. NONE means the pre-existing generic
    // "teleport to current waypoint" behavior (autopilotCompute) — TAKEOFF/
    // LANDING are the new hangar-bay launch/dock sequences, played back by
    // updateAutopilotSequence() instead of the generic per-frame trailing-
    // camera logic. See beginAutopilotSequence/updateAutopilotSequence.
    enum class AutopilotSequence { NONE, TAKEOFF, LANDING };
    AutopilotSequence autopilot_sequence{AutopilotSequence::NONE};
    // Start/end transforms captured once when a TAKEOFF/LANDING sequence
    // begins (beginAutopilotSequence), then interpolated per-frame by
    // updateAutopilotSequence(). yaw is in the same tenths-of-a-degree
    // convention as SCPlane::azimuthf/yaw.
    Vector3D autopilot_seq_start_pos{};
    Vector3D autopilot_seq_end_pos{};
    float autopilot_seq_start_yaw{0.0f};
    float autopilot_seq_end_yaw{0.0f};
    /**
     * @brief The pilot AI
     */
    SCPilot pilot;
    /**
     * @brief The player profile
     */
    RSProf *player_prof;
    /**
     * @brief A counter
     */
    float counter;
    /**
     * @brief The current navigation point
     */
    uint8_t nav_point_id{0};
    // Last-seen current_mission->waypoints.size(), used by updateNavPoint()
    // to detect when mission scripting pushes a new current-objective
    // waypoint (SCMissionActorsPlayer::takeOff/land/flyToWaypoint/
    // flyToArea) so nav_point_id can auto-follow it, without stomping a
    // manual pick made via the nav-map screen (which writes nav_point_id
    // directly) on every frame in between.
    size_t last_nav_point_count{0};
    /**
     * @brief The current target — index into current_mission->enemies, or
     * -1 for none. Was uint8_t (silently wrapped -1 to 255); widened to int
     * since auto-targeting now reads/writes this every frame.
     */
    int current_target{0};
    /**
     * @brief A cache of objects
     */
    std::unordered_map<std::string, RSEntity *> object_cache;
    /**
     * @brief The AI planes
     */
    std::vector<SCAiPlane *> ai_planes;
    /**
     * @brief The MFD timeout
     */
    int32_t mfd_timeout{0};
    // Afterburner sound state ('48_8bit_11025.wav', user-identified —
    // 3.74s, 8-bit mono 11025Hz). Real behavior: play the full clip once
    // from the moment the key is pressed, then once only the last second
    // of it remains, loop just that tail for as long as the key stays
    // held; stop immediately on release. See
    // SCStrike::updateAfterburnerSound.
    bool afterburner_sound_engaged_prev{false};
    float afterburner_sound_elapsed{0.0f};
    bool afterburner_sound_tail_started{false};
    void updateAfterburnerSound(bool engaged);

    uint8_t current_object_to_view{0};

    SCMissionActors *target{nullptr};

    SCMission *current_mission{nullptr};
    int radio_mission_timer{0};
    bool show_bbox{false};
    bool follow_dynamic{false};
    int music{2};
    RSEntity * loadWeapon(std::string name);
    void registerSimulatorInputs();
    Keyboard *m_keyboard{nullptr};


    void autopilotCompute();
    void findTarget();
    // Continuously re-targets whatever in-range enemy is in front of the
    // player (nearest if several), called every frame. No-op while
    // cockpit->target_hard_locked is true (see SCCockpit::target_hard_locked)
    // — auto-target never overrides a sticky lock, it only re-engages once
    // that lock is released or its target becomes invalid.
    void updateAutoTarget();
    // Auto-advances nav_point_id to the next waypoint once the player gets
    // within kNavPointReachedRadius of the current one — previously nothing
    // ever advanced this (it stayed at 0, or wherever the nav-map screen's
    // manual override last set it, for the whole mission). Called every
    // frame from runFrame(), alongside updateAutoTarget().
    void updateNavPoint();
    // Real WC3 behavior: autopilot can't be engaged with non-capital-ship
    // enemies nearby (capital ships don't block it — matches
    // SCCockpit::RenderMFDSTargetRadarWC3's same capital/non-capital split).
    // Checked once, on the AUTOPILOT keypress itself, not continuously —
    // if hostiles show up mid-transit that's not modeled here, matching the
    // instant-teleport (not gradual-travel) nature of autopilotCompute().
    bool CanEngageAutopilot();
    // Shared "find the carrier" scan (flight_deck_entity != nullptr) —
    // factored out of getDockedCarrierYawTenths so beginAutopilotSequence
    // can reuse it. Returns false (outActor untouched) if no mission or no
    // carrier-type actor exists.
    bool findCarrierActor(SCMissionActors *&outActor);
    // Starts a scripted TAKEOFF/LANDING autopilot sequence: records start/
    // end transforms (end = a point projected from the carrier's live
    // heading), sets camera_mode=View::AUTO_PILOT/autopilot_sequence/
    // autopilot_timeout. No-op if no carrier actor is found in this
    // mission. See the 'A' keypress handler in checkKeyboard() for when
    // this fires vs. the pre-existing generic autopilotCompute().
    void beginAutopilotSequence(AutopilotSequence kind);
    // Per-frame playback of a TAKEOFF/LANDING sequence, called from
    // runFrame()'s View::AUTO_PILOT case instead of the generic trailing-
    // camera logic whenever autopilot_sequence != NONE. Interpolates the
    // plane's position/yaw between the recorded start/end transforms; for
    // TAKEOFF drives a 2-phase camera (front-above swinging to underneath,
    // then a hard cut to the normal chase cam) per a real user-observed
    // shot description; for LANDING uses the chase-cam framing for the
    // whole sequence as a placeholder (no real shot description available
    // yet). On completion, snaps to the exact end transform and either
    // reverts to View::FRONT (TAKEOFF — has_left_carrier_bay picks up
    // naturally via SCMission::update()'s own area-crossing check) or sets
    // player_plane->landed=true first (LANDING — reuses the existing
    // manual-landing mission-end path).
    void updateAutopilotSequence();

    float mouseAxisAccumX = 0.0f;
    float mouseAxisAccumY = 0.0f;
    SCState &GameState = SCState::getInstance();
    void renderVirtualCockpit();
    FrameBuffer *virtual_mouse_cockpit_buffer{nullptr};
    static void cleanupVirtualCockpitTextures();
    Vector2D gunsight_hud_offset{0.0f, -0.077f};
    bool zoom_cockpit{false};
    void renderVirtualF16Cockpit();
    void renderVirtualF22Cockpit();
    int world_lod{BLOCK_LOD_MAX};
public:
    float verticalOffset{0.45f};
    /**
     * @brief Constructor
     */
    SCStrike();
    /**
     * @brief Destructor
     */
    ~SCStrike();
    /**
     * @brief Initializes the game
     */
    void init();
    /**
     * @brief Sets the current mission
     * @param missionName The name of the mission to load
     */
    void setMission(char const *missionName);
    void setCameraFront();
    void setCameraFollow(SCPlane *plane);
    // Diagnostic camera: positioned below the ship's true belly (using its
    // actual, unfudged up vector -- unlike setCameraFollow's chase-cam up,
    // which is deliberately skewed by -100 tenths-of-degree for framing),
    // looking up at it, to visually confirm gun hardpoint mount positions
    // against the hull.
    void setCameraBelow(SCPlane *plane);
    void setCameraRLR();
    void setCameraLookat(Vector3D obj_pos);
    // While docked inside a capital ship's hangar bay (flight_deck_entity,
    // see the actor render loop), the player's own tracked yaw can't be
    // trusted to face down the corridor: it comes from the mission's own
    // PART record just like every other actor's, but rendering the hangar
    // interior and the carrier's exterior hull both key off the carrier's
    // OWN azymuth, not the player's — so the two only line up if we
    // explicitly borrow the carrier's heading for the camera while docked.
    // Returns true and fills yawTenths (same tenths-of-a-degree convention
    // as SCPlane::azimuthf) if the player is currently within a hangar.
    bool getDockedCarrierYawTenths(float &yawTenths);
    /**
     * @brief Checks for keyboard input
     */
    void checkKeyboard(void);
    /**
     * @brief Runs the game simulation
     */
    void runFrame(void);

protected:
    // Hook called from runFrame() the instant mission_ended is detected —
    // current_mission (and its mission_won outcome) is still fully valid
    // here, unlike after Game->stopTopActivity() returns control to
    // whichever activity is now on top: this SCStrike gets popped and
    // deleted on the *next* GameEngine::run() loop iteration, so by the
    // time that activity's own runFrame() runs, this object (and anything
    // it held a raw pointer to) is already gone. Empty by default — SC's
    // own game-state bookkeeping above already runs unconditionally in this
    // same block, independent of this hook. WC3Strike overrides this to
    // capture the mission outcome for WC3GameFlow's own use.
    virtual void onMissionEnded() {}
    // Hook called once per checkKeyboard() — same empty-by-default/override
    // pattern as onMissionEnded() above, for the same layering reason:
    // strike_commander is the shared SC/WC3 base and must stay game-
    // agnostic, so anything needing a game-specific class (e.g. WC3Strike's
    // Alt+X quit-confirm overlay, which lives in wing_commander_3/) can't
    // be added directly in this file's own checkKeyboard(). checkKeyboard()
    // itself is deliberately NOT virtual (see its own declaration above),
    // so a subclass override of it would silently never run — this hook
    // exists so WC3Strike (or any other SCStrike subclass) has a real,
    // working extension point for extra per-frame key checks instead.
    virtual void checkGameSpecificKeyboard() {}
    // Factory hook, same layering reason as the two above: checkKeyboard()'s
    // SHOW_NAVMAP handler needs to construct a SCNavMap, but WC3 missions
    // need the WC3NavMap subclass (wing_commander_3/) specifically — see
    // SCNavMap::drawWC3TextOverlay's own comment — which this file can't
    // name directly. Defaults to the plain SC1 SCNavMap; WC3Strike
    // overrides this to return a WC3NavMap instead. Defined in SCStrike.cpp,
    // not inline here: by this point in the header, precomp.h has only
    // forward-declared SCNavMap (enough for the pointer return type, not
    // enough to construct one) — the .cpp sees the real SCNavMap.h.
    virtual SCNavMap* createNavMap();
    // Hook called once per runFrame() while the mission is live, same
    // empty-by-default/override pattern as onMissionEnded()/
    // checkGameSpecificKeyboard() above. Default plays SC1's bank-based
    // victory/defeat/in-combat music switch (Mixer.playMusic(uint32_t),
    // RSMusic/AMUSIC.PAK) — that bank is never populated for WC3 (see
    // WC3Strike::setMission's own comment), so leaving this unguarded for
    // WC3 missions spammed "No music found for index N in bank 0" to
    // stdout every single frame. WC3Strike overrides this to a no-op:
    // WC3 mission music is set once at mission start via setMission's
    // playMissionMusic() (routed through WC3GameFlow's own GM-based
    // system), and there's no per-combat-state WC3 track set identified/
    // wired yet to hot-switch to here.
    virtual void updateMissionMusic();
};

#endif /* defined(__libRealSpace__SCStrike__) */
