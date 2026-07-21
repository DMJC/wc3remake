#pragma once

enum weapon_type_shp_id {
    AIM9J = 29,
    AIM9M = 30,
    AIM120 = 116,
    AGM65D = 31,
    DURANDAL = 32,
    MK20 = 33,
    MK82 = 34,
    GBU15 = 35,
    LAU3 = 36
};

enum Hud_weapon_mode {
    WM_HUD_LCOS = 0,
    WM_HUD_STRAF = 1,
    WM_HUD_SRM = 2,
    WM_HUD_CCIP = 3,
    WM_HUD_CCRP = 4,
    WM_HUD_LRM = 5,
    WM_HUD_IRST = 6,
    WM_HUD_NONE = 7
};
static std::unordered_map<Hud_weapon_mode, std::string> hud_weapon_mode_names = {
    {WM_HUD_LCOS, "DGFT"},
    {WM_HUD_STRAF, "STRF"},
    {WM_HUD_SRM, "SRM"},
    {WM_HUD_CCIP, "CCIP"},
    {WM_HUD_CCRP, "CCRP"},
    {WM_HUD_LRM, "MRM"},
    {WM_HUD_IRST, "IR"},
    {WM_HUD_NONE, ""}
};
enum weapon_ids {
    ID_AIM9J = 1,
    ID_AIM9M = 2,
    ID_AGM65D = 3,
    ID_LAU3 = 4,
    ID_MK20 = 5,
    ID_MK82 = 6,
    ID_DURANDAL = 7,
    ID_GBU15 = 8,
    ID_AIM120 = 9,
    ID_20MM = 12,
    // WC3 (SSHP) weapon types. Deliberately offset well clear of the SC1
    // ids above — the raw WC3 hardpoint type-id bytes baked into SSHP
    // files (RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS/MISL: NEUTGUN=3,
    // ION_GUN=4, RLASER=2, REAPGUN=6, TACHGUN=10) collide with existing
    // SC1 weapon_ids values (ID_AIM9M=2, ID_AGM65D=3, ...) and must never
    // be used directly as a weapon_id — only as a transient parse-time
    // lookup key that gets translated into one of these instead.
    ID_NEUTGUN = 20,
    ID_IONGUN = 21,
    ID_RLASER_WC3 = 22,
    ID_REAPGUN = 23,
    ID_TACHGUN = 24,
    ID_HSMISS = 25,
    ID_IRMISS = 26,
    ID_FFMISS = 27,
    ID_TORKMISS = 28,   // torpedo — cannot fire unless locked (SCPlane::Shoot)
    ID_TEMBMISS = 29,   // T-bomb — same lock-required rule as torpedo, longer lock time
    // Mine — fired from a missile hardpoint like the above, but
    // WDAT::is_guided_flight is false (byte-confirmed real, MINEMISS.IFF's
    // own OBJT>MISL>DATA), so SCPlane::Shoot() drops it in place instead of
    // launching it, and SCSimulatedObject::ComputeTrajectory keeps it
    // stationary — it detonates only via the existing dumbfire proximity-
    // scan when an enemy strays into CheckCollision's range.
    ID_MINEMISS = 30,
    // 4 more real WC3 guns (user-confirmed, 2026-07 session: PHOTGUN.IFF/
    // PLASGUN.IFF/MASSGUN.IFF/MESOGUN.IFF all exist in OBJECTS.TRE,
    // alongside the 5 already above) — no raw SSHP hardpoint type-id byte
    // confirmed yet for any of these (see wc3GunRawIdToWeaponId's own
    // comment), so they're only reachable via the name-based
    // wc3WeaponNameToId lookup for now.
    ID_PHOTGUN = 31,
    ID_PLASGUN = 32,
    ID_MASSGUN = 33,
    ID_MESOGUN = 34,
    // 2 more real WC3 missiles (user-confirmed, 2026-07 session — matching
    // real OBJECTS.TRE files DFMISS.IFF/LEECHMIS.IFF): dumbfire (an
    // unguided rocket — distinct from HSMISS/IRMISS/FFMISS's own
    // *ability* to dumbfire unlocked, see SCPlane::Shoot()'s comment) and
    // leech missile (drains the target's shield energy).
    ID_DFMISS = 35,
    ID_LEECHMISS = 36,
};
// Real per-weapon stats (damage/effective_range/target_range/
// weapon_category) for WC3 weapons — a FALLBACK ONLY now: RSEntity::
// getWC3RealWeaponEntity resolves the weapon's real DATA\OBJECTS\<name>.IFF
// asset and parses its real OBJT>GUNS>DATA/OBJT>MISL>DATA chunk directly
// into wdat (RSEntity.cpp's parseREAL_OBJT_GUNS_DATA/parseREAL_OBJT_MISL_
// DATA) whenever that succeeds; this table's values are only used if asset
// resolution fails entirely (prints its own diagnostic when that happens).
// damage/effective_range are confirmed real for TACHGUN (70/3200 — real
// per-shot stats aren't parsed for effective_range the same way; this
// entry keeps the real damage but an estimated range) and for HSMISS/
// IRMISS/FFMISS's damage (real: 40->real varies, see parseREAL_OBJT_MISL_
// DATA — kept here only as this table's own fallback estimate); everything
// else remains an unconfirmed placeholder, roughly proportionate to its
// closest SC1 analogue (gun < missile < torpedo, in damage/range).
// weapon_category follows the existing SCMissionActors.cpp convention:
// 0=guns, 2=missiles (dumbfire-vs-guided is a lock-state distinction, not
// a category one), 3=torpedo (new). lock_time_required is likewise a
// fallback only now — SCPlane::Shoot() prefers the real, per-weapon
// wdat->lock_time_required_seconds (RSEntity::parseREAL_OBJT_MISL_DATA,
// confirmed real: e.g. Friend-or-Foe needs 0s, Image-Recognition needs 1s)
// whenever real asset data was resolved.
struct WC3WeaponStat {
    uint16_t damage;
    uint32_t effective_range;
    uint32_t target_range;
    uint8_t weapon_category;
    float lock_time_required{0.0f};
};
static std::unordered_map<int, WC3WeaponStat> wc3_weapon_stats = {
    {ID_NEUTGUN,  {8,  9000,  12000, 0}},
    {ID_IONGUN,   {10, 8500,  11500, 0}},
    {ID_RLASER_WC3, {6, 10000, 13000, 0}},
    {ID_REAPGUN,  {14, 7500,  10500, 0}},
    {ID_TACHGUN,  {70, 3200,  14000, 0}},  // damage/range confirmed real
    // Unconfirmed placeholders (2026-07 session) — real stats come from
    // each weapon's own OBJT>GUNS>DATA chunk via getWC3RealWeaponEntity
    // whenever that resolves, same as every other WC3 gun above; these
    // values are only a fallback and were never measured against real
    // gameplay for these 4.
    {ID_PHOTGUN,  {10, 9000,  12000, 0}},
    {ID_PLASGUN,  {12, 8500,  12000, 0}},
    {ID_MASSGUN,  {16, 6500,  9500,  0}},
    {ID_MESOGUN,  {20, 6000,  9000,  0}},
    {ID_HSMISS,   {40, 20000, 30000, 2}},
    {ID_IRMISS,   {350, 18000, 28000, 2, 1.0f}},  // damage/lock confirmed real
    {ID_FFMISS,   {250, 22000, 32000, 2, 0.0f}},  // damage/lock confirmed real
    {ID_TORKMISS, {8000, 16000, 35000, 3, 12.0f}},  // damage/lock confirmed real; range = decoded duration(16s) x speed(1000)
    {ID_TEMBMISS, {250, 30000, 40000, 3, 4.0f}},
    {ID_MINEMISS, {400, 0, 0, 2, 0.0f}},  // damage/lock confirmed real; stationary, no meaningful range
    // Unconfirmed placeholders (2026-07 session) — see ID_PHOTGUN etc.'s
    // own comment above.
    {ID_DFMISS,   {30, 15000, 25000, 2, 0.0f}},
    {ID_LEECHMISS,{20, 18000, 28000, 2, 1.0f}},
};
enum CockpitFace {
    CP_FRONT = 0,
    CP_RIGHT = 1,
    CP_LEFT = 2,
    CP_REAR = 3,
    CP_BIG = 4
};
static std::unordered_map<weapon_ids, weapon_type_shp_id> weapon_inv_to_loadout = {
    {ID_AIM9J, AIM9J},
    {ID_AIM9M, AIM9M},
    {ID_AGM65D, AGM65D},
    {ID_LAU3, LAU3},
    {ID_MK20, MK20},
    {ID_MK82, MK82},
    {ID_DURANDAL, DURANDAL},
    {ID_GBU15, GBU15},
    {ID_AIM120, AIM120}
};
static std::unordered_map<weapon_ids, std::string> weapon_names = {
    {weapon_ids::ID_20MM, "GUN"},
    {weapon_ids::ID_AIM9J, "AIM-9J"},
    {weapon_ids::ID_AIM9M, "AIM-9M"},
    {weapon_ids::ID_AGM65D, "AGM-65"},
    {weapon_ids::ID_LAU3, "POD"},
    {weapon_ids::ID_MK20, "MK-20"},
    {weapon_ids::ID_MK82, "MK-82"},
    {weapon_ids::ID_DURANDAL, "DUR"},
    {weapon_ids::ID_GBU15, "GBU-15"},
    {weapon_ids::ID_AIM120, "AIM-120"},
    {weapon_ids::ID_NEUTGUN, "NEUTRON GUN"},
    {weapon_ids::ID_IONGUN, "ION CANNON"},
    {weapon_ids::ID_RLASER_WC3, "LASER"},
    {weapon_ids::ID_REAPGUN, "REAPER GUN"},
    {weapon_ids::ID_TACHGUN, "TACHYON GUN"},
    {weapon_ids::ID_HSMISS, "HEATSEEKER"},
    // "IMREC MISSILE" (image-recognition), not "IR MISSILE" — user-
    // confirmed, 2026-07 session, read directly off the real Weapons MFD
    // text ("underneath Full Guns is IMREC MISSILE 3").
    {weapon_ids::ID_IRMISS, "IMREC MISSILE"},
    {weapon_ids::ID_FFMISS, "FRIEND-FOE"},
    {weapon_ids::ID_TORKMISS, "TORPEDO"},
    {weapon_ids::ID_TEMBMISS, "T-BOMB"},
    {weapon_ids::ID_PHOTGUN, "PHOTON GUN"},
    {weapon_ids::ID_PLASGUN, "PLASMA GUN"},
    {weapon_ids::ID_MASSGUN, "MASS DRIVER"},
    {weapon_ids::ID_MESOGUN, "MESON GUN"},
    {weapon_ids::ID_DFMISS, "DUMBFIRE"},
    {weapon_ids::ID_LEECHMISS, "LEECH MISSILE"},
};
enum prog_compare_return_values {
    PROG_CMP_EQUAL = 1,          // 000001
    PROG_CMP_LESS = 2,           // 000010
    PROG_CMP_GREATER = 4,        // 000100
    PROG_CMP_LESS_EQUAL = 3,     // 000011
    PROG_CMP_GREATER_EQUAL = 5,  // 000101
    PROG_CMP_NOT_EQUAL = 6,      // 000110
    PROG_CMP_UNSET = 0           // 000000
};

