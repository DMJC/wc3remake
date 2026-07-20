#pragma once
#include "../strike_commander/precomp.h"

class WC3Mission : public SCMission {
public:
    WC3Mission();
    WC3Mission(std::string mission_name, std::unordered_map<std::string, RSEntity*>* objCache);
    ~WC3Mission();
    void loadMission() override;
    void update() override;
    // WC3's pilot/AI profile IFFs (PLAYER.IFF, HOBBES.IFF, etc.) live under
    // DATA\PROFILE\, not DATA\INTEL\ (SCMission::LoadProfile's hardcoded
    // path — correct for Strike Commander's own layout, wrong for WC3's).
    // Not virtual in the base class; this hides it for calls made through a
    // WC3Mission (or WC3Mission*) — which loadMission() below always is.
    RSProf* LoadProfile(std::string name);

    bool is_space_mission{true};

private:
    // Builds and registers (pushes into this->actors, sets this->player if
    // applicable) an actor for one PART/CAST pairing. Shared by the normal
    // PART-driven construction pass and the scene-fallback pass (for cast
    // members that have no PART record of their own — see loadMission()).
    SCMissionActors* buildActorFromPart(MISN_PART* part, CAST* cast);
};
