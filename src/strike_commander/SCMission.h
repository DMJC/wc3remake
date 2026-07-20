#pragma once
#include "precomp.h"
struct SCAiPlane {
    SCPlane *plane{nullptr};
    SCPilot *pilot{nullptr};
    MISN_PART *object{nullptr};
    RSProf *ai{nullptr};
    std::string name;
};

struct SCMissionWaypoint {
    SPOT *spot{nullptr};
    std::string *message{nullptr};
    std::string *objective{nullptr};
};

struct RadioMessages {
    std::string message;
    MemSound *sound{nullptr};
};
class SCProg;

class SCMission {
protected:
    std::string mission_name;
    std::unordered_map<std::string, RSEntity *> *obj_cache{nullptr};
    RSEntity * LoadEntity(std::string name);
    RSProf * LoadProfile(std::string name);
    uint32_t last_time{0};
    uint32_t last_tick{0};
    uint32_t tick_counter{0};
    uint32_t tps{0};
    SCState &GameState = SCState::getInstance();
    AssetManager &Assets = AssetManager::getInstance();
    SCRenderer &Renderer = SCRenderer::getInstance();
    RSMixer &Mixer = RSMixer::getInstance();
public:    
    std::vector<SCMissionActors *> actors;
    std::vector<SCMissionActors *> enemies;
    std::vector<SCMissionActors *> friendlies;
    std::vector<SCMissionWaypoint *> waypoints;
    std::vector<SCExplosion *> explosions;
    // Wreckage pieces spawned on ship destruction (RSEntity::debris — see
    // SCMissionActors::hasBeenHit) — same lifecycle pattern as explosions
    // above (rendered + pruned once finished in SCStrike's render loop).
    std::vector<SCDebris *> debris;
    std::unordered_map<uint16_t, int16_t> gameflow_registers;
    std::unordered_map<uint8_t, std::vector<uint8_t>> progs_traces;
    bool success{false};
    bool failure{false};
    bool mission_ended{false};
    uint8_t current_area_id{0};
    // Space missions (no_gravity) have no runway to taxi to a stop on, so
    // SCPlane::landed never gets set by the ground-speed model — see
    // SCMission::update()'s carrier-bay-proximity approximation of it
    // instead. Must leave the carrier's own bay area at least once before
    // re-entering it can count as "landed", or the player would instantly
    // "land" at their own mission-start spawn point.
    bool has_left_carrier_bay{false};
    // Set once the player radios the carrier and requests landing (the 'f'
    // "request land" radio option — see SCMissionActors::respondToRadioMessage)
    // and gates the carrier-landing approximation in update().
    bool landing_clearance_granted{false};
    SCMissionActors *player{nullptr};
    RSArea *area{nullptr};
    RSMission *mission{nullptr};
    RSWorld *world{nullptr};
    // Resolved entities for world->skyFaces, same index correspondence —
    // may contain null entries (e.g. an unresolvable name) alongside valid
    // ones; see WC3Mission::loadMission(). Used to render the space-scene
    // skybox (galaxy backdrop billboards).
    std::vector<RSEntity *> skyEntities;
    RSSound &sound = RSSound::getInstance();
    bool in_combat{false};
    std::vector<RadioMessages*> radio_messages;
    bool mission_over{false};
    bool mission_won{false};
    // Set by OP_SELECT_NEXT_MISSION (SCProg.cpp) — an index into
    // mission->mission_data.messages whose string is the next mission's
    // real filename (e.g. "misnd3bd"), not display text. -1 = never set by
    // this mission's own PROG (the normal case: sequential LOOKMISN
    // progression applies). Most missions using this opcode set it
    // unconditionally to a single candidate; a few (confirmed: MISND002,
    // Laconda2) wrap it in real branching logic first — see
    // OP_SELECT_NEXT_MISSION's own comment in SCenums.h. Read by
    // WC3GameFlow at mission end (see WC3Strike::onMissionEnded) to resolve
    // an explicit next-mission override instead of the default scene+1
    // sequential pick.
    int next_mission_message_index{-1};
    SCMission();
    SCMission(std::string mission_name, std::unordered_map<std::string, RSEntity *> *objCache);
    ~SCMission();
    virtual void loadMission();
    void cleanup();
    virtual void update();
    void executeProg(std::vector<PROG> *prog);
    uint8_t getAreaID(Vector3D position);
};