# PROG chunk OPCODE LIST for missions

## Introduction

This document details the opcodes used in the PROG chunk of Strike Commander mission files. These opcodes control AI actor behavior and define mission logic.

## Instruction Format

Each instruction is **8 bytes**: `opcode(1) + pad(1, always 0) + marker(2, always the constant bytes 0xED,0xFE) + value(4, signed little-endian)`.

Confirmed by checking every one of the ~11,600 `PROG` instructions across every WC3 mission file in the game — the 2-byte marker is invariant (100% `0xED 0xFE`) at the same offset in every single instruction, with no exceptions. A sub-program ends on an all-zero 8-byte instruction (`opcode=0`, `value=0`).

This is wider than it first appears from a naive 2-byte `[opcode][1-byte arg]` read (which is what an earlier version of the parser assumed) — that misreading silently truncated every argument to its low byte and misinterpreted the rest of each 8-byte instruction as extra, bogus "instructions." If you're referencing raw hex dumps against this table, make sure you're grouping by 8, not 2.

## Opcode Categories

### Flow Control

| Dec | Hex | Opcode | Description |
|-----|-----|--------|-------------|
| 1 | 0x01 | OP_EXIT_PROG | Immediately terminates program execution |
| 8 | 0x08 | OP_SET_LABEL | Defines a label point with ID specified in the argument |
| 2 | 0x02 | OP_EXEC_SUB_PROG | Executes a subprogram referenced by the ID in the argument |
| 70 | 0x46 | OP_GOTO_IF_CURRENT_COMMAND_IN_PROGRESS | If the current command is in progress, jumps to specified label |
| 72 | 0x48 | OP_BRANCH_IF_EQUAL | If the last comparison resulted in equality, jumps to specified label |
| 73 | 0x49 | OP_BRANCH_IF_NOT_EQUAL | If the last comparison did not result in equality, jumps to specified label |
| 74 | 0x4A | OP_BRANCH_IF_LESS | If the last comparison resulted in "less than", jumps to specified label |
| 75 | 0x4B | OP_BRANCH_IF_GREATER | If the last comparison resulted in "greater than", jumps to specified label |
| 79 | 0x4F | OP_EXECUTE_CALL | Jumps to the label number stored in the work register |

### Registers and Flags

| Dec | Hex | Opcode | Description |
|-----|-----|--------|-------------|
| 16 | 0x10 | OP_MOVE_VALUE_TO_WORK_REGISTER | Loads an immediate value into the work register |
| 17 | 0x11 | OP_MOVE_FLAG_TO_WORK_REGISTER | Loads a flag value into the work register |
| 20 | 0x14 | OP_SAVE_VALUE_TO_GAMFLOW_REGISTER | Saves the work register to a game flow register |
| 80 | 0x50 | OP_MOVE_WORK_REGISTER_TO_FLAG | Saves the work register to a flag |
| 82 | 0x52 | OP_SET_FLAG_TO_TRUE | Sets a flag to 1 (true) |
| 83 | 0x53 | OP_SET_FLAG_TO_FALSE | Sets a flag to 0 (false) |
| 85 | 0x55 | OP_ADD_1_TO_FLAG | Increments a flag by 1 |
| 86 | 0x56 | OP_REMOVE_1_TO_FLAG | Decrements a flag by 1 |
| 35 | 0x23 | OP_ADD_WORK_REGISTER_TO_FLAG | Adds the work register value to the specified flag |
| 46 | 0x2E | OP_MUL_VALUE_WITH_WORK | Multiplies the work register by the specified value |

### Tests and Comparisons

| Dec | Hex | Opcode | Description |
|-----|-----|--------|-------------|
| 64 | 0x40 | OP_CMP_WORK_WITH_VALUE | Compares the work register with a value |
| 65 | 0x41 | OP_CMP_VALUE_WITH_WORK | Compares a value with the work register |
| 69 | 0x45 | OP_TEST_FLAG | Tests if a flag is non-zero |
| 146 | 0x92 | OP_IF_TARGET_IN_AREA | Checks if the target is in the same area |
| 147 | 0x93 | OP_IS_TARGET_ALIVE | Checks if the target is alive |
| 152 | 0x98 | OP_IS_TARGET_ACTIVE | Checks if the target is active |
| 149 | 0x95 | OP_DIST_TO_TARGET | Calculates distance to target and stores it in the work register |
| 151 | 0x97 | OP_DIST_TO_SPOT | Calculates distance to a spot and stores it in the work register |

### Mission Objectives

