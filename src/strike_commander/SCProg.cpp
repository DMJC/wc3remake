#include "precomp.h"


/**
 * @brief Interpret and execute the scripted program (bytecode) attached to this SCProg instance.
 *
 * This method walks sequentially through the vector of PROG instructions (this->prog) and
 * interprets each opcode, mutating actor state, mission state, flags, command queues, and
 * various runtime control variables used for mission logic (e.g. branching, sub‑program
 * invocation, conditional evaluation).
 *
 * Control Flow Mechanism:
 * - A simple linear interpreter with manual flow control using:
 *   - jump_to + OP_SET_LABEL + setting exec=false to defer execution until a matching label.
 *   - Conditional gotos (OP_GOTO_LABEL_IF_TRUE / OP_GOTO_LABEL_IF_FALSE /
 *     OP_IF_LESS_THAN_GOTO / OP_IF_GREATER_THAN_GOTO).
 *   - Sub‑program execution via OP_EXEC_SUB_PROG (recursively creates and runs a new SCProg).
 *   - Indirect jump via OP_EXECUTE_CALL (jump target taken from work_register).
 * - The variable 'exec' gates whether the current instruction is active. When a jump is
 *   scheduled, exec is set to false until a matching OP_SET_LABEL with the desired label id
 *   (jump_to) is encountered, at which point execution resumes.
 *
 * State / Working Registers:
 * - work_register: General purpose integer scratch register used by arithmetic, comparisons,
 *   indirect jumps, and data movement (e.g. flags <-> work register).
 * - true_flag / compare_flag: Results of condition evaluation (boolean or tri-state compare)
 *   drive conditional jumps and branching logic.
 * - jump_to: Holds the label id to resume execution at after a jump is requested.
 * - call_to (currently unused) and flag_number (currently unused) appear reserved for future
 *   extensions.
 *
 * Side Effects on Actor:
 * - Clears and repopulates actor->executed_opcodes with indices of instructions actually
 *   executed (some conditional instructions only push if taken).
 * - Issues or updates current command (take off, land, move, destroy target, defend, follow,
 *   etc.) and tracks its completion state (current_command_executed).
 * - Queries spatial / combat related conditions (distance to target / spot, target alive,
 *   target in area) to set true_flag.
 * - Can instantly destroy a target actor (OP_INSTANT_DESTROY_TARGET), optionally spawning
 *   explosion effects.
 * - Can activate/deactivate actors or scenes.
 * - Sets in‑game messages via OP_SET_MESSAGE.
 *
 * Side Effects on Mission:
 * - Reads / writes mission->mission->mission_data.flags (increment, decrement, set boolean,
 *   arithmetic with work_register).
 * - Stores to mission->gameflow_registers via OP_SAVE_VALUE_TO_GAMFLOW_REGISTER.
 * - Activates scenes matching an area id.
 *
 * Comparison & Branching:
 * - OP_CMP_GREATER_EQUAL_THAN sets compare_flag to -1 / 0 / 1 for less / equal / greater,
 *   and sets true_flag to true if >=, false otherwise.
 * - Subsequent conditional opcodes test compare_flag or true_flag to decide jumps.
 *
 * Sub‑program Execution:
 * - OP_EXEC_SUB_PROG looks up a referenced PROG vector in mission data, constructs a temporary
 *   SCProg, and executes it immediately (recursive interpretation).
 *
 * Notable Implementation Details / Caveats:
 * - Some fields (call_to, flag_number) are declared but never used.
 * - true_flag is sometimes reset implicitly (e.g., only changed by certain opcodes); logic that
 *   depends on stale values must ensure an opcode establishing it has run.
 * - No bounds checking for many flag / actor indices (assumes valid script).
 * - No cycle detection; malicious or ill‑formed scripts with self‑referential jumps could stall
 *   progression (although loop constructs rely on label scanning rather than rewinding i).
 *
 * Logging / Tracing:
 * - The order and subset of executed instructions can be reconstructed from actor->executed_opcodes,
 *   which stores the numeric index (i) of each opcode actually processed (including some that
 *   only trigger under conditional execution).
 *
 * Thread Safety:
 * - Not thread‑safe; mutates shared mission and actor state without synchronization.
 *
 * Performance:
 * - Single pass over instructions; label resolution is linear (no precomputed label index), so
 *   large scripts with many jumps could incur extra scanning cost.
 *
 * Lifecycle:
 * - Returns immediately upon first OP_EXIT_PROG unless true_flag was set (in which case it clears
 *   true_flag and continues), enabling a "conditional early exit" pattern.
 *
 * Preconditions:
 * - this->actor, this->mission, and mission->mission->mission_data structures must be valid.
 * - Flags / actors / scenes referenced by indices must exist.
 *
 * Postconditions:
 * - Actor command state, flags, and mission side effects reflect all executed opcodes up to
 *   normal termination or early exit.
 *
 *
 * @return void (effects are applied through side effects).
 */
