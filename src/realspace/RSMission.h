#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
}
#endif

#include "../commons/IFFSaxLexer.h"
#include "TreArchive.h"
#include "RSEntity.h"
#include "RSProf.h"
#include <stdint.h>
#include <string>
#include <algorithm>


struct AREA {
    int id;
    unsigned char AreaType;
    char AreaName[33];
    Vector3D position;
    unsigned int AreaWidth;
    unsigned int AreaHeight;
    std::vector<uint8_t> unknown_bytes;
};

struct MISN_PART {
    // Full 16-bit LE value — was uint8_t, which silently truncated the
    // second byte on assignment (`id |= byte << 8` into a uint8_t drops the
    // high byte). Confirmed empirically: a padding/unused record in
    // misna001 has raw bytes 00 01 (id=256, out of range of any real CAST
    // index), but with the 8-bit truncation it read as id=0 — colliding
    // with the real PLAYER part (also id=0) and overwriting
    // WC3Mission::player with a bogus non-existent-entity actor after the
    // real player had already been built correctly.
    uint16_t id{0};
    // 2 bytes between id and member_name that the parser previously skipped
    // entirely, shifting every following field 2 bytes early and corrupting
    // member_name for any record after the first (confirmed empirically:
    // without this field, "HELLCATP" — a real DATA\OBJECTS\HELLCATP.IFF
    // entity — decoded as garbage). Meaning not yet determined.
    uint16_t unknown0{0};
    std::string member_name;
	std::string member_name_destroyed;
	std::string weapon_load;
	uint8_t area_id{0};      
    uint8_t unknown1{0};
    int16_t unknown2{0};
    Vector3D position{0, 0, 0};
    uint8_t unknown3{0};
    uint16_t azymuth{0};
    uint16_t roll{0};
    uint16_t pitch{0};
    std::vector<uint8_t> progs_id;
    
    uint8_t on_is_activated{255};
    uint8_t on_mission_update{255};
    uint8_t on_is_destroyed{255};
    uint8_t on_missions_init{255};

    std::vector<uint8_t> unknown_bytes;
    RSEntity *entity{nullptr};
    bool alive{true};
    // True once this part's area-relative position has already been
    // resolved to an absolute world position (WC3Mission::loadMission()
    // does this once for every part, real or scene-synthesized, at load
    // time). Guards SCMission::update()'s own scene-activation position
    // correction from re-applying the same area offset a second time —
    // without this, any actor a scene's cast list references (e.g. WC3's
    // VICA1/TDESTRO2, already positioned at load) gets its position pushed
    // out by a second copy of the area offset the first time its scene
    // activates, landing it far from where it actually spawned.
    bool area_corrected{false};
};

