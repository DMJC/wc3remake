//
//  SCPlane.h
//  libRealSpace
//
//  Created by Rémi LEONARD on 31/08/2024.
//  Copyright (c) 2014 Fabien Sanglard. All rights reserved.
//
#pragma once
#ifndef __libRealSpace__SCPlane__
#define __libRealSpace__SCPlane__

#define NAME_LENGTH 8
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
static float ro[75] = {
    .0023081f,                                                  /* 1000 feet	*/
    .0022409f,  .0021752f,  .0021110f,  .0020482f,              /* 5000 feet	*/
    .0019869f,  .0019270f,  .0018685f,  .0018113f,  .0017556f,  /* 10000 feet	*/
    .0017011f,  .0016480f,  .0015961f,  .0015455f,  .0014962f,  /* 15000 feet	*/
    .0014480f,  .0014011f,  .0013553f,  .0013107f,  .0012673f,  /* 20000 feet	*/
    .0012249f,  .0011836f,  .0011435f,  .0011043f,  .0010663f,  /* 25000 feet	*/
    .0010292f,  .00099311f, .00095801f, .00092387f, .00089068f, /* 30000 feet	*/
    .00085841f, .00082704f, .00079656f, .00076696f, .00073820f, /* 35000 feet	*/
    .00071028f, .00067800f, .00064629f, .00061608f, .00058727f, /* 40000 feet	*/
    .00055982f, .00053365f, .00050871f, .00048493f, .00046227f, /* 45000 feet	*/
    .00044067f, .00042008f, .00040045f, .00038175f, .00036391f, /* 50000 feet	*/
    .00034692f, .00033072f, .00031527f, .00030055f, .00028652f, /* 55000 feet	*/
    .00027314f, .00026039f, .00024824f, .00023665f, .00022561f, /* 60000 feet	*/
    .00021508f, .00020505f, .00019548f, .00018336f, .00017767f, /* 65000 feet	*/
    .00016938f, .00016148f, .00015395f, .00014678f, .00013993f, /* 70000 feet	*/
    .00013341f, .00012719f, .00012126f, .00011561f, .00011022f, /* 75000 feet	*/
};

#define MEXPLODE 0
#define TPS 20
#define MAX_TURNRATE (600 / TPS)
#define DELAY_FACTOR 4
#define DDELAY TPS / 4
#define DDELAYF TPS / 4.0f

#define G_ACC 32.17f

#define COORD_SCALE 1.0f

class SCMissionActors;
class SCMission;
// Ajoutez ces déclarations dans SCPlane.h
class SCWeaponPredictor;


struct SCWeaponLoadoutHardPoint {
    RSEntity *objct;
    int nb_weap;
    int hpts_type;
    Point2D hud_pos;
    Vector3D position;
    std::string name;
};

class SCPlane {

protected:
    int anim_ticks{0};
    int thrust{0};
    float ELEVF_CSTE{0.0f};
    float ROLLFF_CSTE{0.0f};
    float LminDEF{0.0f};
    float LmaxDEF{0.0f};
    /* maximum flap deflection	*/
    float Fmax{0.0f};
    float Smax{0.0f};


    float fuel_rate{0.0f};

    float gefy{0.0f};
    float Splf{0.0f};
    float Spdf{0.0f};

    float fuel_weight{0.0f};
    
    float ipi_AR{0.0f};
    

    float ELEVF{0.0f};
    float ROLLF{0.0f};

    /* TRUE if wing g-limit is hit	*/
    short g_limit{0};
    /* fuel (0 - 12800)		*/
    int fuel{0};
    /* upper limit on engines	*/
    short max_throttle{0};
    /* lower limit on engines	*/
    short min_throttle{0};

    /* minimum lift-up speed fps	*/
    short MIN_LIFT_SPEED{0};
    short climbspeed{0};

    /* fps to knots conversion factor */
    float fps_knots{0.0f};
    

    int nocrash{1};
    int IN_BOX(int llx, int urx, int llz, int urz);
    int report_card(int descent, float roll, int vx, int vz, int wheels);
    
    float fuel_consump(float f, float b);

    uint32_t last_time{0};
    uint32_t tick_counter{0};
    uint32_t last_tick{0};

    /* TRUE if the wheels are down	*/
    short wheels{0};
    /* flap and spoiler settings	*/
    int flaps{0};
    int spoilers{0};
    int wheel_index{0};
    int wheel_anim{10};
    
    SCSmokeSet *smoke_set{nullptr};
    std::vector<Vector3D> smoke_positions;
    std::vector<int> smoke_anim_counters;

