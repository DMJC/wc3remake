#pragma once
#include "precomp.h"

class SCMission;

// Which of the ship's 4 facings a hit landed on — matches TARGSHAP.PAK's own
// per-side frame groups (top/left/bottom/right shield, then top/left/bottom/
// right hull damage) and RSEntity::SHLD_FX's front/back/left/right (armor_*)
// quadruples, both confirmed against the WC3 manual's printed per-ship
// armor stats (see SHLD_FX struct comment).
enum class HitQuadrant { Front, Back, Left, Right };

// WC3 manual's internal-component layout: each HitQuadrant holds 2-3
// components, in "most likely to fail first" -> "least likely" order.
// Front's three (VDU1=left VDU, VDU2=right VDU, TacticalDisplay=radar
// display — user-confirmed 2026-07 session) are never repairable; every
// other component is, via the AutoRepair component itself (which can also
// be damaged/destroyed, at which point nothing repairs anymore). Per the
// manual, only the player's ship tracks this at all — enemy/wingman ships
// explode well before their (untracked) components would ever fail.
enum class ShipComponent : uint8_t {
    VDU1, VDU2, TacticalDisplay,           // Front
    Communications, Shields, AutoRepair,   // Right
    Afterburners, PowerPlant, Engine,      // Back
    Targeting, Guns,                       // Left
    Count
};

class SCMissionActors {
public:
    // Which of `target`'s 4 facings `attackerPos` is on, from `target`'s own
    // heading. Generic (subject, otherPos) — reused both for "is the target
    // in front of the player" (ClassifyHitQuadrant(player, targetPos)) and
    // "is the player behind the target" (ClassifyHitQuadrant(target,
    // playerPos) == Back) by SCStrike/SCCockpit's targeting/lock-on code.
    // See the function body's own comment for the axis-convention caveat.
    static HitQuadrant ClassifyHitQuadrant(SCMissionActors *target, const Vector3D &attackerPos);

    // Ship-name-based capital-ship classification (MISN_PART::member_name,
    // upper-cased) — no engine field currently distinguishes ship class, so
    // this is a hand-built list from real-world WC3 ship-class knowledge,
    // not independently verified against the manual. Used by the Target
    // Radar's cross-vs-dot marker (SCCockpit::RenderMFDSTargetRadarWC3) and
    // autopilot's "no non-capital enemies nearby" engage gate
    // (SCStrike::checkKeyboard's AUTOPILOT handler).
    static bool IsCapitalShipName(const std::string &upperName);
    // Subset of IsCapitalShipName's own prefix list: only the handful of
    // truly massive, named/unique assets (dreadnought, carriers, the
    // player's own Victory, a star base, the Behemoth superweapon) — not
    // corvettes/destroyers/transports/shuttles, which are "capital-tier"
    // for radar/IFF purposes but not what "a large capital ship" means for
    // the death cinematic below. Drives SCStrike's large-ship explosion
    // sequence (see [[project_wc3_hit_visuals_and_collision]]): first-two-
    // frames, screen whiteout, then the rest of the animation, gated by
    // has_exploded the same way the ordinary explosion spawn is.
    static bool IsLargeCapitalShipName(const std::string &upperName);
    // Species check for the same real-world WC3 ship-class list above,
    // used as the friend/foe fallback for capital ships (MISN>TEAM only
    // ever lists the player's fighter wing, never capital ships — see
    // WC3Mission::loadMission's classification pass). Cross-referenced
    // across every campaign mission's capital-ship PART records (2026-07
    // session): none of their unknown_bytes fields correlate with
    // allegiance (e.g. VICTORY itself shows both 0,0 and 255,255 across
    // different missions), but ship name reliably does — every Kilrathi-
    // named capital ship in the whole campaign is unconditionally
    // hostile, every Terran one unconditionally friendly.
    static bool IsKilrathiShipName(const std::string &upperName);
    // Jump buoys/asteroid supply depots — always-friendly navigation
    // structures, not ships (see IsCapitalShipName's own comment for why
    // these are kept separate). Used only for the target-radar's light-
    // blue-vs-plain-blue friendly color, not the capital-ship marker/
    // autopilot gate.
    static bool IsFriendlySupplyStructureName(const std::string &upperName);