enum prog_op {
    OP_NOOP = 0,
    OP_EXIT_PROG = 1,
    OP_EXEC_SUB_PROG = 2,
    OP_SET_LABEL = 8,
    OP_SPOT_DATA = 9,
    OP_MOVE_VALUE_TO_WORK_REGISTER = 16,
    OP_MOVE_FLAG_TO_WORK_REGISTER = 17,
    OP_SAVE_VALUE_TO_GAMFLOW_REGISTER = 20,
    // Reverse-engineered (2026-07 session): every one of the 7 real uses
    // across the corpus (MISNR001/R002/CAPSHIP/MISND003/MISND3BD/
    // MISNDEMO/TSIM010) is immediately followed by OP_EXECUTE_CALL(0) —
    // the exact same "read a flag into the work register, then indirect-
    // jump through it" idiom OP_MOVE_FLAG_TO_WORK_REGISTER (17) already
    // drives elsewhere in these same files (e.g. MISNR001's own wave
    // dispatcher: `(17,5),(79,0)`, structurally identical to this
    // opcode's own `(21,5),(79,0)`). No observed case distinguishes it
    // from 17 — implemented as the same operation.
    OP_MOVE_FLAG_TO_WORK_REGISTER_ALT = 21,
    OP_ADD_WORK_REGISTER_TO_FLAG = 35,
    OP_MUL_VALUE_WITH_WORK = 46,
    OP_CMP_WORK_WITH_VALUE = 64,
    OP_CMP_VALUE_WITH_WORK = 65,
    // Reverse-engineered (2026-07 session, MISNJ3NT.IFF — the Behemoth
    // fuel-convoy defense mission). Sits directly between a near-identical
    // sibling block pair: one wave-activation block gates its
    // OP_ACTIVATE_OBJ calls behind a plain OP_TEST_FLAG(7)/
    // GOTO_IF_CURRENT_COMMAND_IN_PROGRESS check; the very next block gates
    // an equivalent wave behind OP_MOVE_VALUE_TO_WORK_REGISTER(6) +
    // OP_66(9) + OP_BRANCH_IF_EQUAL — the same "branch on condition" shape,
    // but comparing a literal (6) against something addressed by arg (9)
    // instead of a plain boolean flag. OP_CMP_WORK_WITH_VALUE (64) already
    // covers "work vs. a literal baked into the instruction"; this fills
    // the missing "work vs. a *flag's current value*" case — needed here
    // because mission flags aren't just booleans, they're incrementing
    // wave counters (OP_ADD_1_TO_FLAG appears throughout this same file),
    // and OP_TEST_FLAG can only check truthiness, not a specific count.
    // Mirrors OP_CMP_WORK_WITH_VALUE's own compare_flag bit-setting logic
    // exactly, just reading mission_data.flags[arg] as the right-hand side
    // instead of prog.arg itself. Only one real example in the whole
    // 85-file corpus.
    OP_CMP_WORK_WITH_FLAG = 66,
    OP_TEST_FLAG = 69,
    OP_GOTO_IF_CURRENT_COMMAND_IN_PROGRESS = 70,
    OP_BRANCH_IF_EQUAL = 72,
    OP_BRANCH_IF_NOT_EQUAL = 73,
    OP_BRANCH_IF_LESS = 74,
    OP_BRANCH_IF_GREATER = 75,
    // User-confirmed context (2026-07 session, MISNP004.IFF — the
    // campaign's final Kilrah-run mission): DIST_TO_TARGET /
    // CMP_WORK_WITH_VALUE(15000) / OP_76(label) / ...fallback: EXIT_PROG...
    // / label: defendTarget(0). Numerically completes the OP_72..OP_75
    // (EQUAL/NOT_EQUAL/LESS/GREATER) branch family using the same
    // compare_flag bitmask (see SCProg.cpp's own comment on PROG_CMP_*)
    // — the natural 5th/6th comparison. Narratively reads as "once close
    // enough to the target, branch ahead to defend it; otherwise exit
    // early and retry next tick" (the same wait-and-poll idiom
    // OP_GOTO_IF_CURRENT_COMMAND_IN_PROGRESS uses elsewhere), which only
    // makes sense if the branch fires on <=, not >= — LESS_OR_EQUAL over
    // GREATER_OR_EQUAL. Only one real example exists in the whole 85-file
    // corpus; treat this as a confident but not airtight reverse-engineer.
    OP_BRANCH_IF_LESS_OR_EQUAL = 76,
    OP_EXECUTE_CALL = 79,
    OP_MOVE_WORK_REGISTER_TO_FLAG = 80,
    OP_SET_FLAG_TO_TRUE = 82,
    OP_SET_FLAG_TO_FALSE = 83,
    // Reverse-engineered (2026-07 session, MISNJ3NT.IFF and MISNL002.IFF):
    // sits right between OP_SET_FLAG_TO_FALSE (83) and OP_ADD_1_TO_FLAG
    // (85) — the missing OP_SET_FLAG_TO_TRUE-shaped slot. Every real use
    // in MISNJ3NT fires right after a group of OP_ACTIVATE_OBJ calls
    // (spawning a wave), with the same arg as the TEST_FLAG this same
    // block checks at its own top to decide whether that wave has
    // already been spawned — i.e. "mark this wave's flag true" — the
    // exact role OP_SET_FLAG_TO_TRUE already plays elsewhere. Implemented
    // identically to 82.
    OP_SET_FLAG_TO_TRUE_ALT = 84,
    OP_ADD_1_TO_FLAG = 85,
    OP_REMOVE_1_TO_FLAG = 86,
    OP_ACTIVATE_SCENE = 128,
    OP_DEACTIVATE_SCENE = 129,
    OP_ACTIVATE_OBJ = 144,
    // Reverse-engineered (2026-07 session, MISNJ002.IFF "Torgo 2"): found
    // while chasing the enemy-wave-spawn prerequisite for the OP_228
    // mine-count mechanic. Every real use (18 total, all in this file)
    // takes a valid VAKTOTH/PAKTAHN PART actor_id and appears in exactly
    // the OP_ACTIVATE_OBJ shape: grouped in pairs/fours right after a
    // TEST_FLAG/wait gate, immediately followed by a SET_FLAG_TO_TRUE
    // "this wave is now spawned" marker. Cross-checked against this
    // file's own PLAY>SCNE data (RSMission::parseMISN_PLAY_SCEN): each
    // nav-point scene's cast list (e.g. scene area_id=1's [BUOY(4),
    // VAKTOTH(8,9),PAKTAHN(10,11)]) is exactly the pool these opcodes
    // draw individual actor ids from — i.e. this is the real mechanism
    // that brings a scene's enemies into play, which WC3Mission::
    // loadMission()'s own scene handling never wires up as a script
    // dispatch (it only reads scene->cast/area_id/spawn_position to
    // synthesize missing PART records, never scene->on_is_activated —
    // that field is a Strike-Commander-only SCMission.cpp mechanic).
    // Implemented identically to OP_ACTIVATE_OBJ (calls the same
    // activateTarget(), which already does exactly the "unhide + apply
    // this actor's own area_id/SPOT position correction" needed to place
    // a wave at its nav point).
    OP_ACTIVATE_OBJ_ALT = 174,
    // Reverse-engineered (2026-07 session): all 7 real occurrences across
    // the corpus (incl. MISNP004's own OP_76 distance-check block) pass
    // arg == -1 every time — never a valid actor id — ruling out any
    // "act on actor N" reading. Consistently appears right before
    // OP_EXIT_PROG in a distance-check branch, i.e. the "not close enough
    // yet, give up on this target" path. No existing opcode releases an
    // actor's own current_target/target/attacker/target_position state
    // (destroyTarget() does it inline on kill, but nothing exposes it to
    // PROG) — implemented as that same release sequence.
    OP_CLEAR_TARGET = 145,
    OP_IF_TARGET_IN_AREA = 146,
    OP_IS_TARGET_ALIVE = 147,
    OP_INSTANT_DESTROY_TARGET = 148,
    OP_DIST_TO_TARGET = 149,
    OP_DIST_TO_SPOT = 151,
    OP_IS_TARGET_ACTIVE = 152,
    // Reverse-engineered (2026-07 session, MISNP003.IFF — "LAST LEG",
    // leading straight into the Kilrah run). User-confirmed lead: this
    // file's own MISN>MSGS[5] is literally "KILRAH -- ACTIVATE CLOAK
    // BEFORE ENTRY, LADDIE". OP_155(0) sits in the on_update shared by 4
    // kdestl destroyers + the ace bloodfng, feeding a BRANCH_IF_EQUAL that
    // skips the rest of the block (wait + defendTarget) entirely — reads
    // as "if actor 0 (EXCALK, the player in this file) is cloaked, skip;
    // Kilrathi defenders can't engage what they can't detect." Reuses the
    // exact same SCPlane::cloaked field already driving the player's own
    // cloak toggle and enemy radar/targeting exclusion elsewhere this
    // session — a symmetric extension, not a new mechanic.
    OP_IS_TARGET_CLOAKED = 155,
    // Reverse-engineered (2026-07 session, MISNJ003.IFF — "Torgo", whose
    // MISN>MSGS literally has "Disable tankers" as an objective).
    // User-confirmed: real WC3 "disable" is achieved by shooting the
    // target's engines (ShipComponent::Engine, HitQuadrant::Back) down to
    // ~50% remaining, not a Leech-missile-specific power drain. Always
    // paired with OP_GET_TARGET_ENGINE_HEALTH (226) in the real data —
    // that opcode reads a target's current engine health into the work
    // register; once a CMP_WORK_WITH_VALUE(50)/BRANCH_IF_GREATER gate lets
    // execution fall through (health <= 50), this opcode fires with the
    // same target id to actually mark it disabled. Stops the target
    // flying/fighting (see SCMissionActors::is_disabled and its own check
    // in SCMission.cpp's per-actor update loop) without destroying it.
    OP_SET_TARGET_DISABLED = 157,
    OP_SET_WAIT_FOR_SECONDS = 160,
    OP_SET_OBJ_TAKE_OFF = 161,
    OP_SET_OBJ_LAND = 162,
    OP_SET_OBJ_FLY_TO_WP = 165,
    OP_SET_OBJ_FLY_TO_AREA = 166,
    OP_SET_OBJ_DESTROY_TARGET = 167,
    OP_SET_OBJ_DEFEND_TARGET = 168,
    OP_SET_OBJ_DEFEND_AREA = 169,
    OP_SET_OBJ_FOLLOW_ALLY = 170,
    OP_SET_MESSAGE = 171,
    OP_DEACTIVATE_OBJ = 190,
    // Selects the next mission's filename by index into MISN>MSGS — the
    // argument is a message-table index whose string is a lowercase mission
    // basename (e.g. "misnd003"), not display text. Confirmed by scanning
    // every PROG chunk across all 110 files in missions.tre: appears in 10
    // files total, always with an MSGS entry that resolves to a mission
    // filename. Most uses are unconditional (a single candidate, no branch
    // — the mission just always continues to that file); MISND002.IFF
    // (Laconda2) is the one file that wraps it in a real TEST_FLAG/BRANCH
    // condition choosing between two candidates (misnd003 vs. the misnd3bd
    // Flint-rescue side-mission) — see SCMission::next_mission_message_index's
    // own comment for how the result is surfaced.
    OP_SELECT_NEXT_MISSION = 198,
    // Always observed immediately after OP_SELECT_NEXT_MISSION with arg 0,
    // in every one of the 10 files checked — plausibly a fixed "end of
    // selection" marker, but its own effect (if any) isn't confirmed.
    // No-op for now, same as OP_SELECT_FLAG_208 below.
    OP_SELECT_NEXT_MISSION_END = 199,
    // Reverse-engineered (2026-07 session, MISNP000.IFF — "JUMP POINT",
    // named to lead straight into misnp001). This file uses OP_200 as its
    // *only* mission-transition opcode — no OP_SELECT_NEXT_MISSION(198)
    // or its _END(199) marker appear anywhere in the file at all — with
    // arg=3, which is exactly MISN>MSGS index 3: the literal string
    // "misnp001". Same shape as OP_SELECT_NEXT_MISSION (an index into
    // MISN>MSGS resolving to a mission basename), just apparently used in
    // files that don't also need the paired _END marker (this block is a
    // simple wait-then-fire trigger, not the TEST_FLAG/BRANCH two-
    // candidate pattern OP_SELECT_NEXT_MISSION's own comment describes
    // for MISND002). Implemented identically to OP_SELECT_NEXT_MISSION.
    OP_SELECT_NEXT_MISSION_ALT = 200,
    OP_SELECT_FLAG_208 = 208,
    // See OP_SET_TARGET_DISABLED's own comment (always paired with it in
    // real data) — reads the target actor's current
    // ShipComponent::Engine health (100 - component_damage[Engine],
    // clamped 0-100) into the work register.
    OP_GET_TARGET_ENGINE_HEALTH = 226,
    // Reverse-engineered (2026-07 session, user-described real mechanic):
    // MISNJ002.IFF ("Torgo 2") is the only file that uses this opcode, 4
    // times (prog blocks 16-19, args 1/2/3/4 respectively), each shaped
    // identically: `TEST_FLAG(22+n) -> BRANCH_IF_EQUAL(skip) ->
    // OP_228(n) -> CMP_WORK_WITH_VALUE(2) -> BRANCH_IF_LESS(skip) ->
    // ADD_1_TO_FLAG(2) -> SET_FLAG_TO_TRUE(22+n)`. flag[2] (a counter
    // incremented once per nav point that reaches the threshold) is read
    // back in block 0's own EXECUTE_CALL jump table: flag[2]==4 fires the
    // win path (SET_FLAG_TO_TRUE(1) — the mission-complete flag). Matches
    // the user's own description exactly: deploy 2 mines at each of Nav
    // 1-4 to win. Previously ruled out as "distance to actor" (would be
    // redundant with OP_149/DIST_TO_TARGET, which already takes an
    // explicit actor id) — this is "mine count," not a distance, and the
    // 4 real args (1-4) don't correspond to any existing actor_id in the
    // file (the JUBOUY buoys are actor ids 4-7, not 1-4); arg is instead
    // a 1-based ordinal among this mission's own JUBOUY actors, resolved
    // generically at the call site rather than hardcoding a specific id
    // offset. See SCPlane::Shoot's own comment for the write side
    // (SCMissionActors::mines_deployed, incremented by proximity-matching
    // a dropped ID_MINEMISS mine to the nearest JUBOUY actor).
    OP_GET_MINE_COUNT_AT_NAVPOINT = 228,
};