    Vector3D thrust_vector {0.0f, 0.0f, 0.0f};
    Vector3D lift_vector {0.0f, 0.0f, 0.0f};
    Vector3D gravity_vector {0.0f, 0.0f, 0.0f};
    Vector3D drag_vector {0.0f, 0.0f, 0.0f};


    Vector3D acceleration {0.0f, 0.0f, 0.0f};
    


    float airspeed_in_ms {0.0f};
    virtual void updatePosition();
    virtual void updateAcceleration();
    virtual void updateVelocity();
    virtual void updateForces();
    virtual void updateSpeedOfSound();
    virtual void checkStatus();
    virtual void computeLift();
    virtual void computeDrag();
    virtual void computeGravity();
    virtual void computeThrust();
    virtual void processInput();
    virtual void updatePlaneStatus();
    SCRenderer &Renderer = SCRenderer::getInstance();
    RSMixer &Mixer = RSMixer::getInstance();
     // Stocke le prédicteur de trajectoire
    SCWeaponPredictor* weapon_predictor = nullptr;
    
    // Trajectoire actuellement affichée
    bool is_showing_trajectory{false};
    int trajectory_display_counter{0};
public:
    short alive;
    unsigned int status;
    int wp_cooldown{0};
    // WC3 guns are energy-capacitor based (RSEntity::gun_energy_capacity/
    // _recharge_rate, parsed off the ship's own SSHP>WEAP>FGTR>GUNS
    // header), not ammo-count limited like SC1's ID_20MM. -1 sentinel
    // means "not yet initialized" — Shoot() lazily seeds it to full
    // capacity on first WC3 gun fire and regenerates it on-demand from
    // elapsed real time since the last shot, rather than needing a
    // separate per-tick update hook.
    float gun_energy_current{-1.0f};
    uint32_t gun_energy_last_update_ticks{0};
    // Round-robin cursor into the current gun type's matching hardpoints —
    // used by SCStrike's FIRE_PRIMARY handler to pick which single hardpoint
    // fires when there isn't enough energy for every matching hardpoint to
    // fire together (so low energy alternates hardpoints instead of always
    // draining the same one further while its siblings sit unused).
    int gun_alternate_index{0};
    // WC3's real default: gun pairs fire desynchronized (one cannon fires
    // slightly before the other, picked at random each shot) unless toggled
    // on with Ctrl+G (WC3Strike::checkGunSyncToggleInput).
    bool guns_synchronized{false};
    float mach{0.0f};
    float mcc{0.0f};
    float mratio{0.0f};
    float ie_pi_AR{0.0f};
    float g_load{0.0f};
    bool cloaked{false};
    float cloak_factor{0.0f};
    float Cdp{0.0f};
    float Lmax{0.0f};
    float Lmin{0.0f};
    float groundlevel{0.0f};
    float s{0.0f};
    float b{0.0f};
    float W{0.0f};
    float Mthrust{0.0f};
    // Internal-component-damage multipliers (see SCMissionActorsPlayer's
    // component_damage array) — 1.0 = undamaged/no effect. Only ever
    // written for the player's own plane (SCMissionActorsPlayer::
    // RollComponentDamage/TickComponentRepair); every other plane keeps
    // the 1.0 default forever, since the manual only tracks internal
    // component damage for the player. engine_power_fraction scales
    // computeThrust()'s output (Engine component: "reduce top speed and
    // acceleration"); gun_power_fraction scales GetCurrentGunEnergy()'s
    // recharge rate (Power Plant component: "lower available power to
    // engine, weapons, shields and damage repair").
    float engine_power_fraction{1.0f};
    float gun_power_fraction{1.0f};
    float inverse_mass{0.0f};
    float elevation_speedf{0.0f};
    float azimuth_speedf{0.0f};


    float m_old_yaw{0.0f};
    float m_old_pitch{0.0f};

    float m_yaw_var{0.0f};
    float m_pitch_var{0.0f};

    float lift_force {0.0f};
    float drag_force {0.0f};
    float gravity_drag_force {0.0f};
    float lift_drag_force {0.0f};
    float gravity_force {0.0f};

    int tps{0};
    int fps{0};
    /* plane velocity */
    float vx{0.0f};
    float vy{0.0f};
    float vz{0.0f};

        /* angle of attack for wing		*/
    float ae{0.0f};
    /* max and min coefficient of lift	*/
    float max_cl{0.0f};
    float min_cl{0.0f};
    /* wing angle tilt due to flaps		*/
    float tilt_factor{0.0f};

