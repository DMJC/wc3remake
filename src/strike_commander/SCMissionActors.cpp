#include "precomp.h"
#include "SCMissionActors.h"
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <unordered_set>

bool SCMissionActors::wait(int seconds) {
    if (this->current_command != prog_op::OP_SET_WAIT_FOR_SECONDS) {
        if (this->wait_timer == 0) {
            if (this->plane != nullptr && this->plane->tps > 0) {
                this->wait_timer = seconds * this->plane->tps;
            } else {
                this->wait_timer = seconds * 60;
            }
                
        }
    }
    if (this->wait_timer > 0) {
        this->wait_timer--;
    }
    if (this->wait_timer <= 0) {
        this->wait_timer = -1;
        return true;
    }
    return false;
}

bool SCMissionActors::execute() { return true; }
/**
 * SCMissionActors::takeOff
 *
 * Called when the "take off" mission objective is triggered.
 *
 * If the actor has already taken off, this function does nothing and
 * returns true.
 *
 * Otherwise, this function sets the actor's current objective to
 * OP_SET_OBJ_TAKE_OFF and sets the pilot's target climb to 300 units above
 * the current height.  If the actor is close enough to the target height,
 * the actor is marked as having taken off and the function returns true.
 *
 * @param arg Unused argument.
 *
 * @return True if the actor has taken off, false otherwise.
 */
bool SCMissionActors::takeOff(uint8_t arg) {
    if (taken_off) {
        return true;
    }
    this->current_objective = OP_SET_OBJ_TAKE_OFF;
    if (this->pilot->target_climb == 0) {
        this->pilot->target_speed = -15;
        this->pilot->target_climb = (int) (this->plane->y + 1000.0f);
        this->pilot->target_azimut = this->plane->yaw;
    }
    if (std::abs(this->plane->y-this->pilot->target_climb) < 10.0f) {
        this->taken_off = true;
    }
    return this->taken_off;
}
bool SCMissionActors::land(uint8_t arg) {
    this->current_objective = OP_SET_OBJ_LAND;
    auto it = std::find(this->mission->friendlies.begin(), this->mission->friendlies.end(), this);
    if (it != this->mission->friendlies.end()) {
        this->mission->friendlies.erase(it);
    }
    if (arg < this->mission->mission->mission_data.spots.size()) {
        SPOT *wp = this->mission->mission->mission_data.spots[arg];
        this->pilot->SetTargetWaypoint(wp->position);
        this->pilot->target_speed = -10;
        Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
        Vector3D diff = wp->position - position;
        float dist = diff.Length();
        const float landing_dist = 3000.0f;
        if (dist < landing_dist) {
            this->pilot->turning = false;
            this->pilot->land = true;
            this->pilot->target_speed = (int) (-10.0f * (dist/landing_dist));
        }
        if (dist < 2000.0f) {
            return true;
        }
    }
    return false;
}
bool SCMissionActors::flyToWaypoint(uint8_t arg) {
    this->current_objective = OP_SET_OBJ_FLY_TO_WP;
    if (arg < this->mission->mission->mission_data.spots.size()) {
        SPOT *wp = this->mission->mission->mission_data.spots[arg];
        if (this->pilot != nullptr) {
            this->pilot->SetTargetWaypoint(wp->position);
            this->pilot->target_speed = -10;
            Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
            Vector3D diff = wp->position - position;
            float dist = diff.Length();
            if (dist < 5000.0f) {
                return true;
            }
        }
    }
    
    return false;
}
bool SCMissionActors::flyToArea(uint8_t arg) {
    this->current_objective = OP_SET_OBJ_FLY_TO_AREA;
    SPOT *wp = nullptr;
    for (auto spot: this->mission->mission->mission_data.spots) {
        if (spot->id == arg) {
            wp = spot;
            break;
        }
    }
    
    if (wp != nullptr) {
        int area_id = this->mission->getAreaID({wp->position.x, wp->position.y, wp->position.z});

        AREA *area = nullptr;
        for (auto area_test: this->mission->mission->mission_data.areas) {
            if (area_test->id == area_id) {
                area = area_test;
                break;
            }
        }
        this->pilot->SetTargetWaypoint(area->position);
        this->pilot->target_speed = -10;
        Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
        Vector3D diff = area->position - position;
        float dist = diff.Length();
        if (dist < 3000.0f) {
            return true;
        }
    }
    return false;
}
/**
 * SCMissionActors::destroyTarget
 *
 * Called when the "destroy target" mission objective is triggered.
 *
 * This function sets the actor's current objective to
 * OP_SET_OBJ_DESTROY_TARGET and sets the pilot's target waypoint to the
 * position of the target actor.  If the target actor is not on the ground,
 * the pilot's target climb is set to the target actor's current height and
 * the target speed is set to either -60 or the target actor's current
 * vertical speed, depending on how far away the actor is.
 *
 * If the target actor is already destroyed, this function returns true.
 *
 * @param arg The ID of the target actor to destroy.
 *
 * @return True if the target actor is already destroyed, false otherwise.
 */