void SCProg::execute() {
    uint8_t i = 0;
    size_t jump_to = 0;
    size_t call_to = 0;
    int flag_number = 0;
    int work_register = 0;
    int compare_flag = prog_compare_return_values::PROG_CMP_UNSET;
    bool exec = true;
    bool objective_flag = false;
    bool true_flag = false;
    SPOT *spot = nullptr;
    this->actor->executed_opcodes.clear();
    this->actor->executed_opcodes.shrink_to_fit();
    this->mission->progs_traces[this->prog_id].clear();
    this->mission->progs_traces[this->prog_id].shrink_to_fit();
    for (auto prog : this->prog) {
        if (exec || (!exec && jump_to == prog.arg && prog.opcode == OP_SET_LABEL)) {
            this->actor->executed_opcodes.push_back(i);
            this->mission->progs_traces[this->prog_id].push_back(i);
            switch (prog.opcode) {
                case OP_EXIT_PROG:
                    return;
                break;
                case OP_SPOT_DATA:
                    if (prog.arg < this->mission->mission->mission_data.spots.size()) {
                        spot = this->mission->mission->mission_data.spots[prog.arg];
                    }
                break;
                case OP_SET_LABEL:
                    if (jump_to == prog.arg) {
                        exec = true;
                    }
                break;
                case OP_MOVE_VALUE_TO_WORK_REGISTER:
                    work_register = prog.arg;
                break;
                case OP_GOTO_IF_CURRENT_COMMAND_IN_PROGRESS:
                    if (!this->actor->current_command_executed) {
                        jump_to = prog.arg;
                        exec = false;
                        this->actor->current_command = prog_op::OP_NOOP;
                    }
                break;
                case OP_IF_TARGET_IN_AREA:
                    if (this->actor->ifTargetInSameArea(prog.arg)) {
                        compare_flag = prog_compare_return_values::PROG_CMP_EQUAL;
                    } else {
                        compare_flag = prog_compare_return_values::PROG_CMP_NOT_EQUAL;
                    }
                break;
                case OP_ADD_1_TO_FLAG:
                    this->mission->mission->mission_data.flags[prog.arg]++;
                break;
                case OP_REMOVE_1_TO_FLAG:
                    this->mission->mission->mission_data.flags[prog.arg]--;
                break;
                case OP_ADD_WORK_REGISTER_TO_FLAG:
                    this->mission->mission->mission_data.flags[prog.arg] += work_register;
                break;
                case OP_INSTANT_DESTROY_TARGET:
                    for (auto actor: this->mission->actors) {
                        if (actor->actor_id == prog.arg) {
                            actor->object->alive = false;
                            actor->is_destroyed = false;
                            // Death radio message — this scripted-destroy path
                            // (the one real WC3 capital ships mostly die
                            // through) never triggered one before, unlike
                            // hasBeenHit's own weapon-kill death branch. Same
                            // guard/RADI code as that one.
                            if (actor->profile != nullptr) {
                                actor->setMessage(0x0A);
                            }
                            if (!actor->has_exploded && actor->object->entity->explos != nullptr &&
                                actor->object->entity->explos->objct != nullptr) {
                                actor->has_exploded = true;
                                // 12x explosion scale for capital ships — see
                                // hasBeenHit's own comment (SCMissionActors.cpp)
                                // for why. This is the path real WC3 capital
                                // ships mostly die through (scripted mission
                                // destruction, not sustained player fire).
                                std::string upperName = actor->object->member_name;
                                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                                float explosionScale = SCMissionActors::IsCapitalShipName(upperName) ? 600.0f : 50.0f;
                                bool bigDeathSequence = SCMissionActors::IsLargeCapitalShipName(upperName);
                                actor->mission->explosions.push_back(new SCExplosion(actor->object->entity->explos->objct, actor->object->position, explosionScale, bigDeathSequence));
                            }
                            break;
                        }
                    }
                break;
                case OP_SET_WAIT_FOR_SECONDS:
                    this->actor->current_command_executed = this->actor->wait(prog.arg);
                    this->actor->current_command = OP_SET_WAIT_FOR_SECONDS;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_TAKE_OFF:
                    this->actor->current_command_executed = this->actor->takeOff(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_TAKE_OFF;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_LAND:
                    this->actor->current_command_executed = this->actor->land(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_LAND;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_FLY_TO_WP:
                    this->actor->current_command_executed = this->actor->flyToWaypoint(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_FLY_TO_WP;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_FLY_TO_AREA:
                    this->actor->current_command_executed = this->actor->flyToArea(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_FLY_TO_AREA;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_DESTROY_TARGET:
                    this->actor->current_command_executed = this->actor->destroyTarget(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_DESTROY_TARGET;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_DEFEND_TARGET:
                    this->actor->current_target = 0;
                    this->actor->current_command_executed = this->actor->defendTarget(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_DEFEND_TARGET;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_OBJ_DEFEND_AREA:
                    this->actor->current_command_executed = this->actor->defendArea(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_DEFEND_AREA;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_SET_MESSAGE:
                    this->actor->setMessage(prog.arg);
                break;
                case OP_SET_OBJ_FOLLOW_ALLY:
                    this->actor->current_command_executed = this->actor->followAlly(prog.arg);
                    this->actor->current_command = OP_SET_OBJ_FOLLOW_ALLY;
                    this->actor->current_command_arg = prog.arg;
                break;
                case OP_DEACTIVATE_OBJ:
                    this->actor->deactivate(prog.arg);
                break;
                case OP_ACTIVATE_OBJ:
                case OP_ACTIVATE_OBJ_ALT:
                    this->actor->activateTarget(prog.arg);
                break;
                case OP_MOVE_FLAG_TO_WORK_REGISTER:
                case OP_MOVE_FLAG_TO_WORK_REGISTER_ALT:
                    work_register = this->mission->mission->mission_data.flags[prog.arg];
                break;
                case OP_SAVE_VALUE_TO_GAMFLOW_REGISTER:
                    this->mission->gameflow_registers[prog.arg] = work_register;
                break;
                case OP_MOVE_WORK_REGISTER_TO_FLAG:
                    this->mission->mission->mission_data.flags[prog.arg] = (uint8_t) work_register;
                break;
                case OP_EXECUTE_CALL:
                    jump_to = work_register;
                    exec = false;
                break;
                case OP_EXEC_SUB_PROG:
                {
                    // No cycle detection on sub-program indices — a self- or
                    // mutually-referential OP_EXEC_SUB_PROG chain recurses
                    // forever and stack-overflows. Cap nesting depth as a
                    // last-resort guard against malformed/cyclic program data.
                    constexpr int kMaxSubProgDepth = 64;
                    if (this->depth >= kMaxSubProgDepth) {
                        printf("SCProg: sub-program nesting exceeded %d (prog_id=%u -> arg=%u), likely a cyclic reference — aborting this call\n",
                               kMaxSubProgDepth, (unsigned)this->prog_id, (unsigned)prog.arg);
                    } else if (prog.arg < this->mission->mission->mission_data.prog.size()) {
                        std::vector<PROG> *sub_prog = this->mission->mission->mission_data.prog[prog.arg];
                        SCProg *sub_prog_obj = new SCProg(this->actor, *sub_prog, this->mission, prog.arg, this->depth + 1);
                        sub_prog_obj->execute();
                        delete sub_prog_obj;
                    } else {
                        // Invalid sub-program index; no operation performed.
                    }
                }
                break;
                case OP_CMP_WORK_WITH_VALUE:
                {
                    compare_flag = prog_compare_return_values::PROG_CMP_UNSET;
                    bool is_equal = (work_register == prog.arg);
                    bool is_less = (work_register < prog.arg);
                    bool is_greater = (work_register > prog.arg);
                    
                    if (is_equal) {
                        compare_flag |= PROG_CMP_EQUAL;
                    }
                    if (is_less) {
                        compare_flag |= PROG_CMP_LESS;
                    }
                    if (is_greater) {
                        compare_flag |= PROG_CMP_GREATER;
                    }
                }
                break;
                // See OP_CMP_WORK_WITH_FLAG's own comment (SCenums.h) — same
                // as OP_CMP_WORK_WITH_VALUE, but the right-hand side is a
                // flag's current value instead of a literal baked into the
                // instruction.
                case OP_CMP_WORK_WITH_FLAG:
                {
                    compare_flag = prog_compare_return_values::PROG_CMP_UNSET;
                    int flag_value = this->mission->mission->mission_data.flags[prog.arg];
                    bool is_equal = (work_register == flag_value);
                    bool is_less = (work_register < flag_value);
                    bool is_greater = (work_register > flag_value);

                    if (is_equal) {
                        compare_flag |= PROG_CMP_EQUAL;
                    }
                    if (is_less) {
                        compare_flag |= PROG_CMP_LESS;
                    }
                    if (is_greater) {
                        compare_flag |= PROG_CMP_GREATER;
                    }
                }
                break;
                case OP_CMP_VALUE_WITH_WORK:
                {
                    compare_flag = prog_compare_return_values::PROG_CMP_UNSET;
                    bool is_equal = (prog.arg == work_register);
                    bool is_less = (prog.arg < work_register);
                    bool is_greater = (prog.arg > work_register);
                    
                    if (is_equal) {
                        compare_flag |= PROG_CMP_EQUAL;
                    }
                    if (is_less) {
                        compare_flag |= PROG_CMP_LESS;
                    }
                    if (is_greater) {
                        compare_flag |= PROG_CMP_GREATER;
                    }
                }
                break;
                case OP_BRANCH_IF_EQUAL:
                    if (compare_flag & prog_compare_return_values::PROG_CMP_EQUAL) {
                        jump_to = prog.arg;
                        exec = false;
                    }
                break;
                case OP_BRANCH_IF_NOT_EQUAL:
                    if (!(compare_flag & prog_compare_return_values::PROG_CMP_EQUAL)) {
                        jump_to = prog.arg;
                        exec = false;
                    }
                break;
                case OP_BRANCH_IF_LESS:
                    if (compare_flag & prog_compare_return_values::PROG_CMP_LESS) {
                        jump_to = prog.arg;
                        exec = false;
                    }
                break;
                case OP_BRANCH_IF_GREATER:
                    if (compare_flag & prog_compare_return_values::PROG_CMP_GREATER) {
                        jump_to = prog.arg;
                        exec = false;
                    }
                break;
                // See OP_BRANCH_IF_LESS_OR_EQUAL's own comment (SCenums.h).
                case OP_BRANCH_IF_LESS_OR_EQUAL:
                    if (compare_flag & (prog_compare_return_values::PROG_CMP_LESS | prog_compare_return_values::PROG_CMP_EQUAL)) {
                        jump_to = prog.arg;
                        exec = false;
                    }
                break;
                case OP_SELECT_NEXT_MISSION:
                // See OP_SELECT_NEXT_MISSION_ALT's own comment (SCenums.h)
                // — same MISN>MSGS-index-to-mission-basename mechanism,
                // just the only transition opcode some files use instead
                // of OP_SELECT_NEXT_MISSION/_END.
                case OP_SELECT_NEXT_MISSION_ALT:
                    this->mission->next_mission_message_index = prog.arg;
                break;
                case OP_SELECT_NEXT_MISSION_END:
                    // Always observed right after OP_SELECT_NEXT_MISSION with
                    // arg 0 — see that opcode's own comment. No confirmed
                    // effect; no-op for now.
                break;
                case OP_SELECT_FLAG_208:

                    // This opcode is a no-op in the original game, possibly reserved for future use.

                break;
                // See OP_SET_TARGET_DISABLED's own comment (SCenums.h).
                case OP_GET_TARGET_ENGINE_HEALTH:
                    work_register = 100;
                    for (auto test_actor: this->mission->actors) {
                        if (test_actor->actor_id == prog.arg) {
                            work_register = 100 - (int)test_actor->component_damage[(size_t)ShipComponent::Engine];
                            break;
                        }
                    }
                break;
                // See OP_GET_MINE_COUNT_AT_NAVPOINT's own comment
                // (SCenums.h). arg is a 1-based ordinal among this
                // mission's own JUBOUY actors (nav-point buoys), not an
                // actor_id — resolved generically here rather than
                // hardcoding MISNJ002's specific actor id numbers.
                case OP_GET_MINE_COUNT_AT_NAVPOINT: {
                    work_register = 0;
                    int navpoint_ordinal = 0;
                    for (auto test_actor: this->mission->actors) {
                        if (test_actor->object == nullptr || test_actor->object->member_name != "JUBOUY") {
                            continue;
                        }
                        navpoint_ordinal++;
                        if (navpoint_ordinal == prog.arg) {
                            work_register = test_actor->mines_deployed;
                            break;
                        }
                    }
                }
                break;
                case OP_CLEAR_TARGET:
                    this->actor->releaseTarget();
                break;
                case OP_DIST_TO_TARGET:
                    work_register = this->actor->getDistanceToTarget(prog.arg);
                break;
                case OP_DIST_TO_SPOT:
                    work_register = this->actor->getDistanceToSpot(prog.arg);
                break;
                case OP_IS_TARGET_ALIVE:
                    for (auto test_actor: this->mission->actors) {
                        if (test_actor->actor_id == prog.arg) {
                            if (test_actor->object->alive == true) {
                                compare_flag = prog_compare_return_values::PROG_CMP_NOT_EQUAL;
                            } else {
                                compare_flag = prog_compare_return_values::PROG_CMP_EQUAL;
                            }
                            break;
                        }
                    }
                break;
                case OP_IS_TARGET_ACTIVE:
                    for (auto test_actor: this->mission->actors) {
                        if (test_actor->actor_id == prog.arg) {
                            if ((test_actor->is_active) || (test_actor->actor_name == "PLAYER")) {
                                compare_flag = prog_compare_return_values::PROG_CMP_EQUAL;
                            } else {
                                compare_flag = prog_compare_return_values::PROG_CMP_NOT_EQUAL;
                            }
                            break;
                        }
                    }
                break;
                // See OP_IS_TARGET_CLOAKED's own comment (SCenums.h).
                case OP_IS_TARGET_CLOAKED:
                    for (auto test_actor: this->mission->actors) {
                        if (test_actor->actor_id == prog.arg) {
                            if (test_actor->plane != nullptr && test_actor->plane->cloaked) {
                                compare_flag = prog_compare_return_values::PROG_CMP_EQUAL;
                            } else {
                                compare_flag = prog_compare_return_values::PROG_CMP_NOT_EQUAL;
                            }
                            break;
                        }
                    }
                break;
                // See OP_SET_TARGET_DISABLED's own comment (SCenums.h).
                case OP_SET_TARGET_DISABLED:
                    for (auto test_actor: this->mission->actors) {
                        if (test_actor->actor_id == prog.arg) {
                            test_actor->is_disabled = true;
                            break;
                        }
                    }
                break;
                case OP_SET_FLAG_TO_TRUE:
                case OP_SET_FLAG_TO_TRUE_ALT:
                    this->mission->mission->mission_data.flags[prog.arg] = 1;
                break;
                case OP_SET_FLAG_TO_FALSE:
                    this->mission->mission->mission_data.flags[prog.arg] = 0;
                break;
                case OP_TEST_FLAG:
                    if (this->mission->mission->mission_data.flags[prog.arg] != 0) {
                        compare_flag = prog_compare_return_values::PROG_CMP_EQUAL;
                    } else {
                        compare_flag = prog_compare_return_values::PROG_CMP_NOT_EQUAL;
                    }
                break;
                case OP_MUL_VALUE_WITH_WORK:
                    work_register *= prog.arg;
                break;
                case OP_ACTIVATE_SCENE:
                {
                    int scene_id=0;
                    for (auto scen: this->mission->mission->mission_data.scenes) {
                        if (scene_id == prog.arg) {
                            scen->is_active = 1;
                            break;
                        }
                        scene_id++;
                    }
                }
                break;
                case OP_DEACTIVATE_SCENE:
                {
                    int scene_id=0;
                    for (auto scen: this->mission->mission->mission_data.scenes) {
                        if (scene_id == prog.arg) {
                            scen->is_active = 0;
                            break;
                        }
                        scene_id++;
                    }
                }
                break;
                default:
                break;
            }
        }
        i++;
    }
}