    /* last plane position	*/
    float last_px{0.0f};
    float last_py{0.0f};
    float last_pz{0.0f};

    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    /* plane acceleration	*/
    float ax{0.0f};
    float ay{0.0f};
    float az{0.0f};

    /* the effect of gravity realtive to tps */
    float gravity{0.0f};
    float drag{0.0f};
    float thrust_force{0.0f};
    
    /* coefficients of lift and drag	*/
    float uCl{0.0f};
    float Cl{0.0f};
    float Cd{0.0f};
    float Cdc{0.0f};
    float kl{0.0f};
    /* ground effect, ro/2*Vz*s		*/
    float qs{0.0f};
    /* TRUE if wing is stalling	*/
    short wing_stall{0};
    /* air density / 2.0, speed of sound	*/
    float ro2{0.0f};
    float sos{0.0f};

    int control_stick_x{0};
    int control_stick_y{0};

    float pitch_speed{0.0f};
    float yaw_speed{0.0f};
    float roll_speed{0.0f};

    short on_ground{1};

    int airspeed{0};
    float twist{0.0f};
    float azimuthf{0.0f};
    float elevationf{0.0f};

    float rollers{0.0f};
    float rudder{0.0f};
    float elevator{0.0f};
    RSArea *area{nullptr};
    // True for actors in a space mission (no terrain — area stays null; see
    // WC3Mission::loadMission()): skips gravitational acceleration and
    // ground-collision checks. Always false for Strike Commander's own
    // atmosphere-only missions.
    bool no_gravity{false};
    bool infinite_ammo{false};
    float lift{0.0f};
    std::vector <SCWeaponLoadoutHardPoint *> weaps_load;
    std::vector <SCSimulatedObject *> weaps_object;
    int selected_weapon{0};
    // Which gun TYPE FIRE_PRIMARY fires (CYCLE_GUNS/G), separate from
    // selected_weapon (which cycles ordnance for FIRE_MISSILE/Enter) — an
    // index into the ship's distinct gun-type list (first-encountered
    // order scanning weaps_load), or == that list's size for the final
    // "fire every gun together" state. See SCStrike.cpp's
    // collectDistinctGunTypes/FIRE_PRIMARY handler.
    int selected_gun_group{0};
    // Same idea as selected_gun_group above, but for FIRE_MISSILE/Enter: an
    // index into the ship's distinct missile/ordnance-type list (weapon_
    // category != 0 hardpoints), independent of both selected_weapon (the
    // old SC1 MDFS_WEAPONS-driven single shared index, which mixes guns and
    // missiles together and uses SC1-specific HUD weapon-mode logic that
    // doesn't apply to WC3 at all) and selected_gun_group. See SCStrike.cpp's
    // collectDistinctMissileTypes/its own WC3-only MDFS_WEAPONS branch.
    int selected_missile_group{0};
    // Set true by InitLoadout()'s WC3 branch (entityWeaps.size()==entityHpts
    // .size(), the same detection that branch already uses) — lets shared
    // SCStrike.cpp code (MDFS_WEAPONS, FIRE_MISSILE) tell WC3's simpler
    // fixed 1:1 hardpoint layout apart from SC1's JETP swappable-loadout one
    // without re-deriving the check or needing a WC3-specific subclass.
    bool is_wc3_ship{false};
    MISN_PART *object{nullptr};
    SCMissionActors *pilot {nullptr};
    bool takeoff{false};
    bool landed{false};
    float pitch{0.0f};
    float roll{0.0f};
    float yaw{0.0f};
    Vector3D velocity {0.0f, 0.0f, 0.0f};
    Vector3D position {0.0f, 0.0f, 0.0f};
    Vector3D forward = {0, 0, -1};
    Vector3D angular_velocity {0.0f, 0.0f, 0.0f};
    bool simple_simulation{true};
    /* my ptw matrix, temp matrix	*/
    Matrix ptw;
    Matrix incremental;
    // Authoritative orientation state, continuously integrated frame-to-
    // frame via quaternion multiplication (SCJdynPlane::updatePosition) —
    // gimbal-lock-free, unlike yaw/pitch/roll below, which are still
    // Euler angles extracted from ptw purely for display/other systems to
    // read and are never fed back into next frame's rotation composition.
    Quaternion orientationQuat;
    bool disable_azimuth{false};
    SCPlane();
    SCPlane(float LmaxDEF, float LminDEF, float Fmax, float Smax, float ELEVF_CSTE, float ROLLFF_CSTE, float s, float W,
            float fuel_weight, float Mthrust, float b, float ie_pi_AR, int MIN_LIFT_SPEED,
            RSArea *area, float x, float y, float z);
    ~SCPlane();
    virtual void init();
    int isOnRunWay();
    void SetThrottle(int throttle);
    int GetThrottle();
    // Real speed-boost state (SCStrike::checkKeyboard sets this from the
    // AFTERBURNER key each frame), consumed by SCJdynPlane's thrust_force
    // calculation to scale thrust toward the ship's own calibrated
    // max-afterburner-speed stat (RSEntity::weight_in_kg — confirmed via
    // manual cross-check to actually hold max afterburn KPS, not mass;
    // RSEntity::thrust_in_newton is likewise actually max normal-flight
    // speed in KPS) rather than a single hardcoded multiplier for every
    // ship. Also drives the engine-flame LVL7 visual in SCRenderer.cpp,
    // replacing the old thrust>=60 approximation.
    bool afterburner_engaged{false};
    void SetFlaps();
    void SetSpoilers();
    void SetWheel();
    short GetWheel();
    int GetFlaps();
    int GetSpoilers();

