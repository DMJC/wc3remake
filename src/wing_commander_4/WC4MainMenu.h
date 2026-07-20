#pragma once
#include "../strike_commander/precomp.h"

class WC4MainMenu : public IActivity {
public:
    WC4MainMenu();
    ~WC4MainMenu();
    void init();
    void runFrame(void);
};
