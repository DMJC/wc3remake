#pragma once
#include "../strike_commander/precomp.h"

class WC3MainMenu : public IActivity {
public:
    WC3MainMenu();
    ~WC3MainMenu();
    void init();
    void runFrame(void);
};