    void SetControlStick(int x, int y);
    virtual void Simulate();
    virtual void getPosition(Point3D *position);
    virtual void Render();
    virtual void RenderSimulatedObject();
    virtual void RenderSmoke(); 
    // lockProgressSeconds: elapsed seconds the firer has held its target
    // within lock range+cone (SCCockpit::lock_progress) — defaults to a
    // large sentinel so AI call sites (which gate firing via their own
    // fire-arc/range checks in SCMissionActors.cpp, not a real lock timer)
    // are never blocked by the WC3 torpedo/T-bomb lock-time gate this
    // introduces. The player's own FIRE_PRIMARY call site passes the real
    // value. See wc3_weapon_stats' own comment (SCenums.h) for how
    // weapon_category==3 weapons use this directly instead of a bool.
    // Returns true iff a weapon object was actually spawned this call —
    // false on every early-return path (still on cooldown, no ammo/
    // energy, lock not met, invalid hardpoint...). Callers that want to
    // fire several hardpoints as one simultaneous salvo (e.g. SCStrike's
    // gun-group fire handler) need this to distinguish "the first
    // hardpoint just ticked its cooldown down and didn't fire" from
    // "it fired" — wp_cooldown is a single value shared across the whole
    // plane, not per-hardpoint, so a caller silently gating a whole salvo
    // on wp_cooldown<=0 without ever calling Shoot() prevents that shared
    // counter from ever decrementing again once it goes positive (a real
    // bug this return value exists to let callers avoid).
    // spawnAdvanceDistance: pre-advances the fired bolt's spawn position
    // along its own firing direction by this many world units before the
    // first Simulate() tick -- since a constant-velocity tracer's position
    // at time t is spawn + velocity*t regardless of when it "actually"
    // fired, this is mathematically identical to having fired earlier by
    // (spawnAdvanceDistance / velocity) seconds, just without needing
    // sub-tick timing. Used by SCStrike's FIRE_PRIMARY handler to desync a
    // fired gun pair (WC3's real default: unsynchronized fire, toggled via
    // Ctrl+G) by about half a bolt's length, without touching non-gun
    // callers (default 0 leaves them unaffected).
    virtual bool Shoot(int weapon_hard_point_id, SCMissionActors *target, SCMission *mission, float lockProgressSeconds = 999.0f, float spawnAdvanceDistance = 0.0f);
    virtual void InitLoadout();
    void renderPlaneLined();
    // Nouvelle méthode pour simuler un tir
    Vector3D PredictShot(int weapon_hard_point_id, SCMissionActors *target);

    // Méthode pour tirer avec ajustement basé sur la prédiction
    bool ShootWithPrediction(int weapon_hard_point_id, SCMissionActors *target, SCMission *mission, float lockProgressSeconds = 999.0f);
    
    // Visualisation de la trajectoire projetée
    void RenderWeaponTrajectories();
    Vector3D getWeaponIntialVector(float speedFactor);
    Vector3D getWeaponIntialVector(float speedFactor, float pitchOffsetDegrees);
    float GetCurrentGunEnergy();
    // The 5 WC3 gun weapon_ids that are energy-capacitor gated (see Shoot()'s
    // own switch-case) rather than ammo-count limited. Shared by Shoot()
    // (skips ammo decrement for these) and SCStrike's FIRE_PRIMARY handler
    // (decides whether energy covers firing every matching hardpoint at
    // once, or should alternate one at a time instead).
    static bool IsWC3EnergyGunWeaponId(int weaponId);
    
};

#endif