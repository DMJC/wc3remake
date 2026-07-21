#include "precomp.h"
#include <limits>

SCMission::SCMission() {
    this->last_time = SDL_GetTicks();
    this->last_tick = 0;
    this->tick_counter = 0;
    this->tps = 0;
}
SCMission::SCMission(std::string mission_name, std::unordered_map<std::string, RSEntity *> *objCache) {
    this->mission_name = mission_name;
    this->obj_cache = objCache;
    this->last_time = SDL_GetTicks();
    this->last_tick = 0;
    this->tick_counter = 0;
    this->tps = 0;
    this->loadMission();
}
SCMission::~SCMission() {
    this->cleanup();
}
void SCMission::cleanup() {
    if (this->mission != nullptr) {
        delete this->mission;
        this->mission = nullptr;
    }
    if (this->area != nullptr) {
        delete this->area;
        this->area = nullptr;
    }
    
    for (auto actor : this->actors) {
        delete actor;
    }
    this->actors.clear();
    this->actors.shrink_to_fit();
    for (auto wp : this->waypoints) {
        delete wp;
    }
    this->waypoints.clear();
    this->waypoints.shrink_to_fit();
    
}
RSProf *SCMission::LoadProfile(std::string name) {
    RSProf *profile = new RSProf();
    std::string filename = Assets.intel_root_path+ name + ".IFF";
    TreEntry *profile_tre = Assets.GetEntryByName(filename);
    if (profile_tre != nullptr) {
        profile->InitFromRAM(profile_tre->data, profile_tre->size);
    } else {
        printf("Unable to load profile %s\n", name.c_str());
        return nullptr;
    }
    return profile;
}
void SCMission::loadMission() {
    std::string miss_file_name = Assets.mission_root_path + this->mission_name; 
    std::transform(miss_file_name.begin(), miss_file_name.end(), miss_file_name.begin(), ::toupper);
    TreEntry *mission_tre = Assets.GetEntryByName(miss_file_name.c_str());
    this->mission = new RSMission();
    this->mission->InitFromRAM(mission_tre->data, mission_tre->size);


    std::string area_filename = Assets.mission_root_path+this->mission->mission_data.world_filename + ".IFF";
    std::transform(area_filename.begin(), area_filename.end(), area_filename.begin(), ::toupper);   
    this->world = new RSWorld();
    TreEntry *treEntry = NULL;
    treEntry = Assets.GetEntryByName(area_filename.c_str());
    if (treEntry != NULL) {
        this->world->InitFromRAM(treEntry->data, treEntry->size);
    }
    std::string area_fn = this->world->tera+".PAK";
    std::transform(area_fn.begin(), area_fn.end(), area_fn.begin(), ::toupper);
    this->area = new RSArea();
    this->area->InitFromPAKFileName(area_fn.c_str());

    Renderer.InvalidateAABBCache();           // ou InvalidateAABBCache(oldArea);
    Renderer.PrecomputeAABBs(this->area, 0, 1);  // optionnel: pré-calc LOD 0-1

    for (auto &area_entity: this->area->objects) {
        area_entity.entity = LoadEntity(area_entity.name);
    }
    int cpt_actor=0;
    for (auto part : mission->mission_data.parts) {
        int search_id = 0;
        if (part->entity == nullptr) {
            part->entity = LoadEntity(part->member_name);
        }
        for (auto cast : mission->mission_data.casting) {
            if (part->id == search_id) {
                SCMissionActors *actor = new SCMissionActors();
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
                if (cast->actor == "TEAM0") {
                    cast->actor = GameState.wingman;
                }
                actor->team_id = part->unknown_bytes[2];
                actor->actor_name = cast->actor;
                actor->actor_id = part->id;
                actor->object = part;
                actor->health = part->entity->health;
                actor->profile = this->LoadProfile(cast->actor);
                actor->mission = this;
                if (actor->object->on_is_activated != 255) {
                    for (auto op: *this->mission->mission_data.prog[actor->object->on_is_activated]) {
                        actor->on_is_activated.push_back(op);
                    }
                }
                if (actor->object->on_is_destroyed != 255) {
                    for (auto op: *this->mission->mission_data.prog[actor->object->on_is_destroyed]) {
                        actor->on_is_destroyed.push_back(op);
                    }
                }
                if (actor->object->on_missions_init != 255) {
                    for (auto op: *this->mission->mission_data.prog[actor->object->on_missions_init]) {
                        actor->on_mission_start.push_back(op);
                    }
                }
                if (actor->object->on_mission_update != 255) {
                    for (auto op: *this->mission->mission_data.prog[actor->object->on_mission_update]) {
                        actor->on_update.push_back(op);
                    }
                }
                
                if (actor->profile != nullptr && actor->profile->ai.isAI) {
                    if (actor->profile->ai.goal.size() > 0) {
                        actor->pilot = new SCPilot();
                        actor->pilot->actor = actor;
                        BoudingBox *bb = actor->object->entity->GetBoudingBpx();
                        
                        actor->plane = new SCJdynPlane(
                            10.0f,
                            -7.0f,
                            40.0f,
                            40.0f,
                            30.0f,
                            100.0f,
                            actor->object->entity->wing_area,
                            (float) actor->object->entity->weight_in_kg,
                            (float) actor->object->entity->jdyn->FUEL,
                            (float) actor->object->entity->thrust_in_newton,
                            (bb->max.z - bb->min.z) / 2.0f,
                            .93f,
                            120,
                            this->area,
                            part->position.x,
                            part->position.y,
                            part->position.z
                        );
                        actor->plane->yaw = (360 - part->azymuth) * 10.0f;
                        
                        part->weapon_load.shrink_to_fit();
                        if (part->weapon_load.size() > 0) {
                            std::string weapon_path_name = Assets.object_root_path + part->weapon_load + ".IFF";
                            std::transform(weapon_path_name.begin(), weapon_path_name.end(), weapon_path_name.begin(), ::toupper);
                            TreEntry *weapon_entry = Assets.GetEntryByName(weapon_path_name.c_str());
                            part->entity->weaps.clear();
                            part->entity->weaps.shrink_to_fit();
                            part->entity->parseREAL_OBJT_JETP(weapon_entry->data, weapon_entry->size);   
                        }
                        actor->plane->object = part;
                        actor->plane->InitLoadout();
                        actor->plane->pilot = actor;
                        if (abs(this->area->getY(part->position.x, part->position.z)-part->position.y) <= 10 ) {
                            part->position.y = this->area->getY(part->position.x, part->position.z);
                            actor->plane->on_ground = true;
                        } else {
                            actor->plane->on_ground = false;
                        }
                        if (part->position.y < this->area->getY(part->position.x, part->position.z)) {
                            actor->plane->on_ground = true;
                        }
                        if (!actor->plane->on_ground) {
                            actor->plane->SetThrottle(100);
                            actor->pilot->target_climb = (int) (part->position.y);
                            actor->plane->vz = -20;
                            actor->pilot->target_azimut = actor->plane->azimuthf / 10.0f;
                            actor->pilot->target_speed = -20;
                        } else {
                            actor->plane->SetThrottle(0);
                            actor->pilot->target_climb = 0;
                            actor->plane->vz = 0;
                            actor->pilot->target_azimut = actor->plane->azimuthf / 10.0f;
                            actor->pilot->target_speed = 0;
                        }
                        actor->pilot->plane = actor->plane;
                    }
                    this->actors.push_back(actor);
                } else if (actor->profile != nullptr && cast->actor == "PLAYER") {
                    /*actor->plane = new SCPlane(10.0f, -7.0f, 40.0f, 40.0f, 30.0f, 100.0f, 390.0f, 18000.0f, 8000.0f,
                                                23000.0f, 32.0f, .93f, 120, this->area, part->position.x,
                                                part->position.y, part->position.z);*/
                    BoudingBox *bb = actor->object->entity->GetBoudingBpx();
                    actor->plane = new SCJdynPlane(
                        10.0f,
                        -7.0f,
                        40.0f,
                        40.0f,
                        30.0f,
                        100.0f,
                        actor->object->entity->wing_area,
                        (float) actor->object->entity->weight_in_kg,
                        (float) actor->object->entity->jdyn->FUEL,
                        (float) actor->object->entity->thrust_in_newton,
                        (bb->max.z - bb->min.z) / 2.0f,
                        .93f,
                        120,
                        this->area,
                        part->position.x,
                        part->position.y,
                        part->position.z
                    );
                    actor->plane->yaw = (360 - part->azymuth) * 10.0f;
                    actor->plane->simple_simulation = false;
                    actor->plane->yaw = (360 - part->azymuth) * (float) M_PI / 180.0f;
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
    for (auto area_actor: this->area->objects) {
        SCMissionActors *actor = new SCMissionActors();
        MISN_PART *part = new MISN_PART();
        part->id = cpt_actor++;
        part->member_name = area_actor.name;
        part->member_name_destroyed = area_actor.destroyedName;
        part->entity = area_actor.entity;
        part->position = area_actor.position;
        actor->actor_name = area_actor.name;
        actor->plane = nullptr;
        actor->pilot = nullptr;
        actor->actor_id = part->id;
        actor->object = part;
        actor->profile = nullptr;
        actor->is_active = true;
        actor->is_hidden = false;
        actor->team_id = 255; // by default set it to enemy
        if (area_actor.entity == nullptr) {
            continue;
        }
        actor->mission = this;
        for (auto prg_id: area_actor.progs_id) {
            if (prg_id != 255 && prg_id != 0 && prg_id < this->mission->mission_data.prog.size()) {
                for (auto prg: *this->mission->mission_data.prog[prg_id]) {
                    actor->prog.push_back(prg);
                }
            }
        }
        if (actor->object->entity->entity_type == EntityType::rnwy) {
            for (auto runway: this->area->objectOverlay) {
                Vector3D pos = actor->object->position;
                
                // Vérifier si la position de l'objet est à l'intérieur de la piste
                if (pos.x >= runway.lx && pos.x <= runway.hx && 
                    pos.z <= -runway.ly && pos.z >= -runway.hy) {
                    
                    // Calculer les dimensions de la piste
                    float width = std::abs(runway.lx - runway.hx); 
                    float length = std::abs(runway.ly - runway.hy);
                    
                    // Calculer l'orientation (angle) de la piste
                    float angle = std::atan2(runway.ly - runway.hy, 
                                            runway.lx - runway.hx);
                    
                    // Recalculer la bounding box
                    actor->object->entity->bb.min.x = -width / 2.0f;
                    actor->object->entity->bb.max.x = width / 2.0f;
                    actor->object->entity->bb.min.y = -5;
                    actor->object->entity->bb.max.y = 5;
                    actor->object->entity->bb.min.z = -length / 2.0f;
                    actor->object->entity->bb.max.z = length / 2.0f;
                    
                    // Appliquer l'orientation à l'objet
                    actor->object->azymuth = angle * 180.0f / M_PI;
                    
                    break;
                }
            }
        }
        this->actors.push_back(actor);
    }
    for (auto member: this->mission->mission_data.team) {
        int id = this->mission->mission_data.parts[member]->id;
        for (auto actor: this->actors) {
            if (actor->actor_id == id) {
                this->friendlies.push_back(actor);
            }
        }
    }
    for (auto enemis : this->actors) {
        if (std::find(this->friendlies.begin(), this->friendlies.end(), enemis) == this->friendlies.end()) {
            this->enemies.push_back(enemis);
        }
    }
    if (this->player->on_is_activated.size() > 0) {
        SCProg *p = new SCProg(this->player, this->player->on_is_activated, this, this->player->object->on_is_activated);
        p->execute();
    }
    if (this->player->on_mission_start.size() > 0 && this->player->prog_executed == false) {
        SCProg *p = new SCProg(this->player, this->player->on_mission_start, this, this->player->object->on_missions_init);
        p->execute();
        this->player->prog_executed = true;
    }
    for (auto spot: this->mission->mission_data.spots) {
        spot->origin = {spot->position.x, spot->position.y, spot->position.z};
        if (spot->area_id != -1) {
            AREA *ar = this->mission->mission_data.areas[spot->area_id];
            spot->position += ar->position;
        }
    }
    
}
RSEntity * SCMission::LoadEntity(std::string name) {
    std::string tmpname = Assets.object_root_path + name + ".IFF";
    RSEntity *objct = new RSEntity();
    TreEntry *entry = Assets.GetEntryByName((char *)tmpname.c_str());
    if (entry != nullptr) {
        objct->InitFromRAM(entry->data, entry->size, tmpname);
        return objct;
    }
    return nullptr;
}
void SCMission::update() {
    uint32_t current_time = SDL_GetTicks();
    uint32_t elapsed_time = (current_time - this->last_time) / 1000;
    uint32_t newtps = 0;
    if (elapsed_time > 1) {
        uint32_t ticks = this->tick_counter - this->last_tick;
        newtps = ticks / elapsed_time;
        this->last_time = current_time;
        this->last_tick = this->tick_counter;
        if (newtps > this->tps / 2) {    
            this->tps = newtps;
        }
    }
    if (this->mission_ended) {
        return;
    }
    this->tick_counter++;
    // No-op on every actor except the player (SCMissionActorsPlayer's
    // override) — see ShipComponent's own comment in SCMissionActors.h for
    // why only the player tracks internal component damage/repair.
    if (this->player != nullptr) {
        this->player->TickComponentRepair();
    }
    uint8_t area_id = this->getAreaID({this->player->plane->x, this->player->plane->y, this->player->plane->z});
    float yawRad = this->player->plane->yaw * (float)M_PI / 1800.0f; // Convert from 0.1 degrees to radians
    // Manual, 2026-07 session: "If the player ship's speed is under 150
    // enemy ships will attack the player ship head-on." Below that speed,
    // attackers' approach point flips to 300 units in FRONT of the
    // player (same magnitude, opposite sign) instead of behind — a slow
    // player can't out-turn/out-run a rear approach the way real combat
    // tactics assume, so real WC3 has enemies merge head-on instead. Only
    // applies to attacks *on the player* specifically (this block) — AI
    // vs. AI approach behavior below is unaffected, matching the manual's
    // own wording ("attack the player ship").
    float attackOffsetSign = (this->player->plane->airspeed < 150) ? 300.0f : -300.0f;
    // Position the offset behind (or, below 150 speed, in front of) the
    // aircraft based on current yaw
    this->player->attack_pos_offset.x = -std::sin(yawRad) * attackOffsetSign;
    this->player->attack_pos_offset.z = -std::cos(yawRad) * attackOffsetSign;
    this->player->attack_pos_offset.y = 0.0f; // Same altitude
    if (area_id != this->current_area_id) {
        this->current_area_id = area_id;
    }
    for (auto scene: this->mission->mission_data.scenes) {
        if (scene->area_id == area_id - 1 || scene->area_id == -1) {
            if (scene->is_active == 0) {
                if (scene->on_mission_update != -1) {
                    if (scene->on_mission_update < this->mission->mission_data.prog.size()) {
                        std::vector<PROG> prog;
                        for (auto prg: *this->mission->mission_data.prog[scene->on_mission_update]) {
                            prog.push_back(prg);
                        }
                        SCProg *p = new SCProg(this->player, prog, this, scene->on_mission_update);
                        p->execute();
                        delete p;
                        prog.clear();
                        prog.shrink_to_fit();
                    }
                }
                if (scene->on_leaving != -1) {
                    if (scene->on_leaving < this->mission->mission_data.prog.size()) {
                        std::vector<PROG> prog;
                        for (auto prg: *this->mission->mission_data.prog[scene->on_leaving]) {
                            prog.push_back(prg);
                        }
                        SCProg *p = new SCProg(this->player, prog, this, scene->on_leaving);
                        p->execute();
                        delete p;
                        prog.clear();
                        prog.shrink_to_fit();
                    }
                }
                continue;
            }
            // scene->cast entries are CAST array indices (same numbering as
            // MISN_PART::id) — not positions into mission_data.parts. The
            // old `i == cast` loop over `parts` happened to work for actors
            // whose PART record's array position coincided with their id,
            // but silently matched nothing (or the wrong actor) whenever it
            // didn't, which is exactly the case for cast members that only
            // exist via a scene (no PART record at all — see
            // WC3Mission::loadMission()'s scene-fallback construction).
            for (auto cast: scene->cast) {
                for (auto actor: this->actors) {
                    if (actor->actor_name == "PLAYER") {
                        continue;
                    }
                    if (actor->actor_id == cast && actor->is_active == false) {
                        actor->is_active = true;
                        actor->is_hidden = false;
                        if (scene->area_id != -1) {
                            Vector3D correction;
                            if (actor->object->unknown2 == 0) {
                                correction = this->mission->mission_data.areas[scene->area_id]->position;
                            } else if (actor->object->unknown2 == 1) {
                                correction = {
                                    this->player->plane->x,
                                    this->player->plane->y,
                                    this->player->plane->z
                                };
                            }
                            // area_corrected is set by WC3Mission::loadMission()
                            // for every part it positions at load time (real
                            // PART records and scene-only synthesized ones
                            // alike) — without this guard, an actor whose
                            // position was already fully resolved at load
                            // gets the same area offset added a second time
                            // the first time its scene activates here,
                            // landing it far from where it actually spawned.
                            if (actor->object->area_id != 255 && !actor->object->area_corrected) {
                                actor->object->position += correction;
                            }
                            // Space missions have no terrain (this->area
                            // stays null) — nothing to snap to, and every
                            // actor simply starts airborne.
                            if (this->area != nullptr) {
                                float ground_y = this->area->getY(actor->object->position.x, actor->object->position.z);

                                if (actor->plane != nullptr) {
                                    actor->plane->on_ground = false;
                                    actor->plane->x = actor->object->position.x;
                                    actor->plane->y = actor->object->position.y;
                                    actor->plane->z = actor->object->position.z;
                                    if (abs(ground_y - actor->object->position.y) <= 10 ) {
                                        actor->object->position.y = ground_y;
                                        actor->plane->on_ground = true;
                                    } else {
                                        actor->plane->on_ground = false;
                                    }
                                    if (actor->object->position.y < ground_y) {
                                        actor->plane->position.y += ground_y;
                                    }
                                } else if (actor->object->position.y < ground_y) {
                                    actor->object->position.y = ground_y;
                                }
                            } else if (actor->plane != nullptr) {
                                actor->plane->on_ground = false;
                                actor->plane->x = actor->object->position.x;
                                actor->plane->y = actor->object->position.y;
                                actor->plane->z = actor->object->position.z;
                            }
                        }

                        if (actor->on_is_activated.size() > 0) {
                            SCProg *p = new SCProg(actor, actor->on_is_activated, this, actor->object->on_is_activated);
                            p->execute();
                            delete p;
                        }
                        if (this->area != nullptr && actor->object->entity->entity_type == EntityType::rnwy) {
                            for (auto runway: this->area->objectOverlay) {
                                Vector3D pos = actor->object->position;
                                
                                // Vérifier si la position de l'objet est à l'intérieur de la piste
                                if (pos.x >= runway.lx && pos.x <= runway.hx && 
                                    pos.z <= -runway.ly && pos.z >= -runway.hy) {
                                    
                                    // Calculer les dimensions de la piste
                                    float width = (float)std::abs(runway.lx - runway.hx); 
                                    float length = (float)std::abs(runway.ly - runway.hy);
                                    
                                    // Calculer l'orientation (angle) de la piste
                                    float angle = (float)std::atan2(runway.ly - runway.hy, 
                                                            runway.lx - runway.hx);
                                    
                                    // Recalculer la bounding box
                                    actor->object->entity->bb.min.x = -width / 2.0f;
                                    actor->object->entity->bb.max.x = width / 2.0f;
                                    actor->object->entity->bb.min.y = -5;
                                    actor->object->entity->bb.max.y = 5;
                                    actor->object->entity->bb.min.z = -length / 2.0f;
                                    actor->object->entity->bb.max.z = length / 2.0f;
                                    
                                    // Appliquer l'orientation à l'objet
                                    actor->object->azymuth = angle * 180.0f / M_PI;
                                    
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
            }
            if (scene->on_is_activated != -1) {
                if (scene->on_is_activated < this->mission->mission_data.prog.size() && scene->has_been_activated == 0) {
                    std::vector<PROG> prog;
                    for (auto prg: *this->mission->mission_data.prog[scene->on_is_activated]) {
                        prog.push_back(prg);
                    }
                    SCProg *p = new SCProg(this->player, prog, this, scene->on_is_activated);
                    p->execute();
                    scene->has_been_activated = 1;
                    delete p;
                    prog.clear();
                    prog.shrink_to_fit();
                }
            }
            scene->is_active = 0;
        }
        
    }
    this->in_combat = false;
    float cloakDt = (this->tps > 0) ? 1.0f / (float)this->tps : 0.0f;
    for (auto ai_actor : this->actors) {
        if (ai_actor != nullptr && !ai_actor->is_destroyed && cloakDt > 0.0f) {
            ai_actor->UpdateCloak(cloakDt);
            ai_actor->UpdateShieldRegen(cloakDt);
            if (ai_actor->collision_cooldown > 0.0f) {
                ai_actor->collision_cooldown -= cloakDt;
            }
        }
        if (ai_actor->object->alive == false && ai_actor->is_destroyed == false) {
            ai_actor->is_destroyed = true;
            if (ai_actor->on_is_destroyed.size() > 0 && ai_actor->plane == nullptr) {
                ai_actor->is_active = false;
                SCProg *p = new SCProg(ai_actor, ai_actor->on_is_destroyed, this, ai_actor->object->on_is_destroyed);
                p->execute();
                delete p;
            }
            // Fallback EXPL spawn for anything that went alive=false without
            // already exploding via hasBeenHit — mainly capital ships and
            // static/ground objects (planes are covered further below, in
            // the flying-actor wreck-falling loop, or already covered here
            // via hasBeenHit itself for a weapon kill). has_exploded keeps
            // this from double-firing on top of hasBeenHit's own spawn.
            if (!ai_actor->has_exploded && ai_actor->object->entity != nullptr &&
                ai_actor->object->entity->explos != nullptr) {
                ai_actor->has_exploded = true;
                // 12x explosion scale for capital ships — see hasBeenHit's
                // own comment (SCMissionActors.cpp) for why.
                std::string upperName = ai_actor->object->member_name;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                float explosionScale = SCMissionActors::IsCapitalShipName(upperName) ? 600.0f : 50.0f;
                bool bigDeathSequence = SCMissionActors::IsLargeCapitalShipName(upperName);
                this->explosions.push_back(new SCExplosion(ai_actor->object->entity->explos->objct, ai_actor->object->position, explosionScale, bigDeathSequence));
                if (this->sound.sounds.size() > 0) {
                    MemSound *sound = this->sound.sounds[SoundEffectIds::EXPLOSION_1];
                    Mixer.playSoundVoc(sound->data, sound->size);
                }
            }
            if (ai_actor->object->member_name_destroyed != "") {
                RSEntity *entity = LoadEntity(ai_actor->object->member_name_destroyed);
                if (entity != nullptr && ai_actor->object->entity->jdyn == nullptr) {
                    ai_actor->object->entity = entity;
                }
            }
            if (ai_actor->object->entity->destroyed_object != nullptr && ai_actor->plane == nullptr) {
                if (ai_actor->object->entity->jdyn == nullptr) {
                    ai_actor->object->entity = ai_actor->object->entity->destroyed_object;
                }
            }
            if (ai_actor->target != nullptr) {
                if (ai_actor->target->attacker == ai_actor) {
                    ai_actor->target->attacker = nullptr;
                }
            }
        }
        if (ai_actor->object->entity != nullptr && ai_actor->object->entity->entity_type == EntityType::swpn && !ai_actor->is_destroyed && ai_actor->is_active) {
            
            if (ai_actor->target != nullptr && ai_actor->target->is_destroyed == false) {
                if (ai_actor->retarget_cooldown > 0) {
                    ai_actor->shootWeapon(ai_actor->target);
                    ai_actor->retarget_cooldown--;
                } else {
                    ai_actor->target = nullptr;
                }
            } else if (ai_actor->target != nullptr && ai_actor->target->is_destroyed == true) {
                ai_actor->target = nullptr;
            } else if (ai_actor->target == nullptr) {
                for (auto targets: this->actors) {
                    if (targets->plane == nullptr) {
                        continue;
                    }
                    if (targets->team_id == ai_actor->team_id) {
                        continue;
                    }
                    if ((targets->is_active && targets->is_destroyed == false && targets->object->alive) || (targets->actor_name == "PLAYER")) {
                        ai_actor->shootWeapon(targets);
                        ai_actor->target = targets;
                        ai_actor->retarget_cooldown = 100 + (rand() % 200);
                        break;
                    }
                }   
            }
        }
        for (auto weapon: ai_actor->weapons_shooted) {
            if (weapon) {
                weapon->Simulate(tps);
            }
        }
        for (auto weapon: ai_actor->weapons_shooted) {
            if (weapon->alive == false) {
                ai_actor->weapons_shooted.erase(
                    std::remove(ai_actor->weapons_shooted.begin(), ai_actor->weapons_shooted.end(), weapon),
                    ai_actor->weapons_shooted.end()
                );
                delete weapon;
                weapon = nullptr;
            }
        }
        
        if (ai_actor->profile == nullptr) {
            continue;
        }
        if (ai_actor->profile->radi.info.callsign == "Strike Base") {
            SCProg *p = new SCProg(ai_actor, ai_actor->on_update, this, ai_actor->object->on_mission_update);
            p->execute();
            delete p;
            continue;
        }
        if (ai_actor->is_active == false) {
            continue;
        }
        
        if (ai_actor->on_update.size() > 0 && ai_actor->is_destroyed == false && ai_actor->override_progs.size() == 0) {
            this->in_combat = ai_actor->target != nullptr && ai_actor->target == this->player;
            SCProg *p = new SCProg(ai_actor, ai_actor->on_update, this, ai_actor->object->on_mission_update);
            p->execute();
            delete p;
        } else if (ai_actor->override_progs.size() > 0 && ai_actor->is_destroyed == false) {
            SCProg *p = new SCProg(ai_actor, ai_actor->override_progs, this, 255);
            p->execute();
            delete p;
            if (ai_actor->current_command_executed) {
                ai_actor->override_progs.clear();
                ai_actor->override_progs.shrink_to_fit();
            }
        }
        
        // Disabled actors (OP_SET_TARGET_DISABLED, SCProg.cpp — the "shoot
        // the engines until it reports disabled" mechanic) stop flying and
        // fighting but stay alive/on-screen, dead in space at wherever they
        // were when disabled — distinct from is_destroyed, which already
        // short-circuits above. Skips plane->Simulate()/pilot->FlyTo()
        // below (no further movement) as well as the AI command dispatch
        // just above.
        if (ai_actor->is_disabled) {
            continue;
        }
        ai_actor->protectSelf();
        switch (ai_actor->current_command) {
            case OP_SET_WAIT_FOR_SECONDS:
                ai_actor->current_command_executed = ai_actor->wait(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_TAKE_OFF:
                ai_actor->current_command_executed = ai_actor->takeOff(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_LAND:
                ai_actor->current_command_executed = ai_actor->land(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_FLY_TO_WP:
                ai_actor->current_command_executed = ai_actor->flyToWaypoint(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_FLY_TO_AREA:
                ai_actor->current_command_executed = ai_actor->flyToArea(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_DESTROY_TARGET:
                ai_actor->current_command_executed = ai_actor->destroyTarget(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_DEFEND_TARGET:
                ai_actor->current_command_executed = ai_actor->defendTarget(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_DEFEND_AREA:
                ai_actor->current_command_executed = ai_actor->defendArea(ai_actor->current_command_arg);
            break;
            case OP_SET_OBJ_FOLLOW_ALLY:
                ai_actor->current_command_executed = ai_actor->followAlly(ai_actor->current_command_arg);
            break;
            default:
            break;
        }
        
        if (ai_actor->pilot == nullptr) {
            continue;
        }
        if (ai_actor->plane == nullptr) {
            continue;
        }
        
        ai_actor->plane->Simulate();
        // Update attack position offset based on plane's yaw to keep it behind the plane
        
        yawRad = ai_actor->plane->yaw * (float)M_PI / 1800.0f; // Convert from 0.1 degrees to radians
        // Position the offset behind the aircraft based on current yaw
        ai_actor->attack_pos_offset.x = -std::sin(yawRad) * -300.0f; // 200 units behind
        ai_actor->attack_pos_offset.z = -std::cos(yawRad) * -300.0f;
        ai_actor->attack_pos_offset.y = 0.0f; // Same altitude
    
        ai_actor->pilot->FlyTo();
        
        Vector3D npos;
        ai_actor->plane->getPosition(&npos);
        
        ai_actor->object->position.x = npos.x;
        ai_actor->object->position.z = npos.z;
        ai_actor->object->position.y = npos.y;
        ai_actor->object->azymuth = 360 - (uint16_t)(ai_actor->plane->azimuthf / 10.0f);
        ai_actor->object->roll = (uint16_t)(ai_actor->plane->twist / 10.0f);
        ai_actor->object->pitch = (uint16_t)(ai_actor->plane->elevationf / 10.0f);
        // Space missions have no terrain (this->area stays null) — there's no
        // ground for wreckage to fall to, so treat "destroyed" as final
        // immediately instead of waiting to fall below a ground level.
        if (ai_actor->is_destroyed == true &&
            (this->area == nullptr || ai_actor->object->position.y < this->area->getY(ai_actor->object->position.x, ai_actor->object->position.z))) {
            ai_actor->object->alive = false;
            ai_actor->is_active = false;
            if (ai_actor->on_is_destroyed.size() > 0 && ai_actor->plane != nullptr) {
                SCProg *p = new SCProg(ai_actor, ai_actor->on_is_destroyed, this, ai_actor->object->on_is_destroyed);
                p->execute();
                delete p;
            }
            // has_exploded guard: a weapon kill already spawned this
            // actor's explosion via hasBeenHit at the moment it died (mid-
            // air) — don't spawn a second one here just because its wreck
            // has now finished falling/reached "final" status. Also guards
            // the entity->explos dereference itself, previously
            // unconditional (crashed on any actor with no EXPL chunk —
            // live-tested 2026-07 session).
            if (!ai_actor->has_exploded && ai_actor->object->entity != nullptr &&
                ai_actor->object->entity->explos != nullptr) {
                ai_actor->has_exploded = true;
                this->explosions.push_back(new SCExplosion(ai_actor->object->entity->explos->objct, ai_actor->object->position));
                if (this->sound.sounds.size() > 0) {
                    MemSound *sound = this->sound.sounds[SoundEffectIds::EXPLOSION_4];
                    Mixer.playSoundVoc(sound->data, sound->size, 4, 0);
                }
            }
            if (ai_actor->object->entity->destroyed_object != nullptr && ai_actor->plane == nullptr) {
                ai_actor->object->entity = ai_actor->object->entity->destroyed_object;
            }
            //ai_actor->plane = nullptr;
        }
    }
    // Ship-to-ship collision damage (manual, 2026-07 session). Pairwise
    // world-space AABB overlap test, restricted to EntityType::jet (every
    // SSHP-parsed entity — fighters and capital ships alike; excludes
    // turret sub-actors, debris, ground props, etc., which would
    // otherwise spuriously "collide" with their own parent ship every
    // tick). collision_cooldown on both sides guards against re-applying
    // damage every tick while two actors stay overlapping — no friendly-
    // fire exclusion, matching the existing weapon friendly-fire policy
    // (see [[project_wc3_hit_visuals_and_collision]]).
    //
    // Bug fix (2026-07 session, user-reported: wingman colliding on
    // takeoff and exploding): capital ships used to be full participants
    // here, same as any fighter. WC3Mission.cpp's own hangar-bay scene-
    // spawn fallback places a docked wingman only ~40 units from the
    // player (position += 40+spreadOffset), which — with the player
    // starting inside/against the TCS Victory's own hull — lands well
    // inside the Victory's raw, unscaled, hundreds-of-units-across mesh
    // AABB. GetCollisionDamage() derives damage from the *attacker's own*
    // shield+armor total, so overlapping the Victory (a capital ship's
    // shield+armor dwarfs any fighter's) was instantly lethal the moment
    // the wingman's is_active flag flipped true — normally the very first
    // SCMission::update() tick, with collision_cooldown still at its
    // default 0.0f. Capital ships already have their own separate DAMG>
    // CPTL hull-point system for weapons fire; there's no equivalent real
    // mechanic for a fighter grazing a carrier's hull to one-shot it (or
    // for a capital ship to take fighter-collision damage at all), so
    // capital ships are excluded from this system entirely rather than
    // adding a docked-state/grace-period exception that would still leave
    // every other capital-ship-adjacent maneuver (formation flying close
    // to the Victory, launch/landing approaches) exposed to the same bug.
    for (size_t i = 0; i < this->actors.size(); i++) {
        SCMissionActors *a = this->actors[i];
        if (a == nullptr || a->is_destroyed || !a->is_active || a->collision_cooldown > 0.0f ||
            a->object == nullptr || a->object->entity == nullptr ||
            a->object->entity->entity_type != EntityType::jet ||
            SCMissionActors::IsCapitalShipName(a->object->member_name)) {
            continue;
        }
        BoudingBox *bbA = a->object->entity->GetBoudingBpx();
        if (bbA == nullptr) {
            continue;
        }
        Point3D aMin = {a->object->position.x + bbA->min.x, a->object->position.y + bbA->min.y, a->object->position.z + bbA->min.z};
        Point3D aMax = {a->object->position.x + bbA->max.x, a->object->position.y + bbA->max.y, a->object->position.z + bbA->max.z};
        for (size_t j = i + 1; j < this->actors.size(); j++) {
            SCMissionActors *b = this->actors[j];
            if (b == nullptr || b->is_destroyed || !b->is_active || b->collision_cooldown > 0.0f ||
                b->object == nullptr || b->object->entity == nullptr ||
                b->object->entity->entity_type != EntityType::jet ||
                SCMissionActors::IsCapitalShipName(b->object->member_name)) {
                continue;
            }
            BoudingBox *bbB = b->object->entity->GetBoudingBpx();
            if (bbB == nullptr) {
                continue;
            }
            Point3D bMin = {b->object->position.x + bbB->min.x, b->object->position.y + bbB->min.y, b->object->position.z + bbB->min.z};
            Point3D bMax = {b->object->position.x + bbB->max.x, b->object->position.y + bbB->max.y, b->object->position.z + bbB->max.z};
            bool overlap = aMin.x <= bMax.x && aMax.x >= bMin.x &&
                           aMin.y <= bMax.y && aMax.y >= bMin.y &&
                           aMin.z <= bMax.z && aMax.z >= bMin.z;
            if (!overlap) {
                continue;
            }
            int damageFromA = a->GetCollisionDamage();
            int damageFromB = b->GetCollisionDamage();
            b->ApplyDamage(damageFromA, a);
            a->ApplyDamage(damageFromB, b);
            // 1 second — long enough to clear the overlap in the common
            // case (a ram normally separates within a frame or two of
            // real gameplay speed) without needing real bounce/separation
            // physics, which this engine doesn't model for actor-actor
            // contact at all.
            a->collision_cooldown = 1.0f;
            b->collision_cooldown = 1.0f;
        }
    }
    // Space missions have no runway/ground for SCJdynPlane's own landed
    // model (on_ground stays false unconditionally — see the no_gravity
    // guards in SCJdynPlane::Simulate()/updateAcceleration()) to ever set
    // SCPlane::landed. Approximate it instead: inside the carrier's own
    // hangar-bay area (the actor whose cast name gave it a
    // SCMissionActorsStrikeBase, e.g. WC3's "VIC"-prefixed TCS Victory
    // entry — see WC3Mission::loadMission()) at under 100 KPS, and only
    // after having left that same area at least once since mission start.
    //
    // The player must explicitly radio the Victory and request landing
    // (the radio 'f' option, "NEED CLEARANCE" — see
    // SCMissionActors::respondToRadioMessage) before landing_clearance_granted
    // is set; only then does approach+slow-down actually land the plane.
    //
    // Known gap: real WC3 normally auto-grants clearance once the mission's
    // win/loss condition is already met, but a few missions (e.g. the
    // Laconda series) gate it behind an additional in-flight decision
    // (there: whether to help Flint). No mission-objective/win-condition
    // tracking exists yet, so respondToRadioMessage's 'f' case grants
    // clearance and reports a win unconditionally as soon as it's asked for.
    if (this->player->plane->no_gravity && !this->player->plane->landed) {
        for (auto* base_actor : this->actors) {
            if (dynamic_cast<SCMissionActorsStrikeBase*>(base_actor) == nullptr) continue;
            bool in_bay = base_actor->object->area_id != 255 &&
                          area_id == (uint8_t)(base_actor->object->area_id + 1);
            if (!in_bay) {
                this->has_left_carrier_bay = true;
            } else if (this->has_left_carrier_bay && this->landing_clearance_granted &&
                       this->player->plane->airspeed < 100) {
                this->player->plane->landed = true;
            }
            break;
        }
    }
    if (this->player->plane->landed) {
        this->mission_ended = true;
    }
}


void SCMission::executeProg(std::vector<PROG> *prog) {
    
}
uint8_t SCMission::getAreaID(Vector3D position) {
    uint8_t area_id = 255;
    float smallest_area_width = (std::numeric_limits<float>::max)();
    
    for (auto ar: this->mission->mission_data.areas) {
        if (ar->position.x - ar->AreaWidth / 2 <= position.x && ar->position.x + ar->AreaWidth / 2 >= position.x) {
            if (ar->position.z - ar->AreaWidth / 2 <= position.z && ar->position.z + ar->AreaWidth / 2 >= position.z) {
                if (area_id == 255 || ar->AreaWidth <= smallest_area_width) {
                    smallest_area_width = ar->AreaWidth;
                    area_id = ar->id;
                }
            }
        }
    }
    
    return area_id;
}