    // Ordered (most-likely-to-fail-first) component list for one facing —
    // see ShipComponent's own comment. Backs both the weighted-random pick
    // in RollComponentDamage and TickComponentRepair's iteration.
    static const std::vector<ShipComponent> &ComponentsForQuadrant(HitQuadrant quadrant);
    // Front's three components (VDU1/VDU2/TacticalDisplay) are never
    // repaired, per the manual; everything else is (unless already fully
    // destroyed, or the AutoRepair component itself is destroyed).
    static bool IsComponentRepairable(ShipComponent component);

    bool talkative{true};
    int score{0};
    int plane_down{0};
    int ground_down{0};
    std::string actor_name;
    uint8_t actor_id{0};
    RSProf *profile{nullptr};
    std::vector<PROG> prog;
    std::vector<PROG> on_mission_start;
    std::vector<PROG> on_is_activated;
    std::vector<PROG> on_is_destroyed;
    std::vector<PROG> on_update;
    std::vector<PROG> override_progs;
    std::vector<SCSimulatedObject *> weapons_shooted;
    MISN_PART *object{nullptr};
    SCPlane *plane{nullptr};
    SCPilot *pilot{nullptr};
    SCMission *mission{nullptr};
    SCMissionActors *target{nullptr};
    SCMissionActors *attacker{nullptr};
    prog_op current_objective;
    Vector3D formation_pos_offset{150.0f, 0.0f, 0.0f};
    Vector3D attack_pos_offset{0.0f, 0.0f, -1000.0f};
    bool is_active{false};
    bool is_hidden{true};
    bool taken_off{false};
    bool is_destroyed{false};
    // Guards against spawning the EXPL explosion more than once for the
    // same death — set by whichever site fires first (normally hasBeenHit,
    // at the moment a weapon hit brings health to 0), checked by the
    // generic per-tick destruction finalization in SCMission::update()
    // (which needs its own fallback spawn for ships/objects that go
    // alive=false via some path other than hasBeenHit, so EVERY destroyed
    // ship/object is guaranteed an explosion, not just weapon kills) so it
    // doesn't double up on actors hasBeenHit already handled.
    bool has_exploded{false};
    // "Disabled" (not destroyed) — set by OP_SET_TARGET_DISABLED
    // (SCProg.cpp), the WC3 "shoot the engines until it reports disabled"
    // mechanic (user-confirmed 2026-07 session, the Torgo mission's
    // "disable tankers" objective). A disabled actor stops flying/
    // fighting (see its own check in SCMission.cpp's per-actor update
    // loop) but is still alive — distinct from is_destroyed.
    bool is_disabled{false};
    bool prog_executed{false};
    // Total remaining hit points across every shield+armor quadrant below —
    // kept in sync by hasBeenHit purely so the existing threshold-based
    // logic that already depends on it (radio "status" messages at 75/50/
    // 25%, SCMissionActorsPlayer's death check) keeps working unchanged.
    // The per-quadrant fields below are the source of truth for anything
    // new (gauges, hit-direction damage).
    int health{0};
    // Current shield/armor hit points per facing, seeded from
    // RSEntity::SHLD_FX (see WC3Mission.cpp's buildActorFromPart) — 0 for
    // any actor whose entity has no SHLD chunk (e.g. simple ground props).
    // Shields absorb damage before armor on the SAME facing; only once a
    // facing's shield is fully depleted does further damage on that facing
    // reach its armor.
    float shield_front{0.0f}, shield_back{0.0f}, shield_left{0.0f}, shield_right{0.0f};
    float armor_front{0.0f}, armor_back{0.0f}, armor_left{0.0f}, armor_right{0.0f};
    // Same 4 values at full/spawn strength — needed to compute a fraction
    // for gauges (current/max) since max varies per ship type.
    float max_shield_front{0.0f}, max_shield_back{0.0f}, max_shield_left{0.0f}, max_shield_right{0.0f};
    float max_armor_front{0.0f}, max_armor_back{0.0f}, max_armor_left{0.0f}, max_armor_right{0.0f};
    // Total shield points/second this ship regenerates, split evenly
    // across every currently-damaged facing (manual, 2026-07 session:
    // "calibrated to regenerate totally in 10 seconds... divided among
    // all sides with damaged shields"). Seeded from RSEntity::SHLD_FX::
    // regen_rate (WC3Mission::buildActorFromPart) — 0 for actors with no
    // SHLD chunk, which UpdateShieldRegen's own early-out handles.
    // Unlike shields, armor never regenerates at all (manual, same
    // message) — no equivalent field/logic exists for armor, by design.
    float shield_regen_rate{0.0f};
    // Seconds remaining to keep showing the shield-hit visual (RSEntity::
    // shield->objct, the SHLDFX mesh — see [[project_wc3_shield_effect_mesh]]
    // memory note for its own APPR>SHLD parsing fix) after a hit was
    // actually absorbed by shield on some facing. Set by hasBeenHit,
    // ticked down and drawn in SCStrike's per-actor render loop.
    float shield_hit_flash_timer{0.0f};
    // Seconds until this actor can deal/take ship-to-ship collision damage
    // again (SCMission.cpp's own pairwise overlap check) — without this,
    // two actors whose bounding boxes stay overlapping for several ticks
    // (e.g. a kill that doesn't instantly separate them) would re-apply a
    // near-lethal hit every single tick instead of once per collision.
    float collision_cooldown{0.0f};
    // Internal-component damage, 0 (undamaged) to 100 (fully destroyed —
    // permanently unrepairable, per the manual). Indexed by ShipComponent.
    // Present on every actor (matching shield_front/armor_front's own
    // always-present-but-only-meaningful-for-some-actors pattern) but only
    // ever written by SCMissionActorsPlayer's RollComponentDamage/
    // TickComponentRepair overrides — the base-class no-op implementations
    // mean enemy/wingman actors' arrays just stay all-zero forever,
    // matching the manual's "only the player needs to worry about this."
    float component_damage[(size_t)ShipComponent::Count]{};
    // Elapsed time (seconds) for cloak-cycle entities (RSEntity::CLOK_DATA
    // — e.g. STRAKHA.IFF, a real Kilrathi stealth fighter) — see
    // UpdateCloak's own comment.
    float cloak_timer{0.0f};
    // Repeating cloaked/decloaked duty cycle for actors whose entity
    // carries cloak data, driven once per SCMission::update() tick.
    // Unlike SCSimulatedObject::cloaked's one-shot weapon transition (a
    // missile only needs to sneak in once), a fighter plausibly cycles
    // cloak on and off through a fight — cloaked for entity->clok->duration
    // seconds, decloaked for the same, repeating. Writes this->plane->
    // cloaked (SCPlane.h) — the same field the player's own Excalibur
    // cloak already uses — so SCCockpit::RenderMFDSTargetRadarWC3's
    // existing cloaked-actor radar skip picks it up for free. Not
    // independently confirmed against real WC3 Strakha behavior (the
    // 50/50 duty-cycle split is a reasonable guess, not measured).
    void UpdateCloak(float dt);
    // Driven once per SCMission::update() tick, same as UpdateCloak — see
    // shield_regen_rate's own comment.
    void UpdateShieldRegen(float dt);
    int team_id{0};
    int current_target{0};
    // Mines the player has dropped near this actor (ID_MINEMISS,
    // proximity-matched at drop time — see SCPlane::Shoot's own comment).
    // Read by OP_GET_MINE_COUNT_AT_NAVPOINT (228); only meaningful on the
    // JUBOUY nav-point buoys in MISNJ002 ("Torgo 2"), the only file that
    // uses the opcode, but tracked generically per-actor like
    // component_damage/current_target rather than mission-specific state.
    int mines_deployed{0};
    bool current_command_executed{false};
    prog_op current_command{prog_op::OP_NOOP};
    prog_op override_command{prog_op::OP_NOOP};
    uint8_t current_command_arg;
    Vector3D aiming_vector{0.0f, 0.0f, 0.0f};
    std::vector<uint8_t> executed_opcodes;
    int retarget_cooldown{0};
    int timer{0};
    int wait_timer{0};
    virtual bool wait(int seconds);
    virtual bool execute();
    virtual bool takeOff(uint8_t arg); 
    virtual bool land(uint8_t arg);
    virtual bool flyToWaypoint(uint8_t arg);
    virtual bool flyToArea(uint8_t arg);
    virtual bool destroyTarget(uint8_t arg);
    virtual bool defendTarget(uint8_t arg);
    virtual bool defendArea(uint8_t arg);
    virtual bool deactivate(uint8_t arg);
    virtual bool setMessage(uint8_t arg);
    virtual bool followAlly(uint8_t arg);
    virtual bool protectSelf();
    virtual bool ifTargetInSameArea(uint8_t arg);
    virtual bool respondToRadioMessage(int message_id, SCMission *mission, SCMissionActors *sender=nullptr);
    virtual bool activateTarget(uint8_t arg);
    virtual int getDistanceToTarget(uint8_t arg);
    virtual int getDistanceToSpot(uint8_t arg);
    // Releases this actor's current_target/target/attacker/target_position
    // state, mirroring the inline release destroyTarget() does on a kill.
    // Used by OP_CLEAR_TARGET (145).
    void releaseTarget();
    virtual void shootWeapon(SCMissionActors *target);
    virtual void hasBeenHit(SCSimulatedObject *weapon, SCMissionActors *attacker);
    // The shield/armor/health-gated damage-application core hasBeenHit
    // extracts `damage` and then runs — factored out so ship-to-ship
    // collision damage (SCMission.cpp, see [[project_wc3_ship_collision]])
    // can drive the exact same shield->armor->hull->death pipeline
    // without a weapon object. `weapon` stays optional purely so the
    // existing gun-impact-vs-explosion death-sound distinction still
    // works unchanged for real weapon hits; leave it null for anything
    // else (collisions included).
    void ApplyDamage(int damage, SCMissionActors *attacker, SCSimulatedObject *weapon = nullptr);
    // Manual, 2026-07 session: "Each fighter type has a unique damage
    // value for collisions, which is just short of the total of a given
    // side's armor and shields." No stored per-ship collision-damage
    // field exists anywhere in the real IFF data (checked exhaustively —
    // see [[project_wc3_capital_ship_stats]]), so this derives it from
    // this actor's own front-facing shield+armor total instead — bigger/
    // tougher ships naturally deal a bigger ram hit, matching the
    // manual's own description without inventing an unstored constant.
    // 0.9x keeps it "just short of" rather than exactly equal to that
    // total, per the manual's own wording.
    int GetCollisionDamage();
    // Called from hasBeenHit with whatever damage got past shield+armor on
    // the hit facing (0 if none did — component damage only has a chance
    // to happen on a genuine hull hit, per the manual). Base no-op; real
    // weighted-random component selection lives in SCMissionActorsPlayer's
    // override (see ShipComponent's own comment for why only the player
    // tracks this at all).
    virtual void RollComponentDamage(HitQuadrant quadrant, int overflowDamage);
    // Called once per SCMission::update() tick to slowly heal repairable,
    // not-fully-destroyed components. Base no-op; real repair logic lives
    // in SCMissionActorsPlayer's override.
    virtual void TickComponentRepair();

private:
    Vector3D target_position{0.0f, 0.0f, 0.0f};
    int target_position_update{0};
    int current_weapon_index{-1};
    
    AssetManager &Assets = AssetManager::getInstance();
};

class SCMissionActorsPlayer : public SCMissionActors {
public:
    bool takeOff(uint8_t arg) override; 
    bool land(uint8_t arg) override;
    bool flyToWaypoint(uint8_t arg) override;
    bool flyToArea(uint8_t arg) override;
    bool destroyTarget(uint8_t arg) override;
    bool defendTarget(uint8_t arg) override;
    bool setMessage(uint8_t arg) override;
    void hasBeenHit(SCSimulatedObject *weapon, SCMissionActors *attacker) override;
    void RollComponentDamage(HitQuadrant quadrant, int overflowDamage) override;
    void TickComponentRepair() override;
private:
    // Pushes component_damage[Engine]/[PowerPlant] out to this->plane's
    // engine_power_fraction/gun_power_fraction (see SCPlane.h) — called
    // after both a hit and a repair tick change either value.
    void UpdatePlanePowerFractions();
};

class SCMissionActorsStrikeBase : public SCMissionActors {
public:
    bool setMessage(uint8_t arg) override;
};