static std::unordered_map<prog_op, std::string> prog_op_names = {
    {OP_EXIT_PROG, "OP_EXIT_PROG"},
    {OP_EXEC_SUB_PROG, "OP_EXEC_SUB_PROG"},
    {OP_SET_LABEL, "OP_SET_LABEL"},
    {OP_SPOT_DATA, "OP_SPOT_DATA"},
    {OP_MOVE_VALUE_TO_WORK_REGISTER, "OP_MOVE_VALUE_TO_WORK_REGISTER"},
    {OP_MOVE_FLAG_TO_WORK_REGISTER, "OP_MOVE_FLAG_TO_WORK_REGISTER"},
    {OP_MOVE_FLAG_TO_WORK_REGISTER_ALT, "OP_MOVE_FLAG_TO_WORK_REGISTER_ALT"},
    {OP_SAVE_VALUE_TO_GAMFLOW_REGISTER, "OP_SAVE_VALUE_TO_GAMFLOW_REGISTER"},
    {OP_ADD_WORK_REGISTER_TO_FLAG, "OP_ADD_WORK_REGISTER_TO_FLAG"},
    {OP_MUL_VALUE_WITH_WORK, "OP_MUL_VALUE_WITH_WORK"},
    {OP_CMP_WORK_WITH_VALUE, "OP_CMP_WORK_WITH_VALUE"},
    {OP_CMP_VALUE_WITH_WORK, "OP_CMP_VALUE_WITH_WORK"},
    {OP_CMP_WORK_WITH_FLAG, "OP_CMP_WORK_WITH_FLAG"},
    {OP_TEST_FLAG, "OP_TEST_FLAG"},
    {OP_GOTO_IF_CURRENT_COMMAND_IN_PROGRESS, "OP_GOTO_IF_CURRENT_COMMAND_IN_PROGRESS"},
    {OP_BRANCH_IF_EQUAL, "OP_BRANCH_IF_EQUAL"},
    {OP_BRANCH_IF_NOT_EQUAL, "OP_BRANCH_IF_NOT_EQUAL"},
    {OP_BRANCH_IF_LESS, "OP_BRANCH_IF_LESS"},
    {OP_BRANCH_IF_GREATER, "OP_BRANCH_IF_GREATER"},
    {OP_BRANCH_IF_LESS_OR_EQUAL, "OP_BRANCH_IF_LESS_OR_EQUAL"},
    {OP_EXECUTE_CALL, "OP_EXECUTE_CALL"},
    {OP_MOVE_WORK_REGISTER_TO_FLAG, "OP_MOVE_WORK_REGISTER_TO_FLAG"},
    {OP_SET_FLAG_TO_TRUE, "OP_SET_FLAG_TO_TRUE"},
    {OP_SET_FLAG_TO_TRUE_ALT, "OP_SET_FLAG_TO_TRUE_ALT"},
    {OP_SET_FLAG_TO_FALSE, "OP_SET_FLAG_TO_FALSE"},
    {OP_ADD_1_TO_FLAG, "OP_ADD_1_TO_FLAG"},
    {OP_REMOVE_1_TO_FLAG, "OP_REMOVE_1_TO_FLAG"},
    {OP_ACTIVATE_SCENE, "OP_ACTIVATE_SCENE"},
    {OP_DEACTIVATE_SCENE, "OP_DEACTIVATE_SCENE"},
    {OP_ACTIVATE_OBJ, "OP_ACTIVATE_OBJ"},
    {OP_ACTIVATE_OBJ_ALT, "OP_ACTIVATE_OBJ_ALT"},
    {OP_CLEAR_TARGET, "OP_CLEAR_TARGET"},
    {OP_IF_TARGET_IN_AREA, "OP_IF_TARGET_IN_AREA"},
    {OP_IS_TARGET_ALIVE, "OP_IS_TARGET_ALIVE"},
    {OP_INSTANT_DESTROY_TARGET, "OP_INSTANT_DESTROY_TARGET"},
    {OP_DIST_TO_TARGET, "OP_DIST_TO_TARGET"},
    {OP_DIST_TO_SPOT, "OP_DIST_TO_SPOT"},
    {OP_IS_TARGET_ACTIVE, "OP_IS_TARGET_ACTIVE"},
    {OP_IS_TARGET_CLOAKED, "OP_IS_TARGET_CLOAKED"},
    {OP_SET_TARGET_DISABLED, "OP_SET_TARGET_DISABLED"},
    {OP_SET_WAIT_FOR_SECONDS, "OP_SET_WAIT_FOR_SECONDS"},
    {OP_SET_OBJ_TAKE_OFF, "OP_SET_OBJ_TAKE_OFF"},
    {OP_SET_OBJ_LAND, "OP_SET_OBJ_LAND"},
    {OP_SET_OBJ_FLY_TO_WP, "OP_SET_OBJ_FLY_TO_WP"},
    {OP_SET_OBJ_FLY_TO_AREA, "OP_SET_OBJ_FLY_TO_AREA"},
    {OP_SET_OBJ_DESTROY_TARGET, "OP_SET_OBJ_DESTROY_TARGET"},
    {OP_SET_OBJ_DEFEND_TARGET, "OP_SET_OBJ_DEFEND_TARGET"},
    {OP_SET_OBJ_DEFEND_AREA, "OP_SET_OBJ_DEFEND_AREA"},
    {OP_SET_OBJ_FOLLOW_ALLY, "OP_SET_OBJ_FOLLOW_ALLY"},
    {OP_SET_MESSAGE, "OP_SET_MESSAGE"},
    {OP_DEACTIVATE_OBJ, "OP_DEACTIVATE_OBJ"},
    {OP_SELECT_NEXT_MISSION, "OP_SELECT_NEXT_MISSION"},
    {OP_SELECT_NEXT_MISSION_END, "OP_SELECT_NEXT_MISSION_END"},
    {OP_SELECT_NEXT_MISSION_ALT, "OP_SELECT_NEXT_MISSION_ALT"},
    {OP_SELECT_FLAG_208, "OP_SELECT_FLAG_208"},
    {OP_GET_TARGET_ENGINE_HEALTH, "OP_GET_TARGET_ENGINE_HEALTH"},
    {OP_GET_MINE_COUNT_AT_NAVPOINT, "OP_GET_MINE_COUNT_AT_NAVPOINT"}
};

