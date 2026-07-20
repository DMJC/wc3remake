//
//  RSProf.h
//  libRealSpace
//
//  Created by Rémi LEONARD on 27/09/2024.
//  Copyright (c) 2013 Fabien Sanglard. All rights reserved.
//
#pragma once

#include "../commons/IFFSaxLexer.h"

struct RADI_INFO {
    uint16_t id;
    std::string name;
    std::string callsign;
};
// index -> DATA\MOVIES\<name>.mve radio-FMV clip filename (a "radio face"
// video clip shown alongside a wingman's spoken line, e.g. sc_575a.mve).
struct RADI_FMV {
    uint8_t index;
    std::string filename;
};
struct RADI {
    uint16_t spch;
    RADI_INFO info;
    std::unordered_map<std::uint8_t, std::string> msgs;
    // German/French text variants of msgs, same key space (RADI codes) —
    // seen as sibling MSGG/MSGF chunks alongside MSGS in WC3's own
    // MISSIONS.TRE-embedded pilot profiles.
    std::unordered_map<std::uint8_t, std::string> msgg;
    std::unordered_map<std::uint8_t, std::string> msgf;
    std::unordered_map<std::string, std::string> asks;
    std::vector<std::string> asks_vector;
    std::vector<char> opts;
    // RADI-code -> raw 8-bit unsigned mono PCM radio-comm audio (death
    // screams, damage-status reports, enemy-spotted/engaging calls, etc —
    // see the RADI code table in WC3Mission/combat-event callers). Only
    // present on WC3's own MISSIONS.TRE-embedded pilot profiles; SC's
    // DATA\PROFILE\ profiles instead resolve voice indirectly via
    // spch+SPEECH.PAK (see RSSound).
    std::unordered_map<std::uint8_t, std::vector<uint8_t>> sond;
    std::vector<RADI_FMV> fmv;
};
struct AI_ATTR {
    uint8_t TH{0};
    uint8_t CN{0};
    uint8_t VB{0};
    uint8_t LY{0};
    uint8_t FL{0};
    uint8_t AG{0};
    uint8_t AA{0};
    uint8_t SM{0};
    uint8_t AR{0};
};
struct AI_STATE {
    uint8_t node_id;
    uint8_t value;
};
struct AI {
    std::vector <AI_STATE> mvrs;
    std::vector <uint8_t> goal;
    AI_ATTR atrb;
    bool isAI{false};
};

class RSProf {
private:
    void parsePROF(uint8_t *data, size_t size);
    void parsePROF_VERS(uint8_t *data, size_t size);
    void parsePROF_RADI(uint8_t *data, size_t size);
    void parsePROF_RADI_INFO(uint8_t *data, size_t size);
    void parsePROF_RADI_OPTS(uint8_t *data, size_t size);
    void parsePROF_RADI_MSGS(uint8_t *data, size_t size);
    void parsePROF_RADI_MSGG(uint8_t *data, size_t size);
    void parsePROF_RADI_MSGF(uint8_t *data, size_t size);
    void parseRadiMessagesInto(uint8_t *data, size_t size, std::unordered_map<std::uint8_t, std::string> &target);
    void parsePROF_RADI_ASKS(uint8_t *data, size_t size);
    void parsePROF_RADI_SPCH(uint8_t *data, size_t size);
    void parsePROF_RADI_SOND(uint8_t *data, size_t size);
    void parsePROF_RADI_FMV(uint8_t *data, size_t size);
    void parsePROF__AI_(uint8_t *data, size_t size);
    void parsePROF__AI_AI(uint8_t *data, size_t size);
    void parsePROF__AI_MVRS(uint8_t *data, size_t size);
    void parsePROF__AI_GOAL(uint8_t *data, size_t size);
    void parsePROF__AI_ATRB(uint8_t *data, size_t size);


public:

    uint16_t version{0};
    RADI radi;
    AI ai;

    RSProf();
    ~RSProf();
    void InitFromRAM(uint8_t *data, size_t size);

    // Wraps radi.sond[code]'s headerless raw PCM in a minimal 44-byte WAV
    // header so it can be handed to RSMixer::playSoundVoc (which loads
    // through SDL_mixer's Mix_LoadWAV_RW and needs a recognized container).
    // Built and cached on first request — the returned pointer stays valid
    // for the lifetime of this RSProf, so callers (e.g. a MemSound wrapper)
    // can point directly at it without copying. Returns nullptr if this
    // profile has no clip for that code.
    const std::vector<uint8_t>* getRadiSoundWav(uint8_t code);

private:
    std::unordered_map<std::uint8_t, std::vector<uint8_t>> radiSoundWavCache;
};