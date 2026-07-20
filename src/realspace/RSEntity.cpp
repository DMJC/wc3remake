//
//  RSEntity.cpp
//  libRealSpace
//
//  Created by fabien sanglard on 12/29/2013.
//  Copyright (c) 2013 Fabien Sanglard. All rights reserved.
//


#include <cfloat>
#include <algorithm>
#include "RSEntity.h"
#include "RLEShape.h"
#include "RSImageSet.h"
#include "RSWC3Shape.h"
#include "RSPalette.h"
#include "../commons/RLEBuffer.h"
// weapon_ids/wc3_weapon_stats — same cross-module include already
// established by RSGameflow.cpp for this specific header (SCenums.h is
// header-only, no strike_commander runtime dependency, so it doesn't
// create the realspace->wing_commander_3-style layering problem noted
// elsewhere in this codebase).
#include "../strike_commander/SCenums.h"


RSEntity::~RSEntity() {
    while (!images.empty()) {
        RSImage *image = images.back();
        images.pop_back();
        delete image;
    }
}

void RSEntity::InitFromRAM(uint8_t *data, size_t size, std::string name) {
    this->name = name;
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["REAL"] = std::bind(&RSEntity::parseREAL, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
    CalcBoundingBox();
    calcWingArea();
}

BoudingBox *RSEntity::GetBoudingBpx(void) { return &this->bb; }

void RSEntity::CalcBoundingBox(void) {

    this->bb.min.x = FLT_MAX;
    this->bb.min.y = FLT_MAX;
    this->bb.min.z = FLT_MAX;

    this->bb.max.x = FLT_MIN;
    this->bb.max.y = FLT_MIN;
    this->bb.max.z = FLT_MIN;
    if (this->vertices.size() == 0)
        return;
    for (int i=0; i < this->lods[LOD_LEVEL_MAX].numTriangles; i++) {
        int triangle_id = this->lods[LOD_LEVEL_MAX].triangleIDs[i];
        char attribute = 'T';
        if (this->attrs.size() > 0) {
            if (this->attrs[triangle_id] != nullptr) {
                attribute = this->attrs[triangle_id]->type;
                triangle_id = this->attrs[triangle_id]->id;
            }
        }
        
        if (attribute == 'T') {
            for (int j = 0; j < 3; j++) {
                if (triangle_id >= this->triangles.size()) {
                    continue;
                }
                Point3D vertex = this->vertices[this->triangles[triangle_id].ids[j]];

                if (bb.min.x > vertex.x)
                    bb.min.x = vertex.x;
                if (bb.min.y > vertex.y)
                    bb.min.y = vertex.y;
                if (bb.min.z > vertex.z)
                    bb.min.z = vertex.z;

                if (bb.max.x < vertex.x)
                    bb.max.x = vertex.x;
                if (bb.max.y < vertex.y)
                    bb.max.y = vertex.y;
                if (bb.max.z < vertex.z)
                    bb.max.z = vertex.z;
            }
        } else if (attribute == 'Q') {
            for (int j = 0; j < 4; j++) {
                Point3D vertex = this->vertices[this->quads[triangle_id]->ids[j]];

                if (bb.min.x > vertex.x)
                    bb.min.x = vertex.x;
                if (bb.min.y > vertex.y)
                    bb.min.y = vertex.y;
                if (bb.min.z > vertex.z)
                    bb.min.z = vertex.z;

                if (bb.max.x < vertex.x)
                    bb.max.x = vertex.x;
                if (bb.max.y < vertex.y)
                    bb.max.y = vertex.y;
                if (bb.max.z < vertex.z)
                    bb.max.z = vertex.z;
            }
        }
        
    }
}

void RSEntity::calcWingArea(void) {
    if (this->vertices.size() == 0)
        return;
    if (this->jdyn == NULL)
        return;
    std::vector<int> wing_ids;
    int vert_id = 0;
    for (auto vertex: this->vertices) {
        if (vertex.z == bb.min.z) {
            wing_ids.push_back(vert_id);
        }
        if (vertex.z == bb.max.z) {
            wing_ids.push_back(vert_id);
        }
        vert_id++;
    }

    std::vector<Point2Df> wing_surface_points;
    for (int i=0; i < this->lods[LOD_LEVEL_MAX].numTriangles; i++) {
        int triangle_id = this->lods[LOD_LEVEL_MAX].triangleIDs[i];
        char attribute = 'T';
        
        if (this->attrs.size() > 0) {
            if (this->attrs[triangle_id] != nullptr) {
                attribute = this->attrs[triangle_id]->type;
                triangle_id = this->attrs[triangle_id]->id;

            }
        }
        if (attribute == 'T') {
            if (triangle_id >= this->triangles.size()) {
                continue;
            }
            Triangle triangle = this->triangles[triangle_id];
            for (auto id : triangle.ids) {
                if (std::find(wing_ids.begin(), wing_ids.end(), id) != wing_ids.end()) {
                    for (int j = 0; j < 3; j++) {
                        Vector3D v = this->vertices[triangle.ids[j]];
                        Point2Df p = {v.x*0.5f, v.z * 0.5f};
                        if (std::find(wing_surface_points.begin(), wing_surface_points.end(), p) == wing_surface_points.end()) {
                            wing_surface_points.push_back(p);
                        }
                    }
                }
            }
        } else {
            Quads *quad = this->quads[triangle_id];
            for (auto id : quad->ids) {
                if (std::find(wing_ids.begin(), wing_ids.end(), id) != wing_ids.end()) {
                    for (int j = 0; j < 4; j++) {
                        Vector3D v = this->vertices[quad->ids[j]];
                        Point2Df p = {v.x*0.5f, v.z * 0.5f};
                        if (std::find(wing_surface_points.begin(), wing_surface_points.end(), p) == wing_surface_points.end()) {
                            wing_surface_points.push_back(p);
                        }
                    }
                }
            }
        }
        
    }

    if (!wing_surface_points.empty()) {
        float area = 0.0f;
        
        // Compute the convex hull of wing_surface_points using the monotone chain algorithm.
        auto cross = [](const Point2Df &O, const Point2Df &A, const Point2Df &B) -> float {
            return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
        };

        // Tri des points par coordonnées (x, puis y)
        std::sort(wing_surface_points.begin(), wing_surface_points.end(), [](const Point2Df &a, const Point2Df &b) {
            return (a.x == b.x) ? (a.y < b.y) : (a.x < b.x);
        });

        std::vector<Point2Df> hull;
        // Construction de la chaîne inférieure
        for (const auto &p : wing_surface_points) {
            while (hull.size() >= 2 && cross(hull[hull.size()-2], hull.back(), p) <= 0)
                hull.pop_back();
            hull.push_back(p);
        }
        // Construction de la chaîne supérieure
        for (int i = wing_surface_points.size() - 2, t = hull.size() + 1; i >= 0; i--) {
            while (hull.size() >= t && cross(hull[hull.size()-2], hull.back(), wing_surface_points[i]) <= 0)
                hull.pop_back();
            hull.push_back(wing_surface_points[i]);
        }
        // Retirer le dernier point car il est identique au premier
        if (!hull.empty())
            hull.pop_back();
        
        
        wing_surface_points = hull;
        int n = wing_surface_points.size();
        // Calcul de la surface selon la formule du polygone (formule de shoelace)
        for (int i = 0; i < n; i++) {
            Vector3D sp1 = {wing_surface_points[i].x*2, 0.0f , wing_surface_points[i].y*2};
            Vector3D sp2 = {wing_surface_points[(i + 1) % n].x*2, 0.0f , wing_surface_points[(i + 1) % n].y*2};
        }
        for (int i = 0; i < n; i++) {
            int next = (i + 1) % n;
            area += wing_surface_points[i].x * wing_surface_points[next].y - wing_surface_points[i].y * wing_surface_points[next].x;
        }
        area = std::fabs(area) / 2.0f;
        this->wing_area = area;
    }
}
size_t RSEntity::NumImages(void) { return this->images.size(); }

size_t RSEntity::NumVertice(void) { return this->vertices.size(); }

size_t RSEntity::NumUVs(void) { return this->uvs.size(); }

size_t RSEntity::NumLods(void) { return this->lods.size(); }

size_t RSEntity::NumTriangles(void) { return this->triangles.size(); }

void RSEntity::AddImage(RSImage *image) { this->images.push_back(image); }

void RSEntity::AddVertex(Point3D *vertex) { this->vertices.push_back(*vertex); }

void RSEntity::AddUV(uvxyEntry *uv) { this->uvs.push_back(*uv); }

void RSEntity::AddLod(Lod *lod) { this->lods.push_back(*lod); }

void RSEntity::AddTriangle(Triangle *triangle) { this->triangles.push_back(*triangle); }

void RSEntity::parseREAL(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["OBJT"] = std::bind(&RSEntity::parseREAL_OBJT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["APPR"] = std::bind(&RSEntity::parseREAL_APPR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["INFO"] = std::bind(&RSEntity::parseREAL_INFO, this, std::placeholders::_1, std::placeholders::_2);
    
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_INFO(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSEntity::parseREAL_OBJT_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AFTB"] = std::bind(&RSEntity::parseREAL_OBJT_AFTB, this, std::placeholders::_1, std::placeholders::_2);
    handlers["JETP"] = std::bind(&RSEntity::parseREAL_OBJT_JETP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SSHP"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["GRND"] = std::bind(&RSEntity::parseREAL_OBJT_GRND, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ORNT"] = std::bind(&RSEntity::parseREAL_OBJT_ORNT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["EXTE"] = std::bind(&RSEntity::parseREAL_OBJT_EXTE, this, std::placeholders::_1, std::placeholders::_2);
    handlers["RNWY"] = std::bind(&RSEntity::parseREAL_OBJT_RNWY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DIST"] = std::bind(&RSEntity::parseREAL_OBJT_DIST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MISS"] = std::bind(&RSEntity::parseREAL_OBJT_MISS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MISL"] = std::bind(&RSEntity::parseREAL_OBJT_MISL, this, std::placeholders::_1, std::placeholders::_2);
    // WC3's standalone gun weapon files (e.g. NEUTGUN.IFF/TACHGUN.IFF) —
    // a top-level OBJT sub-tag distinct from SSHP/JETP/MISS/MISL, byte-
    // confirmed against 5 real gun files (see parseREAL_OBJT_GUNS_DATA).
    handlers["GUNS"] = std::bind(&RSEntity::parseREAL_OBJT_GUNS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["BOMB"] = std::bind(&RSEntity::parseREAL_OBJT_BOMB, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRCR"] = std::bind(&RSEntity::parseREAL_OBJT_TRCR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SWPN"] = std::bind(&RSEntity::parseREAL_OBJT_SWPN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMKG"] = std::bind(&RSEntity::parseREAL_OBJT_SMKG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["OMOB"] = std::bind(&RSEntity::parseREAL_OBJT_OMOB, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEBR"] = std::bind(&RSEntity::parseREAL_OBJT_DEBR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["PODR"] = std::bind(&RSEntity::parseREAL_OBJT_PODR, this, std::placeholders::_1, std::placeholders::_2);
    
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_AFTB(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::aftb;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXTE"] = std::bind(&RSEntity::parseREAL_OBJT_EXTE, this, std::placeholders::_1, std::placeholders::_2);
    handlers["APPR"] = std::bind(&RSEntity::parseREAL_OBJT_AFTB_APPR, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

void RSEntity::parseREAL_OBJT_AFTB_APPR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["AFTB"] = std::bind(&RSEntity::parseREAL_APPR_POLY, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}

void RSEntity::parseREAL_OBJT_MISS(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::missiles;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SIGN"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMOK"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SMOK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WDAT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_WDAT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DATA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM, this, std::placeholders::_1, std::placeholders::_2);


    lexer.InitFromRAM(data, size, handlers);
}
// WC3's own missile/torpedo/bomb weapon format (HSMISS.IFF, IRMISS.IFF,
// FFMISS.IFF, TORKMISS.IFF, TEMBMISS.IFF, ...) — see the MISL_DATA struct
// comment in RSEntity.h. A flat DATA+DYNM+EXPL+AFTB layout, matching WC3's
// general pattern of flatter sub-chunk trees than Strike Commander's own
// equivalent formats (c.f. OBJT>SSHP vs OBJT>JETP).
void RSEntity::parseREAL_OBJT_MISL(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::missiles;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_MISL_DATA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] = std::bind(&RSEntity::parseREAL_OBJT_MISL_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    // EXPL matches SSHP>EXPL>DATA's layout exactly (name(8) + 1 pad byte +
    // 2x int32LE) — confirmed byte-for-byte against HSMISS.IFF/TEMBMISS.IFF,
    // so reuse the existing SSHP handler rather than duplicating it.
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AFTB"] = std::bind(&RSEntity::parseREAL_OBJT_MISL_AFTB, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CLOK"] = std::bind(&RSEntity::parseREAL_OBJT_CLOK, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// 35-byte fixed record (same overall size as OBJT>GUNS>DATA, but a
// different layout), byte-checked against four real, user-supplied data
// points: Friend-or-Foe (damage 250, lock 0s), Image-Recognition (damage
// 350, lock 1s), Torpedo (damage 8000, lock 12s), capital-ship missile/
// CAPMISS.IFF (damage 60000, lock 5s).
//   offset  0-15 (16): display name, null-padded ("IFF MISSILE", "IMREC
//                       MISSILE", "BIG FUCKING MIS", ...)
//   offset    16  (1): flag1 -- "is_guided_flight" (WDAT), confirmed real
//                       against a 5th data point added later (2026-07
//                       session): 1 for every one of FF/IR/Torpedo/
//                       CapMiss/HSMISS (all of which fly), 0 for
//                       MINEMISS (which drops and stays stationary
//                       instead — see SCPlane::Shoot's ID_MINEMISS case
//                       and SCSimulatedObject::ComputeTrajectory).
//   offset    17  (1): flag2 -- unconfirmed (1 in every sample so far)
//   offset    18  (1): flag3 -- unconfirmed (0 for FF/IR/Torpedo/CapMiss,
//                       1 for HSMISS — possibly a homing-type marker, not
//                       confirmed)
//   offset 19-22 (u32 LE): damage -- direct value, confirmed exact across
//                          all 4 data points, including 60000 (needs the
//                          full u32, doesn't fit a smaller field)
//   offset 23-26 (u32 LE): unconfirmed (135/179/90/180 across the 4
//                          samples) — no real data point yet, stored raw
//                          and unused
//   offset    27  (1): unconfirmed, 0 in every sample so far
//   offset    28  (1): lock time required, in seconds -- direct value, no
//                      scaling, confirmed exact across all 4 data points
//   offset 29-31  (3): unconfirmed, 0 in every sample so far
//   offset    32  (1): NOT flight duration -- retracted. Looked like a
//                      direct-seconds duration against the first two data
//                      points (FF=20/IR=15, matching their stated
//                      durations), but Torpedo (16, stated duration 14)
//                      and CapMissile (16, stated duration 120) both
//                      contradict that reading — same raw byte (16) on
//                      two weapons with very different real durations, so
//                      this is coincidence, not the real field. True
//                      duration encoding is still unknown; kept as a raw,
//                      unused value (still named duration_seconds on
//                      WDAT, but treat it as unreliable — effective_range,
//                      the one thing derived from it, is a rough estimate
//                      at best for torpedo/capital-ship-class weapons).
//   offset 33-34  (2): unconfirmed, 0 in every sample so far
void RSEntity::parseREAL_OBJT_MISL_DATA(uint8_t *data, size_t size) {
    if (size < 35)
        return;
    ByteStream bs(data, size);
    WDAT *wdat = new WDAT();
    this->weapon_display_name = bs.ReadString(16);
    wdat->is_guided_flight = bs.ReadByte() != 0; // flag1 — see WDAT::is_guided_flight
    bs.ReadByte(); // flag2, unconfirmed
    bs.ReadByte(); // flag3, unconfirmed
    wdat->damage = (uint16_t)bs.ReadUInt32LE();
    wdat->unknown_misl_stat = bs.ReadUInt32LE();
    bs.ReadByte(); // unconfirmed
    wdat->lock_time_required_seconds = (float)bs.ReadByte();
    bs.MoveForward(3); // unconfirmed
    wdat->duration_seconds = (float)bs.ReadByte();
    this->wdat = wdat;
}
// 3x uint32LE, same field order/shape as SC1's own MISS>DYNM>MISS (which
// populates this same DYNN_MISS struct via a different 24-bit-field parser
// — parseREAL_OBJT_MISS_DYNM_MISS, unrelated scale/units, unconfirmed).
// For WC3's version specifically, confirmed via three real, user-supplied
// data points (Friend-or-Foe: speed 1200/accel 800/maneuverability 80deg-
// per-sec; Image-Recognition: speed 1600/accel 600/maneuverability
// 80deg-per-sec; Torpedo: speed 1000/accel 200/maneuverability 20deg-
// per-sec) that turn_degre_per_sec/256 = maneuverability (deg/s) and
// velovity_m_per_sec/256 = speed — the same /256 fixed-point scale
// already established elsewhere in this codebase (e.g.
// MISN_AREA_POSITION_SCALE, OBJT>GUNS>DATA's own refire_delay_seconds) —
// confirmed exact across all three weapons. proximity_cm's real meaning is
// acceleration, not a proximity-fuse distance (the field name is a legacy
// guess from before this was confirmed) — but its SCALE is weapon-type
// dependent, not a single universal factor: FF/IR (missiles) match at
// value/256, while Torpedo matches at the raw value directly (200 ==
// 200, no division) — kept as raw uint32 here rather than pre-dividing,
// since there's no single conversion that's correct for every weapon.
// proximity_cm's own field name is kept as-is (not renamed to
// "acceleration") since it's also read directly by SCSimulatedObject.cpp/
// SCMissionActors.cpp with their own established raw-value scale factors,
// unrelated to any of this — renaming risks implying a semantic change to
// those call sites that isn't intended.
void RSEntity::parseREAL_OBJT_MISL_DYNM(uint8_t *data, size_t size) {
    if (size < 12)
        return;
    ByteStream bs(data, size);
    DYNN_MISS *dynn_miss = new DYNN_MISS();
    dynn_miss->turn_degre_per_sec = bs.ReadUInt32LE();
    dynn_miss->velovity_m_per_sec = bs.ReadUInt32LE();
    dynn_miss->proximity_cm = bs.ReadUInt32LE();
    this->dynn_miss = dynn_miss;
}
void RSEntity::parseREAL_OBJT_MISL_AFTB(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_MISL_AFTB_DATA, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// u32 pointCount + name(8, e.g. "MISS5AFB") + pointCount x mount position —
// same idea as SSHP>AFTB>DATA (engine thrust-glow mount), but without that
// chunk's extra "constant 2" u32 between count and name. The trailing
// per-point byte layout (13 bytes for a pointCount of 1 across every
// missile checked) doesn't cleanly resolve to 3 clean int32LE values the
// way SSHP>AFTB's does — read as a best-effort 2x int32LE position here;
// not independently confirmed, same caveat as DEBR_PIECE's position field.
void RSEntity::parseREAL_OBJT_MISL_AFTB_DATA(uint8_t *data, size_t size) {
    if (size < 12)
        return;
    ByteStream bs(data, size);
    uint32_t pointCount = bs.ReadUInt32LE();
    AFTB *aftb = new AFTB();
    aftb->name = bs.ReadString(8);
    std::string tmpname = assetsManager.object_root_path + aftb->name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        aftb->objct = new RSEntity();
        aftb->objct->InitFromRAM(entry->data, entry->size, tmpname);
    }
    for (uint32_t i = 0; i < pointCount && bs.GetCurrentPosition() + 8 <= size; i++) {
        int32_t x = bs.ReadInt32LE();
        int32_t y = bs.ReadInt32LE();
        aftb->positions.push_back(Point3D(x, y, 0));
    }
    this->afterburner = aftb;
}
// WC3's standalone gun weapon files (e.g. NEUTGUN.IFF, TACHGUN.IFF,
// LASER.IFF) — OBJT>GUNS, NOT to be confused with SSHP>WEAP>FGTR>GUNS (a
// ship's own hardpoint-position list, a completely different chunk at a
// different nesting level). Real files confirmed to always report a
// GUNS>DATA sub-chunk; no other GUNS sub-chunk observed across the 5
// files sampled (LASER/RLASER/NEUTGUN/ION_GUN/TACHGUN/REAPGUN).
void RSEntity::parseREAL_OBJT_GUNS(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_GUNS_DATA, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// 35-byte fixed record, byte-confirmed identical across 5 real gun files
// and against real published stats for the Tachyon Gun (user-supplied:
// damage 70 / energy 40 / range 3200 / refire 0.35s):
//   offset  0-14 (15): display name, null-padded ("Tachyon Gun", "Laser", ...)
//   offset    15  (1): damage -- direct value, confirmed exact (0x46=70)
//   offset 16-18  (3): reserved/padding -- 0x000000 in every sample
//   offset 19-22 (u32 LE): effective range -- stored value x2 (0x640=1600,
//                          x2=3200, confirmed exact)
//   offset 23-26 (u32 LE): unconfirmed (512 for Tachyon Gun) -- no real
//                          data point yet, stored raw and unused
//   offset 27-30 (u32 LE): refire delay -- stored value / 256.0 (89/256=
//                          0.3477 ~= 0.35, matching the same fixed-point
//                          scale already established elsewhere in this
//                          codebase, e.g. MISN_AREA_POSITION_SCALE)
//   offset 31-34 (u32 LE): energy cost per shot -- direct value, confirmed
//                          exact (40)
void RSEntity::parseREAL_OBJT_GUNS_DATA(uint8_t *data, size_t size) {
    if (size < 35)
        return;
    ByteStream bs(data, size);
    WDAT *wdat = new WDAT();
    this->weapon_display_name = bs.ReadString(15);
    wdat->damage = bs.ReadByte();
    bs.MoveForward(3);
    wdat->effective_range = bs.ReadUInt32LE() * 2;
    wdat->unknown_gun_stat = bs.ReadUInt32LE();
    wdat->refire_delay_seconds = (float)bs.ReadUInt32LE() / 256.0f;
    wdat->energy_cost = bs.ReadUInt32LE();
    this->wdat = wdat;
}
void RSEntity::parseREAL_OBJT_BOMB(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::bomb;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SIGN"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMOK"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SMOK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WDAT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_WDAT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DATA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM, this, std::placeholders::_1, std::placeholders::_2);


    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_TRCR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::tracer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SIGN"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMOK"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SMOK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WDAT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_WDAT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DATA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM, this, std::placeholders::_1, std::placeholders::_2);


    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_PODR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::podr;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SIGN"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMOK"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_SMOK, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WDAT"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_WDAT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_PODR_DATA, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_PODR_DATA(uint8_t *data, size_t size){
    ByteStream bs(data, size);
    std::string podr_subweapon_name = bs.ReadString(8);
    uint8_t podr_subweapon_number = bs.ReadByte();
    WEAPS *podr_data = new WEAPS();
    podr_data->name = podr_subweapon_name;
    podr_data->nb_weap = podr_subweapon_number;
    std::transform(podr_data->name.begin(), podr_data->name.end(), podr_data->name.begin(), ::toupper);
    std::string tmpname = assetsManager.object_root_path + podr_data->name + ".IFF";
    RSEntity *objct = new RSEntity();
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        objct->InitFromRAM(entry->data, entry->size, tmpname);
        podr_data->objct = objct;
    }
    this->weaps.push_back(podr_data);
}
void RSEntity::parseREAL_OBJT_JETP_INFO(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_EXPL(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_SIGN(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_TRGT(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_SMOK(uint8_t *data, size_t size){
    
}
void RSEntity::parseREAL_OBJT_MISS_DAMG(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_WDAT(uint8_t *data, size_t size){
    WDAT *wdat = new WDAT();
    ByteStream bs(data, size);
    wdat->damage = bs.ReadShort();
    wdat->radius = bs.ReadShort();
    wdat->unknown1 = bs.ReadByte();
    wdat->weapon_id = bs.ReadByte();
    wdat->weapon_category = bs.ReadByte();
    wdat->radar_type = bs.ReadByte();
    wdat->weapon_aspec = bs.ReadByte();
    wdat->target_range = bs.ReadInt32LE();
    wdat->tracking_cone = bs.ReadByte();
    wdat->effective_range = bs.ReadInt32LE();
    wdat->unknown6 = bs.ReadByte();
    wdat->unknown7 = bs.ReadByte();
    wdat->unknown8 = bs.ReadByte();
    this->wdat = wdat;
}
void RSEntity::parseREAL_OBJT_MISS_DATA(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_DYNM(uint8_t *data, size_t size){
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DYNM"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MISS"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM_MISS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ATMO"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM_ATMO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["GRAV"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM_GRAV, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AGRV"] = std::bind(&RSEntity::parseREAL_OBJT_MISS_DYNM_AGRV, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_MISS_DYNM_AGRV(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_DYNM_GRAV(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_DYNM_ATMO(uint8_t *data, size_t size){}
void RSEntity::parseREAL_OBJT_MISS_DYNM_MISS(uint8_t *data, size_t size){
    ByteStream bs(data, size);
    DYNN_MISS *dynn_miss = new DYNN_MISS();
    bs.ReadByte();
    dynn_miss->turn_degre_per_sec = bs.ReadInt24LE();
    dynn_miss->velovity_m_per_sec = bs.ReadInt24LE();
    this->dynn_miss = dynn_miss;
}
void RSEntity::parseREAL_OBJT_INFO(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::jet;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEBR"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEBR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEST"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SMOK"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_SMOK, this, std::placeholders::_1, std::placeholders::_2);

    handlers["CHLD"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_CHLD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["JINF"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_JINF, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["EJEC"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_EJEC, this, std::placeholders::_1, std::placeholders::_2);

    handlers["SIGN"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CTRL"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_CTRL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TOFF"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_TOFF, this, std::placeholders::_1, std::placeholders::_2);

    handlers["LAND"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_LAND, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WEAP"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CKPT"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_CKPT, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_ORNT(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::ornt;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEBR"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEBR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEST"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SIGN"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_GRND(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::ground;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEBR"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEBR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEST"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}

void RSEntity::parseREAL_OBJT_SWPN(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::swpn;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["EXPL"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEBR"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEBR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEST"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DEST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SIGN"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_SIGN, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRGT"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_TRGT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] =
        std::bind(&RSEntity::parseREAL_OBJT_SWPN_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DATA"] =
        std::bind(&RSEntity::parseREAL_OBJT_SWPN_DATA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ALGN"] =
        std::bind(&RSEntity::parseREAL_OBJT_SWPN_ALGN, this, std::placeholders::_1, std::placeholders::_2);
        
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_SWPN_DYNM(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_SWPN_DATA(uint8_t *data, size_t size) {
    ByteStream bs(data, size);
    SWPN_DATA *swpn_data = new SWPN_DATA();
    swpn_data->weapons_round = bs.ReadInt32LE();
    swpn_data->detection_range = bs.ReadInt32LE();
    swpn_data->effective_range = bs.ReadInt32LE();
    swpn_data->unknown1 = bs.ReadShort();
    swpn_data->unknown2 = bs.ReadShort();
    swpn_data->unknown3 = bs.ReadShort();
    swpn_data->max_simultaneous_shots = bs.ReadByte();
    swpn_data->weapons_round2 = bs.ReadShort();
    swpn_data->weapon_name = bs.ReadString(8);
    std::transform(swpn_data->weapon_name.begin(), swpn_data->weapon_name.end(), swpn_data->weapon_name.begin(), ::toupper);
    swpn_data->weapon_name = assetsManager.object_root_path + swpn_data->weapon_name + ".IFF";
    TreEntry *entry = assetsManager.GetEntryByName(swpn_data->weapon_name);
    if (entry != nullptr) {
        RSEntity *objct = new RSEntity();
        objct->InitFromRAM(entry->data, entry->size, swpn_data->weapon_name);
        swpn_data->weapon_entity = objct;
    } else {
        swpn_data->weapon_entity = nullptr;
    }
    swpn_data->unknown4 = bs.ReadInt32LE();
    this->swpn_data = swpn_data;
}
void RSEntity::parseREAL_OBJT_SWPN_ALGN(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_EXPL(uint8_t *data, size_t size) {
    EXPL *expl = new EXPL();
    
    std::string tmpname;
    ByteStream bs(data, size);
    expl->name = bs.ReadString(8);
    tmpname = assetsManager.object_root_path + expl->name + ".IFF";
    expl->x = bs.ReadShort();
    expl->y = bs.ReadShort();
    std::transform (tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    expl->objct = new RSEntity();
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        expl->objct->InitFromRAM(entry->data, entry->size, tmpname);
    }
    this->explos = expl;
}
void RSEntity::parseREAL_OBJT_JETP_DEBR(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_DEST(uint8_t *data, size_t size) {
    ByteStream bs(data, size);
    std::string tmpname = bs.ReadString(8);
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    this->destroyed_object_name = assetsManager.object_root_path + tmpname + ".IFF";
    RSEntity *objct = new RSEntity();
    TreEntry *entry = assetsManager.GetEntryByName(this->destroyed_object_name);
    if (this->destroyed_object_name == name) {
        entry = nullptr;
    }
    if (entry != nullptr) { 
        objct->InitFromRAM(entry->data, entry->size, this->destroyed_object_name);
        this->destroyed_object = objct;
    } else {
        this->destroyed_object = nullptr;
    }
    
}
void RSEntity::parseREAL_OBJT_JETP_SMOK(uint8_t *data, size_t size) {
    
}
void RSEntity::parseREAL_OBJT_JETP_CHLD(uint8_t *data, size_t size) {
    size_t nb_child = size /32;
    ByteStream bs(data, size);
    for (size_t i = 0; i < nb_child; i++) {
        CHLD *chld = new CHLD();
        
        std::string tmpname = bs.ReadString(8);
        std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
        chld->name = assetsManager.object_root_path+tmpname+".IFF";
        chld->x = bs.ReadInt32LE();
        chld->z = bs.ReadInt32LE();
        chld->y = bs.ReadInt32LE();
        
        for (size_t j = 0; j < 12; j++) {
            chld->data.push_back(bs.ReadByte());
        }
        RSEntity *objct = new RSEntity();
        
        TreEntry *entry = assetsManager.GetEntryByName(chld->name);
        if (entry != nullptr) {
            objct->InitFromRAM(entry->data, entry->size, chld->name);
            chld->objct = objct;
            this->chld.push_back(chld);
        }
    }
}
void RSEntity::parseREAL_OBJT_JETP_JINF(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_DAMG(uint8_t *data, size_t size) {
    if (size > 2) {
        IFFSaxLexer lexer;

        std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
        handlers["SYSM"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_DAMG_SYSM, this, std::placeholders::_1, std::placeholders::_2);

        lexer.InitFromRAM(data, size, handlers);
    } else {
        ByteStream bs(data, size);
        this->health = bs.ReadByte();
    }
}
void RSEntity::parseREAL_OBJT_JETP_EJEC(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_SIGN(uint8_t *data, size_t size) {
    if (size < 3)
        return;
    ByteStream bs(data, size);
    RADAR_SIGN *radar_signature = new RADAR_SIGN();
    radar_signature->unknown1 = bs.ReadByte();
    radar_signature->unknown2 = bs.ReadByte();  
    radar_signature->unknown3 = bs.ReadByte();
    this->radar_signature = radar_signature;
}
void RSEntity::parseREAL_OBJT_JETP_TRGT(uint8_t *data, size_t size) {
    if (size >= 1)
        this->target_type = data[0];
}
void RSEntity::parseREAL_OBJT_JETP_CTRL(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_CKPT(uint8_t *data, size_t size) {
    if (size < 26)
        return;
    ByteStream bs(data, size);
    std::string str1 = bs.ReadString(10);
    std::string str2 = bs.ReadString(8);
    std::string str3 = bs.ReadString(8);
    std::transform(str3.begin(), str3.end(), str3.begin(), ::toupper);
    this->cockpit_name = str3;
}
void RSEntity::parseREAL_OBJT_JETP_TOFF(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_LAND(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_DYNM(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DYNM"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ORDY"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_ORDY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["STBL"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_STBL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ATMO"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_ATMO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["GRAV"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_GRAV, this, std::placeholders::_1, std::placeholders::_2);
    handlers["THRS"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_THRS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["JDYN"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_DYNM_JDYN, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_JETP_DYNM_DYNM(uint8_t *data, size_t size) {
    ByteStream bs(data, size);
    if (size == 4) {
        bs.ReadByte();
        this->weight_in_kg = bs.ReadInt24LEByte3();
    }
}
void RSEntity::parseREAL_OBJT_JETP_DYNM_ORDY(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_DYNM_STBL(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_DYNM_ATMO(uint8_t *data, size_t size) {
    if (size < 2)
        return;
    ByteStream bs(data, size);
    this->drag = bs.ReadUShort();
}
void RSEntity::parseREAL_OBJT_JETP_DYNM_GRAV(uint8_t *data, size_t size) {
    this->gravity = true;
}
void RSEntity::parseREAL_OBJT_JETP_DYNM_THRS(uint8_t *data, size_t size) {
    if (size < 6)
        return;
    ByteStream bs(data, size);
    if (size == 6 || size == 7 || size == 8) {
        bs.ReadByte();
        this->thrust_in_newton = bs.ReadInt24LEByte3();
        /* il reste 2 ou 3 octets (en fonction de la taille du chunk 6 ou 7) en suite dont je ne sais rien*/
    } else if (size == 19 || size == 20) {
        bs.ReadByte();
        bs.ReadByte();
        bs.ReadByte();
        bs.ReadByte();
        bs.ReadByte();
        this->thrust_in_newton = bs.ReadInt24LEByte3()*15;
    }
    
}
void RSEntity::parseREAL_OBJT_JETP_DYNM_JDYN(uint8_t *data, size_t size) {
    if (size < 50)
        return;
    ByteStream bs(data, size);
    bs.ReadByte();
    JDYN *dyn = new JDYN();
    
    dyn->FUEL = bs.ReadInt24LEByte3();
    dyn->U1 = bs.ReadInt24LE();
    dyn->C1 = bs.ReadInt24LE();
    dyn->C2 = bs.ReadInt24LE();
    dyn->U2 = bs.ReadInt24LE();
    dyn->U3 = bs.ReadInt24LE();
    bs.ReadByte();
    dyn->ROLL_RATE = bs.ReadInt24LEByte3();
    dyn->ROLL_RATE_MAX = bs.ReadInt24LEByte3();
    dyn->CS3_qqch_lift = bs.ReadUShort();
    dyn->CS4 = bs.ReadUShort();
    dyn->U5 = bs.ReadInt24LE();
    dyn->U6 = bs.ReadInt24LEByte3();
    dyn->U7 = bs.ReadUShort();
    dyn->U8 = bs.ReadUShort();
    dyn->LIFT_SPD = bs.ReadInt24LE();
    dyn->DRAG = bs.ReadInt24LE();
    dyn->LIFT = bs.ReadInt24LE();
    dyn->aileron = bs.ReadByte();
    dyn->gouverne = bs.ReadByte();
    dyn->MAX_G = bs.ReadByte();
    dyn->U13 = bs.ReadUShort();
    dyn->TWIST_RATE = bs.ReadUShort();
    dyn->TWIST_RATE_MAX = bs.ReadUShort();
    dyn->U16 = bs.ReadInt24LE();
    dyn->U17 = bs.ReadInt24LE();
    this->jdyn = dyn;
}
void RSEntity::parseREAL_OBJT_JETP_WEAP(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DCOY"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_DCOY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WPNS"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_WPNS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["HPTS"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_HPTS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_JETP_WEAP_INFO(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_WEAP_DCOY(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_OBJT_JETP_WEAP_WPNS(uint8_t *data, size_t size) {
    if (size < 10)
        return;
    size_t nb_weap = 0;
    nb_weap = size / 10;
    ByteStream bs(data, size);  

    for (size_t i = 0; i < nb_weap; i++) {
        WEAPS *htps = new WEAPS();
        htps->nb_weap = bs.ReadShort();
        htps->name = bs.ReadString(8);
        std::transform(htps->name.begin(), htps->name.end(), htps->name.begin(), ::toupper);
        std::string tmpname = assetsManager.object_root_path + htps->name + ".IFF";
        RSEntity *objct = new RSEntity();
        TreEntry *entry = assetsManager.GetEntryByName(tmpname);
        if (entry != nullptr) {
            objct->InitFromRAM(entry->data, entry->size, tmpname);
            htps->objct = objct;
        }
        this->weaps.push_back(htps);
    }
}
void RSEntity::parseREAL_OBJT_JETP_WEAP_HPTS(uint8_t *data, size_t size) {
    if (size < 13)
        return;
    size_t nb_hpts = size / 13;
    ByteStream bs(data, size);
    for (size_t i = 0; i < nb_hpts; i++) {
        HPTS *hpts = new HPTS();
        hpts->id = bs.ReadByte();
        hpts->x = bs.ReadFixedFloatLE();
        hpts->z = bs.ReadFixedFloatLE();
        hpts->y = bs.ReadFixedFloatLE();
        this->hpts.push_back(hpts);
    }
}
void RSEntity::parseREAL_OBJT_JETP_WEAP_DAMG(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["SYSM"] =
        std::bind(&RSEntity::parseREAL_OBJT_JETP_WEAP_DAMG_SYSM, this, std::placeholders::_1, std::placeholders::_2);
}
void RSEntity::parseREAL_OBJT_JETP_WEAP_DAMG_SYSM(uint8_t *data, size_t size) {
    if (size < 2)
        return;
    ByteStream bs(data, size);

    this->life = bs.ReadShort();
    size_t nb_sysm = (size - 2) / 18;
    
    for (size_t i=0; i<nb_sysm;i++) {
        uint16_t pv = bs.ReadShort();
        std::string sub_system = bs.ReadString(8);
        std::string main_system = bs.ReadString(8);
        this->sysm[main_system][sub_system] = pv;
    }
}
// ---------------------------------------------------------------------------
// OBJT>SSHP ("space ship") — WC3's own fighter/bomber format, structurally
// unrelated to Strike Commander's OBJT>JETP despite sharing sibling chunk
// names (EXPL/DEBR/DEST/DYNM/CTRL/DAMG/AFTB/WEAP/SHLD): JETP nests each of
// those into further sub-forms (DYNM>DYNM/ORDY/STBL/ATMO/GRAV/THRS/JDYN,
// WEAP>INFO/DCOY/WPNS/HPTS/DAMG, etc.); SSHP instead wraps a single flat
// "FGTR" record directly under each. No CKPT (WC3 resolves the cockpit from
// the *mission's* own COCK chunk instead — see WC3Strike::setMission) and no
// TOFF/LAND/EJEC (no runway takeoff/landing or ejection seat in space).
//
// Reverse-engineered empirically by comparing DYNM>FGTR (28 bytes = 7
// little-endian uint32 fields) and DAMG>FGTR (12 bytes = 3 fields) across
// four ships of different weight classes (HELLCAT/HELLCATP medium fighter,
// ARROW light fighter, EXCAL top-tier fighter):
//   DYNM>FGTR: [0]=60/90/75 [1]=60/80/70 [2]=60/90/70 [3]=420/520/500
//              [4]=1200/1400/1300 [5]=15/15/15 (constant!) [6]=900/1000/1100
// [0..2] scale with agility (Arrow, the "light fighter", is highest;
// Hellcat, the aging mid-tier trainer, is lowest) — mapped to roll/pitch/yaw
// rate. [5] is constant across every ship checked, exactly matching a
// plausible shared MAX_G fighter limit — high confidence. [3]/[4]/[6] are
// each monotonic across the three ships but their exact real-world meaning
// (thrust/mass/fuel vs. something else) isn't independently confirmed —
// mapped to thrust_in_newton/weight_in_kg/FUEL as the best-fit remaining
// JDYN/RSEntity fields actually consumed by SCJdynPlane's flight model.
// wing_area is NOT part of this chunk — it's computed geometrically from the
// APPR/POLY mesh (calcWingArea), which is byte-identical between JETP and
// SSHP ships, so it already works unmodified.
void RSEntity::parseREAL_OBJT_SSHP(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->entity_type = EntityType::jet;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DEST"] = std::bind(&RSEntity::parseREAL_OBJT_JETP_DEST, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DYNM"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_DYNM, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DAMG"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_DAMG, this, std::placeholders::_1, std::placeholders::_2);
    handlers["WEAP"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["EXPL"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_EXPL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DEBR"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_DEBR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AFTB"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_AFTB, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHLD"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_SHLD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CLOK"] = std::bind(&RSEntity::parseREAL_OBJT_CLOK, this, std::placeholders::_1, std::placeholders::_2);
    // CTRL (always FFFFFFFF, meaning unknown) is intentionally left
    // unregistered; IFFSaxLexer just skips it.
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_SSHP_DYNM(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["FGTR"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_DYNM_FGTR, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_SSHP_DYNM_FGTR(uint8_t *data, size_t size) {
    if (size < 28)
        return;
    ByteStream bs(data, size);
    JDYN *dyn = new JDYN();
    // Field order/names here are as originally guessed; see JDYN's own
    // WC3_TRUE_ROLL_RATE_DPS comment for the manual-confirmed actual
    // meaning of each (roll_rate below is really pitch dps, pitch_rate is
    // really yaw dps, and the 3rd field — previously discarded — is really
    // roll dps).
    uint32_t roll_rate = bs.ReadUInt32LE();
    uint32_t pitch_rate = bs.ReadUInt32LE();
    uint32_t yaw_rate = bs.ReadUInt32LE();
    uint32_t thrust = bs.ReadUInt32LE();
    uint32_t weight = bs.ReadUInt32LE();
    uint32_t max_g = bs.ReadUInt32LE();
    uint32_t fuel = bs.ReadUInt32LE();

    dyn->FUEL = fuel;
    dyn->ROLL_RATE = (uint16_t)roll_rate;
    dyn->ROLL_RATE_MAX = (uint16_t)roll_rate;
    dyn->TWIST_RATE = (uint16_t)pitch_rate;
    dyn->TWIST_RATE_MAX = (uint16_t)pitch_rate;
    dyn->WC3_TRUE_ROLL_RATE_DPS = (uint16_t)yaw_rate;
    dyn->MAX_G = (uint8_t)max_g;
    this->jdyn = dyn;

    this->thrust_in_newton = (int32_t)thrust;
    this->weight_in_kg = (int32_t)weight;
}
void RSEntity::parseREAL_OBJT_SSHP_DAMG(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["FGTR"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_DAMG_FGTR, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_SSHP_DAMG_FGTR(uint8_t *data, size_t size) {
    if (size < 12)
        return;
    ByteStream bs(data, size);
    bs.ReadUInt32LE(); // constant 10 across every ship checked — meaning unknown
    bs.ReadUInt32LE(); // varies 2-5 — meaning unknown (damage-stage count?)
    uint32_t hitpoints = bs.ReadUInt32LE();
    this->health = (uint8_t)std::min<uint32_t>(hitpoints, 255);
}
// SSHP's WEAP chunk wraps a single flat "FGTR" record (matching the
// DYNM/DAMG pattern above) containing GUNS/MISL/DECY sub-chunks — each a
// flat array of fixed-size records giving every individually-mounted
// weapon's hardpoint position, unlike JETP's WEAP (a swappable-loadout
// system: WPNS lists weapon *types* + ammo counts, HPTS lists hardpoint
// *slots* separately, and SCPlane::InitLoadout() matches them up via
// SC-specific weapon-id tables). WC3 ships have a fixed, per-ship gun/
// missile layout baked directly into the file instead.
//
// Reverse-engineered empirically across HELLCATP/HELLCAT/ARROW/EXCAL:
// GUNS is a 9-byte header (meaning unconfirmed) followed by 21-byte
// records — name(8, e.g. "NEUTGUN\0") + weapon-type id(1, stable per name
// across every ship: NEUTGUN=3, ION_GUN=4, RLASER=2, REAPGUN=6,
// TACHGUN=10) + x/z/y (matching JETP HPTS's own id+x+z+y convention; each
// field is the same 8-bit-fractional fixed-point encoding as APPR>POLY>VERT
// -- see HPTS's own comment (RSEntity.h) for how that was confirmed).
// MISL is the same shape after a 4-byte header, but each
// 21-byte name+id+x+z+y record is followed by 4 more unidentified bytes
// (constant per weapon name+ship — possibly an ammo count) — skipped for
// now. DECY (decoy dispenser) has no position at all and isn't a
// hardpoint, so it's not parsed here.
//
// this->hpts (positions) and this->weaps (weapon type + a real, asset-
// loaded RSEntity — see getWC3RealWeaponEntity below) are both populated,
// 1:1 by index (this->weaps[i] is always the weapon mounted at
// this->hpts[i]) — unlike JETP's WEAP (type+count list matched against
// hardpoint *slots* separately by SCPlane::InitLoadout()'s own weap_map/
// max_load_out tables), since WC3 has no swappable loadout: each
// hardpoint has exactly one fixed weapon baked into the file. WC3 weapon
// names DO resolve to a loadable DATA\OBJECTS\ asset (confirmed directly:
// HELLCATP.IFF's own GUNS/MISL/DECY link to NEUTGUN.IFF/ION_GUN.IFF/
// IRMISS.IFF/DECOY.IFF — this comment previously claimed otherwise,
// which was wrong), giving real geometry; wdat is still synthesized from
// a hardcoded stat table (SCenums.h's wc3_weapon_stats) since WC3 weapon
// files aren't confirmed to carry a WDAT-shaped stat chunk of their own —
// see getWC3RealWeaponEntity's own comment.
void RSEntity::parseREAL_OBJT_SSHP_WEAP(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["FGTR"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR, this, std::placeholders::_1, std::placeholders::_2);
    // Capital ships (e.g. VICTORY.IFF) use WEAP>CPTL instead of WEAP>FGTR —
    // turrets and flight-deck cargo instead of fixed fighter hardpoints.
    handlers["CPTL"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_CPTL, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["GUNS"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MISL"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_MISL, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DECY"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_DECY, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// Maps the 8-byte weapon-name string embedded in each SSHP GUNS/MISL
// hardpoint record to the new WC3 weapon_ids values (SCenums.h) — NOT the
// raw per-hardpoint id byte also present in the record (that byte
// collides with SC1's own weapon_ids numbering, e.g. RLASER's raw id 2 ==
// ID_AIM9M; see weapon_ids' own comment). Missile-record names are
// inferred from MISL_DATA's own file-name convention (RSEntity.h:
// HSMISS/IRMISS/FFMISS/TORKMISS/TEMBMISS) rather than independently
// confirmed against a live hardpoint record — flagged here in case a real
// dump ever shows a mismatch.
int RSEntity::wc3WeaponNameToId(const std::string &name) {
    static const std::unordered_map<std::string, int> kNameToId = {
        {"NEUTGUN", ID_NEUTGUN}, {"ION_GUN", ID_IONGUN}, {"RLASER", ID_RLASER_WC3},
        {"REAPGUN", ID_REAPGUN}, {"TACHGUN", ID_TACHGUN},
        {"HSMISS", ID_HSMISS}, {"IRMISS", ID_IRMISS}, {"FFMISS", ID_FFMISS},
        {"TORKMISS", ID_TORKMISS}, {"TEMBMISS", ID_TEMBMISS}, {"MINEMISS", ID_MINEMISS},
    };
    auto it = kNameToId.find(name);
    return (it != kNameToId.end()) ? it->second : -1;
}
// See declaration comment (RSEntity.h). Prefer this over
// wc3WeaponNameToId for GUNS records specifically.
int RSEntity::wc3GunRawIdToWeaponId(uint8_t rawId) {
    switch (rawId) {
        case 2: return ID_RLASER_WC3;
        case 3: return ID_NEUTGUN;
        case 4: return ID_IONGUN;
        case 6: return ID_REAPGUN;
        case 10: return ID_TACHGUN;
        default: return -1;
    }
}
// See declaration comment (RSEntity.h) — loads the real DATA\OBJECTS\
// <name>.IFF asset (confirmed real: HELLCATP.IFF's GUNS/MISL/DECY link to
// NEUTGUN.IFF/ION_GUN.IFF/IRMISS.IFF/DECOY.IFF), exactly mirroring the
// sibling parseREAL_OBJT_SSHP_WEAP_FGTR_DECY's own resolution code just
// below. Replaces the earlier synthetic-entity-only approach (this
// function used to be getWC3SyntheticWeapon, built from wc3_weapon_stats
// alone with no geometry) — that was based on this file's own
// now-corrected assumption that WC3 weapon names don't resolve under
// DATA\OBJECTS\ the way JETP's do.
RSEntity *RSEntity::getWC3RealWeaponEntity(const std::string &name, int weaponId) {
    static std::unordered_map<std::string, RSEntity *> cache;
    auto found = cache.find(name);
    if (found != cache.end()) {
        return found->second;
    }
    // static method -- can't use the instance member `assetsManager` alias
    // (RSEntity.h), go straight to the singleton like it does.
    AssetManager &assets = AssetManager::instance();
    std::string tmpname = assets.object_root_path + name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry *entry = assets.GetEntryByName(tmpname);
    RSEntity *entity = new RSEntity();
    if (entry != nullptr) {
        entity->InitFromRAM(entry->data, entry->size, tmpname);
    } else {
        printf("RSEntity::getWC3RealWeaponEntity: %s not found under DATA\\OBJECTS\\ -- falling back to a geometry-less placeholder\n",
               tmpname.c_str());
    }
    auto statIt = wc3_weapon_stats.find(weaponId);
    bool isGunCategory = (statIt != wc3_weapon_stats.end()) && (statIt->second.weapon_category == 0);
    if (entity->wdat != nullptr) {
        // Real stats already parsed off the file itself (guns via
        // OBJT>GUNS>DATA, missiles/torpedo/T-bomb via OBJT>MISL>DATA —
        // parseREAL_OBJT_GUNS_DATA/parseREAL_OBJT_MISL_DATA) — only
        // weapon_id/weapon_category are missing (the per-file parser has
        // no way to know the caller's resolved weapon_ids value), fill
        // those in without touching the real fields already set.
        entity->wdat->weapon_id = (uint8_t)weaponId;
        if (statIt != wc3_weapon_stats.end()) {
            entity->wdat->weapon_category = statIt->second.weapon_category;
        }
        // WC3 missiles expire by flight TIME (wdat->duration_seconds,
        // confirmed real — see parseREAL_OBJT_MISL_DATA's own comment),
        // not a stored travel distance the way SCSimulatedObject::
        // Simulate()'s existing expiry check (this->distance >
        // wdat->effective_range) assumes — so effective_range is derived
        // here as duration x real speed once both the DATA and DYNM
        // chunks have been parsed (this runs after InitFromRAM finishes,
        // so both are available by now). Real speed is dynn_miss->
        // velovity_m_per_sec/256 (see parseREAL_OBJT_MISL_DYNM's own
        // comment for that confirmed /256 scale) — deliberately not the
        // raw dynn_miss field directly, which existing consumers
        // (SCSimulatedObject.cpp, SCMissionActors.cpp) already use as-is
        // with their own established scale factors for velocity/thrust,
        // unrelated to this distance derivation.
        if (entity->wdat->duration_seconds > 0.0f && entity->dynn_miss != nullptr) {
            float realSpeed = (float)entity->dynn_miss->velovity_m_per_sec / 256.0f;
            entity->wdat->effective_range = (uint32_t)(entity->wdat->duration_seconds * realSpeed);
        }
    } else if (statIt != wc3_weapon_stats.end()) {
        // No real stat chunk was parsed (asset failed to resolve, or a
        // weapon type without a confirmed real-data parser yet) — fall
        // back to the placeholder table entirely, same as before real
        // asset loading existed.
        const WC3WeaponStat &stat = statIt->second;
        WDAT *wdat = new WDAT();
        wdat->damage = stat.damage;
        wdat->weapon_id = (uint8_t)weaponId;
        wdat->weapon_category = stat.weapon_category;
        wdat->target_range = stat.target_range;
        wdat->effective_range = stat.effective_range;
        entity->wdat = wdat;
    }
    // EntityType has no default member initializer (RSEntity.h). The real
    // parse above may already have set a sensible value (e.g. OBJT>MISL
    // sets EntityType::missiles, RSEntity.cpp:316) — this override is a
    // no-op in that case. For guns specifically, OBJT>GUNS's own handler
    // doesn't set it, so without this they'd stay indeterminate;
    // GunSimulatedObject::Simulate() specifically checks entity_type==
    // tracer for one of its expiry conditions, matching SC1's own ID_20MM
    // gun.
    if (statIt != wc3_weapon_stats.end()) {
        entity->entity_type = isGunCategory ? EntityType::tracer : EntityType::missiles;
    }
    cache[name] = entity;
    return entity;
}
void RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_GUNS(uint8_t *data, size_t size) {
    constexpr size_t kHeader = 9;
    constexpr size_t kRecord = 21;
    if (size < kHeader + kRecord)
        return;
    ByteStream bs(data, size);
    this->gun_flag = bs.ReadByte();
    this->gun_energy_capacity = bs.ReadUInt32LE();
    this->gun_energy_recharge_rate = bs.ReadUInt32LE();
    for (size_t pos = kHeader; pos + kRecord <= size; pos += kRecord) {
        std::string weaponName = bs.ReadString(8);
        HPTS *hpt = new HPTS();
        hpt->id = bs.ReadByte();
        hpt->x = bs.ReadFixedFloatLE();
        hpt->z = bs.ReadFixedFloatLE();
        hpt->y = bs.ReadFixedFloatLE();
        this->hpts.push_back(hpt);

        // Raw id byte (not the name string) — confirmed reliable, see
        // wc3GunRawIdToWeaponId's own comment.
        int weaponId = wc3GunRawIdToWeaponId(hpt->id);
        if (weaponId < 0) {
            printf("RSEntity[%s]: unrecognized WC3 gun raw id %u (name field read as \"%s\") -- this hardpoint will be unarmed\n",
                   this->name.c_str(), hpt->id, weaponName.c_str());
        }
        WEAPS *weap = new WEAPS();
        // Guns are energy-gated (this->gun_energy_capacity/_recharge_rate
        // above), not ammo-count limited — nb_weap is unused for the gun
        // fire path but set to 1 so generic "is this slot occupied" checks
        // elsewhere don't treat it as empty.
        weap->nb_weap = 1;
        weap->name = weaponName;
        weap->objct = (weaponId >= 0) ? getWC3RealWeaponEntity(weaponName, weaponId) : nullptr;
        this->weaps.push_back(weap);
    }
}
void RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_MISL(uint8_t *data, size_t size) {
    constexpr size_t kHeader = 4;
    constexpr size_t kRecord = 25;
    if (size < kHeader + kRecord)
        return;
    ByteStream bs(data, size);
    bs.MoveForward(kHeader);
    for (size_t pos = kHeader; pos + kRecord <= size; pos += kRecord) {
        std::string weaponName = bs.ReadString(8);
        HPTS *hpt = new HPTS();
        hpt->id = bs.ReadByte();
        hpt->x = bs.ReadFixedFloatLE();
        hpt->z = bs.ReadFixedFloatLE();
        hpt->y = bs.ReadFixedFloatLE();
        // Adopted as an ammo count per this field's own long-standing
        // "possibly an ammo count" comment — the best available signal,
        // not independently confirmed.
        uint32_t ammoCount = bs.ReadUInt32LE();
        this->hpts.push_back(hpt);

        int weaponId = wc3WeaponNameToId(weaponName);
        if (weaponId < 0) {
            // No confirmed raw-id table exists for MISL the way
            // wc3GunRawIdToWeaponId does for GUNS (see that function's own
            // comment) -- name-string matching is the only signal we have,
            // and it's unconfirmed. Logging the raw name+id here is the
            // real data needed to fix this properly if it turns out wrong.
            printf("RSEntity[%s]: unrecognized WC3 missile name \"%s\" (raw id byte %u) -- this hardpoint will be unarmed\n",
                   this->name.c_str(), weaponName.c_str(), hpt->id);
        }
        WEAPS *weap = new WEAPS();
        weap->nb_weap = (int)ammoCount;
        weap->name = weaponName;
        weap->objct = (weaponId >= 0) ? getWC3RealWeaponEntity(weaponName, weaponId) : nullptr;
        this->weaps.push_back(weap);
    }
}
// SSHP>WEAP>FGTR>DECY: a single 12-byte record — u32 carried decoy count
// (confirmed against known in-game values: Arrow=16, Hellcat=24,
// Excalibur=30, every Kilrathi fighter checked=6-8) + name(8), e.g. "DECOY"
// -> DATA\OBJECTS\DECOY.IFF. No per-hardpoint position (decoys dispense
// from a generic location, unlike guns/missiles).
void RSEntity::parseREAL_OBJT_SSHP_WEAP_FGTR_DECY(uint8_t *data, size_t size) {
    constexpr size_t kHeader = 4;
    if (size < kHeader + 8)
        return;
    ByteStream bs(data, size);
    this->decoy_count = bs.ReadUInt32LE();
    std::string name = bs.ReadString(8);
    this->decoy_name = name;
    std::string tmpname = assetsManager.object_root_path + name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        RSEntity *objct = new RSEntity();
        objct->InitFromRAM(entry->data, entry->size, tmpname);
        this->decoy_entity = objct;
    }
}
// SSHP>WEAP>CPTL (capital ships, e.g. VICTORY.IFF): TURT (turrets) + CRGO
// (flight-deck cargo), both plain chunks directly under CPTL (like FGTR's
// own GUNS/MISL/DECY — no further FORM-unwrap needed).
void RSEntity::parseREAL_OBJT_SSHP_WEAP_CPTL(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["TURT"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_CPTL_TURT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["CRGO"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_WEAP_CPTL_CRGO, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// u32 header — likely per-turret armor/HP, applied uniformly across the
// whole battery (see turret_armor field comment in RSEntity.h; not
// independently confirmed against a specific known number) + N x 52-byte
// records: mount mesh name(9, e.g. "CRUL_TRT\0") + gun/barrel mesh name(9, "CRUL_GUN\0") +
// weapon-type name(9, "TURLASER\0" — not a mesh, matches GUNS/MISL's own
// weapon-type strings, so no DATA\OBJECTS\ resolution attempted for it) +
// x/y/z (same 8-bit-fractional fixed-point as APPR>POLY>VERT, mount
// position) + pitch(int32LE/256=degrees) + yaw(int32LE/256=degrees) +
// elevation_cap(int32LE, raw degrees, typically 90) + 1 trailing flag byte
// (constant 1). x/y/z scale and pitch/yaw/elevation_cap all confirmed via
// cross-check against an independent decoder
// (/home/james/development/wc-iff-loader) — see TURRET struct comment.
void RSEntity::parseREAL_OBJT_SSHP_WEAP_CPTL_TURT(uint8_t *data, size_t size) {
    constexpr size_t kHeader = 4;
    constexpr size_t kRecord = 52;
    if (size < kHeader + kRecord)
        return;
    ByteStream bs(data, size);
    this->turret_armor = bs.ReadUInt32LE();
    for (size_t pos = kHeader; pos + kRecord <= size; pos += kRecord) {
        TURRET *turret = new TURRET();
        turret->mount_name = bs.ReadString(9);
        turret->gun_name = bs.ReadString(9);
        turret->weapon_type = bs.ReadString(9);
        // Plain x, y, z order (NOT the z,x,y swap APPR>POLY>VERT's own
        // vertices use) — confirmed by directly decoding VICTORY.IFF's raw
        // TURT bytes: this order lands elevation_cap exactly on 90 (byte
        // offset 20-23 of the post-names tail) for every one of its 11
        // records, and produces clean mirrored left/right turret pairs
        // (e.g. x=-210.00/+226.00 with identical y/z, or yaw=45/-45 on
        // another pair) — the z,x,y swap tried previously scattered these
        // into nonsensical values and was wrong.
        turret->x = bs.ReadFixedFloatLE();
        turret->y = bs.ReadFixedFloatLE();
        turret->z = bs.ReadFixedFloatLE();
        turret->pitch = bs.ReadInt32LE() / 256.0f;
        turret->yaw = bs.ReadInt32LE() / 256.0f;
        turret->elevation_cap = bs.ReadInt32LE();
        bs.ReadByte();

        std::string mountPath = assetsManager.object_root_path + turret->mount_name + ".IFF";
        std::transform(mountPath.begin(), mountPath.end(), mountPath.begin(), ::toupper);
        TreEntry *mountEntry = assetsManager.GetEntryByName(mountPath);
        if (mountEntry != nullptr) {
            turret->mount_objct = new RSEntity();
            turret->mount_objct->InitFromRAM(mountEntry->data, mountEntry->size, mountPath);
        }
        std::string gunPath = assetsManager.object_root_path + turret->gun_name + ".IFF";
        std::transform(gunPath.begin(), gunPath.end(), gunPath.begin(), ::toupper);
        TreEntry *gunEntry = assetsManager.GetEntryByName(gunPath);
        if (gunEntry != nullptr) {
            turret->gun_objct = new RSEntity();
            turret->gun_objct->InitFromRAM(gunEntry->data, gunEntry->size, gunPath);
        }
        this->turrets.push_back(turret);
    }
}
// Flight-deck cargo (parked fighters, crates, a truck): name(8) confirmed;
// see CARGO_OBJECT struct comment — of the remaining 29 bytes, three
// fields decode cleanly (yaw at record-offset 17, deckOffsetY at
// record-offset 20 -- they share one raw byte, hence the MoveBackward
// below -- and rowOffset, a real per-instance position, at record-offset
// 29); the rest is skipped rather than misparsed.
void RSEntity::parseREAL_OBJT_SSHP_WEAP_CPTL_CRGO(uint8_t *data, size_t size) {
    constexpr size_t kRecord = 37;
    constexpr size_t kYawOffset = 17;
    constexpr size_t kOffsetYOffset = 20;
    constexpr size_t kRowOffsetOffset = 29;
    if (size < kRecord)
        return;
    ByteStream bs(data, size);
    for (size_t pos = 0; pos + kRecord <= size; pos += kRecord) {
        std::string name = bs.ReadString(8);
        bs.MoveForward(kYawOffset - 8);
        float yaw = bs.ReadFixedFloatLE();
        bs.MoveBackward(int((kYawOffset + 4) - kOffsetYOffset));
        float deckOffsetY = bs.ReadFixedFloatLE();
        bs.MoveForward(kRowOffsetOffset - (kOffsetYOffset + 4));
        float rowOffset = bs.ReadFixedFloatLE();
        bs.MoveForward(kRecord - (kRowOffsetOffset + 4));

        CARGO_OBJECT *obj = new CARGO_OBJECT();
        obj->name = name;
        obj->deckOffsetY = deckOffsetY;
        obj->yaw = yaw;
        obj->rowOffset = rowOffset;
        std::string tmpname = assetsManager.object_root_path + name + ".IFF";
        std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
        TreEntry *entry = assetsManager.GetEntryByName(tmpname);
        if (entry != nullptr) {
            obj->objct = new RSEntity();
            obj->objct->InitFromRAM(entry->data, entry->size, tmpname);
        }
        this->cargo.push_back(obj);
    }
}
// SSHP>EXPL is itself a FORM wrapping a single plain "DATA" sub-chunk (just
// like SSHP>SHLD below), so — unlike WEAP>FGTR>GUNS/MISL/DECY, which are
// plain chunks directly under FGTR — it needs a one-tag sub-lexer dispatch
// before the real payload is reachable.
void RSEntity::parseREAL_OBJT_SSHP_EXPL(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_EXPL_DATA, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// name(8) + 1 pad byte + 2 x int32LE (25600/25600 for every ship checked —
// a shared scale/radius parameter, exact meaning unconfirmed). A completely
// different, much flatter layout than JETP's own EXPL (name+x+y int16 each,
// 12 bytes total) despite sharing the chunk name and the this->explos
// consumer (SCExplosion, SCMissionActors, SCSimulatedObject — all of which
// unconditionally dereference explos->objct on destruction, so leaving this
// unparsed was a live crash risk for every WC3 ship).
void RSEntity::parseREAL_OBJT_SSHP_EXPL_DATA(uint8_t *data, size_t size) {
    if (size < 9)
        return;
    EXPL *expl = new EXPL();
    ByteStream bs(data, size);
    expl->name = bs.ReadString(8);
    expl->x = 0;
    expl->y = 0;
    if (size >= 17) {
        bs.ReadByte(); // pad
        expl->strength1 = bs.ReadInt32LE() / 256.0f;
        expl->strength2 = bs.ReadInt32LE() / 256.0f;
    }
    std::string tmpname = assetsManager.object_root_path + expl->name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    expl->objct = new RSEntity();
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        expl->objct->InitFromRAM(entry->data, entry->size, tmpname);
    }
    this->explos = expl;
}
// SSHP>DEBR is also a FORM wrapping a single "DATA" sub-chunk — see the
// EXPL comment above for why this dispatch step is needed.
void RSEntity::parseREAL_OBJT_SSHP_DEBR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_DEBR_DATA, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// u16 count + count x 34-byte records: name(8) + type(i16) + size xyz(3x
// i16, constant 5,5,5 across every piece/ship checked) + offset xyz(3x i16,
// constant -25,-25,-25) + velocity_range xyz(3x i16, constant -10,-10,-10)
// + trailing xyz(3x u16, varies per piece — position vs. velocity not
// resolved, see DEBR_PIECE struct comment).
void RSEntity::parseREAL_OBJT_SSHP_DEBR_DATA(uint8_t *data, size_t size) {
    constexpr size_t kRecord = 34;
    if (size < 2)
        return;
    ByteStream bs(data, size);
    uint16_t count = bs.ReadUShort();
    for (uint16_t i = 0; i < count && 2 + (size_t)(i + 1) * kRecord <= size; i++) {
        std::string name = bs.ReadString(8);
        DEBR_PIECE *piece = new DEBR_PIECE();
        piece->name = name;
        piece->type = bs.ReadShort();
        piece->size_x = bs.ReadShort();
        piece->size_y = bs.ReadShort();
        piece->size_z = bs.ReadShort();
        piece->offset_x = bs.ReadShort();
        piece->offset_y = bs.ReadShort();
        piece->offset_z = bs.ReadShort();
        piece->velocity_range_x = bs.ReadShort();
        piece->velocity_range_y = bs.ReadShort();
        piece->velocity_range_z = bs.ReadShort();
        piece->x = bs.ReadUShort();
        piece->y = bs.ReadUShort();
        piece->z = bs.ReadUShort();
        std::string tmpname = assetsManager.object_root_path + name + ".IFF";
        std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
        TreEntry *entry = assetsManager.GetEntryByName(tmpname);
        if (entry != nullptr) {
            piece->objct = new RSEntity();
            piece->objct->InitFromRAM(entry->data, entry->size, tmpname);
        }
        this->debris.push_back(piece);
    }
}
// SSHP>AFTB is also a FORM wrapping a single "DATA" sub-chunk — see the
// EXPL comment above for why this dispatch step is needed.
void RSEntity::parseREAL_OBJT_SSHP_AFTB(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_AFTB_DATA, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// u32 pointCount + u32 engine_count (previously read as an unexplained
// "constant 2" — every ship checked so far just happens to have 2 engines;
// confirmed as a real, meaningful count, not a constant, via cross-check
// against an independent decoder) + name(8, e.g. "AFBURN2") + pointCount x
// (x,y,z int32LE, /256 = world units — same scale confirmed elsewhere for
// VERT/TURT position) engine-mount positions. The named subobject uses
// APPR>POLY>DETA with exactly 8 levels (LVL0-LVL7): LVL0-LVL6 are the 7
// throttle-percentage thrust-glow states (0%..100%), LVL7 is the
// afterburner-engaged flame.
void RSEntity::parseREAL_OBJT_SSHP_AFTB_DATA(uint8_t *data, size_t size) {
    if (size < 16)
        return;
    ByteStream bs(data, size);
    uint32_t pointCount = bs.ReadUInt32LE();
    uint32_t engineCount = bs.ReadUInt32LE();
    (void)engineCount; // not currently stored on AFTB; count derives from positions.size()
    AFTB *aftb = new AFTB();
    aftb->name = bs.ReadString(8);
    std::string tmpname = assetsManager.object_root_path + aftb->name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        aftb->objct = new RSEntity();
        aftb->objct->InitFromRAM(entry->data, entry->size, tmpname);
    }
    for (uint32_t i = 0; i < pointCount && 16 + (size_t)(i + 1) * 12 <= size; i++) {
        Point3D pos;
        pos.x = (float)bs.ReadInt32LE() / 256.0f;
        pos.y = (float)bs.ReadInt32LE() / 256.0f;
        pos.z = (float)bs.ReadInt32LE() / 256.0f;
        aftb->positions.push_back(pos);
    }
    this->afterburner = aftb;
}
// SSHP>SHLD: DATA(8) names the shield-hit visual effect entity (e.g.
// "SHLDFX\0\0" -> DATA\OBJECTS\SHLDFX.IFF); FGTR carries per-quadrant
// shield hitpoints (a leading u32 of unconfirmed meaning, then front/back/
// left/right as u32 each, then two more u32 pairs that look like
// max-recharge-rate-ish values — only the quadrant HP is captured here).
void RSEntity::parseREAL_OBJT_SSHP_SHLD(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    this->m_parsingShield = new SHLD_FX();
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_SHLD_DATA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["FGTR"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_SHLD_FGTR, this, std::placeholders::_1, std::placeholders::_2);
    // Capital ships (e.g. VICTORY.IFF) use SHLD>CPTL instead of SHLD>FGTR —
    // confirmed directly against VICTORY.IFF's own raw bytes (a 36-byte
    // CPTL chunk sibling to DATA, same size as FGTR elsewhere). Was
    // previously unregistered entirely, so every capital ship's own shield
    // data was silently skipped. Field *meaning* within those 36 bytes is
    // a separate, unconfirmed question — see SHLD_FX struct comment.
    handlers["CPTL"] = std::bind(&RSEntity::parseREAL_OBJT_SSHP_SHLD_FGTR, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
    this->shield = this->m_parsingShield;
    this->m_parsingShield = nullptr;
}
void RSEntity::parseREAL_OBJT_SSHP_SHLD_DATA(uint8_t *data, size_t size) {
    if (this->m_parsingShield == nullptr || size < 8)
        return;
    ByteStream bs(data, size);
    this->m_parsingShield->name = bs.ReadString(8);
    std::string tmpname = assetsManager.object_root_path + this->m_parsingShield->name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        this->m_parsingShield->objct = new RSEntity();
        this->m_parsingShield->objct->InitFromRAM(entry->data, entry->size, tmpname);
    }
}
// front/back/left/right were already read before this round. regen_rate
// was a NOT-independently-confirmed layout guess. The chunk is 36 bytes
// (9 fields), not 32 (8) — see SHLD_FX struct comment for the byte-level
// verification (against HELLCAT.IFF's raw bytes + the WC3 manual's printed
// Hellcat armor stats) that turned up the previously-truncated 9th field.
void RSEntity::parseREAL_OBJT_SSHP_SHLD_FGTR(uint8_t *data, size_t size) {
    if (this->m_parsingShield == nullptr || size < 36)
        return;
    ByteStream bs(data, size);
    this->m_parsingShield->regen_rate = bs.ReadUInt32LE();
    this->m_parsingShield->front = bs.ReadUInt32LE();
    this->m_parsingShield->back = bs.ReadUInt32LE();
    this->m_parsingShield->left = bs.ReadUInt32LE();
    this->m_parsingShield->right = bs.ReadUInt32LE();
    this->m_parsingShield->armor_front = bs.ReadUInt32LE();
    this->m_parsingShield->armor_side_a = bs.ReadUInt32LE();
    this->m_parsingShield->armor_back = bs.ReadUInt32LE();
    this->m_parsingShield->armor_side_b = bs.ReadUInt32LE();
}
// See CLOK_DATA's own comment (RSEntity.h) — shared between SSHP and MISL.
void RSEntity::parseREAL_OBJT_CLOK(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;
    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["DATA"] = std::bind(&RSEntity::parseREAL_OBJT_CLOK_DATA, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_OBJT_CLOK_DATA(uint8_t *data, size_t size) {
    if (size < 20)
        return;
    ByteStream bs(data, size);
    CLOK_DATA *clok = new CLOK_DATA();
    clok->field0 = bs.ReadInt32LE();
    clok->field1 = bs.ReadInt32LE();
    clok->field2 = bs.ReadInt32LE();
    clok->field3 = bs.ReadInt32LE();
    clok->duration = bs.ReadInt32LE();
    if (size >= 24) {
        clok->field5 = bs.ReadInt32LE();
    }
    this->clok = clok;
}
void RSEntity::parseREAL_OBJT_EXTE(uint8_t *data, size_t size) {

}
void RSEntity::parseREAL_OBJT_RNWY(uint8_t *data, size_t size) {
    this->entity_type = EntityType::rnwy;
}
void RSEntity::parseREAL_OBJT_DIST(uint8_t *data, size_t size) {
    // FORM DIST's own body is empty (just the 4-byte sub-tag) — everything
    // renderable lives in the sibling APPR>DFLT chunk, parsed generically.
    this->entity_type = EntityType::dist;
}
void RSEntity::parseREAL_APPR(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["POLY"] = std::bind(&RSEntity::parseREAL_APPR_POLY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["AFTB"] = std::bind(&RSEntity::parseREAL_APPR_POLY, this, std::placeholders::_1, std::placeholders::_2);
    // OBJT>DIST's APPR>DFLT (a single flat textured quad, e.g. WC3's galaxy
    // skybox backdrops) reuses the exact same VERT/TXMS/TXMP/QUAD/ATTR/XATR
    // sub-chunk layout as APPR>POLY's own ship-mesh format (already
    // confirmed byte-compatible between JETP and SSHP ships), just with far
    // fewer vertices/faces — no separate parser needed.
    handlers["DFLT"] = std::bind(&RSEntity::parseREAL_APPR_POLY, this, std::placeholders::_1, std::placeholders::_2);
    // OBJT>EXPL's own APPR>FLAT (e.g. EXPLODE.IFF's animated fireball) is
    // the same shape again: single quad, VERT/QUAD/ATTR/XATR identical to
    // POLY/DFLT. Its texture lives under a TXMV tag instead of TXMP/TXMA —
    // see parseREAL_APPR_POLY_TRIS_TXMS_TXMV.
    handlers["FLAT"] = std::bind(&RSEntity::parseREAL_APPR_POLY, this, std::placeholders::_1, std::placeholders::_2);
    // SHLDFX.IFF (the 3D shield-hit-effect entity named by every fighter's
    // own SSHP>SHLD>DATA chunk, loaded into RSEntity::shield->objct — see
    // parseREAL_OBJT_SSHP_SHLD_DATA) uses APPR>SHLD instead of APPR>POLY
    // for its actual mesh — user-confirmed (2026-07 session). Same
    // DETA/VERT/TXMS/TRIS/QUAD/ATTR/XATR sub-chunk shape as POLY/AFTB/
    // DFLT/FLAT (confirmed byte-for-byte against SHLDFX.IFF's own raw
    // dump), so it reuses the same parser rather than duplicating it —
    // its extra leading AINF chunk (8 bytes, no POLY equivalent) is
    // simply unregistered/skipped, same as any other unknown tag, and its
    // INFO chunk (1 byte here vs. POLY's 2) is harmless either way since
    // parseREAL_APPR_POLY_INFO is a no-op that never reads its payload.
    // Previously entirely unparsed — every fighter's shield-effect mesh
    // (triangles, textures, "SHIELD"/"TOPHALF" GRUP facet groups) was
    // silently dropped.
    handlers["SHLD"] = std::bind(&RSEntity::parseREAL_APPR_POLY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ANIM"] = std::bind(&RSEntity::parseREAL_APPR_ANIM, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_APPR_POLY(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["VERT"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_VERT, this, std::placeholders::_1, std::placeholders::_2);
    handlers["DETA"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TRIS"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["QUAD"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_QUAD, this, std::placeholders::_1, std::placeholders::_2);
    handlers["ATTR"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_ATTR, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TXMS"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_TXMS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LNTH"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_LNTH, this, std::placeholders::_1, std::placeholders::_2);
    // Capital ships only (e.g. VICTORY.IFF) — names a separate, flyable-into
    // IFF model for the ship's own hangar bay interior.
    handlers["SUPR"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_SUPR, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
// Checked every file in OBJECTS.TRE (460 files): LNTH never actually
// occurs anywhere. Registered defensively, not a real gap — nothing to
// parse.
void RSEntity::parseREAL_APPR_POLY_TRIS_LNTH(uint8_t *data, size_t size) {}
// entry_point_index(u16) + name(9, e.g. "VICTEST3\0" -> DATA\OBJECTS\
// VICTEST3.IFF) + parent_index (remaining bytes, up to 4, signed LE — -1/
// 0xFFFFFF for VICTORY.IFF meaning "no parent"). Confirmed via cross-check
// against an independent decoder (/home/james/development/wc-iff-loader).
void RSEntity::parseREAL_APPR_POLY_SUPR(uint8_t *data, size_t size) {
    constexpr size_t kHeader = 2;
    if (size < kHeader + 9)
        return;
    ByteStream bs(data, size);
    this->flight_deck_entry_point_index = bs.ReadUShort();
    this->flight_deck_name = bs.ReadString(9);
    size_t remaining = size - kHeader - 9;
    if (remaining > 0) {
        int32_t parentIndex = 0;
        for (size_t i = 0; i < remaining && i < 4; i++) {
            parentIndex |= (int32_t)bs.ReadByte() << (8 * i);
        }
        // Sign-extend if the top byte read looks like a negative value.
        if (remaining < 4 && (parentIndex & (1 << (8 * (int)remaining - 1)))) {
            parentIndex |= (int32_t)(~0u << (8 * remaining));
        }
        this->flight_deck_parent_index = parentIndex;
    }
    std::string tmpname = assetsManager.object_root_path + this->flight_deck_name + ".IFF";
    std::transform(tmpname.begin(), tmpname.end(), tmpname.begin(), ::toupper);
    TreEntry *entry = assetsManager.GetEntryByName(tmpname);
    if (entry != nullptr) {
        this->flight_deck_entity = new RSEntity();
        this->flight_deck_entity->is_interior_geometry = true;
        this->flight_deck_entity->InitFromRAM(entry->data, entry->size, tmpname);
    }
}
void RSEntity::parseREAL_APPR_POLY_INFO(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_APPR_POLY_VERT(uint8_t *data, size_t size) {
    if (size < 12)
        return;
    ByteStream stream(data, size);
    size_t numVertice = size / 12;
    for (int i = 0; i < numVertice; i++) {
        Point3D vertex;

        vertex.z = stream.ReadFixedFloatLE();
        vertex.x = stream.ReadFixedFloatLE();
        vertex.y = stream.ReadFixedFloatLE();

        AddVertex(&vertex);
    }
}
void RSEntity::parseREAL_APPR_POLY_DETA(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["LVL0"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LVL1"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LVL2"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LVL3"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    // WC3's engine-thrust subobjects (AFBURN1/AFBURN2) use 8 DETA levels
    // (LVL0-LVL7) instead of SC's usual 4: LVL0-LVL6 are the 7 throttle-
    // percentage thrust-glow states (0%..100%), LVL7 is the afterburner-
    // engaged flame. Same handler either way — Lod entries are just
    // appended in chunk order, consumed by index (this->lods[N]).
    handlers["LVL4"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LVL5"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LVL6"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);
    handlers["LVL7"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_DETA_LVLX, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_APPR_POLY_DETA_LVLX(uint8_t *data, size_t size) {
    if (size < 4)
        return;
    ByteStream stream(data, size);
    Lod lod;

    lod.numTriangles = static_cast<uint16_t>((size - 4) / 2);
    lod.dist = stream.ReadUInt32LE();

    for (int i = 0; i < lod.numTriangles; i++) {
        lod.triangleIDs[i] = stream.ReadUShort();
    }

    AddLod(&lod);
}
void RSEntity::parseREAL_APPR_POLY_ATTR(uint8_t *data, size_t size) {
    if (size < 9)
        return;
    size_t num_attr = size / 9;
    ByteStream stream(data, size);
    std::unordered_map<char, uint16_t> compteurs = {{'L', 0}, {'T', 0}, {'Q', 0}};
    for (size_t i = 0; i < num_attr; i++) {
        Attr *attr = new Attr();
        uint16_t id = stream.ReadUShort();
        attr->type = stream.ReadByte();
        attr->id = compteurs[attr->type]++;
        attr->props1 = stream.ReadByte();
        attr->props2 = stream.ReadByte();
        this->attrs[id] = attr;
        stream.ReadByte();
        stream.ReadByte();
        stream.ReadByte();
        stream.ReadByte();
    }
}
void RSEntity::parseREAL_APPR_POLY_TRIS(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["VTRI"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_VTRI, this, std::placeholders::_1, std::placeholders::_2);
    handlers["FACE"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_FACE, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TXMS"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_TXMS, this, std::placeholders::_1, std::placeholders::_2);
    handlers["UVXY"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_UVXY, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MAPS"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_MAPS, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}

void RSEntity::parseREAL_APPR_POLY_TRIS_VTRI(uint8_t *data, size_t size) {
    if (size < 8)
        return;
    size_t numTriangle = size / 8;
    ByteStream stream(data, size);

    Triangle triangle;
    for (int i = 0; i < numTriangle; i++) {

        triangle.property = stream.ReadByte();

        triangle.ids[0] = stream.ReadByte();
        triangle.ids[1] = stream.ReadByte();
        triangle.ids[2] = stream.ReadByte();

        triangle.color = stream.ReadByte();

        triangle.flags[0] = stream.ReadByte();
        triangle.flags[1] = stream.ReadByte();
        triangle.flags[2] = stream.ReadByte();

        AddTriangle(&triangle);
    }
}
void RSEntity::parseREAL_APPR_POLY_TRIS_FACE(uint8_t *data, size_t size) {
    if (size < 8)
        return;
    size_t numTriangle = size / 8;
    ByteStream stream(data, size);

    Triangle triangle;
    for (int i = 0; i < numTriangle; i++) {
        triangle.property = stream.ReadByte();
        triangle.color = stream.ReadByte();
        triangle.ids[0] = stream.ReadUShort();
        triangle.ids[1] = stream.ReadUShort();
        triangle.ids[2] = stream.ReadUShort();
        

        //triangle.flags[0] = stream.ReadByte();
        //triangle.flags[1] = stream.ReadByte();
        //triangle.flags[2] = stream.ReadByte();

        AddTriangle(&triangle);
    }
}
void RSEntity::parseREAL_APPR_POLY_TRIS_TXMS(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    handlers["INFO"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TXMP"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_TXMP, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TXMA"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_TXMA, this, std::placeholders::_1, std::placeholders::_2);
    handlers["TXMV"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_TXMV, this, std::placeholders::_1, std::placeholders::_2);
    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_INFO(uint8_t *data, size_t size) {}
// WC3's own animated-sprite texture format (used by effect entities like
// EXPLODE.IFF's APPR>FLAT, e.g. the "BOOM" fireball sequence): an 8-byte
// name (e.g. "BOOM\0\0\0\0") + a 10-byte header, then one WC3 "1.1x"
// shape-pak entry (same RLE codec as the cockpit's SVGA art and WRLD's
// STAR glints — see RSWC3Shape.h) holding N animation frames. The header
// (bytes 8-18, all u16LE) is confirmed redundant with fields already
// inside the shape-pak entry itself, cross-checked against 3 files
// (EXPLODE.IFF/"BOOM", MISSPOOF.IFF/"POOF", POW2.IFF/"POW2"): byte 0-2 is
// canvas_width-1 and byte 2-4 is canvas_height-1 (matches frame 0's own
// full_w/full_h header fields via the exact same "+1" encoding
// RSWC3DecodeShapeEntry already uses), and byte 4-6 exactly equals the
// shape-pak's own frame count N (e.g. both read 19 for EXPLODE, 14 for
// MISSPOOF, 11 for POW2). Bytes 6-8 (signed) and 8-10 don't have a
// confirmed meaning — not obviously a size/checksum/loop flag, only
// nonzero (=1) in one of the 3 files checked. None of this blocks
// decoding below, which finds the "1.1" marker directly and reads
// everything it needs from the shape-pak entry itself — the header is
// purely a redundant summary, not load-bearing.
void RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_TXMV(uint8_t *data, size_t size) {
    size_t markerOff = 0;
    bool found = false;
    for (size_t i = 0; i + 3 <= size; i++) {
        if (data[i] == '1' && data[i + 1] == '.' && data[i + 2] == '1') {
            markerOff = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }

    RSImageSet *frames = RSWC3DecodeShapeEntry(data + markerOff, size - markerOff);

    RSPalette palette;
    TreEntry *entries = assetsManager.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (entries != nullptr) {
        palette.initFromFileRam(entries->data, entries->size);
    } else {
        FileData *f = assetsManager.GetFileData("PALETTE.IFF");
        if (f != nullptr) {
            palette.initFromFileData(f);
        }
    }

    for (size_t i = 0; i < frames->GetNumImages(); i++) {
        RLEShape *shape = frames->GetShape(i);
        int w = shape->GetWidth();
        int h = shape->GetHeight();
        if (w <= 0 || h <= 0) {
            continue;
        }
        std::vector<uint8_t> indexBuf((size_t)w * h, 255);
        shape->buffer_size.x = w;
        shape->buffer_size.y = h;
        size_t byteRead = 0;
        shape->Expand(indexBuf.data(), &byteRead);

        Texture *tex = new Texture();
        tex->width = (size_t)w;
        tex->height = (size_t)h;
        tex->data = (uint8_t *)malloc((size_t)w * h * 4);
        uint8_t *dst = tex->data;
        for (int j = 0; j < w * h; j++) {
            Texel *rgba = palette.GetColorPalette()->GetRGBColor(indexBuf[j]);
            uint8_t a = (indexBuf[j] == 255) ? 0 : rgba->a;
            dst[0] = rgba->r;
            dst[1] = rgba->g;
            dst[2] = rgba->b;
            dst[3] = a;
            dst += 4;
        }
        tex->initialized = false;
        this->animations.push_back(tex);
    }
    delete frames;
}
void RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_TXMP(uint8_t *data, size_t size) {
    if (size < 14)
        return;
    ByteStream stream(data, size);

    RSImage *image = new RSImage();

    char name[9];

    for (int i = 0; i < 8; i++)
        name[i] = stream.ReadByte();

    uint32_t width = stream.ReadShort();
    uint32_t height = stream.ReadShort();

    image->Create(name, width, height, 0);
    uint8_t *src = stream.GetPosition();
    uint8_t *pic_data = nullptr;
    size_t csize = 0;
    if (src[0]=='L' && src[1]=='Z'){
        LZBuffer lzbuffer;
        pic_data = lzbuffer.DecodeLZW(src+2,size-14,csize);
        src = pic_data;
    } else if (src[0]=='P' && src[1]=='+'){
        PKWareDecompressor pkware;
        size_t remain = size - ((src+2) - data);
        size_t byte_read = 0;
        uint8_t *temp_data = pkware.DecompressPKWare(src+2,remain,csize);
        
        // Doubler chaque ligne
        size_t source_height = csize / width;
        size_t new_size = width * source_height * 2;
        size_t real_size = width * height;
        pic_data = (uint8_t*)malloc(width * height);
        size_t line = 0;
        for (line = 0; line < source_height; line++) {
            size_t src_offset = line * width;
            size_t dst_offset = line * width * 2;
            
            // Copier la ligne deux fois
            memcpy(pic_data + dst_offset, temp_data + src_offset, width);
            memcpy(pic_data + dst_offset + width, temp_data + src_offset, width);
        }
        if (line*2 < height) {
            // Si la hauteur cible est plus grande, remplir le reste avec des zéros
            memset(pic_data + line*2 * width, 0, (height - line*2) * width);
        }
        free(temp_data);
        csize = real_size;
        src = pic_data;
        image->flags = 1;
    }
    
    if (csize == width * height || pic_data == nullptr) {
        image->UpdateContent(src);
    } else {
        image->UpdateContent(src, csize);
    }
    
    AddImage(image);
}
void RSEntity::parseREAL_APPR_POLY_TRIS_TXMS_TXMA(uint8_t *data, size_t size) {
    if (size < 14)
        return;
    ByteStream stream(data, size);

    RSImage *image = new RSImage();

    char name[9];

    for (int i = 0; i < 8; i++)
        name[i] = stream.ReadByte();

    uint32_t width = stream.ReadShort();
    uint32_t height = stream.ReadShort();
    uint16_t nbframe = stream.ReadShort();
    
    
    uint8_t *src = stream.GetPosition();
    
    uint8_t *pic_data = nullptr;
    size_t csize = 0;
    if (src[0]=='L' && src[1]=='Z'){
        image->Create(name, width, height*nbframe, 0);
        image->nbframes = nbframe;
        LZBuffer lzbuffer;
        size_t remain = size - ((src+4) - data);
        size_t byte_read = 0;
        pic_data = lzbuffer.DecodeLZW(src+4,remain,csize);
        if (pic_data == nullptr) {
            printf("Error decoding image %s\n", name);
            return;
        }
        src = pic_data;
        image->UpdateContent(src);
        image->width = width;
        image->height = height;
        AddImage(image);
    }  else if (src[0]=='P' && src[1]=='+'){
        stream.ReadByte();
        stream.ReadByte();
        size_t next_frame_offset = 0;
        uint8_t *frame_data = nullptr;
        frame_data = (uint8_t*)malloc(width * height * nbframe);
        size_t remaind_bytes = size - 16;
        int current_frame_index = 0;
        while (remaind_bytes > 0) {
            uint16_t next_frame_offset = stream.ReadShort();
            PKWareDecompressor pkware;
            size_t remain = size - ((src+2) - data);
            std::vector<uint8_t> picture_pkware = stream.ReadBytes(next_frame_offset);
            size_t byte_read = 0;
            uint8_t *temp_data = pkware.DecompressPKWare(picture_pkware.data(),next_frame_offset,csize);
            size_t source_height = csize / width;
            size_t new_size = width * source_height * 2;
            size_t real_size = width * height;
            pic_data = (uint8_t*)malloc(width * height);
            size_t line = 0;
            for (line = 0; line < source_height; line++) {
                size_t src_offset = line * width;
                size_t dst_offset = line * width * 2;
                
                // Copier la ligne deux fois
                memcpy(pic_data + dst_offset, temp_data + src_offset, width);
                memcpy(pic_data + dst_offset + width, temp_data + src_offset, width);
            }
            if (line*2 < height) {
                // Si la hauteur cible est plus grande, remplir le reste avec des zéros
                memset(pic_data + line*2 * width, 0, (height - line*2) * width);
            }
            memcpy(frame_data + current_frame_index * width * height, pic_data, width * height);
            free(temp_data);
            char a = stream.ReadByte();
            char b = stream.ReadByte();
            remaind_bytes -= (next_frame_offset + 4);
            if (a == 'P' && b == '+') {
                current_frame_index++;
            } else {
                // On a atteint la fin des frames
                remaind_bytes = 0;
            }
            image->flags = 1;
        }
        image->Create(name, width, height*nbframe, 0);
        image->nbframes = nbframe;
        image->UpdateContent(frame_data);
        image->width = width;
        image->height = height;
        AddImage(image);
        free(frame_data);
        
    } else {
        image->Create(name, width, height*nbframe, 0);
        image->nbframes = nbframe;
        image->UpdateContent(src);
        image->width = width;
        image->height = height;
        AddImage(image);
    }
    
    
}
void RSEntity::parseREAL_APPR_POLY_TRIS_UVXY(uint8_t *data, size_t size) {
    if (size < 8)
        return;
    ByteStream stream(data, size);

    size_t numEntries = size / 8;

    uvxyEntry uvEntry;
    for (size_t i = 0; i < numEntries; i++) {

        uvEntry.triangleID = stream.ReadByte();
        uvEntry.textureID = stream.ReadByte();

        uvEntry.uvs[0].u = stream.ReadByte();
        uvEntry.uvs[0].v = stream.ReadByte();

        uvEntry.uvs[1].u = stream.ReadByte();
        uvEntry.uvs[1].v = stream.ReadByte();

        uvEntry.uvs[2].u = stream.ReadByte();
        uvEntry.uvs[2].v = stream.ReadByte();

        AddUV(&uvEntry);
    }
}
void RSEntity::parseREAL_APPR_POLY_TRIS_MAPS(uint8_t *data, size_t size) {
    if (size < 16)
        return;
    ByteStream stream(data, size);

    size_t numEntries = size / 16;

    uvxyEntry uvEntry;
    for (size_t i = 0; i < numEntries; i++) {

        uvEntry.triangleID = stream.ReadUShort();
        uvEntry.textureID = stream.ReadUShort();

        uvEntry.uvs[0].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry.uvs[0].v = stream.ReadByte();
        stream.ReadByte();
        uvEntry.uvs[1].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry.uvs[1].v = stream.ReadByte();
        stream.ReadByte();

        uvEntry.uvs[2].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry.uvs[2].v = stream.ReadByte();
        stream.ReadByte();

        AddUV(&uvEntry);
    }
}
void RSEntity::parseREAL_APPR_POLY_QUAD(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    
    handlers["FACE"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_QUAD_FACE, this, std::placeholders::_1, std::placeholders::_2);
    handlers["MAPS"] =
        std::bind(&RSEntity::parseREAL_APPR_POLY_QUAD_MAPS, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}

void RSEntity::parseREAL_APPR_POLY_QUAD_FACE(uint8_t *data, size_t size) {
    if (size < 10)
        return;
    size_t numQuad = size / 10;
    ByteStream stream(data, size);
    
    Quads *q;
    for (int i = 0; i < numQuad; i++) {
        uint8_t property = stream.ReadByte();
        uint8_t color = stream.ReadByte();
        uint16_t verts[4];
        verts[0] = stream.ReadUShort();
        verts[1] = stream.ReadUShort();
        verts[2] = stream.ReadUShort();
        verts[3] = stream.ReadUShort();

        q = new Quads();
        q->property = property;
        q->color = color;
        q->ids[0] = verts[0];
        q->ids[1] = verts[1];
        q->ids[2] = verts[2];
        q->ids[3] = verts[3];
        quads.push_back(q);
    }
}
void RSEntity::parseREAL_APPR_POLY_QUAD_MAPS(uint8_t *data, size_t size) {
    if (size < 20)
        return;
    ByteStream stream(data, size);

    size_t numEntries = size / 20;

    qmapuvxyEntry *uvEntry;
    for (size_t i = 0; i < numEntries; i++) {
        uvEntry = new qmapuvxyEntry();
        uvEntry->triangleID = stream.ReadUShort();
        uvEntry->textureID = stream.ReadUShort();

        uvEntry->uvs[0].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry->uvs[0].v = stream.ReadByte();
        stream.ReadByte();
        uvEntry->uvs[1].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry->uvs[1].v = stream.ReadByte();
        stream.ReadByte();

        uvEntry->uvs[2].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry->uvs[2].v = stream.ReadByte();
        stream.ReadByte();

        uvEntry->uvs[3].u = stream.ReadByte();
        stream.ReadByte();
        uvEntry->uvs[3].v = stream.ReadByte();
        stream.ReadByte();
        this->qmapuvs.push_back(uvEntry);
    }
}
void RSEntity::parseREAL_APPR_ANIM(uint8_t *data, size_t size) {
    IFFSaxLexer lexer;

    std::unordered_map<std::string, std::function<void(uint8_t * data, size_t size)>> handlers;
    
    handlers["INFO"] =
        std::bind(&RSEntity::parseREAL_APPR_ANIM_INFO, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SEQU"] =
        std::bind(&RSEntity::parseREAL_APPR_ANIM_SEQU, this, std::placeholders::_1, std::placeholders::_2);
    handlers["SHAP"] =
        std::bind(&RSEntity::parseREAL_APPR_ANIM_SHAP, this, std::placeholders::_1, std::placeholders::_2);

    lexer.InitFromRAM(data, size, handlers);
}
void RSEntity::parseREAL_APPR_ANIM_INFO(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_APPR_ANIM_SEQU(uint8_t *data, size_t size) {}
void RSEntity::parseREAL_APPR_ANIM_SHAP(uint8_t *data, size_t size) {
    if (size < 1)
        return;
    RSImageSet *img_set = new RSImageSet();
    PakArchive pak;
    uint8_t* data2 = (uint8_t*) malloc(size);
    memcpy(data2, data, size);
    if (data2[0]=='L' && data2[1]=='Z'){
        pak.InitFromRAM("SHAPE", data2, size);
        img_set->InitFromSubPakEntry(&pak);
    } else {
        pak.InitFromRAM("SHAPE", data2, size);
        img_set->InitFromSubPakEntry(&pak);    
    }
    
    RSPalette palette;
    
    TreEntry *entries = (TreEntry *)assetsManager.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (entries != nullptr) {
        palette.initFromFileRam(entries->data, entries->size);
    } else {
        FileData *f = assetsManager.GetFileData("PALETTE.IFF");
        if (f != nullptr) {
            palette.initFromFileData(f);
        }
    }

    for (auto img : img_set->shapes) {
        Texture *tex = new Texture();
        tex->width = img->GetWidth();
        tex->height = img->GetHeight();
        img->position.x = 0;
        img->position.y = 0;
        img->buffer_size.x = (int32_t) tex->width;
        img->buffer_size.y = (int32_t) tex->height;
        size_t imgsize = tex->width*tex->height;
        uint8_t *imgdata = (uint8_t *)malloc(imgsize);
        memset(imgdata, 255, imgsize);
        size_t byteRead = 0;
        img->Expand(imgdata, &byteRead);
        if (byteRead > imgsize) {
            printf("RLEShape::Expand failed\n");
        }
        
        tex->data = (uint8_t *)malloc(imgsize*4);
        memset(tex->data, 255, imgsize*4);
        uint8_t *dst = tex->data;
        long checksum = 0;
        for (size_t j = 0; j < imgsize; j++) {
            Texel *rgba = palette.GetColorPalette()->GetRGBColor(imgdata[j]);
            if (imgdata[j] == 255) {
                rgba->a = 0;
            }
            if (rgba->a == 255 && rgba->r == 211 && rgba->g == 211 && rgba->b == 211) {
                rgba->a = 0;
            }
            dst[0] = rgba->r;
            dst[1] = rgba->g;
            dst[2] = rgba->b;
            dst[3] = rgba->a;
            dst += 4;
        }
        tex->initialized = false;
        this->animations.push_back(tex);
    }
    this->images_set.push_back(img_set);
}
void RSEntity::parseREAL_OBJT_EXPL(uint8_t *data, size_t size) {
    this->entity_type = EntityType::explosion;
}
void RSEntity::parseREAL_OBJT_OMOB(uint8_t *data, size_t size) {
    this->entity_type = EntityType::object_mobile;
}
void RSEntity::parseREAL_OBJT_DEBR(uint8_t *data, size_t size) {
    this->entity_type = EntityType::debris;
}
void RSEntity::parseREAL_OBJT_SMKG(uint8_t *data, size_t size) {
    this->entity_type = EntityType::destroyed_object;
}