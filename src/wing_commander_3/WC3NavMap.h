#pragma once
// Not a direct #include "../strike_commander/SCNavMap.h": that file's own
// #include "precomp.h" sits before its class body, so entering via it
// directly (instead of through precomp.h's own already-correct SCNavMap.h-
// before-SCStrike.h ordering) leaves SCNavMap undeclared by the time
// SCStrike.h is reached from within that same chain — a real link/compile
// failure hit while first wiring this up. Matches WC3Strike.h's own
// #include "../strike_commander/precomp.h" for the same reason.
#include "../strike_commander/precomp.h"

// WC3-specific override of SCNavMap's empty text-overlay hook — see
// SCNavMap::drawWC3TextOverlay's own comment for why this can't live in
// strike_commander/ directly (WC3Font/WC3Globals are wing_commander_3/
// classes; SC1's own nav map draws text via RSFont, which WC3 has no
// equivalent asset for at all).
class WC3NavMap : public SCNavMap {
protected:
    void drawWC3TextOverlay(float center, float map_width, int w, int h, int t, int l) override;
};