bool SCMissionActors::destroyTarget(uint8_t arg) {
    Vector3D wp;
    bool should_talk = false;
    bool is_new_target = false;
    bool is_ground_target = false;
    if (this->is_destroyed) {
        return false;
    }
    if (this->plane != nullptr && this->plane->alive == 0) {
        return false;
    }
    if (this->pilot == nullptr) {
        return false;
    }
    if (this->target == nullptr) {
        should_talk = true;
    }
    if (this->current_target == 0) {
        this->current_target = arg;
        is_new_target = true;
    }
    Vector3D position;
    if (this->plane == nullptr) {
        position = {this->object->position.x, this->object->position.y, this->object->position.z};
    } else {
        position = {this->plane->x, this->plane->y, this->plane->z};
    }
    if (this->target == nullptr) {
        for (auto actor: this->mission->actors) {
            if (actor->actor_id == this->current_target) {
                this->target = actor;
                actor->attacker = this;
                break;
            }
        }
    }
    if (this->current_target != 0 && this->target != nullptr && this->target->plane != nullptr && this->target->plane->object->alive == 0) {
        this->releaseTarget();
        if (!is_new_target && this->current_objective == OP_SET_OBJ_DESTROY_TARGET) {
            std::srand(static_cast<unsigned int>(std::time(nullptr)));
            int talking = std::rand() % 16;
            if (talking >= this->profile->ai.atrb.VB) {
                this->setMessage(13);
            }
        }
        return true;
    }
    this->current_objective = OP_SET_OBJ_DESTROY_TARGET;
    uint8_t area_id = this->mission->getAreaID(position);
    if (this->target != nullptr) {
        SCMissionActors *actor = this->target;
        if (actor->plane != nullptr) {
            wp.x = actor->plane->x;
            wp.y = actor->plane->y;
            wp.z = actor->plane->z;
            wp = wp + actor->attack_pos_offset;
            if (target_position.Length() == 0.0f) {
                target_position = wp;
                target_position_update = 2000;
            }
            Vector3D target_position_diff = target_position - wp;
            if (target_position_diff.Length() > 0.0f && target_position_update == 0) {
                target_position = wp;
                target_position_update = 2000;
            } else if (target_position_diff.Length() > 0.0f) {
                target_position_update -= 1;
            }
            this->pilot->SetTargetWaypoint(wp);
            Vector3D diff = wp - position;
            float dist = diff.Length();
            if (!actor->plane->on_ground) {
                Vector3D real_pos = {actor->plane->x, actor->plane->y, actor->plane->z};
                Vector3D real_dist = real_pos - position;
                float real_distance = real_dist.Length();
                int hpt_id = 0;
                int attack_range = 0;
                int max_weap = 1;
                for (auto weap : this->plane->weaps_load) {
                    if (weap == nullptr) {
                        hpt_id++;
                        continue;
                    }
                    int effective_range = weap->objct->wdat->effective_range + 500 * (this->profile->ai.atrb.AA / 16);
                    if (effective_range >= real_distance) {
                        if (weap->nb_weap > 0) {
                            attack_range = weap->objct->wdat->target_range;
                            if (current_weapon_index != weap->objct->wdat->weapon_category) {
                                current_weapon_index = weap->objct->wdat->weapon_category;
                                this->plane->wp_cooldown = 0;
                            }
                            if (weap->objct->wdat->weapon_category==0) {
                                max_weap = 40;
                            }
                            break;
                        }
                    }
                    hpt_id++;
                }
                this->pilot->target_climb = (int) wp.y;
                if (dist > attack_range - 1000.0f) {
                    this->pilot->target_speed = -60;
                } else if (dist < attack_range - 300.0f) {
                    this->pilot->target_speed = (int) actor->plane->vz;
                    // Calculate azimuth between plane and target
                    float target_azimuth = 0.0f;
                    if (actor->plane != nullptr) {
                        Vector3D target_dir = {actor->plane->x - this->plane->x,
                                              0.0f,  // Ignore Y for horizontal azimuth
                                              actor->plane->z - this->plane->z};
                        // Calculate the angle in radians and convert to degrees
                        target_azimuth = atan2f(target_dir.z, target_dir.x) * 180.0f / M_PI;
                        target_azimuth -= 360.0f;
                        target_azimuth += 90.0f;
                        if (target_azimuth > 360.0f) {
                            target_azimuth -= 360.0f;
                        }
                        while (target_azimuth < 0.0f) {
                            target_azimuth += 360.0f;
                        }
                        target_azimuth = 360.0f - target_azimuth;;
                        float target_diff = target_azimuth - (this->plane->yaw/10.0f);
                        while (target_diff > 180.0f) target_diff -= 360.0f;
                        while (target_diff < -180.0f) target_diff += 360.0f;
     
                        // Only shoot if target is within firing arc
                        if (std::abs(target_diff) < 30.0f) {
                            if (this->plane->weaps_object.size() < max_weap) {
                                int should_shoot = std::rand() % 16;
                                if (should_shoot <= this->profile->ai.atrb.TH) {
                                    this->plane->Shoot(hpt_id, actor, this->mission);
                                }
                                
                            }
                        }    
                    }
                }
            }
        } else {
            is_ground_target = true;
            wp.x = actor->object->position.x;
            wp.y = this->plane->y; // Garder l'altitude actuelle
            wp.z = actor->object->position.z;
            
            this->pilot->SetTargetWaypoint(wp);
            Vector3D diff = wp - position;
            float dist = diff.Length();
            
            // Trouver l'arme appropriée pour attaquer la cible au sol
            int hpt_id = 0;
            int attack_range = 0;
            int max_weap = 1;
            bool can_attack = false;
            
            for (auto weap : this->plane->weaps_load) {
                if (weap == nullptr) {
                    hpt_id++;
                    continue;
                }
                
                // Les armes efficaces contre les cibles au sol (catégories 0=guns, 1=rockets, 2=missiles)
                int effective_range = weap->objct->wdat->effective_range + 500 * (this->profile->ai.atrb.AG / 16);
                bool is_bomb = false;
                const float desired_drop_height = this->plane->y;
                if (weap->objct->wdat->weapon_id == ID_MK82 || weap->objct->wdat->weapon_id == ID_MK20) {
                    is_bomb = true;
                    // Calculer la vitesse horizontale de l'avion (en m/s)
                    float horizontal_speed = std::sqrt(this->plane->vx * this->plane->vx + this->plane->vz * this->plane->vz);
                    const float gravity = 9.81f;
                    if (horizontal_speed > 0.1f) {
                        // Temps de chute depuis l'altitude de largage prévue
                        float fall_time = std::sqrt(2.0f * desired_drop_height / gravity);
                        
                        // Distance horizontale parcourue pendant la chute
                        attack_range = (int)(horizontal_speed * fall_time);
                        
                        // Ajouter une marge de sécurité
                        attack_range += (int)(desired_drop_height * 0.1f);
                        
                        effective_range = attack_range + 3000; // Zone d'approche large
                    }
                }
                if (effective_range >= dist) {
                    if (weap->nb_weap > 0) {
                        if (attack_range == 0 && !is_bomb) {
                            attack_range = weap->objct->wdat->target_range;
                        }
                        can_attack = true;

                        if (current_weapon_index != weap->objct->wdat->weapon_category) {
                            current_weapon_index = weap->objct->wdat->weapon_category;
                            this->plane->wp_cooldown = 0;
                        }
                        
                        if (weap->objct->wdat->weapon_category == 0) {
                            max_weap = 40;
                        }
                        break;
                    }
                }
                hpt_id++;
            }
            
            // Ajuster la vitesse et l'altitude en fonction de la distance
            if (dist > attack_range) {
                this->pilot->target_climb = (int) wp.y;
                this->pilot->target_speed = -60;
            } else if (dist < attack_range && can_attack) {
                this->pilot->target_speed = -20;
                this->pilot->target_climb = (int) actor->object->position.y + 500.0f;
                
                // Calculer l'azimut vers la cible
                Vector3D target_dir = {actor->object->position.x - this->plane->x,
                                      0.0f,
                                      actor->object->position.z - this->plane->z};
                
                float target_azimuth = atan2f(target_dir.z, target_dir.x) * 180.0f / M_PI;
                target_azimuth -= 360.0f;
                target_azimuth += 90.0f;
                if (target_azimuth > 360.0f) {
                    target_azimuth -= 360.0f;
                }
                while (target_azimuth < 0.0f) {
                    target_azimuth += 360.0f;
                }
                target_azimuth = 360.0f - target_azimuth;
                
                float target_diff = target_azimuth - (this->plane->yaw / 10.0f);
                while (target_diff > 180.0f) target_diff -= 360.0f;
                while (target_diff < -180.0f) target_diff += 360.0f;
                
                // Tirer si la cible est dans l'arc de tir (arc plus large pour les cibles au sol)
                if (std::abs(target_diff) < 45.0f) {
                    if (this->plane->weaps_object.size() < max_weap) {
                        int should_shoot = std::rand() % 16;
                        if (should_shoot <= this->profile->ai.atrb.AG) { // Utiliser AG au lieu de TH
                            this->plane->Shoot(hpt_id, actor, this->mission);
                        }
                    }
                }
            }
        }
        if (should_talk) {
            std::srand(static_cast<unsigned int>(std::time(nullptr)));
            int talking = std::rand() % 16;
            if (talking >= this->profile->ai.atrb.VB) {
                if (is_ground_target) {
                    this->setMessage(11);
                } else {
                    this->setMessage(12);
                }
            }
        }
    }
    return false;
}

void SCMissionActors::releaseTarget() {
    this->current_target = 0;
    if (this->target != nullptr) {
        this->target->attacker = nullptr;
    }
    this->target = nullptr;
    this->target_position.Clear();
}
/**
 * SCMissionActors::defendTarget
 *
 * Called when the "defend target" mission objective is triggered.
 *
 * This function sets the actor's current objective to OP_SET_OBJ_DEFEND_TARGET.
 * It first checks if there is an existing goal to destroy a target. If so,
 * it attempts to destroy that target and resets the goal if successful.
 *
 * If no current goal exists, the function determines the area in which the actor is located
 * and checks for any enemies within the same area. If an enemy is found, it sets the goal
 * to destroy that enemy and attempts to do so.
 *
 * @param arg Unused argument.
 *
 * @return True if no action is taken or if the current goal is successfully completed,
 *         false otherwise.
 */

bool SCMissionActors::defendTarget(uint8_t arg) {
    if (this->current_target != 0) {
        bool ret = this->destroyTarget(this->current_target);
        if (ret) {
            this->current_target = 0;
        }
        return ret;
    }
    this->current_objective = OP_SET_OBJ_DEFEND_TARGET;
    Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
    for (auto actor: this->mission->actors) {
        if (actor->team_id == this->team_id) {
            continue;
        }
        if (actor->plane != nullptr) {
            if (actor->is_active && actor->target != nullptr && actor->target->actor_id == arg) {
                if (actor->plane != nullptr && actor->plane->object->alive == 0) {
                    continue;
                }
                this->current_target = actor->actor_id;
                this->target = actor;
                bool ret = this->destroyTarget(actor->actor_id);
                if (ret) {
                    this->current_target = 0;
                }
                return ret;
            }
        }
    }
    for (auto actor: this->mission->actors) {
        if (actor->team_id == this->team_id) {
            continue;
        }
        if (actor->plane != nullptr) {
            if (actor->is_active && actor->target != nullptr && actor->target->actor_id == this->actor_id) {
                if (actor->plane != nullptr && actor->plane->object->alive == 0) {
                    continue;
                }
                this->current_target = actor->actor_id;
                bool ret = this->destroyTarget(actor->actor_id);
                if (ret) {
                    this->current_target = 0;
                }
                return ret;
            }
        }
    }
    return this->followAlly(arg);
}
bool SCMissionActors::defendArea(uint8_t arg) { 
    this->current_objective = OP_SET_OBJ_DEFEND_AREA;
    if (this->current_target != 0) {
        bool ret = this->destroyTarget(this->current_target);
        if (ret) {
            this->current_target = 0;
        }
        return ret;
    }
    Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
    SPOT *wp = nullptr;
    for (auto spot: this->mission->mission->mission_data.spots) {
        if (spot->id == arg) {
            wp = spot;
            break;
        }
    }
    
    if (wp != nullptr) {
        int area_id = this->mission->getAreaID({wp->position.x, wp->position.y, wp->position.z});
        Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
        int test_area_id = this->mission->getAreaID(position);
        if (area_id != test_area_id) {
            this->current_target = 0;
            return this->flyToArea(arg);
        }
        if (this->current_target == 0) {
            for (auto actor: this->mission->actors) {
                if (actor->team_id == this->team_id) {
                    continue;
                }
                if (actor->plane != nullptr) {
                    if (actor->team_id != this->team_id) {
                        uint8_t actor_area_id = this->mission->getAreaID({actor->plane->x, actor->plane->y, actor->plane->z});
                        if (actor_area_id == area_id) {
                            this->current_target = actor->actor_id;
                            break;
                        }
                    }
                }
            }
        }
    }
    return true;
}
/**
 * @brief Deactivates an actor in the mission based on the provided actor ID.
 *
 * This function iterates through the list of actors in the mission and sets the
 * `is_active` flag to `false` for the actor whose `actor_id` matches the provided argument.
 *
 * @param arg The ID of the actor to deactivate.
 * @return true if an actor with the specified ID was found and deactivated, false otherwise.
 */