| Dec | Hex | Opcode | Description |
|-----|-----|--------|-------------|
| 160 | 0xA0 | OP_SET_WAIT_FOR_SECONDS | Waits the specified number of seconds before the command is considered complete (present in code's opcode enum/switch; was missing from this doc) |
| 161 | 0xA1 | OP_SET_OBJ_TAKE_OFF | Sets objective to take off from the specified waypoint |
| 162 | 0xA2 | OP_SET_OBJ_LAND | Sets objective to land at the specified waypoint |
| 165 | 0xA5 | OP_SET_OBJ_FLY_TO_WP | Sets objective to fly to a specified waypoint |
| 166 | 0xA6 | OP_SET_OBJ_FLY_TO_AREA | Sets objective to fly to a specified area |
| 167 | 0xA7 | OP_SET_OBJ_DESTROY_TARGET | Sets objective to destroy a specified target |
| 168 | 0xA8 | OP_SET_OBJ_DEFEND_TARGET | Sets objective to defend a specified target |
| 169 | 0xA9 | OP_SET_OBJ_DEFEND_AREA | Sets objective to defend a specified area |
| 170 | 0xAA | OP_SET_OBJ_FOLLOW_ALLY | Sets objective to follow a specified ally |
| 171 | 0xAB | OP_SET_MESSAGE | Sets a message for a waypoint in the navigation map |

### Actions on Actors and Scene

| Dec | Hex | Opcode | Description |
|-----|-----|--------|-------------|
| 144 | 0x90 | OP_ACTIVATE_OBJ | Activates a target (enables AI) |
| 148 | 0x94 | OP_INSTANT_DESTROY_TARGET | Instantly destroys a target, may trigger an explosion |
| 190 | 0xBE | OP_DEACTIVATE_OBJ | Deactivates a target (disables AI) |
| 128 | 0x80 | OP_ACTIVATE_SCENE | Activates a scene matching the specified area ID |
| 129 | 0x81 | OP_DEACTIVATE_SCENE | De activates a scene matching the specified area ID |

### Miscellaneous

| Dec | Hex | Opcode | Description |
|-----|-----|--------|-------------|
| 9 | 0x09 | OP_SPOT_DATA | Selects spot data |
| 208 | 0xD0 | OP_SELECT_FLAG_208 | Unknown for the moment |

## Inferred / Unconfirmed Opcodes

Not in the game's opcode enum (`SCenums.h`) or handled by `SCProg.cpp` — these fall through to a no-op `default:` today. Derived from statistical analysis (frequency, value range, and which known opcode most often immediately precedes/follows each one) across every `PROG` chunk in the game, using the corrected 8-byte instruction format above. **None of these are confirmed against the original executable** — treat as a starting hypothesis for further work (e.g. dosbox-debug tracing), not fact.

| Dec | Hex | Count | Context pattern | Best guess |
|-----|-----|-------|------------------|------------|
| 172 | 0xAC | 479 | Follows a `SET_OBJ_*` call 88% of the time (`SET_OBJ_DESTROY_TARGET`, `SET_OBJ_LAND`, `SET_OBJ_TAKE_OFF`, ...); values look like waypoint/objective indices | Links the objective just set to a nav-map waypoint number — natural companion to `OP_SET_MESSAGE` (171, "sets a message for a waypoint in the navigation map") |
| 153 | 0x99 | 101 | Followed by `OP_SPOT_DATA` 100% of the time | A spot-selector immediately consumed by `SPOT_DATA` |
| 173 | 0xAD | 52 | Followed by `OP_SPOT_DATA` 100% of the time | Same role as 153 — possibly a second spot-selection mode (e.g. absolute vs. relative) |
| 150 | 0x96 | 46 | Followed by `OP_BRANCH_IF_EQUAL`/`OP_BRANCH_IF_NOT_EQUAL` 100% of the time | A comparison/test opcode feeding the branch, parallel to `OP_TEST_FLAG` but testing something else |
| 196 | 0xC4 | 239 | Follows `OP_MOVE_FLAG_TO_WORK_REGISTER`/`OP_MOVE_VALUE_TO_WORK_REGISTER`; often immediately followed by opcode 193 | Part of a comparison/action pair, distinct from `OP_CMP_*` |
| 193 | 0xC1 | 124 | Follows opcode 196 in ~36% of its occurrences; otherwise follows `OP_SET_LABEL`/branch opcodes | Second half of the 196/193 pair, or a standalone conditional action |
| 224 | 0xE0 | 105 | Only ever takes value 0 or 1; sits among `OP_BRANCH_IF_*`/`OP_SET_FLAG_*` | A boolean flag setter/getter |
| 182 | 0xB6 | 132 | Tightly clusters with opcodes 184 and 225 (each commonly precedes/follows the other two); dominated by value `-1` | Part of a related setup/teardown triplet; `-1` matches the "unset" sentinel convention used elsewhere in the format (e.g. `SPOT.area_id`) |
| 184 | 0xB8 | 96 | See 182 | See 182 |
| 225 | 0xE1 | 69 | See 182 | See 182 |



1. Objectives (OP_SET_OBJ_*) define a task for an actor and return "true" when completed.

2. Control flow opcodes work with a system of labels and jumps. When a jump is requested, the `exec` variable is set to false until a matching label is found.

3. The work register (`work_register`) is used for temporary calculations and data transfers.

4. Flags (`flags`) are stored in mission data and can be read, modified, and tested by opcodes.

5. Comparison opcodes set a `compare_flag` that can be tested by conditional branch opcodes.

## Team Usage

It appears that teams are defined in the mission file as follows:
- The first ushort (16 bits) is the leader
- The value is the index of the part in the mission file (not the actual ID)