enum KillBoardType {
    AIR_KILL = 0,
    GROUND_KILL = 1
};
enum PilotsId {
    PLAYER=0,
    PRIMETIME=1,
    PHOENIX=2,
    BASELINE=3,
    ZORRO=4,
    TEX=5,
    VIXEN=6,
    HAWK=7
};
enum RadarMode {
    AARD,
    AGRD,
    ASST,
    AFRD
};
static std::unordered_map<uint8_t, std::string> pilot_names = {
    {PilotsId::PRIMETIME, "PRIMETIME"},
    {PilotsId::PHOENIX, "PHOENIX"},
    {PilotsId::BASELINE, "BASELINE"},
    {PilotsId::ZORRO, "ZORRO"},
    {PilotsId::TEX, "TEX"},
    {PilotsId::VIXEN, "VIXEN"},
    {PilotsId::HAWK, "HAWK"}
};

static std::unordered_map<std::string, uint8_t> pilot_profile = {
    {"BILLY", 1},
    {"GWEN", 2},
    {"BASELINE", 3},
    {"ZORRO", 4},
    {"TEX", 5},
    {"VIXEN", 6},
    {"HAWK", 7}
};

// TRACK: real WC3 "Track Camera" (F10) — a world-fixed camera anchored at
// wherever the player was when the mode was engaged, continuously
// re-aiming (lookAt) at the player as they fly on past it, like a
// stationary tracking shot. See SCStrike::checkKeyboard's VIEW_TRACK
// handler (sets track_camera_anchor once, on entry) and its own switch
// case in runFrame's camera_mode dispatch.
enum View { FRONT = 0, FOLLOW, RIGHT, LEFT, REAR, REAL, TARGET, EYE_ON_TARGET, MISSILE_CAM, OBJECT, AUTO_PILOT, CONTROLLER_LOOK, TRACK };