struct SPOT {
    int id;
    short area_id;
    Vector3D position;
    Vector3D origin;
    uint8_t unknown1;
    uint8_t unknown2;
};
struct MSGS {
    char message[255];
    int id;
};
struct CAST {
    std::string actor;
    RSProf *profile;
    // 4 bytes following each actor name that the parser used to skip
    // entirely (see parseMISN_CAST) — meaning not yet determined.
    uint32_t unknown0{0};
};
// Real on-disk instruction is 8 bytes: opcode(1) + pad(1, always 0) +
// a constant 2-byte marker (0xED,0xFE, invariant across all ~11600
// instructions sampled across every mission in the game) + a 4-byte
// signed LE value. The engine previously read this as a 2-byte
// [opcode][1-byte arg] pair (matching Strike Commander's own, narrower
// instruction format) — silently truncating every argument to its low
// byte and misinterpreting every other "instruction" as the tail of the
// previous one's value field. `arg` is kept as the field name (not
// renamed to `value`) purely to avoid touching every one of its ~60
// call sites in SCProg.cpp; its type is now wide enough to hold the
// real value.
struct PROG {
    uint8_t opcode;
    int32_t arg;
};
struct MISN_SCEN {
    uint8_t is_active {0};
    uint8_t has_been_activated {0};
    int16_t area_id {0};
    std::vector<int16_t> progs_id;
    int16_t on_is_activated {-1};
    int16_t on_mission_update {-1};
    int16_t on_leaving {-1};
    bool is_coord_on_area {false};
    std::vector<uint8_t> unknown_bytes;
    // CAST array indices (same numbering as MISN_PART::id) this scene
    // spawns/activates — was previously misread as a byte+padding-byte
    // list; the real on-disk encoding is 4-byte little-endian integers
    // (see RSMission::parseMISN_PLAY_SCEN).
    std::vector<int32_t> cast;
    // World position (already scale-corrected like MISN_PART::position)
    // embedded in the scene, for cast members that have no PART record of
    // their own — e.g. misna001's TDESTROY/TCRUISER/HOBBES, which only
    // exist via their scene entry, not a PART. (0,0,0) together with
    // area_id==-1 means "no fixed position" — WC3Mission::loadMission()
    // treats that as spawning alongside the player instead (confirmed live:
    // Hobbes starts next to the player in the hangar).
    Vector3D spawn_position{0, 0, 0};
};
// MISN>NAV>VGA|SVGA — real per-mission nav-map data (background image,
// player-position icon, and localized title/objective-label text),
// byte-verified against MISNA001.IFF's raw chunks. Previously entirely
// unparsed (no "NAV " handler was ever registered), so the nav-map screen
// had no real background image, icon, or mission-native text to draw at
// all — see SCNavMap.cpp's own comments on the crash this caused and the
// procedural-geometry-only fallback that shipped before this existed.
struct MISN_NAV_RES {
    // DATA: (0,0,width,height,100,mode,?,"PLYR") — VGA is 320x200, SVGA is
    // 640x480, confirmed via MISNA001.IFF's raw DATA chunk bytes.
    int32_t width{0};
    int32_t height{0};
    // TEXT/TEXF/TEXG: English/French/German — 4 fixed 40-byte records each
    // (8-byte int32 pair, unconfirmed meaning — not screen coordinates or
    // string length in any pattern that's matched so far, so left
    // unparsed/unused rather than guessed — followed by a 32-byte
    // null-padded string): [0] a fixed title ("Navigation Map"/"Carte de
    // Navigation"/"Navigationskarte"), [1]-[3] printf-style '%s' format
    // strings for the mission/objective/notes lines ("Mission : %s" /
    // "Objective : %s" / "Notes : %s"). Byte-verified against
    // MISNA001.IFF.
    std::vector<std::string> textLines_en;
    std::vector<std::string> textLines_fr;
    std::vector<std::string> textLines_de;
    // OBJT/OBJF/OBJG: 10 fixed 32-byte localized objective-type labels,
    // indexed the same way as the takeOff/land/flyToWaypoint/flyToArea/
    // destroyTarget/defendTarget/defendArea/followAlly prog-op family
    // already implemented on SCMissionActors (see SCMissionActors.h) —
    // index 0 is "Unknown"/"Inconnu"/"Unbekannt" (no specific objective
    // type), 1-9 map to those ops in the same order the raw chunk lists
    // them (Take Off, Landing, Fly to Precise Way[point], Fly to Way
    // Point, Fly to Way Area, Destroy Target, Defend Ally, Defend Point,
    // Follow Leader).
    std::vector<std::string> objectiveLabels_en;
    std::vector<std::string> objectiveLabels_fr;
    std::vector<std::string> objectiveLabels_de;
    // PLYR/VDU: both raw "1.11" WC3 shape-pak entries (see RSWC3Shape.h —
    // the same decoder already used for cockpit instrument art and the
    // options-screen VFX assets), not a new format. VDU is the actual
    // star-system background map image; PLYR is the small player-position
    // marker icon.
    RSImageSet *playerIcon{nullptr};
    RSImageSet *background{nullptr};
};
struct MISN {
    uint16_t version;
    std::string info;
    uint8_t tune;
    std::string name;
    // COCK: mission-specified cockpit variant (e.g. "medpit" -> DATA\COCKPITS\
    // MEDPIT.IFF), overriding whatever the player's own ship entity declares.
    // Not read by the original Strike Commander mission format at all — WC3-
    // specific.
    std::string cockpit_name;
    std::string world_filename;
    std::vector<AREA *> areas;
    std::vector<SPOT *> spots;
    std::vector<std::string *> messages;
    // MSGF/MSGG: French/German translations of `messages` above, sibling
    // chunks alongside MISN>MSGS (same null-separated-string-list shape,
    // same per-mission ordering) — the mission-level counterpart of the
    // MSGG/MSGF pattern already confirmed on PROF>RADI (see RSProf.cpp's
    // parseRadiMessagesInto). Present in every WC3 mission file checked.
    std::vector<std::string *> messages_fr;
    std::vector<std::string *> messages_de;
    std::vector<int16_t> flags;
    std::vector<CAST *> casting;
    std::vector<std::vector<PROG> *> prog;
    std::vector<uint8_t> nums;
    std::vector<MISN_PART *> parts;
    std::vector<uint16_t> team;
    std::vector<MISN_SCEN *> scenes;
    std::vector<uint8_t> load;
    // See MISN_NAV_RES's own comment — previously entirely unparsed.
    MISN_NAV_RES navVGA;
    MISN_NAV_RES navSVGA;
};
class RSMission {
public:
    std::unordered_map<std::string, RSEntity *> *objCache;
    TreArchive *tre;
    
