#include "WC3Mission.h"
#include "../realspace/XtreArchive.h"

// PART slots that are genuinely unused padding (rather than a real named
// object) tend to hold either an empty member_name or raw leftover/garbage
// bytes (high-bit/control characters) instead of a real uppercased
// identifier like "HELLCATP" or "VICTORY". A real member_name is always
// printable ASCII (letters/digits/punctuation) end to end.
static bool isPlausibleMemberName(const std::string& name) {
    if (name.empty()) return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

WC3Mission::WC3Mission() {
    this->last_time = SDL_GetTicks();
    this->last_tick = 0;
    this->tick_counter = 0;
    this->tps = 0;
}

WC3Mission::WC3Mission(std::string mission_name, std::unordered_map<std::string, RSEntity*>* objCache) {
    this->mission_name = mission_name;
    this->obj_cache = objCache;
    this->last_time = SDL_GetTicks();
    this->last_tick = 0;
    this->tick_counter = 0;
    this->tps = 0;
    this->loadMission();
}

WC3Mission::~WC3Mission() {}

RSProf* WC3Mission::LoadProfile(std::string name) {
    RSProf* profile = new RSProf();
    std::string filename = Assets.profile_root_path + name + ".IFF";
    TreEntry* profile_tre = Assets.GetEntryByName(filename);
    bool owned = false;
    if (profile_tre == nullptr) {
        // Not every profile is in the archives preloaded at startup — e.g.
        // the TCS Victory's own profile (PROFILE\VICA1.IFF, holding its
        // landing-clearance/mission-outcome radio lines) lives in
        // CD1MISS.TRE, not missions.tre. Same on-demand CD-archive fallback
        // pattern as WC3GameFlow::playMovie()/loadBranchPaks().
        static const char* cdTres[] = {
            "cd1miss.tre", "cd2miss.tre", "cd3miss.tre", "cd4miss.tre", nullptr
        };
        for (const char** tre = cdTres; *tre && profile_tre == nullptr; tre++) {
            profile_tre = XtreArchive::LoadSingleEntry(*tre, filename.c_str());
            owned = profile_tre != nullptr;
        }
    }
    if (profile_tre != nullptr) {
        profile->InitFromRAM(profile_tre->data, profile_tre->size);
        if (owned) {
            delete[] profile_tre->data;
            delete profile_tre;
        }
    } else {
        printf("WC3Mission: Unable to load profile %s\n", name.c_str());
        delete profile;
        return nullptr;
    }
    return profile;
}

void WC3Mission::loadMission() {
    std::string miss_file_name = Assets.mission_root_path + this->mission_name;
    std::transform(miss_file_name.begin(), miss_file_name.end(), miss_file_name.begin(), ::toupper);
    TreEntry* mission_tre = Assets.GetEntryByName(miss_file_name.c_str());
    bool mission_tre_owned = false;
    if (mission_tre == nullptr) {
        // Not every mission is in missions.tre (preloaded at startup) — later
        // campaign missions live on later CDs' own cd*miss.tre instead. Same
        // on-demand CD-archive fallback pattern as LoadProfile() above.
        static const char* cdTres[] = {
            "cd1miss.tre", "cd2miss.tre", "cd3miss.tre", "cd4miss.tre", nullptr
        };
        for (const char** tre = cdTres; *tre && mission_tre == nullptr; tre++) {
            mission_tre = XtreArchive::LoadSingleEntry(*tre, miss_file_name.c_str());
            mission_tre_owned = mission_tre != nullptr;
        }
    }
    if (mission_tre == nullptr) {
        printf("WC3Mission: Could not find mission '%s'\n", miss_file_name.c_str());
        return;
    }

    this->mission = new RSMission();
    this->mission->InitFromRAM(mission_tre->data, mission_tre->size);
    if (mission_tre_owned) {
        delete[] mission_tre->data;
        delete mission_tre;
    }

    // Determine if this is a space or ground mission.
    // "world" (lowercase, WRLD/FILE's literal value in most missions) is NOT
    // a ground-terrain reference — WORLD.IFF is a space-scene environment
    // definition (FORM WRLD: HORZ/DUST/STAR/SKYS/LGHT/CAMR — starfield, dust,
    // galaxy skybox layers, camera modes; nothing terrain-shaped, and not
    // the RSArea/ZIP terrain format the ground-mission branch below expects
    // at all). Confirmed empirically: only ~1/3 of missions (the later ones)
    // reference a distinct, real terrain world name (e.g. "wrldd1", "wrldk1")
    // — matching most missions being pure space combat with a handful of
    // planetary/ground missions later in the campaign.
    std::string worldNameUpper = this->mission->mission_data.world_filename;
    std::transform(worldNameUpper.begin(), worldNameUpper.end(), worldNameUpper.begin(), ::toupper);
    std::string worldName = this->mission->mission_data.world_filename;
    if (worldNameUpper.empty() || worldNameUpper == "SPACE" || worldNameUpper == "WORLD") {
        this->is_space_mission = true;
        // "" and "SPACE" aren't real filenames — only WORLD.IFF actually
        // exists in MISSIONS.TRE (confirmed via the xtre catalog listing;
        // there's no SPACE.IFF at all). Left as the raw (possibly empty)
        // world_filename, the lookup below built a bogus path like
        // "..\..\DATA\MISSIONS\.IFF", silently found nothing (no error —
        // see the TreEntry null-check below), and left this->world's
        // skyFaces/starSprites permanently empty — the actual cause of
        // "no galaxy/star background" for the ~2/3 of missions that rely
        // on this shared default instead of naming their own world.
        worldName = "WORLD";
    } else {
        this->is_space_mission = false;
    }

    // WRLD is loaded for every mission, not just ground ones — space
    // missions need it too, for the SKYS skybox (galaxy backdrop billboards)
    // and STAR (twinkling-star glint sprites) chunks it also carries. Only
    // the terrain (RSArea/ZIP, via the WRLD's own TERA chunk) is ground-only.
    {
        std::string area_filename = Assets.mission_root_path + worldName + ".IFF";
        std::transform(area_filename.begin(), area_filename.end(), area_filename.begin(), ::toupper);
        this->world = new RSWorld();
        TreEntry* treEntry = Assets.GetEntryByName(area_filename.c_str());
        if (treEntry == nullptr) {
            printf("WC3Mission: world file %s not found — no skybox/starfield for this mission\n", area_filename.c_str());
        }
        if (treEntry != nullptr) {
            this->world->InitFromRAM(treEntry->data, treEntry->size);

            for (auto& face : this->world->skyFaces) {
                // SKYS names are stored lowercase ("darkgal1"), but
                // SCMission::LoadEntity does an exact-case archive lookup
                // (matches DATA\OBJECTS\DARKGAL1.IFF) — uppercase first.
                std::string upperName = face.name;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                RSEntity* skyEntity = this->LoadEntity(upperName);
                this->skyEntities.push_back(skyEntity);
            }

            // Crash fix + sky/ground fix (2026-07 session, live-tested via
            // TSIM001): the is_space_mission heuristic above only recognizes
            // "", "SPACE", and "WORLD" as non-terrain world names —
            // WRLDSIM.IFF (the training/simulator missions' own world file)
            // doesn't match any of those, so is_space_mission came out false
            // here even though it's a pure space scene with no TERA chunk.
            // Two symptoms from that one wrong flag: (1) it sent an empty
            // this->world->tera into the terrain path below, building a
            // bogus "..\..\DATA\TERRAIN\.ZIP", failing to find it, and
            // leaving this->area newly-constructed but never actually
            // populated with block/vertex data — which segfaulted in
            // SCRenderer::computeBlockAABB (iterating vertices that don't
            // exist); (2) WC3Strike::launchMission reads is_space_mission
            // straight off this WC3Mission to decide renderer.show_ground/
            // show_starfield, so the same wrong flag drew the atmospheric
            // sky/ground gradient instead of the starfield for every
            // simulator mission. Now that the world (and its TERA chunk, if
            // any) is actually loaded, `tera` itself is the authoritative
            // signal for "does this mission have real terrain" — more
            // robust than trying to enumerate every possible non-terrain
            // world filename (WRLDSIM/WRLDSIMA/WRLDSIMG, and potentially
            // others) in the name-based heuristic above, so it overrides
            // that heuristic's guess here.
            this->is_space_mission = this->world->tera.empty();
            if (!this->is_space_mission) {
                std::string area_fn = "..\\..\\DATA\\TERRAIN\\" + this->world->tera + ".ZIP";
                std::transform(area_fn.begin(), area_fn.end(), area_fn.begin(), ::toupper);
                this->area = new RSArea();
                this->area->InitFromZipFileName(area_fn);
                Renderer.InvalidateAABBCache();
                Renderer.PrecomputeAABBs(this->area, 0, 1);
            }
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
    for (auto part : mission->mission_data.parts) {
        if (part->entity == nullptr) {
            part->entity = LoadEntity(part->member_name);
        }

        // PART positions are stored relative to their own area's origin
        // (e.g. the Victory's own PART record is literally (0,0,0) — the
        // *area* named "VICTORY" is what actually carries the real world
        // position). This correction used to only be applied to the PLAYER
        // part, leaving every other area-anchored actor (the Victory,
        // stationary capital ships, etc.) rendered at the wrong absolute
        // position — effectively invisible, since it put them nowhere near
        // where the player actually spawns. Apply it to every part.
        if (part->area_id != 255 && part->unknown2 == 0 &&
            part->area_id < this->mission->mission_data.areas.size()) {
            Vector3D correction = this->mission->mission_data.areas[part->area_id]->position;
            part->position.x += correction.x;
            part->position.y += correction.y;
            part->position.z += correction.z;
            part->area_corrected = true;
        }

        int search_id = 0;
        for (auto cast : mission->mission_data.casting) {
            // Many PART slots share id==0 with the real player/first-cast
            // entry but are actually unused padding (empty or garbage-byte
            // member_name, no real object) — without this check, every one
            // of them false-matches search_id==0 and gets built as a bogus
            // duplicate "PLAYER" actor.
            if (part->id == search_id && isPlausibleMemberName(part->member_name)) {
                this->buildActorFromPart(part, cast);
                break;
            }
            search_id++;
        }
    }

    // Some cast members are never given their own PART position record —
    // WC3 instead spawns/activates them purely via a SCNE ("scene") entry,
    // whose own embedded position (see RSMission::parseMISN_PLAY_SCEN) is
    // the only place their spawn location is stored. misna001's
    // TDESTROY/TCRUISER/HOBBES are exactly this case: absent from every one
    // of the 24 PART records (confirmed via raw hex dump), but present in
    // scene 0's/scene 1's cast lists once those are decoded correctly.
    // Build a synthetic PART for any cast index a scene references that
    // wasn't already constructed by the loop above.
    for (auto scene : this->mission->mission_data.scenes) {
        // A scene carries exactly one shared (x,z) anchor for its whole cast
        // list (see RSMission::parseMISN_PLAY_SCEN) — there's no per-member
        // position, unlike PART. Left unspread, every cast member built here
        // from the same scene lands exactly on top of every other one (e.g.
        // misna001's TDESTROY and TCRUISER, confirmed via live diagnostic
        // output to both resolve to the identical coordinate), which reads
        // as "nothing is there" rather than two overlapping ships. Fan
        // successive members out laterally from the anchor so they're at
        // least visually distinct until their own AI/formation logic (once
        // implemented) takes over.
        int spawnIndex = 0;
        for (auto cast_index : scene->cast) {
            if (cast_index < 0 || (size_t)cast_index >= this->mission->mission_data.casting.size()) {
                continue;
            }
            bool alreadyBuilt = false;
            for (auto actor : this->actors) {
                if (actor->actor_id == cast_index) {
                    alreadyBuilt = true;
                    break;
                }
            }
            if (alreadyBuilt) {
                continue;
            }
            CAST* cast = this->mission->mission_data.casting[cast_index];
            float spreadOffset = 300.0f * (float)spawnIndex;
            int fanIndex = spawnIndex;
            spawnIndex++;

            MISN_PART* part = new MISN_PART();
            part->id = (uint16_t)cast_index;
            part->area_id = (scene->area_id >= 0) ? (uint8_t)scene->area_id : 255;
            part->unknown2 = 0;
            part->unknown_bytes.assign(11, 0);

            if (scene->area_id == -1 && scene->spawn_position.x == 0 && scene->spawn_position.z == 0 &&
                this->player != nullptr) {
                // No fixed position, and always-active (area_id == -1) —
                // spawns docked alongside the player (confirmed live: Hobbes
                // starts next to the player in the hangar).
                part->position = this->player->object->position;
                part->position.x += 40.0f + spreadOffset;
            } else {
                // The scene's own embedded (x,z) anchor is a single shared
                // point for the whole cast list (not a real per-ship
                // position — see the comment above), and it's small/close
                // to the area origin, so the whole fleet ended up clustered
                // right on top of the carrier instead of arrayed around it
                // as a separate formation. Fan successive members out at a
                // fixed 16000-unit radius from the area's own origin,
                // 90 degrees apart, instead of using that anchor at all.
                const float kFleetFanRadius = 16000.0f;
                float fanAngleRad = (float)fanIndex * ((float)M_PI / 2.0f);
                Vector3D center{0, 0, 0};
                if (scene->area_id >= 0 && (size_t)scene->area_id < this->mission->mission_data.areas.size()) {
                    center = this->mission->mission_data.areas[scene->area_id]->position;
                }
                part->position.x = center.x + kFleetFanRadius * cosf(fanAngleRad);
                part->position.y = center.y;
                part->position.z = center.z + kFleetFanRadius * sinf(fanAngleRad);
            }
            // Position is fully resolved above (either copied from the
            // player or area-corrected right here) — stop
            // SCMission::update()'s scene-activation code from adding the
            // same area offset again the first time this scene activates.
            part->area_corrected = true;

            // Best-effort model filename: reuse a real PART's member_name if
            // it shares this cast entry's class (CAST::unknown0 groups
            // sister ships of the same type — e.g. TDESTROY/TDESTRO2 both
            // read 0x55 — see RSMission::parseMISN_CAST), else fall back to
            // the cast's own name (WC3 uses that literally for some ship
            // types, e.g. DARKET).
            std::string modelName = cast->actor;
            for (auto other : this->mission->mission_data.parts) {
                if (!isPlausibleMemberName(other->member_name)) continue;
                if (other->id >= this->mission->mission_data.casting.size()) continue;
                if (this->mission->mission_data.casting[other->id]->unknown0 == cast->unknown0) {
                    modelName = other->member_name;
                    break;
                }
            }
            part->member_name = modelName;
            part->entity = LoadEntity(modelName);
            if (part->entity == nullptr && modelName != "HELLCATP") {
                // Neither a class-donor nor the cast's own name resolved to
                // a real DATA\OBJECTS\ entry (confirmed against the
                // archive's resolved name list — e.g. HOBBES has no
                // dedicated ship model at all). Fall back to the player's
                // own fighter model, since WC3's early campaign has Hobbes
                // (and other wingmen without a distinct model) fly the same
                // Hellcat as the player.
                modelName = "HELLCATP";
                part->member_name = modelName;
                part->entity = LoadEntity(modelName);
            }

            this->mission->mission_data.parts.push_back(part);
            this->buildActorFromPart(part, cast);
        }
    }

    // Classify actors into friendlies and enemies. MISN>TEAM's entries are
    // themselves real MISN_PART ids (see actor->actor_id = part->id in
    // buildActorFromPart below) — not indices into the parts vector, which
    // is ordered by file position, not by id. Indexing parts[member] here
    // used to silently pick the wrong part (or read out of bounds once a
    // team id exceeded parts.size()) whenever a part's id didn't equal its
    // array position, misclassifying that friendly as an enemy (and vice
    // versa by omission) — reported as friendly capital ships/fighters
    // showing up as hostile contacts on the radar.
    std::unordered_set<SCMissionActors *> teamFriendly;
    for (auto member : this->mission->mission_data.team) {
        for (auto actor : this->actors) {
            if (actor->actor_id == member)
                teamFriendly.insert(actor);
        }
    }
    // MISN>TEAM only ever lists the player's fighter wing — capital ships
    // (VICTORY, TCRUISER/TDEST/TTRANS escorts, ...) never appear there even
    // when unambiguously friendly, so they need a fallback. Their own
    // unknown_bytes fields don't encode allegiance either (cross-checked
    // across every campaign mission's capital ships, 2026-07 session:
    // VICTORY itself shows both 0,0 and 255,255 in different missions), but
    // ship name reliably does — every Kilrathi-named capital ship in the
    // whole campaign is unconditionally hostile, every Terran one
    // unconditionally friendly. See SCMissionActors::IsKilrathiShipName.
    for (auto actor : this->actors) {
        bool isFriendly = teamFriendly.count(actor) > 0;
        if (!isFriendly && actor->object != nullptr) {
            std::string nameUpper = actor->object->member_name;
            std::transform(nameUpper.begin(), nameUpper.end(), nameUpper.begin(), ::toupper);
            if (SCMissionActors::IsCapitalShipName(nameUpper) && !SCMissionActors::IsKilrathiShipName(nameUpper)) {
                isFriendly = true;
            }
        }
        if (isFriendly) {
            this->friendlies.push_back(actor);
        } else {
            this->enemies.push_back(actor);
        }
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

SCMissionActors* WC3Mission::buildActorFromPart(MISN_PART* part, CAST* cast) {
    SCMissionActors* actor = new SCMissionActors();
    // WC3 renames the player's own casting slot on some missions — e.g.
    // "PLAYERD2" in the Locanda-3 "track down Flint" mission, presumably to
    // swap in that mission's own PLAYERD2.IFF radio profile instead of the
    // default PLAYER.IFF — rather than always reusing the literal "PLAYER".
    // Match by prefix so the player is still recognized as the player
    // regardless of which variant this mission names.
    bool isPlayerSlot = cast->actor.rfind("PLAYER", 0) == 0;
    if (isPlayerSlot) {
        actor = new SCMissionActorsPlayer();
    } else if (cast->actor == "STRIBASE") {
        actor = new SCMissionActorsStrikeBase();
    } else if (cast->actor.rfind("VIC", 0) == 0) {
        // WC3's analog of Strike Commander's fixed "STRIBASE" literal: every
        // mission names its own TCS Victory cast entry "VIC" + that
        // mission's own series/number (e.g. "VICA1" in misna001, "VICB1" in
        // misnb001, "VICD1" in misnd001, "VICL4" in misnl004) rather than
        // reusing one constant string — so match by prefix instead of by
        // exact value. SCMissionActorsStrikeBase::setMessage()'s
        // arg==20/21/22 handling (mission_over/mission_won) is presumably
        // how "returned to the Victory" ends the mission, same mechanism as
        // SC's own strike base.
        actor = new SCMissionActorsStrikeBase();
    }

    // unknown_bytes is only guaranteed 11 entries for PART records that came
    // straight off disk (RSMission::parseMISN_PART); synthetic PART records
    // built for scene-only actors (see loadMission()'s scene-fallback pass)
    // pre-fill it with zeros for exactly this reason.
    actor->team_id = part->unknown_bytes.size() > 2 ? part->unknown_bytes[2] : 0;
    // SCMissionActors::is_hidden defaults to true (SCMissionActors.h) and is
    // normally cleared by SCMission::loadMission()'s own scene-spawn logic
    // (SCMission.cpp) or by a scripted OP_ACTIVATE_OBJ
    // (SCMissionActors::activateTarget). WC3Mission has its own
    // loadMission() that never calls either — and traced opcode-by-opcode,
    // misna001's PLAYER/VICA1/TDESTRO2 initial scripts (16 ops each, run
    // twice: on_is_activated then on_mission_start) never contain
    // OP_ACTIVATE_OBJ, so nothing WC3-side was ever unhiding any actor: the
    // TCS Victory, escort ships, everyone stayed permanently invisible
    // (confirmed via live testing — only the cockpit and starfield
    // rendered). WC3's mission format simply doesn't use this SC1
    // hide/reveal convention, so every constructed actor starts visible
    // here.
    actor->is_hidden = false;
    // Normalized to the canonical "PLAYER" rather than the raw cast->actor
    // (which may be "PLAYERD2"/"PLAYERS"/etc.) so every downstream
    // actor_name=="PLAYER" comparison shared with Strike Commander's own
    // code (SCCockpit's comms-contact list, SCPlane's pilot checks,
    // SCMissionActors' is_active/attacker checks, ...) still recognizes this
    // as the player. The exact cast->actor name is still used as-is for
    // LoadProfile() below, so the right per-mission profile IFF still gets
    // loaded.
    actor->actor_name = isPlayerSlot ? "PLAYER" : cast->actor;
    actor->actor_id = part->id;
    actor->object = part;
    if (part->entity == nullptr) {
        actor->health = 50;
    } else {
        actor->health = part->entity->health;
        // Per-quadrant shield/armor, from SSHP>SHLD>FGTR|CPTL (see
        // RSEntity::SHLD_FX) — byte-verified against the WC3 manual's
        // printed per-ship stats (HELLCAT.IFF + Hellcat manual figures,
        // VICTORY.IFF + Destroyer/Cruiser/Light-Carrier manual figures).
        // Left null (no SHLD chunk) for simpler entities — e.g. ground
        // props — which just keep the flat `health` scalar above.
        if (part->entity->shield != nullptr) {
            auto *shld = part->entity->shield;
            actor->shield_regen_rate = (float)shld->regen_rate;
            actor->shield_front = actor->max_shield_front = (float)shld->front;
            actor->shield_back = actor->max_shield_back = (float)shld->back;
            actor->shield_left = actor->max_shield_left = (float)shld->left;
            actor->shield_right = actor->max_shield_right = (float)shld->right;
            actor->armor_front = actor->max_armor_front = (float)shld->armor_front;
            // armor_side_a/side_b's left-vs-right assignment is unconfirmed
            // (see SHLD_FX struct comment) — every ship checked so far has
            // side_a == side_b anyway, so this is a placeholder ordering.
            actor->armor_left = actor->max_armor_left = (float)shld->armor_side_a;
            actor->armor_back = actor->max_armor_back = (float)shld->armor_back;
            actor->armor_right = actor->max_armor_right = (float)shld->armor_side_b;
        }
    }
    actor->profile = this->LoadProfile(cast->actor);
    actor->mission = this;

    if (part->on_is_activated != 255 && this->mission->mission_data.prog.size() > part->on_is_activated) {
        for (auto op : *this->mission->mission_data.prog[part->on_is_activated])
            actor->on_is_activated.push_back(op);
    }
    if (part->on_is_destroyed != 255 && this->mission->mission_data.prog.size() > part->on_is_destroyed) {
        for (auto op : *this->mission->mission_data.prog[part->on_is_destroyed])
            actor->on_is_destroyed.push_back(op);
    }
    if (part->on_missions_init != 255 && this->mission->mission_data.prog.size() > part->on_missions_init) {
        for (auto op : *this->mission->mission_data.prog[part->on_missions_init])
            actor->on_mission_start.push_back(op);
    }
    if (part->on_mission_update != 255 && this->mission->mission_data.prog.size() > part->on_mission_update) {
        for (auto op : *this->mission->mission_data.prog[part->on_mission_update])
            actor->on_update.push_back(op);
    }

    if (actor->profile != nullptr && actor->profile->ai.isAI &&
        part->entity != nullptr && part->entity->jdyn != nullptr &&
        !isPlayerSlot) {
        if (actor->profile->ai.goal.size() > 0) {
            actor->pilot = new SCPilot();
            actor->pilot->actor = actor;
            BoudingBox* bb = part->entity->GetBoudingBpx();
            actor->plane = new SCJdynPlane(
                10.0f, -7.0f, 40.0f, 40.0f, 30.0f, 100.0f,
                part->entity->wing_area,
                (float)part->entity->weight_in_kg,
                (float)part->entity->jdyn->FUEL,
                (float)part->entity->thrust_in_newton,
                (bb->max.z - bb->min.z) / 2.0f,
                .93f, 120, this->area,
                part->position.x, part->position.y, part->position.z);
            actor->plane->no_gravity = this->is_space_mission;
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
    } else if (actor->profile != nullptr && isPlayerSlot &&
               part->entity != nullptr && part->entity->jdyn != nullptr) {
        BoudingBox* bb = part->entity->GetBoudingBpx();
        actor->plane = new SCJdynPlane(
            10.0f, -7.0f, 40.0f, 40.0f, 30.0f, 100.0f,
            part->entity->wing_area,
            (float)part->entity->weight_in_kg,
            (float)part->entity->jdyn->FUEL,
            (float)part->entity->thrust_in_newton,
            (bb->max.z - bb->min.z) / 2.0f,
            .93f, 120, this->area,
            part->position.x, part->position.y, part->position.z);
        actor->plane->no_gravity = this->is_space_mission;
        // yaw is in tenths-of-a-degree (matching the AI branch's identical
        // assignment above, and SCPlane's own azimuthf/tenthOfDegreeToRad
        // convention used for camera rotation) — a later duplicate
        // assignment here used to overwrite this with a RADIAN value
        // instead ((360-azymuth)*M_PI/180), which meant the player's facing
        // was always clamped to well under 1 degree of rotation regardless
        // of the mission's actual spawn heading. That's why the player ship
        // faced an arbitrary direction (e.g. into the hangar bay wall)
        // instead of matching the carrier's own alignment.
        actor->plane->yaw = (360 - part->azymuth) * 10.0f;
        actor->plane->simple_simulation = false;
        actor->plane->object = part;
        actor->plane->pilot = actor;
        this->actors.push_back(actor);
        this->player = actor;
    } else if (actor->profile != nullptr && isPlayerSlot) {
        // entity resolves but jdyn is null — e.g. the ship IFF has no
        // DYNM>FGTR chunk to source flight-dynamics data from (see
        // RSEntity.cpp's parseREAL_OBJT_SSHP_DYNM_FGTR). No plane gets
        // built, but this->player must still be set here (matching the
        // sibling branch above) — leaving it null makes WC3Strike's whole
        // per-frame player-update path silently no-op (see its
        // `player == nullptr` guard).
        this->actors.push_back(actor);
        this->player = actor;
    } else {
        this->actors.push_back(actor);
    }
    return actor;
}

void WC3Mission::update() {
    SCMission::update();
}
