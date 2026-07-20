#pragma once
#include "../strike_commander/precomp.h"
#include "WC4Mission.h"

class WC4Strike : public SCStrike {
public:
    WC4Strike();
    ~WC4Strike();
    void setMission(char const* missionName);
    void runFrame(void);

private:
    void updateCloakEffect();
    VGAPalette savedColorPalette;
    VGAPalette* bwPalette{nullptr};
};