    MISN mission_data;
    RSMission();
    ~RSMission();

    MISN_PART *getPlayerCoord();
    MISN_PART *getObject(const char *name);
    void InitFromRAM(uint8_t *data, size_t size);

protected:
    MISN_PART *player{nullptr};
    virtual void paseMissionScript(uint8_t *data, size_t size);
    void parseMISN(uint8_t *data, size_t size);
    void parseMISN_VERS(uint8_t *data, size_t size);
    void parseMISN_INFO(uint8_t *data, size_t size);
    void parseMISN_TUNE(uint8_t *data, size_t size);
    void parseMISN_NAME(uint8_t *data, size_t size);
    void parseMISN_COCK(uint8_t *data, size_t size);
    void parseMISN_WRLD(uint8_t *data, size_t size);
    void parseMISN_AREA(uint8_t *data, size_t size);
    void parseMISN_SPOT(uint8_t *data, size_t size);
    void parseMISN_NUMS(uint8_t *data, size_t size);
    void parseMISN_MSGS(uint8_t *data, size_t size);
    void parseMISN_MSGF(uint8_t *data, size_t size);
    void parseMISN_MSGG(uint8_t *data, size_t size);
    void parseMISN_MSGS_into(uint8_t *data, size_t size, std::vector<std::string *> &target);
    void parseMISN_FLAG(uint8_t *data, size_t size);
    void parseMISN_CAST(uint8_t *data, size_t size);
    void parseMISN_PROG(uint8_t *data, size_t size);
    void parseMISN_PART(uint8_t *data, size_t size);
    void parseMISN_TEAM(uint8_t *data, size_t size);
    void parseMISN_PLAY(uint8_t *data, size_t size);
    void parseMISN_LOAD(uint8_t *data, size_t size);
    void parseMISN_CACH(uint8_t *data, size_t size);
    void parseMISN_PLAY_SCEN(uint8_t *data, size_t size);
    void parseMISN_WRLD_FILE(uint8_t *data, size_t size);
    void parseMISN_NAV(uint8_t *data, size_t size);
    void parseMISN_NAV_RES(uint8_t *data, size_t size, bool isSVGA);
    void parseMISN_NAV_RES_DATA(uint8_t *data, size_t size, MISN_NAV_RES &out);
    void parseMISN_NAV_RES_TEXT(uint8_t *data, size_t size, std::vector<std::string> &lines);
    void parseMISN_NAV_RES_OBJ(uint8_t *data, size_t size, std::vector<std::string> &labels);
};