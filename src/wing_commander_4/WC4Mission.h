#pragma once
#include "../strike_commander/precomp.h"

class WC4Mission : public SCMission {
public:
    WC4Mission();
    WC4Mission(std::string mission_name, std::unordered_map<std::string, RSEntity*>* objCache);
    ~WC4Mission();
    void loadMission() override;
    void update() override;

    bool is_space_mission{true};
};
