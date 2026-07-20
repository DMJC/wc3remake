#pragma once

#include "precomp.h"


class SCProg {
public:
    SCMissionActors *actor{nullptr};
    std::vector<PROG> prog;
    SCMission *mission{nullptr};
    uint8_t prog_id{0};
    std::unordered_map<uint8_t, size_t> labels;
    // Nesting depth of OP_EXEC_SUB_PROG calls that led to this instance (0 =
    // top-level). Recursion has no cycle detection (self- or mutually-
    // referential sub-programs recurse forever) — see execute()'s own guard.
    int depth{0};

    SCProg(SCMissionActors *profile, std::vector<PROG> prog, SCMission *mission, uint8_t prog_id = 0, int depth = 0) {
        this->actor = profile;
        this->prog = prog;
        this->mission = mission;
        this->prog_id = prog_id;
        this->depth = depth;
        this->labels.clear();
    };
    ~SCProg() {
        this->prog.clear();
        this->prog.shrink_to_fit();
        this->labels.clear();
        this->actor = nullptr;
        this->mission = nullptr;
    };
    void execute();
};