bool SCMissionActors::deactivate(uint8_t arg) {
    for (auto actor: this->mission->actors) {
        if (actor->actor_id == arg) {
            actor->is_active = false;
            return true;
        }
    }
    return false;
}
/**
 * @brief Sets a message for the mission actor.
 * 
 * This function sets a message for the mission actor based on the provided argument.
 * 
 * @param arg The index of the message to be set.
 * @return true Always returns true.
 */
bool SCMissionActors::setMessage(uint8_t arg) {
    if (!this->talkative) {
        return false;
    }
    bool hasText = this->profile->radi.msgs.count(arg) > 0;
    bool hasSound = this->profile->radi.sond.count(arg) > 0;
    if (!hasText && !hasSound) {
        return false;
    }
    RadioMessages *msg = new RadioMessages();
    msg->message = this->profile->radi.info.callsign + ": " + this->profile->radi.msgs[arg];

    if (this->mission->sound.inGameVoices.size() > 0) {
        if (this->mission->sound.inGameVoices.find(this->profile->radi.spch) != this->mission->sound.inGameVoices.end()) {
            if (this->mission->sound.inGameVoices[this->profile->radi.spch]->messages.find(arg) != this->mission->sound.inGameVoices[this->profile->radi.spch]->messages.end()) {
                MemSound *message_sound = this->mission->sound.inGameVoices[this->profile->radi.spch]->messages[arg];
                msg->sound = message_sound;
            }
        }
    }
    // WC3's own MISSIONS.TRE-embedded profiles carry radio audio directly
    // (a SOND chunk keyed by the same RADI code) instead of the SC-style
    // indirect spch+SPEECH.PAK reference above — fall back to it when that
    // lookup found nothing.
    if (msg->sound == nullptr && hasSound) {
        const std::vector<uint8_t> *wav = this->profile->getRadiSoundWav(arg);
        if (wav != nullptr) {
            MemSound *sound_msg = new MemSound();
            sound_msg->data = const_cast<uint8_t*>(wav->data());
            sound_msg->size = wav->size();
            sound_msg->id = arg;
            msg->sound = sound_msg;
        }
    }
    // Real "radio face" video clip for this RADI code, if this profile has
    // one (WC3-only; RADI_FMV::index shares the same code space as
    // msgs/sond/msgg/msgf — see RSProf.h). Just resolves the filename
    // here; SCCockpit's own comm rendering is what actually loads and
    // plays it, keeping this shared strike_commander/ file free of any
    // MVE-decoding dependency.
    for (auto &fmv : this->profile->radi.fmv) {
        if (fmv.index == arg) {
            msg->fmvFilename = fmv.filename;
            break;
        }
    }
    this->mission->radio_messages.push_back(msg);
    return true;
}
/**
 * @brief Sets the current objective to follow an ally and adjusts the pilot's target waypoint, speed, and climb.
 * 
 * This function iterates through the mission's actors to find the actor with the specified ID.
 * If the actor is found, it sets the waypoint to the actor's position plus the formation position offset,
 * and adjusts the pilot's target waypoint, speed, and climb based on the distance to the waypoint and whether
 * the actor's plane is on the ground.
 * 
 * @param arg The ID of the ally actor to follow.
 * @return true if the ally actor with the specified ID is found and the objective is set, false otherwise.
 */
bool SCMissionActors::followAlly(uint8_t arg) {
    Vector3D wp;
    this->current_objective = OP_SET_OBJ_FOLLOW_ALLY;
    if (this->attacker != nullptr) {
        this->destroyTarget(this->attacker->actor_id);
    }
    for (auto actor: this->mission->actors) {
        if (actor->actor_id == arg) {
            if (actor->is_destroyed || (!actor->is_active && (actor->actor_name != "PLAYER"))) {
                return false; // Actor not found or plane not alive
            }
            if (actor->plane == nullptr) {
                return false;
            }
            wp.x = actor->plane->x;
            wp.y = actor->plane->y;
            wp.z = actor->plane->z;
            wp = wp + actor->formation_pos_offset;
            this->pilot->SetTargetWaypoint(wp);
            Vector3D position = {this->plane->x, this->plane->y, this->plane->z};
            Vector3D diff = wp - position;
            float dist = diff.Length();
            if (!actor->plane->on_ground) {
                this->pilot->target_climb = (int) wp.y;
                if (dist > 1000.0f) {
                    this->pilot->target_speed = -60;
                } else if (dist < 400.0f) {
                    this->pilot->target_speed = (int) actor->plane->vz;
                    this->pilot->turning = false;
                }
            }
            
            return true;
        }
    }
    return false;
}
/**
 * SCMissionActors::ifTargetInSameArea
 *
 * Returns true if the actor with the given ID is in the same area as
 * the current actor, false otherwise.
 *
 * @param arg The ID of the target actor to check.
 *
 * @return True if the target actor is in the same area as the current
 * actor, false otherwise.
 */
