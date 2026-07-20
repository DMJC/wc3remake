#include "WC4Mission.h"

WC4Mission::WC4Mission() {
    this->last_time = SDL_GetTicks();
    this->last_tick = 0;
    this->tick_counter = 0;
    this->tps = 0;
}

WC4Mission::WC4Mission(std::string mission_name, std::unordered_map<std::string, RSEntity*>* objCache) {
    this->mission_name = mission_name;
    this->obj_cache = objCache;
    this->last_time = SDL_GetTicks();
    this->last_tick = 0;
    this->tick_counter = 0;
    this->tps = 0;
    this->loadMission();
}

WC4Mission::~WC4Mission() {}

void WC4Mission::loadMission() {
    std::string miss_file_name = Assets.mission_root_path + this->mission_name;
    std::transform(miss_file_name.begin(), miss_file_name.end(), miss_file_name.begin(), ::toupper);
    TreEntry* mission_tre = Assets.GetEntryByName(miss_file_name.c_str());
    if (mission_tre == nullptr) {
        printf("WC4Mission: Could not find mission '%s'\n", miss_file_name.c_str());
        return;
    }

    this->mission = new RSMission();
    this->mission->InitFromRAM(mission_tre->data, mission_tre->size);

    // Determine if this is a space or ground mission
    // Space missions have no world/terrain reference or an empty one
    std::string worldName = this->mission->mission_data.world_filename;
    if (worldName.empty() || worldName == "SPACE" || worldName == "space") {
        this->is_space_mission = true;
    } else {
        this->is_space_mission = false;
    }

    if (!this->is_space_mission) {
        std::string area_filename = Assets.mission_root_path + worldName + ".IFF";
        std::transform(area_filename.begin(), area_filename.end(), area_filename.begin(), ::toupper);
        this->world = new RSWorld();
        TreEntry* treEntry = Assets.GetEntryByName(area_filename.c_str());
        if (treEntry != nullptr) {
            this->world->InitFromRAM(treEntry->data, treEntry->size);

            std::string area_fn = "..\\..\\DATA\\TERRAIN\\" + this->world->tera + ".ZIP";
            std::transform(area_fn.begin(), area_fn.end(), area_fn.begin(), ::toupper);
            this->area = new RSArea();
            this->area->InitFromZipFileName(area_fn);
            Renderer.InvalidateAABBCache();
            Renderer.PrecomputeAABBs(this->area, 0, 1);
        }
    }

    // Initialize mission flags
    for (int i = 0; i < 256; i++) {
        this->mission->mission_data.flags.push_back(0);
    }

    // Load area objects for ground missions
    if (this->area != nullptr) {
        for (auto& area_entity : this->area->objects) {
            area_entity.entity = LoadEntity(area_entity.name);
        }
    }

    // Load mission actors
    int cpt_actor = 0;
    for (auto part : mission->mission_data.parts) {
        if (part->entity == nullptr) {
            part->entity = LoadEntity(part->member_name);
        }

        int search_id = 0;
        for (auto cast : mission->mission_data.casting) {
            if (part->id == search_id) {
                SCMissionActors* actor = new SCMissionActors();
                if (cast->actor == "PLAYER") {
                    actor = new SCMissionActorsPlayer();
                    if (part->area_id != 255 && part->unknown2 == 0) {
                        Vector3D correction = this->mission->mission_data.areas[part->area_id]->position;
                        part->position.x += correction.x;
                        part->position.y += correction.y;
                        part->position.z += correction.z;
                    }
                } else if (cast->actor == "STRIBASE") {
                    actor = new SCMissionActorsStrikeBase();
                }

                actor->team_id = part->unknown_bytes[2];
                actor->actor_name = cast->actor;
                actor->actor_id = part->id;
                actor->object = part;
                if (part->entity == nullptr) {
                    actor->health = 50;
                } else {
                    actor->health = part->entity->health;
                }
                actor->profile = this->LoadProfile(cast->actor);
                actor->mission = this;

                if (actor->object->on_is_activated != 255 && this->mission->mission_data.prog.size() > actor->object->on_is_activated) {
                    for (auto op : *this->mission->mission_data.prog[actor->object->on_is_activated])
                        actor->on_is_activated.push_back(op);
                }
                if (actor->object->on_is_destroyed != 255 && this->mission->mission_data.prog.size() > actor->object->on_is_destroyed) {
                    for (auto op : *this->mission->mission_data.prog[actor->object->on_is_destroyed])
                        actor->on_is_destroyed.push_back(op);
                }
                if (actor->object->on_missions_init != 255 && this->mission->mission_data.prog.size() > actor->object->on_missions_init) {
                    for (auto op : *this->mission->mission_data.prog[actor->object->on_missions_init])
                        actor->on_mission_start.push_back(op);
                }
                if (actor->object->on_mission_update != 255 && this->mission->mission_data.prog.size() > actor->object->on_mission_update) {
                    for (auto op : *this->mission->mission_data.prog[actor->object->on_mission_update])
                        actor->on_update.push_back(op);
                }

                if (actor->profile != nullptr && actor->profile->ai.isAI &&
                    actor->object->entity != nullptr && actor->object->entity->jdyn != nullptr &&
                    cast->actor != "PLAYER") {
                    if (actor->profile->ai.goal.size() > 0) {
                        actor->pilot = new SCPilot();
                        actor->pilot->actor = actor;
                        BoudingBox* bb = actor->object->entity->GetBoudingBpx();
                        actor->plane = new SCJdynPlane(
                            10.0f, -7.0f, 40.0f, 40.0f, 30.0f, 100.0f,
                            actor->object->entity->wing_area,
                            (float)actor->object->entity->weight_in_kg,
                            (float)actor->object->entity->jdyn->FUEL,
                            (float)actor->object->entity->thrust_in_newton,
                            (bb->max.z - bb->min.z) / 2.0f,
                            .93f, 120, this->area,
                            part->position.x, part->position.y, part->position.z);
                        actor->plane->yaw = (360 - part->azymuth) * 10.0f;
                        actor->plane->object = part;
                        actor->plane->InitLoadout();
                        actor->plane->pilot = actor;
                        actor->plane->on_ground = false;
                        actor->plane->SetThrottle(100);
                        actor->pilot->target_climb = (int)(part->position.y);
                        actor->plane->vz = -20;
                        actor->pilot->target_azimut = actor->plane->azimuthf / 10.0f;
                        actor->pilot->target_speed = -20;
                        actor->pilot->plane = actor->plane;
                    }
                    this->actors.push_back(actor);
                } else if (actor->profile != nullptr && cast->actor == "PLAYER") {
                    BoudingBox* bb = actor->object->entity->GetBoudingBpx();
                    actor->plane = new SCJdynPlane(
                        10.0f, -7.0f, 40.0f, 40.0f, 30.0f, 100.0f,
                        actor->object->entity->wing_area,
                        (float)actor->object->entity->weight_in_kg,
                        (float)actor->object->entity->jdyn->FUEL,
                        (float)actor->object->entity->thrust_in_newton,
                        (bb->max.z - bb->min.z) / 2.0f,
                        .93f, 120, this->area,
                        part->position.x, part->position.y, part->position.z);
                    actor->plane->yaw = (360 - part->azymuth) * 10.0f;
                    actor->plane->simple_simulation = false;
                    actor->plane->yaw = (360 - part->azymuth) * (float)M_PI / 180.0f;
                    actor->plane->object = part;
                    actor->plane->pilot = actor;
                    this->actors.push_back(actor);
                    this->player = actor;
                } else {
                    this->actors.push_back(actor);
                }
                cpt_actor++;
                break;
            }
            search_id++;
        }
    }

    // Classify actors into friendlies and enemies
    for (auto member : this->mission->mission_data.team) {
        int id = this->mission->mission_data.parts[member]->id;
        for (auto actor : this->actors) {
            if (actor->actor_id == id)
                this->friendlies.push_back(actor);
        }
    }
    for (auto actor : this->actors) {
        if (std::find(this->friendlies.begin(), this->friendlies.end(), actor) == this->friendlies.end())
            this->enemies.push_back(actor);
    }

    // Execute initial mission programs
    if (this->player != nullptr) {
        if (this->player->on_is_activated.size() > 0) {
            SCProg* p = new SCProg(this->player, this->player->on_is_activated, this, this->player->object->on_is_activated);
            p->execute();
        }
        if (this->player->on_mission_start.size() > 0 && this->player->prog_executed == false) {
            SCProg* p = new SCProg(this->player, this->player->on_mission_start, this, this->player->object->on_missions_init);
            p->execute();
            this->player->prog_executed = true;
        }
    }

    for (auto spot : this->mission->mission_data.spots) {
        spot->origin = {spot->position.x, spot->position.y, spot->position.z};
        if (spot->area_id != -1) {
            AREA* ar = this->mission->mission_data.areas[spot->area_id];
            spot->position += ar->position;
        }
    }
}

void WC4Mission::update() {
    SCMission::update();
}