enum CatalogItems {
    CAT_AIM9J = 73,
    CAT_AIM9M = 74,
    CAT_AIM120 = 75,
    CAT_LAU3 = 76,
    CAT_AGM65D = 77,
    CAT_GBU15 = 78,
    CAT_MK20 = 79,
    CAT_MK82 = 80,
    CAT_DURANDAL = 81,
    CAT_PACK1 = 83,
    CAT_PACK2 = 84,
    CAT_PACK3 = 85,
    CAT_PACK4 = 86
};
enum SoundEffectIds {
    MK82_DROP = 0,
    MK20_DROP = 1,
    EXPLOSION_1 = 2,
    EXPLOSION_2 = 3,
    EXPLOSION_3 = 4,
    EXPLOSION_4 = 5,
    AIM9_SHOOT = 6,
    AIM120_SHOOT = 7,
    POD_SHOOT = 8,
    MAVERICK_SHOOT = 9,
    GUN_IMPACT_1 = 10,
    GUN_IMPACT_2 = 11,
    GUN_IMPACT_3 = 12,
    GUN_IMPACT_4 = 13,
    GUN_IMPACT_5 = 14,
    GUN_IMPACT_6 = 15,
    PLANE_PASS_BY_1 = 16,
    PLANE_PASS_BY_2 = 17,
    PLANE_PASS_BY_3 = 18,
    GEARS_UP = 19,
    GEARS_DOWN = 20,
    TOUCH_DOWN = 21,
    ENGINE_MIL = 23,
    ENGINE_AFB = 24,
    ENGINE_START_MIL = 25,
    ENGINE_START_AFB = 26,
    ENGINE_SHUT_DOWN = 27,
    ENGINE_MIL_SHUT_DOWN = 28,
    ENGINE_AFB_SHUT_DOWN = 29,
    ENGINE_AFB_TO_MIL = 30,
    DONT_KNOW = 31
};
enum GameFlowOpCode {
    EFECT_OPT_CONV = 0,
    EFECT_OPT_SCEN = 1,
    EFECT_OPT_PLAY_MIDGAME = 2,
    EFECT_OPT_FLYM2 = 3,
    EFECT_OPT_SETFLAG_TRUE = 6,
    EFECT_OPT_SETFLAG_FALSE = 7,
    EFECT_OPT_SHOT = 8,
    EFECT_OPT_IF_NOT_FLAG = 9,
    EFECT_OPT_IF_FLAG = 10,
    EFECT_OPT_FLYM = 12,
    EFECT_OPT_MIS2 = 13,
    EFECT_OPT_GO = 15,
    EFECT_OPT_SAVE_GAME = 16,
    EFECT_OPT_LOAD_GAME = 17,
    EFFCT_OPT_SHOW_MAP = 18,
    EFECT_OPT_LOOK_AT_KILLBOARD = 19,
    EFECT_OPT_MISS_ACCEPTED = 20,
    EFECT_OPT_MISS_REJECTED = 21,
    EFECT_OPT_END_MISS = 22,
    EFECT_OPT_VIEW_CATALOG = 23,
    EFECT_OPT_LOOK_AT_LEDGER = 25,
    EFECT_OPT_MISS_ELSE = 30,
    EFECT_OPT_MISS_ENDIF = 31,
    EFFCT_OPT_IF_MISS_SUCCESS = 32,
    EFECT_OPT_APPLY_CHANGE = 34,
    EFECT_OPT_TUNE_MODIFIER = 38,
    EFECT_OPT_U2 = 4,
    EFECT_OPT_U8 = 24,
    EFECT_OPT_U10 = 26,
    EFECT_OPT_U11 = 27,
    EFECT_OPT_U12 = 29,
    EFECT_OPT_U14 = 36,
    EFECT_IF_LAST_MISS_SUCCESS = 37,
    EFECT_OPT_U16 = 27,
    EFFCT_OPT_U17 = 35,
    EFFCT_OPT_U18 = 11,
    EFFCT_OPT_U19 = 40,
    EFFCT_OPT_U20 = 14
};
static std::unordered_map<GameFlowOpCode, std::string> game_flow_op_name = {
    {EFECT_OPT_CONV, "PLAY CONV"},
    {EFECT_OPT_SCEN, "PLAY SCEN"},
    {EFECT_OPT_FLYM2, "FLY MISSION (op2)"},
    {EFECT_OPT_SETFLAG_TRUE, "SETFLAG TRUE"},
    {EFECT_OPT_SETFLAG_FALSE, "SETFLAG FALSE"},
    {EFECT_OPT_SHOT, "PLAY SHOT"},
    {EFECT_OPT_IF_NOT_FLAG, "IF_NOT_FLAG"},
    {EFECT_OPT_IF_FLAG, "IF_FLAG"},
    {EFECT_OPT_FLYM, "FLY MISSION (op1)"},
    {EFECT_OPT_MIS2, "MIS2"},
    {EFECT_OPT_GO, "GO TO NEXT MISSION"},
    {EFECT_OPT_MISS_ACCEPTED, "IF_MISS_ACCEPTED"},
    {EFECT_OPT_MISS_REJECTED, "IF_MISS_REJECTED"},
    {EFECT_OPT_END_MISS, "END_MISS"},
    {EFECT_OPT_TUNE_MODIFIER, "MUSIC_TUNE_MODIFIER"},
    {EFECT_OPT_MISS_ELSE, "ELSE"},
    {EFECT_OPT_MISS_ENDIF, "ENDIF"},
    {EFFCT_OPT_IF_MISS_SUCCESS, "IF_MISS_SUCCESS"},
    {EFECT_OPT_LOOK_AT_KILLBOARD, "LOOK_AT_KILLBOARD"},
    {EFFCT_OPT_SHOW_MAP, "SHOW_MAP"},
    {EFECT_OPT_APPLY_CHANGE, "APPLY_CHANGE"},
    {EFECT_OPT_LOOK_AT_LEDGER, "LOOK_AT_LEDGER"},
    {EFECT_OPT_VIEW_CATALOG, "VIEW_CATALOG"},
    {EFECT_OPT_SAVE_GAME, "SAVE_GAME"},
    {EFECT_OPT_LOAD_GAME, "LOAD_GAME"},
    {EFECT_OPT_PLAY_MIDGAME, "PLAY_MIDGAME"},
    {EFECT_OPT_U2, "EFECT_OPT_U2"},
    {EFECT_OPT_U8, "EFECT_OPT_U8"},
    {EFECT_OPT_U10, "EFECT_OPT_U10"},
    {EFECT_OPT_U11, "EFECT_OPT_U11"},
    {EFECT_OPT_U12, "EFECT_OPT_U12"},
    {EFECT_OPT_U14, "EFECT_OPT_U14"},
    {EFECT_IF_LAST_MISS_SUCCESS, "IF_LAST_MISS_SUCCESS"},
    {EFECT_OPT_U16, "EFECT_OPT_U16"}
};