bool SCMissionActors::ifTargetInSameArea(uint8_t arg) {
    Vector3D position;
    if (this->plane == nullptr) {
        position = {this->object->position.x, this->object->position.y, this->object->position.z};
    } else {
        position = {this->plane->x, this->plane->y, this->plane->z};
    }
    Uint8 area_id = this->mission->getAreaID(position);
    for (auto actor: this->mission->actors) {
        if (actor->actor_id == arg) {
            if (actor->is_destroyed) {
                return false;
            }
            if (actor->is_active == true && !actor->is_destroyed) {
                return true;
            }

            if (actor->plane == nullptr) {
                return (area_id == actor->object->area_id);
            }
            Uint8 target_area_id = this->mission->getAreaID({actor->plane->x, actor->plane->y, actor->plane->z});
            if (area_id == target_area_id) {
                return true;
            }
        }
    }
    return false;
}
bool SCMissionActors::respondToRadioMessage(int message_id, SCMission *mission, SCMissionActors *sender) {
    int cpt = 1;
    float health_remaing = this->health / (float) this->object->entity->health * 100.0f;
    bool obeying = true;
    for (auto ask: this->profile->radi.opts) {
        if (cpt == message_id) {
            switch (ask) {
                case 'd':
                {
                    // request status 2 - 5 
                    
                    if (health_remaing > 75.0f) {
                        this->setMessage(2); // All is well
                    } else if (health_remaing > 50.0f) {
                        this->setMessage(3); // Minor damage
                    } else if (health_remaing > 25.0f) {
                        this->setMessage(4); // Major damage
                    } else {
                        this->setMessage(5); // Critical damage
                    }
                }
                break;
                case 'e':
                    // request take off
                break;
                case 'f':
                {
                    // "Request landing." Player-initiated, only meaningful
                    // when talking to the carrier (SCMissionActorsStrikeBase
                    // — e.g. WC3's "VIC"-prefixed TCS Victory entry). No
                    // mission-objective/win-condition tracking exists yet
                    // (nothing scores kills/objectives complete), so
                    // clearance is granted and the mission reported as a win
                    // unconditionally as soon as it's asked for; the actual
                    // landing still requires the player to fly back into the
                    // carrier's bay and slow down (see SCMission::update()).
                    this->mission->landing_clearance_granted = true;
                    this->setMessage(20);
                }
                break;
                case 'g':
                {
                    uint8_t area_to_defend = this->mission->getAreaID({this->plane->x, this->plane->y, this->plane->z});
                    this->override_progs.clear();
                    this->override_progs.push_back({prog_op::OP_SET_OBJ_DEFEND_AREA, area_to_defend});
                    this->setMessage(1);
                }
                break;
                case 'h':
                    // build formation
                    this->override_progs.clear();
                    this->setMessage(1);
                break;
                case 'i':
                    // attack my target
                    if (sender != nullptr && sender->target != nullptr) {
                        if (this->current_command == prog_op::OP_SET_OBJ_DEFEND_TARGET || this->current_command == prog_op::OP_SET_OBJ_DESTROY_TARGET) {
                            obeying = std::rand() % 16 < this->profile->ai.atrb.LY;
                        }
                        if (obeying) {
                            this->override_progs.clear();
                            this->override_progs.push_back({prog_op::OP_SET_OBJ_DESTROY_TARGET, sender->target->actor_id});
                            this->setMessage(1);
                        } else {
                            this->setMessage(0);
                        }
                    }
                break;
                case 'j':
                    if (this->current_command == prog_op::OP_SET_OBJ_DEFEND_TARGET || this->current_command == prog_op::OP_SET_OBJ_DESTROY_TARGET || health_remaing > 75.0f) {
                        obeying = std::rand() % 16 < this->profile->ai.atrb.LY;
                    }
                    if (health_remaing < 30.0f) {
                        obeying = true;
                    }
                    if (obeying) {
                        this->override_progs.clear();
                        this->override_progs.push_back({prog_op::OP_SET_OBJ_LAND, 0});
                        this->setMessage(1);
                    } else {
                        this->setMessage(0);
                    }
                    
                break;
                case 'k':
                    if (sender != nullptr) {
                        this->override_progs.clear();
                        this->override_progs.push_back({prog_op::OP_SET_OBJ_DEFEND_TARGET, sender->actor_id});
                        this->setMessage(1);
                    }
                break;
                case 'l':
                    // maintain radio silence
                    this->talkative = false;
                break;
                case 'm':
                    // break radio silence
                    this->talkative = true;
                    this->setMessage(1);
                break;
                default:
                break;
            }
        }
        cpt++;
    }
    return false;
}
bool SCMissionActors::protectSelf() {
    if (this->attacker != nullptr) {
        if (this->attacker->actor_name == "PLAYER") {
            return false;
        }
        if (this->attacker->plane != nullptr) {
            for (auto weap: this->attacker->plane->weaps_object) {
                if (weap->target == this) {
                    // Check if this actor is in the friendlies list
                    bool is_friendly = false;
                    for (auto friendly : this->mission->friendlies) {
                        if (friendly == this) {
                            is_friendly = true;
                            break;
                        }
                    }

                    // If this is a friendly actor, attempt to evade the incoming weapon
                    if (is_friendly) {
                        weap->target = nullptr; // Disengage the weapon from this actor
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
/**
 * @brief Activates the target actor with the specified ID.
 *
 * This function iterates through the list of actors in the mission and sets the
 * `is_active` flag to true for the actor whose `actor_id` matches the provided argument.
 *
 * @param arg The ID of the actor to be activated.
 * @return true if the actor with the specified ID was found and activated, false otherwise.
 */
bool SCMissionActors::activateTarget(uint8_t arg) {
    for (auto actor: this->mission->actors) {
        if (actor->actor_id == arg && actor->is_active == false && actor->is_destroyed == false) {
            actor->is_active = true;
            actor->is_hidden = false;
            Vector3D correction = {0.0f, 0.0f, 0.0f};
            if (actor->object->area_id != 255 && actor->object->unknown2 == 0) {
                bool spot_found = false;
                for (auto spot: this->mission->mission->mission_data.spots) {
                    if (spot->area_id == actor->object->area_id) {
                        correction = spot->position;
                        spot_found = true;
                        break;
                    }
                }
                if (!spot_found) {
                    correction = this->mission->mission->mission_data.areas[actor->object->area_id]->position;
                }
            } else {
                correction = {
                    this->mission->player->plane->x,
                    this->mission->player->plane->y,
                    this->mission->player->plane->z
                };
            }
            if ((actor->object->area_id != 255) || (actor->object->area_id == 255 && actor->object->unknown2 == 1)) {
                actor->object->position += correction;
            }
            // WC3 space missions have no terrain (area stays null — see
            // WC3Mission::loadMission()'s is_space_mission), unlike Strike
            // Commander which always has some. Same -1000000.0f "no ground"
            // convention SCJdynPlane::updatePosition already uses.
            float ground_y = (this->mission->area != nullptr)
                ? this->mission->area->getY(actor->object->position.x, actor->object->position.z)
                : -1000000.0f;
            if (actor->object->position.y < ground_y) {
                actor->object->position.y = ground_y;
            }
            if (actor->plane != nullptr) {
                if (actor->object->position.y <= ground_y) {
                    actor->object->position.y = ground_y+10.0f;
                    actor->plane->on_ground = true;
                } else {
                    actor->plane->on_ground = false;
                }
                actor->plane->x = actor->object->position.x;
                actor->plane->y = actor->object->position.y;
                actor->plane->z = actor->object->position.z;
            }
            if (actor->plane == nullptr) {
                if (actor->object->position.y > ground_y) {
                    actor->object->position.y = ground_y+2.0f;
                } else if (actor->object->position.y < ground_y) {
                    actor->object->position.y = ground_y+2.0f;
                }
            }
            if (actor->team_id == this->mission->player->team_id) {
                this->mission->friendlies.push_back(actor);
            } else {
                this->mission->enemies.push_back(actor);
            }
            if (actor->on_is_activated.size() > 0) {
                SCProg *p = new SCProg(actor, actor->on_is_activated, this->mission, actor->object->on_is_activated);
                p->execute();
            }
            if (actor->object->entity->entity_type == EntityType::rnwy) {
                for (auto runway: this->mission->area->objectOverlay) {
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
            return true;
        }
    }
    return false;
}

int SCMissionActors::getDistanceToTarget(uint8_t arg) {
    Vector3D position;
    if (this->plane == nullptr) {
        position = {this->object->position.x, this->object->position.y, this->object->position.z};
    } else {
        position = {this->plane->x, this->plane->y, this->plane->z};
    }
    Vector3D diff;
    for (auto actor: this->mission->actors) {
        if (actor->actor_id == arg) {
            if (actor->plane == nullptr) {
                diff = actor->object->position - position;
            } else {
                Vector3D target_pos = {actor->plane->x, actor->plane->y, actor->plane->z};
                diff = target_pos - position;
            }
            break;
        }
    }
    return (int) diff.Length()/1000;
}

int SCMissionActors::getDistanceToSpot(uint8_t arg) { 
    Vector3D position = this->mission->mission->mission_data.spots[arg]->position;
    Vector3D plane_pos = {this->plane->x, this->plane->y, this->plane->z};
    Vector3D diff = plane_pos - position;
    return (int) diff.Length()/1000;
}
void SCMissionActors::shootWeapon(SCMissionActors *target) {
    static int shoot_cooldown = 0;
    if (this->is_destroyed || !this->is_active) {
        return;
    }
    if (shoot_cooldown > 0) {
        shoot_cooldown--;
        return;
    }
    if (this->object->entity->entity_type != EntityType::swpn) {
        return;
    }
    if (this->object->entity->swpn_data == nullptr) {
        return;
    }
    if (this->object->entity->swpn_data->weapon_entity == nullptr) {
        return;
    }
    
    if (this->object->entity->swpn_data->max_simultaneous_shots > 0 && this->weapons_shooted.size() >= this->object->entity->swpn_data->max_simultaneous_shots) {
        return;
    }
    if (this->object->entity->swpn_data->weapons_round <= 0) {
        return; // No ammo left
    }
    RSEntity *weapon_entity = this->object->entity->swpn_data->weapon_entity;
    Vector3D target_position = {0.0f, 0.0f, 0.0f};
    if (target->plane != nullptr) {
        target_position = {target->plane->x, target->plane->y, target->plane->z};
    } else if (target->object != nullptr) {
        target_position = target->object->position;
    }
    Vector3D direction = target_position - this->object->position;
    float distance = direction.Length();
    
    if (distance> weapon_entity->wdat->target_range) {
        return; // No direction to shoot
    }
    this->aiming_vector = direction;
    direction.Normalize();
    float p_y = this->object->position.y;
    // area is null in WC3 space missions — see activateTarget's own
    // comment above. A stationary weapon emplacement (this function) is
    // just as reachable there as in a ground mission (e.g. a capital
    // ship's turret), so this needs the same guard.
    if (this->mission->area != nullptr) {
        float ground_y = this->mission->area->getY(this->object->position.x, this->object->position.z);
        if (ground_y > this->object->position.y) {
            this->object->position.y = ground_y + 10.0f;
        }
    }
    SCSimulatedObject *weapon = nullptr;

    switch (weapon_entity->wdat->weapon_category) {
        case 2: // Missiles
            if (this->weapons_shooted.size()> 1) {
                return; // Too many weapons already shot
            }
            if (weapon_entity->wdat->effective_range == 0) {
                weapon_entity->wdat->effective_range = weapon_entity->wdat->target_range;
            }
            weapon = new SCSimulatedObject();
            weapon->target = target;
            weapon->obj = weapon_entity;
            weapon->x = this->object->position.x;
            weapon->y = p_y;
            weapon->z = this->object->position.z;
            shoot_cooldown = 160;
            
            weapon->vx = direction.x * weapon_entity->dynn_miss->velovity_m_per_sec / 80.0f;
            weapon->vy = direction.y * weapon_entity->dynn_miss->velovity_m_per_sec / 80.0f;
            weapon->vz = direction.z * weapon_entity->dynn_miss->velovity_m_per_sec / 80.0f;
            break;
        case 0: // Guns
            if (this->weapons_shooted.size()> 30) {
                return; // Too many weapons already shot
            }
            weapon = new GunSimulatedObject();
            shoot_cooldown = 30;
            weapon->obj = weapon_entity;
            weapon->x = this->object->position.x;
            weapon->y = p_y;
            weapon->z = this->object->position.z;

            
            weapon->vx = direction.x *  1000.0f;
            weapon->vy = direction.y *  1000.0f;
            weapon->vz = direction.z *  1000.0f;
            break;
        default:
            return;
    }
    weapon->mission = this->mission;
    weapon->shooter = this;
    this->object->entity->swpn_data->weapons_round--;
    this->weapons_shooted.push_back(weapon);
}
// Which of the target's 4 facings an attack came from, derived purely from
// the target's yaw (SCMissionActors::object->azymuth) and the world-space
// direction from the target to the attacker — pitch/roll are intentionally
// ignored (a coarse 4-way front/back/left/right split doesn't need them,
// and folding them in risks a 3rd independent axis-sign guess on top of two
// already-uncertain ones below).
//
// Reuses the exact same heading-relative rotation already used (and
// presumably working, since it drives the live RAWS radar contact display)
// by SCCockpit::IdentifyRAWSContact: heading = 360 - azimuth_degrees,
// headingRad = heading/180*pi, then Vector2D::rotateAroundPoint(origin,
// headingRad) on the world-space (x,z) offset — see SCCockpit.cpp:1017,
// 1881, 1357. Applied here with the TARGET's own azymuth instead of the
// player's heading. Which axis of the rotated result maps to "ahead"
// (local Y here, matching that same code's own convention) vs "right"
// (local X) is NOT independently re-verified for this new use — it's
// inherited from that existing, in-use radar code rather than derived
// fresh, to avoid guessing a 4th axis convention from scratch in an area
// that has repeatedly gotten this kind of thing wrong on the first try
// this session (turret axes, afterburner mount, vertex winding, etc.).
// Needs live confirmation (attack a target head-on and check the front
// shield/armor actually drops, not a side one).
HitQuadrant SCMissionActors::ClassifyHitQuadrant(SCMissionActors *target, const Vector3D &attackerPos) {
    float targetHeadingDeg = 360.0f - (float)target->object->azymuth;
    float targetHeadingRad = targetHeadingDeg / 180.0f * (float)M_PI;
    Vector2D toAttacker = {
        attackerPos.x - target->object->position.x,
        attackerPos.z - target->object->position.z
    };
    Vector2D local = toAttacker.rotateAroundPoint({0, 0}, targetHeadingRad);
    if (fabsf(local.y) >= fabsf(local.x)) {
        return local.y >= 0.0f ? HitQuadrant::Front : HitQuadrant::Back;
    }
    return local.x >= 0.0f ? HitQuadrant::Right : HitQuadrant::Left;
}
bool SCMissionActors::IsCapitalShipName(const std::string &upperName) {
    // Prefix match, not exact: real transport PART records come in several
    // hull-type suffixed variants (TTRANSC, TTRANSCS, ...) that never
    // matched this list under exact-string lookup (2026-07 session, found
    // while cross-referencing every campaign mission's capital-ship PART
    // records for the friend/foe fallback below) — every other prefix here
    // is a real, distinct WC3 namespace (K = Kilrathi, T = Terran capital/
    // station) that doesn't collide with any fighter model name.
    static const std::vector<std::string> kCapitalPrefixes = {
        "VICTORY", "SHUTTLE", "BEHEMOTH", "KCORV", "KCRUISER", "KDESTL", "KDEST",
        "KTRN", "KDREAD", "KCARRIER", "TCRUISER", "TDEST", "TTRANS", "KASBASE",
        "EKAPSHI", "KTRAND", "KSGENR",
        // TBASE ("star base"/"Blackmane base" — user-confirmed 2026-07
        // session): the Confederation station, grouped with the other
        // friendly capital-tier entities for radar/marker purposes.
        "TBASE",
    };
    for (auto &prefix : kCapitalPrefixes) {
        if (upperName.rfind(prefix, 0) == 0) return true;
    }
    return false;
}
bool SCMissionActors::IsLargeCapitalShipName(const std::string &upperName) {
    static const std::vector<std::string> kLargePrefixes = {
        "KDREAD", "KCARRIER", "VICTORY", "TBASE", "BEHEMOTH",
    };
    for (auto &prefix : kLargePrefixes) {
        if (upperName.rfind(prefix, 0) == 0) return true;
    }
    return false;
}
bool SCMissionActors::IsKilrathiShipName(const std::string &upperName) {
    static const std::unordered_set<std::string> kKilrathiNames = {
        "KCORV", "KCRUISER", "KDESTL", "KDEST", "KTRN", "KDREAD", "KCARRIER",
        "KASBASE", "EKAPSHI", "KTRAND", "KSGENR",
        // Kilrathi fighters (TargShapeIndex::kShipNameToIndex) — species,
        // not just capital ships, so any future non-capital fallback use
        // classifies these correctly too.
        "DRALTHI", "VAKTOTH", "BLOODFNG", "STRAKHA", "PAKTAHN",
    };
    return kKilrathiNames.count(upperName) > 0;
}
bool SCMissionActors::IsFriendlySupplyStructureName(const std::string &upperName) {
    // Jump buoys (JUMPLG/JUMPMD/JUMPSM — large/medium/small variants) and
    // asteroid supply depots (DEPOT) — user-confirmed (2026-07 session):
    // neutral/friendly navigation structures, not ships, so kept separate
    // from IsCapitalShipName (which also drives the target-radar cross-vs-
    // dot marker and the autopilot capital-ship gate — these aren't
    // "capital ships" in either of those senses, just always-friendly
    // for radar-color purposes).
    static const std::vector<std::string> kPrefixes = {"JUMPLG", "JUMPMD", "JUMPSM", "DEPOT"};
    for (auto &prefix : kPrefixes) {
        if (upperName.rfind(prefix, 0) == 0) return true;
    }
    return false;
}
const std::vector<ShipComponent> &SCMissionActors::ComponentsForQuadrant(HitQuadrant quadrant) {
    static const std::vector<ShipComponent> kFront = {
        ShipComponent::VDU1, ShipComponent::VDU2, ShipComponent::TacticalDisplay};
    static const std::vector<ShipComponent> kRight = {
        ShipComponent::Communications, ShipComponent::Shields, ShipComponent::AutoRepair};
    static const std::vector<ShipComponent> kBack = {
        ShipComponent::Afterburners, ShipComponent::PowerPlant, ShipComponent::Engine};
    static const std::vector<ShipComponent> kLeft = {
        ShipComponent::Targeting, ShipComponent::Guns};
    switch (quadrant) {
        case HitQuadrant::Front: return kFront;
        case HitQuadrant::Right: return kRight;
        case HitQuadrant::Back:  return kBack;
        case HitQuadrant::Left:  return kLeft;
    }
    return kFront;
}
bool SCMissionActors::IsComponentRepairable(ShipComponent component) {
    return component != ShipComponent::VDU1 && component != ShipComponent::VDU2 &&
           component != ShipComponent::TacticalDisplay;
}
// User-corrected (2026-07 session): this used to be a no-op on the base
// class — only SCMissionActorsPlayer tracked internal components at all,
// so no non-player actor's component_damage[] ever moved off 0. Real WC3
// disable mechanics (e.g. the Torgo mission's "disable tankers" objective
// — user-confirmed: fire guns at the target's engines until it reports
// disabled) depend on this working for AI actors too — engine damage is
// exactly ShipComponent::Engine, already in ComponentsForQuadrant's own
// HitQuadrant::Back list. Promoted the real weighted-random logic here
// from what was previously only SCMissionActorsPlayer's own override
// (see the manual quote on that override, unchanged) — every actor now
// genuinely accumulates component damage, not just the player.
// SCMissionActorsPlayer::RollComponentDamage below calls this via
// SCMissionActors::RollComponentDamage(...) and then additionally pushes
// the result into this->plane's power fractions (a player/cockpit-HUD-only
// concern — AI actors have no such gauges to update).
void SCMissionActors::RollComponentDamage(HitQuadrant quadrant, int overflowDamage) {
    if (overflowDamage <= 0) {
        return;
    }
    const std::vector<ShipComponent> &candidates = SCMissionActors::ComponentsForQuadrant(quadrant);
    // Flat chance of pure structural damage with no component hit at all —
    // exact rate not given by the manual, chosen to make component damage
    // common but not guaranteed.
    constexpr int kNoComponentDamagePercent = 40;
    if ((std::rand() % 100) < kNoComponentDamagePercent) {
        return;
    }
    // Weighted pick favoring earlier list entries: a 3-component facing
    // gets weights 3/2/1 (first is 3x as likely as last), a 2-component
    // facing gets 2/1.
    int totalWeight = 0;
    for (size_t i = 0; i < candidates.size(); i++) {
        totalWeight += (int)(candidates.size() - i);
    }
    if (totalWeight <= 0) {
        return;
    }
    int roll = std::rand() % totalWeight;
    size_t chosenIndex = candidates.size() - 1;
    int cumulative = 0;
    for (size_t i = 0; i < candidates.size(); i++) {
        cumulative += (int)(candidates.size() - i);
        if (roll < cumulative) {
            chosenIndex = i;
            break;
        }
    }
    ShipComponent chosen = candidates[chosenIndex];
    float &dmg = this->component_damage[(size_t)chosen];
    if (dmg >= 100.0f) {
        return; // already fully (and permanently) destroyed
    }
    dmg = (std::min)(100.0f, dmg + (float)overflowDamage);
    // "If the power plant or engine is totally destroyed, the ship blows
    // up." Route through the normal health-reaches-0 death path in the
    // caller (hasBeenHit) rather than duplicating its death handling here.
    if (dmg >= 100.0f &&
        (chosen == ShipComponent::PowerPlant || chosen == ShipComponent::Engine)) {
        this->health = 0;
    }
}
void SCMissionActors::TickComponentRepair() {
    // No-op on the base class — see RollComponentDamage's own comment.
}
void SCMissionActors::UpdateCloak(float dt) {
    if (this->object == nullptr || this->object->entity == nullptr ||
        this->object->entity->clok == nullptr || this->plane == nullptr) {
        return;
    }
    // User-confirmed (2026-07 session): the Skipper Missile (SKIPMISS, a
    // real CAST/PART mission actor — not a fired SCSimulatedObject weapon,
    // confirmed via MISNA004.IFF's own CAST list) "drops in and out of
    // cloak every 5 seconds to prevent missile lock" — matches its raw
    // CLOK duration field (5) exactly, so no override needed here (an
    // earlier hardcoded 3.0f override, based on an initial "every 3
    // seconds" report, was removed once the confirmed value turned out to
    // match the file after all).
    float duration = (float)this->object->entity->clok->duration;
    if (duration <= 0.0f) {
        return;
    }
    this->cloak_timer += dt;
    float period = duration * 2.0f;
    float phase = fmodf(this->cloak_timer, period);
    this->plane->cloaked = (phase < duration);
    // Manual, 2026-07 session: enemy ships fade to fully transparent on
    // cloak, back to fully opaque on decloak (SCStrike's own per-actor
    // render loop reads this to drive Renderer.modelAlphaMultiplier — see
    // its own comment). The player's own cloak_factor ramp is handled
    // separately by WC3Strike::updateCloakEffect() (drives a greyscale
    // screen fade instead of transparency) — skip it here to avoid two
    // separate ramps fighting over the same field at different rates.
    if (this->actor_name != "PLAYER") {
        float rampSpeed = 2.0f;
        if (this->plane->cloaked) {
            this->plane->cloak_factor = (std::min)(1.0f, this->plane->cloak_factor + rampSpeed * dt);
        } else {
            this->plane->cloak_factor = (std::max)(0.0f, this->plane->cloak_factor - rampSpeed * dt);
        }
    }
}
// Manual, 2026-07 session: "shields ... calibrated to regenerate totally
// in 10 seconds ... this regeneration is divided among all sides with
// damaged shields ... until at least one of the damaged sides was back
// to full." shield_regen_rate is the ship's full (undivided) per-second
// rate — recomputing damagedCount fresh every tick naturally gives an
// undamaged/just-topped-up side its slot back on the very next tick,
// matching "until at least one ... was back to full" without needing to
// track which sides were damaged across calls.
void SCMissionActors::UpdateShieldRegen(float dt) {
    if (this->shield_regen_rate <= 0.0f) {
        return;
    }
    bool frontDamaged = this->shield_front < this->max_shield_front;
    bool backDamaged = this->shield_back < this->max_shield_back;
    bool leftDamaged = this->shield_left < this->max_shield_left;
    bool rightDamaged = this->shield_right < this->max_shield_right;
    int damagedCount = (int)frontDamaged + (int)backDamaged + (int)leftDamaged + (int)rightDamaged;
    if (damagedCount == 0) {
        return;
    }
    float perSideRegen = (this->shield_regen_rate / (float)damagedCount) * dt;
    if (frontDamaged) {
        this->shield_front = (std::min)(this->max_shield_front, this->shield_front + perSideRegen);
    }
    if (backDamaged) {
        this->shield_back = (std::min)(this->max_shield_back, this->shield_back + perSideRegen);
    }
    if (leftDamaged) {
        this->shield_left = (std::min)(this->max_shield_left, this->shield_left + perSideRegen);
    }
    if (rightDamaged) {
        this->shield_right = (std::min)(this->max_shield_right, this->shield_right + perSideRegen);
    }
}
void SCMissionActors::hasBeenHit(SCSimulatedObject *weapon, SCMissionActors *attacker) {
    if (this->object->alive == false) {
        return;
    }
    // Was `this->health = 0;` unconditionally — any single hit (even a
    // stray 20mm round) instantly destroyed the target regardless of its
    // actual max health, so there was never any gradual shield/hull
    // depletion to show on a gauge. WDAT::damage (RSEntity.h) is the real
    // per-hit damage value, already parsed from the weapon's own IFF data
    // (RSEntity.cpp's WDAT parser) — use it instead.
    int damage = (weapon != nullptr && weapon->obj != nullptr && weapon->obj->wdat != nullptr)
        ? weapon->obj->wdat->damage
        : 10;
    this->ApplyDamage(damage, attacker, weapon);
}
void SCMissionActors::ApplyDamage(int damage, SCMissionActors *attacker, SCSimulatedObject *weapon) {
    if (this->object->alive == false) {
        return;
    }
    // Per-quadrant shield/armor (see SCMissionActors.h) gate the flat
    // `health` scalar below instead of running in parallel with it — per
    // the WC3 manual's damage model: "damage does not begin to accrue to
    // armor until that side's shields are completely down, and the ship
    // itself doesn't start to take damage until all the armor on a side
    // is gone, and the shield is completely down." Shield absorbs first,
    // then armor, and only whatever's left over after both are exhausted
    // on this hit's facing reaches `health`. Entities with no SHLD chunk
    // have max_shield_*/max_armor_* (and so shield_*/armor_*) stuck at 0,
    // so this degrades to "every hit goes straight to health" for them —
    // the same behavior as before this change, just reached explicitly
    // instead of via the old unconditional `health -= damage`.
    Vector3D attackerPos = this->object->position;
    if (attacker != nullptr && attacker->object != nullptr) {
        attackerPos = attacker->object->position;
    } else if (weapon != nullptr) {
        attackerPos = {weapon->x, weapon->y, weapon->z};
    }
    HitQuadrant quadrant = ClassifyHitQuadrant(this, attackerPos);
    float *facingShield = &this->shield_front;
    float *facingArmor = &this->armor_front;
    switch (quadrant) {
        case HitQuadrant::Front: facingShield = &this->shield_front; facingArmor = &this->armor_front; break;
        case HitQuadrant::Back:  facingShield = &this->shield_back;  facingArmor = &this->armor_back;  break;
        case HitQuadrant::Left:  facingShield = &this->shield_left;  facingArmor = &this->armor_left;  break;
        case HitQuadrant::Right: facingShield = &this->shield_right; facingArmor = &this->armor_right; break;
    }
    float remainingDamage = (float)damage;
    if (*facingShield > 0.0f) {
        float absorbed = (std::min)(*facingShield, remainingDamage);
        *facingShield -= absorbed;
        remainingDamage -= absorbed;
        // See shield_hit_flash_timer's own comment (SCMissionActors.h).
        this->shield_hit_flash_timer = 0.3f;
    }
    if (remainingDamage > 0.0f && *facingArmor > 0.0f) {
        float absorbed = (std::min)(*facingArmor, remainingDamage);
        *facingArmor -= absorbed;
        remainingDamage -= absorbed;
    }
    if (remainingDamage > 0.0f) {
        this->health -= (int)remainingDamage;
        // Only a genuine hull hit (shield+armor both already exhausted on
        // this facing) gives internal components a chance to take damage —
        // matches the manual's "each time a ship takes a hit that gets
        // through armor and shields and damages the ship itself, there is
        // a chance that some internal components will be damaged."
        this->RollComponentDamage(quadrant, (int)remainingDamage);
    }
    if (this->health > 0) {
        return;
    }
    this->health = 0;
    if (this->profile != nullptr && this->profile->radi.msgs.size() > 0) {
        std::srand(std::time(0));
        int r = std::rand() % 16;
        if (r<=this->profile->ai.atrb.VB) {
            int message = std::rand() % 4;
            this->setMessage(23 + message); // Bail out messages
        }
    }
    this->object->alive = false;
    // Death scream, RADI code 0x0A. WC3's own profiles also carry a 0x39
    // low-morale variant of the same scream (see PROFILE\SOUND's RADI code
    // table), but nothing in this engine tracks wingman morale yet — see
    // todo.MD — so this always plays the high-morale one.
    if (this->profile != nullptr) {
        this->setMessage(0x0A);
    }
    if (!this->has_exploded && this->object->entity->explos != nullptr) {
        this->has_exploded = true;
        // Capital ships get a 12x-scale explosion billboard (user-confirmed
        // 2026-07 session, raised from an initial 4x) — the default 50.0f
        // fighter scale reads as a barely-visible dot against a ship
        // hundreds of units long.
        std::string upperName = this->object->member_name;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
        float explosionScale = SCMissionActors::IsCapitalShipName(upperName) ? 600.0f : 50.0f;
        // Large capital ships (dreadnought/carriers/Victory/starbase/
        // Behemoth) get the staged death sequence — see bigDeathSequence's
        // own comment (SCExplosion.h). The death radio message itself is
        // already handled above (setMessage(0x0A)) for any profiled actor,
        // capital ship or not — nothing extra needed here for that part.
        bool bigDeathSequence = SCMissionActors::IsLargeCapitalShipName(upperName);
        SCExplosion *explosion = new SCExplosion(this->object->entity->explos->objct, this->object->position, explosionScale, bigDeathSequence);
        this->mission->explosions.push_back(explosion);
        if (this->mission->sound.sounds.size() > 0) {
            // weapon/weapon->obj can be null here (see the damage calc
            // above, which already guards the same pointers) — a target
            // can die from other causes routed through this same death
            // branch, not just a gun/missile hit with a live weapon object.
            MemSound *sound;
            if (weapon != nullptr && weapon->obj != nullptr && weapon->obj->entity_type == EntityType::tracer) {
                sound = this->mission->sound.sounds[SoundEffectIds::GUN_IMPACT_1];
            } else {
                sound = this->mission->sound.sounds[SoundEffectIds::EXPLOSION_1];
            }
            RSMixer::getInstance().playSoundVoc(sound->data, sound->size);
        }
    }
    // Wreckage pieces (RSEntity::debris, OBJT>...>DEBR) — one SCDebris per
    // defined piece, spawned at the ship's own position offset by the
    // piece's own authored offset, scattering with a random velocity
    // within the piece's own authored range (space combat — no gravity to
    // pull them down). Held at the ship's death-moment orientation; no
    // tumble/spin simulated yet.
    Vector3D deathOrientation = {
        (360.0f - (float)this->object->azymuth + 90.0f),
        (float)this->object->pitch,
        -(float)this->object->roll
    };
    for (auto piece : this->object->entity->debris) {
        if (piece == nullptr || piece->objct == nullptr) {
            continue;
        }
        Vector3D piecePos = {
            this->object->position.x + (float)piece->offset_x,
            this->object->position.y + (float)piece->offset_y,
            this->object->position.z + (float)piece->offset_z
        };
        auto randomInRange = [](int16_t range) -> float {
            if (range == 0) return 0.0f;
            return ((float)(std::rand() % 2001 - 1000) / 1000.0f) * (float)range;
        };
        Vector3D pieceVel = {
            randomInRange(piece->velocity_range_x),
            randomInRange(piece->velocity_range_y),
            randomInRange(piece->velocity_range_z)
        };
        this->mission->debris.push_back(new SCDebris(piece->objct, piecePos, pieceVel, deathOrientation));
    }
    attacker->score += 100;
    if (this->plane != nullptr) {
        attacker->plane_down += 1;
    } else {
        attacker->ground_down += 1;
    }
}
int SCMissionActors::GetCollisionDamage() {
    float shieldArmor = this->max_shield_front + this->max_armor_front;
    if (shieldArmor > 0.0f) {
        return (int)(shieldArmor * 0.9f);
    }
    // Fallback for actors with no SHLD chunk at all (max_shield_front and
    // max_armor_front both stay 0) — same "every hit goes straight to
    // health" degradation ApplyDamage's own comment describes, so base
    // it on this actor's own max hull health instead of a hardcoded
    // constant.
    if (this->object != nullptr && this->object->entity != nullptr) {
        return (int)((float)this->object->entity->health * 0.9f);
    }
    return 50;
}
/**
 * SCMissionActorsPlayer::takeOff
 *
 * Sets the current objective to a take-off objective with the given
 * argument as the target spot ID.
 *
 * @param arg The ID of the target spot to take off from.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
// Bug fix (2026-07 session, live-tested after the PART stride fix landed:
// nav points went invisible again). takeOff/land/flyToArea's `arg` is
// 1-based (spot #1..#N), not a 0-based mission_data.spots array index —
// confirmed by scanning every real PROG opcode 161/162 argument across
// the whole shipped campaign (84 files) against its own file's real SPOT
// count: every single "out of range" case found (MISNA002 arg=5/5 spots,
// MISNP000 arg=4/4 spots, MISNK004 & MISNLG4A arg=1/1 spot) has arg
// exactly equal to the spot count — the unambiguous signature of a
// 1-based "last spot in the list" reference being misread as a 0-based
// index one past the end, not a validly-out-of-range value. Direct
// `spots[arg]` indexing (no bounds check at all before this fix) was
// undefined behavior on every one of those real files; it was never
// actually reached before the stride fix (progs_id routing was broken,
// so the player's real on_missions_init/on_mission_update script never
// ran at all), so this is newly exposed, not newly introduced.
bool SCMissionActorsPlayer::takeOff(uint8_t arg) {
    if (arg == 0 || arg > this->mission->mission->mission_data.spots.size()) {
        return false;
    }
    SCMissionWaypoint *waypoint = new SCMissionWaypoint();
    waypoint->spot = this->mission->mission->mission_data.spots[arg - 1];
    waypoint->objective = new std::string("take off");
    this->mission->waypoints.push_back(waypoint);
    return true;
}
/**
 * SCMissionActorsPlayer::land
 *
 * Sets the current objective to a land objective with the given
 * argument as the target spot ID.
 *
 * @param arg The ID of the target spot to land on.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
// See takeOff's own comment — same 1-based spot-index fix.
bool SCMissionActorsPlayer::land(uint8_t arg) {
    if (arg == 0 || arg > this->mission->mission->mission_data.spots.size()) {
        return false;
    }
    SCMissionWaypoint *waypoint = new SCMissionWaypoint();
    waypoint->spot = this->mission->mission->mission_data.spots[arg - 1];
    waypoint->objective = new std::string("landing");
    this->mission->waypoints.push_back(waypoint);
    return true;
}
/**
 * Sets the current objective to a fly-to-waypoint objective with the given
 * argument as the target waypoint ID.
 *
 * @param arg The ID of the target waypoint to fly to.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
bool SCMissionActorsPlayer::flyToWaypoint(uint8_t arg) {
    SCMissionWaypoint *waypoint = new SCMissionWaypoint();
    if (arg >= this->mission->mission->mission_data.spots.size()) {
        return false; // Invalid waypoint ID
    }
    waypoint->spot = this->mission->mission->mission_data.spots[arg];
    waypoint->objective = new std::string("Fly to\nWay Point");
    this->mission->waypoints.push_back(waypoint);
    return true;
}
/**
 * SCMissionActorsPlayer::flyToArea
 *
 * Sets the current objective to a fly-to-waypoint objective with the given
 * argument as the target waypoint ID.
 *
 * @param arg The ID of the target waypoint to fly to.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
bool SCMissionActorsPlayer::flyToArea(uint8_t arg) {
    // Unlike takeOff/land (see that comment — confirmed 1-based), this
    // opcode's arg is genuinely 0-based: scanned all 30 real
    // OP_SET_OBJ_FLY_TO_AREA uses across the campaign, 6 of them use
    // arg=0 (would be a wasted no-op opcode under a "0=invalid" 1-based
    // scheme) and zero are out of range under plain 0-based indexing.
    // Only adding the bounds check this file's Player overrides were
    // otherwise universally missing, not an index-base change.
    if (arg >= this->mission->mission->mission_data.spots.size()) {
        return false;
    }
    SCMissionWaypoint *waypoint = new SCMissionWaypoint();
    waypoint->spot = this->mission->mission->mission_data.spots[arg];
    waypoint->objective = new std::string("Fly to\nWay Area");
    this->mission->waypoints.push_back(waypoint);
    return true;
}
/**
 * Sets the current objective to a destroy-target objective with the given
 * argument as the target object ID.
 *
 * @param arg The ID of the target object to destroy.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
bool SCMissionActorsPlayer::destroyTarget(uint8_t arg) {
    // Bounds-check only (see takeOff's comment for the missing-check
    // history) — NOT applying the 1-based fix here. This opcode's own doc
    // comment says arg is "the target OBJECT to destroy", and real traces
    // (e.g. MISNA002: destroyTarget args 1,2,3,0) include 0, ruling out
    // a 1-based scheme the same way flyToArea's did. Whether arg is
    // actually meant to index spots at all (vs. an actor/CAST id, like
    // the base SCMissionActors::destroyTarget's AI version resolves
    // against this->mission->actors, not spots) is a separate, deeper
    // question not chased down here — this only prevents the
    // out-of-bounds read.
    if (arg >= this->mission->mission->mission_data.spots.size()) {
        return false;
    }
    SCMissionWaypoint *waypoint = new SCMissionWaypoint();
    waypoint->spot = this->mission->mission->mission_data.spots[arg];
    waypoint->objective = new std::string("Destroy\nTarget");
    this->mission->waypoints.push_back(waypoint);
    return true;
}
/**
 * SCMissionActorsPlayer::defendTarget
 *
 * Sets the current objective to a defend-target objective with the given
 * argument as the target object ID.
 *
 * @param arg The ID of the target object to defend.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
bool SCMissionActorsPlayer::defendTarget(uint8_t arg) {
    // Bounds-check only — see destroyTarget's own comment, same reasoning
    // (also "target ally", plausibly an actor id, not chased further).
    if (arg >= this->mission->mission->mission_data.spots.size()) {
        return false;
    }
    SCMissionWaypoint *waypoint = new SCMissionWaypoint();
    waypoint->spot = this->mission->mission->mission_data.spots[arg];
    waypoint->objective = new std::string("Defend\nAlly");
    this->mission->waypoints.push_back(waypoint);
    return true;
}

/**
 * SCMissionActorsPlayer::setMessage
 *
 * Sets the current objective to display a message with the given
 * argument as the message ID.
 *
 * @param arg The ID of the message to display.
 *
 * @return True if the objective was set successfully, false otherwise.
 */
bool SCMissionActorsPlayer::setMessage(uint8_t arg) {
    if (arg >= this->mission->mission->mission_data.messages.size()) {
        return true;
    }
    std::transform(this->mission->mission->mission_data.messages[arg]->begin(), this->mission->mission->mission_data.messages[arg]->end(), this->mission->mission->mission_data.messages[arg]->begin(), ::tolower);
    if (this->mission->waypoints.size() > 0) {
        this->mission->waypoints.back()->message = this->mission->mission->mission_data.messages[arg];
    }
    return true;
}

void SCMissionActorsPlayer::hasBeenHit(SCSimulatedObject *weapon, SCMissionActors *attacker) {
    // Was a no-op stub ("the player cannot be hit"). The base class already
    // does the right thing generically (health=0, object->alive=false once,
    // guarded against re-triggering — see its own `if (object->alive ==
    // false) return;`  — radio scream, score/kill-count for the attacker,
    // explosion+sound): reuse it instead of special-casing the player.
    // WC3Strike::checkDeathTrigger() polls object->alive each frame and
    // plays RSCockpit::deathFrames (the same overlay-compositing path as
    // the Ctrl+E eject sequence) once it goes false.
    SCMissionActors::hasBeenHit(weapon, attacker);
}

void SCMissionActorsPlayer::UpdatePlanePowerFractions() {
    if (this->plane == nullptr) {
        return;
    }
    this->plane->engine_power_fraction =
        1.0f - this->component_damage[(size_t)ShipComponent::Engine] / 100.0f;
    this->plane->gun_power_fraction =
        1.0f - this->component_damage[(size_t)ShipComponent::PowerPlant] / 100.0f;
}

// The real weighted-random logic (manual: "Each time a ship takes a hit
// that gets through armor and shields... there is a chance that some
// internal components will be damaged...") now lives in the base
// SCMissionActors::RollComponentDamage (see its own comment for why —
// every actor needs this now, not just the player). This override just
// adds the player/cockpit-HUD-only concern on top: pushing the result
// out to this->plane's engine/gun power-fraction gauges.
void SCMissionActorsPlayer::RollComponentDamage(HitQuadrant quadrant, int overflowDamage) {
    SCMissionActors::RollComponentDamage(quadrant, overflowDamage);
    this->UpdatePlanePowerFractions();
}

// Manual: "Ships' repair systems will automatically repair most
// components, except those mounted in front (VDU 1, VDU 2 and the
// tactical display), which cannot be repaired. Also, any component
// that's completely destroyed (has taken a full 100 points of damage)
// cannot be repaired." A destroyed AutoRepair component can't repair
// anything, including itself (it's just another component subject to the
// same "100 = permanently gone" rule).
void SCMissionActorsPlayer::TickComponentRepair() {
    if (this->component_damage[(size_t)ShipComponent::AutoRepair] >= 100.0f) {
        return;
    }
    // Power Plant damage "lowers available power to ... damage repair" —
    // scale the repair rate down the same way GetCurrentGunEnergy() scales
    // gun recharge (see gun_power_fraction's own comment in SCPlane.h).
    float powerPlantFraction =
        1.0f - this->component_damage[(size_t)ShipComponent::PowerPlant] / 100.0f;
    // Rate not given by the manual — chosen so a fully-damaged repairable
    // component (100 points) heals in ~30s at a typical ~60 ticks/sec,
    // not independently confirmed against real WC3 timing.
    constexpr float kRepairPointsPerTick = 0.05f;
    float repairAmount = kRepairPointsPerTick * powerPlantFraction;
    if (repairAmount <= 0.0f) {
        return;
    }
    bool changed = false;
    for (size_t i = 0; i < (size_t)ShipComponent::Count; i++) {
        ShipComponent component = (ShipComponent)i;
        if (!SCMissionActors::IsComponentRepairable(component)) {
            continue;
        }
        float &dmg = this->component_damage[i];
        if (dmg <= 0.0f || dmg >= 100.0f) {
            continue;
        }
        dmg -= repairAmount;
        if (dmg < 0.0f) {
            dmg = 0.0f;
        }
        changed = true;
    }
    if (changed) {
        this->UpdatePlanePowerFractions();
    }
}

bool SCMissionActorsStrikeBase::setMessage(uint8_t arg) {
    RadioMessages *msg = new RadioMessages();
    msg->message = this->profile->radi.msgs[arg];
    if (this->mission->sound.inGameVoices.size() > 0) {
        if (this->mission->sound.inGameVoices.find(this->profile->radi.spch) == this->mission->sound.inGameVoices.end()) {
            printf("No voice found for %d\n", this->profile->radi.spch);
        }
        if (this->mission->sound.inGameVoices[this->profile->radi.spch]->messages.find(arg) == this->mission->sound.inGameVoices[this->profile->radi.spch]->messages.end()) {
            printf("No message found for %d\n", arg);
        }
        MemSound *message_sound = this->mission->sound.inGameVoices[this->profile->radi.spch]->messages[arg];
        msg->sound = message_sound;
    }
    // Same WC3 SOND fallback as SCMissionActors::setMessage above (capital
    // ships like the Victory carry their own MISSIONS.TRE-embedded profile
    // too).
    if (msg->sound == nullptr && this->profile->radi.sond.count(arg) > 0) {
        const std::vector<uint8_t> *wav = this->profile->getRadiSoundWav(arg);
        if (wav != nullptr) {
            MemSound *sound_msg = new MemSound();
            sound_msg->data = const_cast<uint8_t*>(wav->data());
            sound_msg->size = wav->size();
            sound_msg->id = arg;
            msg->sound = sound_msg;
        }
    }
    // Same RADI_FMV lookup as SCMissionActors::setMessage above.
    for (auto &fmv : this->profile->radi.fmv) {
        if (fmv.index == arg) {
            msg->fmvFilename = fmv.filename;
            break;
        }
    }
    this->mission->radio_messages.push_back(msg);
    if (arg == 20) {
        this->mission->mission_over = true;
        this->mission->mission_won = true;
    } else if (arg == 21) {
        this->mission->mission_over = true;
        this->mission->mission_won = false;
    } else if (arg == 22) {
        this->mission->mission_over = true;
        this->mission->mission_won = false;
    }
    return true;
}
/*
@TODO
Fly to#Precise Way
Defend#Point
Follow#Leader
*/