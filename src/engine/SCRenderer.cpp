//
//  gfx.cpp
//  iff
//
//  Created by Fabien Sanglard on 12/21/2013.
//  Copyright (c) 2013 Fabien Sanglard. All rights reserved.
//

#include "SCRenderer.h"
#include "GLBatch.h"
#include "Config.hpp"
#include "../realspace/AssetManager.h"
#include "../realspace/RSArea.h"
#include "../realspace/RSEntity.h"
#include "../realspace/RSPalette.h"

#include "RSVGA.h"
#include "Texture.h"
#include <SDL_opengl_glext.h>
#include <SDL_opengl.h>
#include <set>

#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>
#include <string>

SCRenderer &Renderer = SCRenderer::getInstance();

static inline void FixEntityWinding(RSEntity* obj) {
    if (!obj) return;
    Vector3D center{0,0,0};
    if (!obj->vertices.empty()) {
        for (const auto& v : obj->vertices) { center.x += v.x; center.y += v.y; center.z += v.z; }
        float invN = 1.0f / (float)obj->vertices.size();
        center.x *= invN; center.y *= invN; center.z *= invN;
    }
    // Interior geometry (RSEntity::is_interior_geometry, e.g. a capital
    // ship's flight_deck_entity) is a corridor meant to be viewed from
    // inside it, so its faces must point INWARD toward the mesh's own
    // centroid instead of away from it like every exterior-viewed model.
    float outwardSign = obj->is_interior_geometry ? -1.0f : 1.0f;
    // Flipping a triangle/quad's winding here means swapping ids[1]/ids[2]
    // (or ids[1]/ids[3] for a quad) in place — but the corresponding UV
    // data (object->uvs/qmapuvs) is a SEPARATE array, indexed by
    // triangleID, whose per-corner uv0/uv1/uv2 ordering was authored
    // against the file's ORIGINAL (unflipped) ids order and is never
    // touched here. Any face this heuristic flips becomes texture-mapped
    // with its corners paired to the wrong UV coordinates — reads as a
    // rotated/twisted texture — confirmed by live testing (player ship and
    // Hobbes, both HELLCATP, showing rotated textures once they finally
    // rendered lit/visible after other fixes). This used to only matter for
    // hull (which already unconditionally skipped the flip, see below) and
    // was partially papered over for the interior model with an empirical
    // 180-degree UV rotation hack — but it silently hit every other model
    // that underwent a flip, fighters included. Now that backface culling
    // is unconditionally disabled and lighting is unconditionally two-sided
    // for every model (see drawModel/drawModelColorPass/
    // drawModelTexturePass), winding direction has no remaining effect on
    // visibility or lighting — so this flip has nothing left to fix and
    // only breaks UV alignment. Skip it for everything.
    bool skipWindingFix = true;
    auto ensureTriOutward = [&](uint16_t ids[3]) {
        if (skipWindingFix) return;
        const Vector3D& a = obj->vertices[ids[0]];
        const Vector3D& b = obj->vertices[ids[1]];
        const Vector3D& c = obj->vertices[ids[2]];
        Vector3D e1{ b.x-a.x, b.y-a.y, b.z-a.z };
        Vector3D e2{ c.x-a.x, c.y-a.y, c.z-a.z };
        Vector3D n{ e1.y*e2.z - e1.z*e2.y,
                    e1.z*e2.x - e1.x*e2.z,
                    e1.x*e2.y - e1.y*e2.x };
        Vector3D faceC{ (a.x+b.x+c.x)/3.0f, (a.y+b.y+c.y)/3.0f, (a.z+b.z+c.z)/3.0f };
        Vector3D outward{ faceC.x-center.x, faceC.y-center.y, faceC.z-center.z };
        float d = outwardSign * (n.x*outward.x + n.y*outward.y + n.z*outward.z);
        if (d < 0.0f) std::swap(ids[1], ids[2]); // flip winding
    };
    for (auto& t : obj->triangles) ensureTriOutward(t.ids);
    for (auto* q : obj->quads) { // flip tout le quad si nécessaire
        if (!q) continue;
        uint16_t tri[3] = { q->ids[0], q->ids[1], q->ids[2] };
        uint16_t before[3] = { tri[0], tri[1], tri[2] };
        ensureTriOutward(tri);
        bool flipped = !(tri[0]==before[0] && tri[1]==before[1] && tri[2]==before[2]);
        if (flipped) std::swap(q->ids[1], q->ids[3]);
    }
}
static inline GLenum DetectTerrainFrontFace(AreaBlock* blk) {
    if (!blk || blk->sideSize < 2) return GL_CCW;
    MapVertex* a = &blk->vertice[0];
    MapVertex* b = &blk->vertice[1];
    MapVertex* c = &blk->vertice[blk->sideSize + 1];
    Vector3D e1{ b->v.x-a->v.x, b->v.y-a->v.y, b->v.z-a->v.z };
    Vector3D e2{ c->v.x-a->v.x, c->v.y-a->v.y, c->v.z-a->v.z };
    Vector3D n{ e1.y*e2.z - e1.z*e2.y,
                e1.z*e2.x - e1.x*e2.z,
                e1.x*e2.y - e1.y*e2.x };
    // On veut les faces "vers le haut" comme faces avant
    return (n.y >= 0.0f) ? GL_CCW : GL_CW;
}
static inline void normalizePlane(Plane& p) {
    float invLen = 1.0f / std::sqrt(p.a*p.a + p.b*p.b + p.c*p.c);
    p.a *= invLen; p.b *= invLen; p.c *= invLen; p.d *= invLen;
}

// Multiplie des matrices 4x4 (column-major OpenGL): out = A * B
static inline void mulMat4(const float A[16], const float B[16], float out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c*4 + r] =
                A[0*4 + r] * B[c*4 + 0] +
                A[1*4 + r] * B[c*4 + 1] +
                A[2*4 + r] * B[c*4 + 2] +
                A[3*4 + r] * B[c*4 + 3];
        }
    }
}
// Transformations auxiliaires (column-major OpenGL)
static inline Vector3D TransformPointCM(const float M[16], const Vector3D& v) {
    Vector3D out;
    out.x = M[0]*v.x + M[4]*v.y + M[8] *v.z + M[12];
    out.y = M[1]*v.x + M[5]*v.y + M[9] *v.z + M[13];
    out.z = M[2]*v.x + M[6]*v.y + M[10]*v.z + M[14];
    return out;
}
static inline Vector3D TransformDirCM3x3(const float M[16], const Vector3D& v) {
    Vector3D out;
    out.x = M[0]*v.x + M[4]*v.y + M[8] *v.z;
    out.y = M[1]*v.x + M[5]*v.y + M[9] *v.z;
    out.z = M[2]*v.x + M[6]*v.y + M[10]*v.z;
    return out;
}
static inline float ComputeLambertAt(const Vector3D& vLocal,
                                     const Vector3D& nLocal,
                                     const float MV[16],
                                     const Vector3D& lightEye,
                                     float ambient) {
    Vector3D vEye = TransformPointCM(MV, vLocal);
    Vector3D nEye = TransformDirCM3x3(MV, nLocal);
    nEye.Normalize();

    Vector3D L = lightEye;
    L.Substract(&vEye);
    L.Normalize();

    float d = nEye.DotProduct(&L);
    if (d < 0.0f) d = 0.0f;
    d += ambient;
    if (d > 1.0f) d = 1.0f;
    return d;
}
static inline float ComputeLambertAtTwoSided(const Vector3D& vLocal,
                                             const Vector3D& nLocal,
                                             const float MV[16],
                                             const Vector3D& lightEye,
                                             float ambient) {
    Vector3D vEye = TransformPointCM(MV, vLocal);
    Vector3D nEye = TransformDirCM3x3(MV, nLocal);
    nEye.Normalize();

    Vector3D L = lightEye;
    L.Substract(&vEye);
    L.Normalize();

    float d = nEye.DotProduct(&L);
    d = std::fabs(d);            // éclairage double-face
    d += ambient;
    if (d > 1.0f) d = 1.0f;
    return d;
}
void SCRenderer::extractFrustumPlanes(Plane planes[6]) const {
    float proj[16], modl[16], clip[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX,  modl);

    // MVP = PROJECTION * MODELVIEW
    mulMat4(proj, modl, clip);

    // Left, Right, Bottom, Top, Near, Far
    planes[0] = { clip[ 3] + clip[ 0], clip[ 7] + clip[ 4], clip[11] + clip[ 8], clip[15] + clip[12] }; // Left
    planes[1] = { clip[ 3] - clip[ 0], clip[ 7] - clip[ 4], clip[11] - clip[ 8], clip[15] - clip[12] }; // Right
    planes[2] = { clip[ 3] + clip[ 1], clip[ 7] + clip[ 5], clip[11] + clip[ 9], clip[15] + clip[13] }; // Bottom
    planes[3] = { clip[ 3] - clip[ 1], clip[ 7] - clip[ 5], clip[11] - clip[ 9], clip[15] - clip[13] }; // Top
    planes[4] = { clip[ 3] + clip[ 2], clip[ 7] + clip[ 6], clip[11] + clip[10], clip[15] + clip[14] }; // Near
    planes[5] = { clip[ 3] - clip[ 2], clip[ 7] - clip[ 6], clip[11] - clip[10], clip[15] - clip[14] }; // Far

    for (int i=0; i<6; ++i) normalizePlane(planes[i]);
}

bool SCRenderer::isAABBVisible(const AABB& box, const Plane planes[6]) {
    // Rendre le test moins strict: on accepte une petite pénétration négative
    const float cullingEpsilon = 15.0f; // ajustable
    for (int i=0; i<6; ++i) {
        const Plane& p = planes[i];
        Vector3D v;
        v.x = (p.a >= 0.0f) ? box.max.x : box.min.x;
        v.y = (p.b >= 0.0f) ? box.max.y : box.min.y;
        v.z = (p.c >= 0.0f) ? box.max.z : box.min.z;
        float dist = p.a*v.x + p.b*v.y + p.c*v.z + p.d;
        if (dist < -cullingEpsilon) {
            return false;
        }
    }
    return true;
}
void SCRenderer::bindCameraProjectionAndViewViewport(int32_t viewportW, int32_t viewportH, float verticalOffset /*=0.45f*/) {
    glViewport(0, 0, viewportW, viewportH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glTranslatef(0.0f, verticalOffset, 0.0f);
    glMultMatrixf(camera.getProjectionMatrix()->ToGL());

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(camera.getViewMatrix()->ToGL());
}

// Helper historique (viewport = taille fenêtre)
void SCRenderer::bindCameraProjectionAndView(float verticalOffset /*=0.45f*/) {
    bindCameraProjectionAndViewViewport(this->width, this->height, verticalOffset);
}
// Remplace computeBlockAABB pour utiliser un cache par RSArea
const AABB& SCRenderer::computeBlockAABB(RSArea* area, int LOD, int blockId) {
    auto& areaCache = aabbCache_[area].byKey;
    const uint64_t key = (static_cast<uint64_t>(LOD) << 32) | static_cast<uint32_t>(blockId);
    if (auto it = areaCache.find(key); it != areaCache.end()) return it->second;

    AABB box;
    box.min = {  std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity() };
    box.max = { -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() };

    AreaBlock* block = area->GetAreaBlockByID(LOD, blockId);
    // Defense-in-depth alongside the real fix (WC3Mission.cpp's
    // is_space_mission/world->tera check) — a block whose zip data never
    // actually loaded (e.g. a bad/missing terrain filename) can exist as an
    // object but with sideSize left uninitialized/corrupted rather than
    // the real ≤20 (vertice is a fixed 400 = 20×20 array), which turned
    // "x + y*sideSize" below into a genuine out-of-bounds read and
    // segfault. Treat an out-of-range sideSize the same as "no block"
    // rather than crashing.
    if (!block || block->sideSize == 0 || block->sideSize > 20) {
        box.min = {0,0,0}; box.max = {0,0,0};
        return areaCache.emplace(key, box).first->second;
    }

    const uint32_t s = block->sideSize;
    for (uint32_t y=0; y<s; ++y) {
        for (uint32_t x=0; x<s; ++x) {
            const auto& v = block->vertice[x + y * s].v;
            if (v.x < box.min.x) box.min.x = v.x;
            if (v.y < box.min.y) box.min.y = v.y;
            if (v.z < box.min.z) box.min.z = v.z;
            if (v.x > box.max.x) box.max.x = v.x;
            if (v.y > box.max.y) box.max.y = v.y;
            if (v.z > box.max.z) box.max.z = v.z;
        }
    }
    // Padding pour éviter les clips sur bords
    float stepX = 0.f, stepZ = 0.f;
    if (s > 1) {
        stepX = std::abs(block->vertice[1].v.x - block->vertice[0].v.x);
        stepZ = std::abs(block->vertice[s].v.z - block->vertice[0].v.z);
    }
    const float padX = (std::max)(100.0f, stepX * 2.0f);
    const float padZ = (std::max)(100.0f, stepZ * 2.0f);
    const float padY = 200.0f;

    box.min.x -= padX; box.max.x += padX;
    box.min.z -= padZ; box.max.z += padZ;
    box.min.y -= padY; box.max.y += padY;

    return areaCache.emplace(key, box).first->second;
}

// Invalidation globale
void SCRenderer::InvalidateAABBCache() {
    aabbCache_.clear();
}

// Invalidation pour une carte
void SCRenderer::InvalidateAABBCache(RSArea* area) {
    if (!area) return;
    aabbCache_.erase(area);
}

// Invalidation fine d’un bloc
void SCRenderer::InvalidateAABBForBlock(RSArea* area, int LOD, int blockId) {
    if (!area) return;
    auto itArea = aabbCache_.find(area);
    if (itArea == aabbCache_.end()) return;
    const uint64_t key = (static_cast<uint64_t>(LOD) << 32) | static_cast<uint32_t>(blockId);
    itArea->second.byKey.erase(key);
}

// Pré-calcul sur une plage de LOD (lazy-safe: réutilise computeBlockAABB)
void SCRenderer::PrecomputeAABBs(RSArea* area, int minLOD, int maxLOD) {
    if (!area) return;
    if (minLOD > maxLOD) std::swap(minLOD, maxLOD);
    for (int lod = minLOD; lod <= maxLOD; ++lod) {
        for (int i = 0; i < BLOCKS_PER_MAP; ++i) {
            (void)computeBlockAABB(area, lod, i);
        }
    }
}
// Helpers: calcule les normales par sommet pour un LOD donné (Gouraud)
static void AccumulateFaceNormal(const RSEntity* obj, int i0, int i1, int i2, const Point3D& camPos, std::vector<Vector3D>& acc) {
    // Must match FixEntityWinding's own cross-product convention
    // (e1 = ids[1]-ids[0], e2 = ids[2]-ids[0]) exactly, since that function
    // is what decides/fixes each triangle's winding to face outward. Using
    // the previous pivot-at-i1 order here (e1 = i0-i1, e2 = i2-i1) computes
    // the exact NEGATION of that outward normal for the same ids order —
    // confirmed by live testing: single-sided-lit models (regular fighters,
    // which don't get the two-sided/fabs lighting used for hull/interior
    // geometry) rendered fully dark until directly facing away from the
    // light, i.e. inverted normals.
    Vector3D pivot = obj->vertices[i0];
    Vector3D e1 = obj->vertices[i1]; e1.Substract(&pivot);
    Vector3D e2 = obj->vertices[i2]; e2.Substract(&pivot);
    Vector3D n = e1.CrossProduct(&e2);
    n.Normalize();
    // plus de flip vers la caméra
    acc[i0].x+=n.x; acc[i0].y+=n.y; acc[i0].z+=n.z;
    acc[i1].x+=n.x; acc[i1].y+=n.y; acc[i1].z+=n.z;
    acc[i2].x+=n.x; acc[i2].y+=n.y; acc[i2].z+=n.z;
}

static void ComputeVertexNormalsForLOD(RSEntity* obj, size_t lodLevel, const Point3D& camPos, std::vector<Vector3D>& outNormals) {
    outNormals.assign(obj->vertices.size(), Vector3D{0,0,0});
    if (lodLevel >= obj->NumLods()) return;

    const Lod* lod = &obj->lods[lodLevel];

    // Triangles utilisés par ce LOD
    for (int i = 0; i < lod->numTriangles; ++i) {
        uint16_t triID = lod->triangleIDs[i];
        if (obj->attrs.size() > 0) {
            if (obj->attrs[triID] == nullptr) continue;
            // ignorer quads et lignes dans ce passage
            if (obj->attrs[triID]->type == 'Q' || obj->attrs[triID]->type == 'L') continue;
            triID = obj->attrs[triID]->id;
        }
        if (triID >= obj->triangles.size()) continue;
        const Triangle& t = obj->triangles[triID];
        AccumulateFaceNormal(obj, t.ids[0], t.ids[1], t.ids[2], camPos, outNormals);
    }

    // Quads utilisés par ce LOD (comme faces, sans split pour l’accumulation)
    if (obj->quads.size() > 0) {
        for (int i = 0; i < lod->numTriangles; ++i) {
            uint16_t qid = lod->triangleIDs[i];
            if (obj->attrs.size() > 0) {
                if (obj->attrs[qid] == nullptr) continue;
                // ignorer triangles et lignes dans ce passage
                if (obj->attrs[qid]->type == 'T' || obj->attrs[qid]->type == 'L') continue;
                qid = obj->attrs[qid]->id;
            }
            if (qid >= obj->quads.size()) continue;
            const Quads* q = obj->quads[qid];
            // Accumuler avec deux triangles (quad -> 2 tris) pour lisser
            AccumulateFaceNormal(obj, q->ids[0], q->ids[1], q->ids[2], camPos, outNormals);
            AccumulateFaceNormal(obj, q->ids[0], q->ids[2], q->ids[3], camPos, outNormals);
        }
    }

    // Normalisation finale. A vertex never referenced by any triangle/quad
    // actually included in this LOD (attachment/mount-point markers,
    // vertices only used by a different LOD, etc.) never gets a face
    // contribution above and stays the zero vector — see Vector3D::Normalize
    // (Matrix.h) for why that used to poison lighting with NaN colors.
    for (auto& n : outNormals) {
        n.Normalize();
    }
}

SCRenderer::SCRenderer() : initialized(false) {}

SCRenderer::~SCRenderer() {}

Camera *SCRenderer::getCamera(void) { return &this->camera; }

VGAPalette *SCRenderer::getPalette(void) { return &this->palette; }

void SCRenderer::setPlayerPosition(Point3D *position) { camera.SetPosition(position); }

void SCRenderer::init(int width, int height) {
    AssetManager &assets = AssetManager::instance();
    Config &config = Config::instance();
    this->max_view_distance = config.getInt("Game", "max_view_distance", 160000);
    this->counter = 0;

    RSPalette palette;
    
    TreEntry *entries = (TreEntry *)assets.GetEntryByName("..\\..\\DATA\\PALETTE\\PALETTE.IFF");
    if (entries) {
        palette.initFromFileRam(entries->data, entries->size);    
        this->palette = *palette.GetColorPalette();
    }
    
    this->lodLevel = config.getInt("Game", "object_detail", 0);
    this->show_textured = config.getBool("Game", "show_texture", true);
    this->show_fog = config.getBool("Game", "show_fog", true);
    this->show_clouds = config.getBool("Game", "clouds_enabled", false);
    this->width = width;
    this->height = height;

    glClearColor(0.0f, 0.5f, 1.0f, 1.0f); // Black Background
    // glClearDepth(1.0f);								// Depth Buffer Setup
    glDisable(GL_DEPTH_TEST); // Disable Depth Testing
    glShadeModel(GL_SMOOTH);

    camera.setPersective(this->fov, this->width / (float)this->height, 1.5f, this->max_view_distance+this->max_view_distance*0.2f);

    light.SetWithCoo(300, 300, 300);
    initialized = true;
}
void SCRenderer::resetCameraPerspective() {
    camera.setPersective(this->fov, this->width / (float)this->height, 1.5f, this->max_view_distance+this->max_view_distance*0.2f);
}

void SCRenderer::setClearColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (!initialized)
        return;

    glClearColor(red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f);
}

void SCRenderer::clear(void) {

    if (!initialized)
        return;
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    
}

void SCRenderer::createTextureInGPU(Texture *texture) {

    if (!initialized)
        return;
    
    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD);

    glTexImage2D(GL_TEXTURE_2D, 0, 4, (GLsizei)texture->width, (GLsizei)texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 texture->data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

void SCRenderer::uploadTextureContentToGPU(Texture *texture) {
    if (!initialized)
        return;
   
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, (GLsizei)texture->width, (GLsizei)texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 texture->data);
}

void SCRenderer::deleteTextureInGPU(Texture *texture) {
    if (!initialized)
        return;
    glDeleteTextures(1, &texture->id);
}

void SCRenderer::drawTexturedQuad(Vector3D pos, Vector3D orientation, std::vector<Vector3D> quad, Texture *tex) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD);
    glEnable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
    glDisable(GL_CULL_FACE);
    if (!tex->initialized) {
        glGenTextures(1, &tex->id);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)tex->width, (GLsizei)tex->height, 
                     0, GL_RGBA, GL_UNSIGNED_BYTE, tex->data);
        
        tex->initialized = true;
    } else {
        glBindTexture(GL_TEXTURE_2D, tex->id);
    }
    
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    gb.color4f(1.0f, 1.0f, 1.0f, 0.0f);
    gb.begin(GL_QUADS);
    gb.texCoord2f(0.0f, 0.0f);
    gb.vertex3f(quad[0].x, quad[0].y, quad[0].z);
    gb.texCoord2f(1.0f, 0.0f);
    gb.vertex3f(quad[1].x, quad[1].y, quad[1].z);
    gb.texCoord2f(1.0f, 1.0f);
    gb.vertex3f(quad[2].x, quad[2].y, quad[2].z);
    gb.texCoord2f(0.0f, 1.0f);
    gb.vertex3f(quad[3].x, quad[3].y, quad[3].z);
    gb.end();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_CULL_FACE);
}

void SCRenderer::getNormal(RSEntity *object, Triangle *triangle, Vector3D *normal) {
    Vector3D edge1 = object->vertices[triangle->ids[0]];
    edge1.Substract(&object->vertices[triangle->ids[1]]);
    Vector3D edge2 = object->vertices[triangle->ids[2]];
    edge2.Substract(&object->vertices[triangle->ids[1]]);
    *normal = edge1.CrossProduct(&edge2);
    normal->Normalize();
    // plus d’orientation vers la caméra
}

void SCRenderer::drawParticle(Vector3D pos, float alpha) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glPushMatrix();
    Matrix smoke_rotation;
    smoke_rotation.Clear();
    smoke_rotation.Identity();
    smoke_rotation.translateM(pos.x, pos.y, pos.z);
    glMultMatrixf((float *)smoke_rotation.v);
    gb.begin(GL_QUADS);
    gb.color4f(1.0f,1.0f,1.0f, alpha);
    gb.vertex3f(1.0f,-1.0f,-1.0f);
    gb.vertex3f(1.0f,1.0f,-1.0f);
    gb.vertex3f(-1.0f,1.0f,-1.0f);
    gb.vertex3f(-1.0f,-1.0f,1.0f);
    gb.end();
    gb.begin(GL_QUADS);
    gb.color4f(1.0f,1.0f,1.0f, alpha);
    gb.vertex3f(-1.0f,-1.0f,-1.0f);
    gb.vertex3f(-1.0f,1.0f,-1.0f);
    gb.vertex3f(1.0f,1.0f,1.0f);
    gb.vertex3f(1.0f,-1.0f,1.0f);
    gb.end();
    glPopMatrix();
    glDisable( GL_BLEND );
}
void SCRenderer::drawModel(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation, Vector3D ajustement, float scale) { 
    glPushMatrix();
    glTranslatef(static_cast<GLfloat>(position.x), static_cast<GLfloat>(position.y),
                static_cast<GLfloat>(position.z));
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    glTranslatef(ajustement.x, ajustement.y, ajustement.z);
    glScalef(scale, scale, scale);
    drawModel(object, lodLevel);
    glPopMatrix(); 
}
void SCRenderer::drawModel(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation, Vector3D ajustement) { 
    glPushMatrix();
    glTranslatef(static_cast<GLfloat>(position.x), static_cast<GLfloat>(position.y),
                static_cast<GLfloat>(position.z));
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    glTranslatef(ajustement.x, ajustement.y, ajustement.z);
    drawModel(object, lodLevel);
    glPopMatrix(); 
}
void SCRenderer::drawModelFlatColor(RSEntity *object, size_t lodLevel, Vector3D color) {
    if (object == nullptr || object->vertices.empty() || object->lods.empty()) {
        return;
    }
    size_t currentLodLevel = lodLevel;
    if (currentLodLevel >= object->NumLods()) {
        currentLodLevel = object->NumLods() - 1;
    }
    Lod *lod = &object->lods[currentLodLevel];
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    gb.color3f(color.x, color.y, color.z);
    for (int i = 0; i < lod->numTriangles; i++) {
        uint16_t id = lod->triangleIDs[i];
        uint16_t realId = id;
        char type = 'T';
        bool hasAttr = object->attrs.size() > 0;
        if (hasAttr) {
            auto it = object->attrs.find(id);
            if (it == object->attrs.end() || it->second == nullptr) {
                continue;
            }
            type = it->second->type;
            if (type == 'L') {
                continue;
            }
            realId = it->second->id;
        }
        if (type == 'T' && realId < object->triangles.size()) {
            Triangle &t = object->triangles[realId];
            gb.begin(GL_TRIANGLES);
            for (int j = 0; j < 3; j++) {
                Vector3D v = object->vertices[t.ids[j]];
                gb.vertex3f(v.x, v.y, v.z);
            }
            gb.end();
        } else if (type == 'Q' && realId < object->quads.size()) {
            Quads *q = object->quads[realId];
            gb.begin(GL_QUADS);
            for (int j = 0; j < 4; j++) {
                Vector3D v = object->vertices[q->ids[j]];
                gb.vertex3f(v.x, v.y, v.z);
            }
            gb.end();
        }
    }
    // Do NOT re-enable GL_LIGHTING here: this engine never uses OpenGL's
    // fixed-function lighting (no glLightfv/glMaterial calls exist anywhere
    // in the codebase — every model's lighting is computed manually via
    // ComputeLambertAt/ComputeLambertAtTwoSided and applied through
    // glColor). With debugMagentaTurrets on, this function runs once per
    // frame per capital-ship turret; the stray glEnable(GL_LIGHTING) that
    // used to be here left lighting ON for every model drawn afterward in
    // that same frame, and since no GL light/material is ever configured,
    // GL's fallback (ambient-only, ~0.2 factor) rendered them nearly black —
    // confirmed by live testing (player ship/Hobbes went solid black,
    // reappearing only via the manual-lighting glColor when this state
    // wasn't yet clobbered).
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
}
void SCRenderer::drawModel(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation,
                            bool respectLodLevel) {
    glPushMatrix();
    glTranslatef(static_cast<GLfloat>(position.x), static_cast<GLfloat>(position.y),
                static_cast<GLfloat>(position.z));
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    drawModel(object, lodLevel, respectLodLevel);
    // Capital-ship gun turrets (SSHP>WEAP>CPTL>TURT, e.g. VICTORY.IFF —
    // see RSEntity::turrets/TURRET). Each mount sits at a fixed local
    // offset on the hull. Both the mount/base mesh and the separate
    // gun/barrel mesh get the same yaw+pitch and the same position —
    // matching wc-iff-loader's own turret handling (applyRotation bakes
    // the identical (yaw, pitch) into both baseM and gunM, then places
    // both at the same basePos), not a hierarchical "mount yaws only, gun
    // additionally pitches within that frame" scheme. No AI/targeting
    // drives yaw/pitch yet, so turrets currently just render at their
    // static mount orientation from the IFF data.
    if (object != nullptr) {
        for (auto turret : object->turrets) {
            if (turret == nullptr || turret->mount_objct == nullptr) {
                continue;
            }
            glPushMatrix();
            // Derived from the one thing actually confirmed correct — the
            // hull's own vertices (APPR>POLY>VERT): parsed 1st raw field
            // -> vertex.z, 2nd -> vertex.x, 3rd -> vertex.y, then used
            // directly with no further permutation at render time
            // (gb.vertex3f(v.x, v.y, v.z)), and the hull renders correctly.
            // TURT position is parsed in plain 1st->x, 2nd->y, 3rd->z order
            // (confirmed by directly decoding VICTORY.IFF's raw TURT bytes
            // — see parseREAL_OBJT_SSHP_WEAP_CPTL_TURT), so to land on the
            // same engine axes as a vertex whose raw fields matched, this
            // needs render_X = turret.y (2nd field), render_Y = turret.z
            // (3rd field), render_Z = turret.x (1st field). (Previously
            // tried matching RSEntity::AFTB's own render-time X/Z swap
            // instead, but that convention was never independently
            // confirmed correct either — don't propagate an unverified
            // sibling's guess.)
            glTranslatef(turret->y, turret->z, turret->x);
            // Rotation axes must transform under the same raw->render
            // permutation as position (raw1/x->renderZ, raw2/y->renderX,
            // raw3/z->renderY — see the derivation above), since an axis of
            // rotation is itself a direction in the same coordinate space.
            // Yaw (turret swivel) rotating around the file's own raw-Z (its
            // confirmed "up" axis, matching where vertex/position data
            // lands) becomes renderY under that permutation — unchanged
            // from before, and not implicated by the reported bug. Pitch
            // (barrel elevation) was left rotating around renderX (raw-Y)
            // by analogy to SCPlane's own pitch-around-X convention, but
            // that's a different coordinate space (already-remapped engine
            // space) and was never independently confirmed for turrets the
            // way position was — user-reported (2026-07 session, screenshot)
            // wrong-axis rotation on VICTORY.IFF's top turrets. Corrected to
            // raw-X -> renderZ, following the same permutation as position.
            // Bottom-mounted turrets are a real, distinct mount type — but
            // detecting them by position (turret->z < 0, the raw field that
            // maps to renderY) is unreliable: real TCRUISER.IFF data has 3
            // turrets sitting almost exactly on the centerline (z of -3.0/
            // -3.07/-3.0 — noise, not a real ventral mount) that a z<0 check
            // wrongly caught, flipping/reversing turrets that should render
            // like ordinary top-style ones (confirmed by their own yaw=0,
            // identical to the real top turrets — user-reported, 2026-07
            // session, "three turrets on TCRUISER look wrong"). The file's
            // own yaw field is the real, unambiguous marker instead: every
            // confirmed bottom turret (VICTORY.IFF and TCRUISER.IFF, 10
            // turrets total across both) reads exactly yaw=180, while every
            // top turret reads 0 or +-45 — never anything close to 180.
            // isBottomMount keys off that instead of position.
            bool isBottomMount = fabsf(turret->yaw - 180.0f) < 1.0f;
            // Two confirmed-separate real symptoms of bottom mounts (user
            // testing, 2026-07 session, VICTORY.IFF and TCRUISER.IFF): (1)
            // the base rendered entirely upside down — fixed below via an
            // extra 180-degree flip; (2) independently of that, they face
            // backward instead of forward — applying the file's own yaw=180
            // literally, on top of the flip, produces the opposite facing
            // from what an unflipped yaw=0 top turret shows. Corrected by
            // adding a further 180 degrees of yaw for bottom mounts
            // (yaw+180+180 = yaw+360, i.e. the same effective horizontal
            // facing as an ordinary yaw=0 top turret), independent of the
            // base flip.
            float turretYaw = turret->yaw + (isBottomMount ? 180.0f : 0.0f);
            glRotatef(turretYaw, 0, 1, 0);
            glRotatef(turret->pitch, 0, 0, 1);
            // Base-upside-down fix: the mount/gun meshes are presumably
            // authored assuming they sit on top of a surface; a ventral
            // mount needs an extra flip around a horizontal axis to point
            // away from the hull (downward) instead of into it. Applied as
            // the innermost rotation (right before drawModel, so it
            // corrects the mesh's own local orientation before yaw/pitch
            // act on it) rather than folded into yaw/pitch themselves,
            // since it's a structural mount correction, not part of the aim
            // angle. Axis choice (renderX) is the one remaining axis not
            // already used by yaw/pitch — confirmed correct by live testing
            // (base no longer upside down). Separately, the reported
            // "barrels point fore/aft instead of perpendicular to the mesh"
            // symptom affects turrets with pitch=0 too, so it isn't
            // explained by this fix or by pitch's rotation axis at all —
            // still unresolved, needs more real data (e.g. the gun mesh's
            // own default local facing direction) before guessing further.
            if (isBottomMount) {
                glRotatef(180.0f, 1, 0, 0);
            }
            if (this->debugMagentaTurrets) {
                drawModelFlatColor(turret->mount_objct, 0, {1.0f, 0.0f, 1.0f});
                if (turret->gun_objct != nullptr) {
                    drawModelFlatColor(turret->gun_objct, 0, {1.0f, 0.0f, 1.0f});
                }
            } else {
                drawModel(turret->mount_objct, 0);
                if (turret->gun_objct != nullptr) {
                    drawModel(turret->gun_objct, 0);
                }
            }
            glPopMatrix();
        }
        // WEAP>CPTL>CRGO: flight-deck cargo (parked fighters, crates, a
        // truck) — see RSEntity::cargo/CARGO_OBJECT. User-confirmed
        // (2026-07 session): these sit on the ship's own hangar deck.
        // deckY comes from flight_deck_entity's own real bounding box:
        // its vertices already share the hull's coordinate space (both
        // are drawn at the same actor position/orientation — see
        // SCStrike.cpp's showHangarInterior), so its bb.min.y is a real,
        // confirmed deck-floor height.
        //
        // X position (rowOffset, record-offset 29) is now REAL, decoded
        // data — found by comparing KCARRIER.IFF's straight rows of
        // Dralthi/Darket fighters (user: "KCARRIER.IFF has rows of
        // Dralthi and Darket fighters lined up on the flight deck")
        // against VICTORY.IFF: every KCARRIER record is byte-identical
        // except this field, which steps by an exact constant (200.0)
        // between consecutive fighters; VICTORY's own bay-row trio steps
        // by a constant -126.0 the same way, and the diagonal hellcats +
        // truck extend smoothly past the row's end — i.e. genuinely next
        // to it, with no hand-tuned offset needed. Used directly as the
        // local X coordinate (see CARGO_OBJECT's comment for the
        // magnitude cross-check against VICTEST3.IFF's real bounding box).
        //
        // This replaces the entire previous approach (multiple rounds of
        // hand-tuned slot/anchor/cluster placement based on yaw-sign
        // grouping alone), which kept being wrong because it was guessing
        // positions instead of reading them.
        //
        // Z (which wall a bay/cluster sits against) still has no decoded
        // field, so it's still approximated via yaw + deckOffsetY
        // grouping: cargo with yaw 0 sits against the bay-alcove wall
        // (biased toward max.z — user-confirmed after an earlier wrong
        // guess at min.z), including deckOffsetY-flagged equipment
        // (drums/crates, user-confirmed same wall as the row); "tbol_h"
        // (yaw +135, user: "the thunderbolt is on the side of the hangar
        // which doesn't have the hangar bays on it") goes on the opposite
        // wall — user-confirmed correct. The diagonal hellcats (yaw -135)
        // and the truck were first placed at the Z midpoint (open floor,
        // not against either wall) — user reported that was wrong, they
        // should be against a wall too, like the thunderbolt — so they now
        // share the thunderbolt's wall (noBayWallZ) instead of a separate
        // open-floor position. "box7" is a single confirmed exception
        // within the equipment group — user reported it specifically (not
        // the other drums/crates) is on the wrong wall, so it's hand-
        // pinned to noBayWallZ by name, same as "truck".
        if (object->flight_deck_entity != nullptr && !object->cargo.empty()) {
            BoudingBox *deckBB = object->flight_deck_entity->GetBoudingBpx();
            float deckY = deckBB->min.y;
            float deckZSpan = deckBB->max.z - deckBB->min.z;
            float bayRowZ = deckBB->min.z + deckZSpan * 0.85f;
            float noBayWallZ = deckBB->min.z + deckZSpan * 0.15f;

            for (auto *c : object->cargo) {
                if (c == nullptr || c->objct == nullptr) {
                    continue;
                }
                float z;
                if (c->name == "truck" || c->name == "box7" || c->yaw > 1.0f || c->yaw < -1.0f) {
                    z = noBayWallZ;
                } else {
                    z = bayRowZ;
                }
                glPushMatrix();
                glTranslatef(c->rowOffset, deckY, z);
                glRotatef(c->yaw, 0, 1, 0);
                // User-confirmed real behavior (2026-07 session): cargo
                // objects (WEAP>CPTL>CRGO) default to their LOD 1, not
                // LOD 0 — see drawModel(RSEntity*, size_t, bool)'s own
                // respectLodLevel comment for why passing a real index
                // requires that flag (otherwise this silently forces
                // LOD 0 like every other model on the ship).
                drawModel(c->objct, 1, true);
                glPopMatrix();
            }
        }
    }
    glPopMatrix();
}
void SCRenderer::drawPoint(Vector3D point, Vector3D color, Vector3D pos, Vector3D orientation) {
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    glPointSize(5.0f);
    gb.begin(GL_POINTS);
    gb.color3f(color.x, color.y, color.z);
    gb.vertex3f(point.x, point.y, point.z);
    gb.end();
    glPopMatrix();
}
void SCRenderer::drawModel(RSEntity *object, Vector3D position, Vector3D orientation) {
    glPushMatrix();
    Matrix rotation;
    rotation.Clear();
    rotation.Identity();
    rotation.translateM(position.x, position.y, position.z);
    rotation.rotateM(orientation.x, 0.0f, 1.0f, 0.0f);
    rotation.rotateM(orientation.y, 0.0f, 0.0f, 1.0f);
    rotation.rotateM(orientation.z, 1.0f, 0.0f, 0.0f);
    
    glMultMatrixf((float *)rotation.v);
    drawModel(object, lodLevel);
    glPopMatrix();
}
// Energy bolt: a diamond-cross-section prism that tapers to a sharp point
// at one end, reusing the exact vertex/face data authored in
// Laser_bolt.obj verbatim (10 quad faces, long axis along local +X) --
// replaces the old flat 2-quad cross-section shape from Energy_bolt.obj
// (user-confirmed 2026-07 session: shared by every energy weapon via
// getGunBoltColor's per-weapon recolor, same as before -- a shape swap
// only, not a per-weapon lookup). NOT remapped to -Z. Unlike SCPlane::
// forward (which treats ship-model -Z as forward, per ptw's own rotateM
// convention), the azimuthf/elevationf this function receives comes from
// GunSimulatedObject's cartesianToPolar(velocity): phi=atan2(x,z),
// theta=acos(y/r), so azimuthf=elevationf=0 (identity rotation) corresponds
// to a pure +X velocity, not -Z. Real WC3 missile meshes rendered through
// this same orient={azimuthf,elevationf,0} path are therefore authored nose
// along +X too -- confirmed the hard way: remapping to -Z put the bolt's
// long axis 90 degrees off (across the cockpit instead of away from it).
void SCRenderer::drawBolt(Vector3D position, Vector3D orientation, Vector3D color) {
    glPushMatrix();
    Matrix rotation;
    rotation.Clear();
    rotation.Identity();
    rotation.translateM(position.x, position.y, position.z);
    rotation.rotateM(orientation.x, 0.0f, 1.0f, 0.0f);
    rotation.rotateM(orientation.y, 0.0f, 0.0f, 1.0f);
    rotation.rotateM(orientation.z, 1.0f, 0.0f, 0.0f);
    glMultMatrixf((float *)rotation.v);
    // 1.5x scale per user direction (kept from the old Energy_bolt.obj
    // shape, unchanged here) -- not derived from Laser_bolt.obj itself.
    glScalef(1.5f, 1.5f, 1.5f);

    // User-confirmed real behavior (2026-07 session): single-sided, with
    // every normal facing inward instead of outward -- the reverse of a
    // normal solid's convention, and the opposite of the old drawBolt
    // behavior (which disabled culling entirely so both sides of every
    // face always drew, winding/normal direction irrelevant). Saved/
    // restored around this call like the old disable did, but now sets
    // an explicit GL_CCW/GL_BACK convention rather than trusting whatever
    // front-face state some earlier, unrelated draw call left behind --
    // this shape's own winding is reversed below (see `quad`) to actually
    // point inward under that explicit convention, not just whatever the
    // ambient state happened to be.
    GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLint prevCullFaceMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullFaceMode);
    GLint prevFrontFace = GL_CCW;
    glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);

    // Laser_bolt.obj's raw vertices (Blender export, 1-indexed v1..v12,
    // X negated below -- same "point leads, not trails" fix the old
    // Energy_bolt.obj shape needed, and for the same reason: +X is the
    // direction of travel (see this function's own comment above), and
    // the sharp taper (v9-v12, all coincident -- a real point in the raw
    // file) sits at the file's own -X end, which would trail backward
    // instead of leading if drawn as-authored. Unconfirmed against a live
    // render -- flag if the point ends up trailing instead of leading.
    Vector3D v1  = {5.960000f, -0.000000f, 0.565685f};
    Vector3D v2  = {5.960000f, 0.565685f, 0.000000f};
    Vector3D v3  = {5.960000f, -0.565685f, -0.000000f};
    Vector3D v4  = {5.960000f, 0.000000f, -0.565685f};
    Vector3D v5  = {-7.960000f, -0.000000f, 0.282843f};
    Vector3D v6  = {-7.960000f, 0.282843f, 0.000000f};
    Vector3D v7  = {-7.960000f, -0.282843f, -0.000000f};
    Vector3D v8  = {-7.960000f, 0.000000f, -0.282843f};
    Vector3D tip = {8.460001f, 0.000000f, 0.000000f}; // v9/v10/v11/v12: coincident in the source file

    // Reversed vertex order (d,c,b,a instead of a,b,c,d): flips the
    // winding computed under GL_CCW/GL_BACK above from outward- to
    // inward-facing (confirmed by direct cross-product computation on the
    // original a,b,c,d order, which came out outward-facing -- see this
    // function's own history/discussion). Call sites below are left
    // exactly as transcribed from Laser_bolt.obj's own face list for
    // readability/traceability; the flip happens once, here.
    auto quad = [&](Vector3D a, Vector3D b, Vector3D c, Vector3D d) {
        gb.begin(GL_QUADS);
        gb.color3f(color.x, color.y, color.z);
        gb.vertex3f(d.x, d.y, d.z);
        gb.vertex3f(c.x, c.y, c.z);
        gb.vertex3f(b.x, b.y, b.z);
        gb.vertex3f(a.x, a.y, a.z);
        gb.end();
    };
    // Faces transcribed directly from Laser_bolt.obj's own face list
    // (f 1 10 11 2 / f 3 7 8 4 / ... -- v10/v11/v12 all equal `tip`).
    quad(v1, tip, tip, v2);
    quad(v3, v7, v8, v4);
    quad(v7, v5, v6, v8);
    quad(v5, v1, v2, v6);
    quad(v3, v1, v5, v7);
    quad(v8, v6, v2, v4);
    quad(tip, tip, tip, tip); // degenerate in the source file (v10 v9 v12 v11, all coincident) -- kept for fidelity, draws nothing
    quad(v2, tip, tip, v4);
    quad(v3, tip, tip, v1);
    quad(v4, tip, tip, v3);

    glFrontFace(prevFrontFace);
    glCullFace(prevCullFaceMode);
    if (!cullWasEnabled) {
        glDisable(GL_CULL_FACE);
    }

    glPopMatrix();
}
void SCRenderer::drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation) {
    glPushMatrix();
    glTranslatef(start.x, start.y, start.z);
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    glLineWidth(1.2f);
    gb.begin(GL_LINES);
    gb.color3f(color.x, color.y, color.z);
    gb.vertex3f(0.0f,0.0f,0.0f);
    gb.vertex3f(end.x, end.y, end.z);
    gb.end();
    glPopMatrix();
}
void SCRenderer::drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation, Vector3D position) {
    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);
    glRotatef(orientation.x, 0, 1, 0);
    glRotatef(orientation.y, 0, 0, 1);
    glRotatef(orientation.z, 1, 0, 0);
    glLineWidth(1.2f);
    gb.begin(GL_LINES);
    gb.color3f(color.x, color.y, color.z);
    gb.vertex3f(start.x, start.y, start.z);
    gb.vertex3f(end.x, end.y, end.z);
    gb.end();
    glPopMatrix();
}
void SCRenderer::drawLine(Vector3D start, Vector3D end, Vector3D color) {
    glPushMatrix();
    glTranslatef(start.x, start.y, start.z);
    glLineWidth(2.2f);
    gb.begin(GL_LINES);
    gb.color3f(color.x, color.y, color.z);
    gb.vertex3f(0.0f,0.0f,0.0f);
    gb.vertex3f(end.x, end.y, end.z);
    gb.end();
    glPopMatrix();
}
void SCRenderer::drawSprite(Vector3D pos, Texture *tex, float zoom) {
    size_t cpt=0;
    
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD);
    glEnable(GL_BLEND);
    
    glPushMatrix();
    Matrix smoke_rotation;
    smoke_rotation.Clear();
    smoke_rotation.Identity();
    smoke_rotation.translateM(pos.x, pos.y, pos.z);
    smoke_rotation.rotateM(0.0f, 1.0f, 0.0f, 0.0f);

    glMultMatrixf((float *)smoke_rotation.v);
    if (tex->initialized == false) {
        glGenTextures(1, &tex->id);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        // Upload pixels into texture
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)tex->width, (GLsizei)tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            tex->data);
        
        tex->initialized = true;
    } else {
        glBindTexture(GL_TEXTURE_2D, tex->id);
    }
    float smoke_size = 4.0f * zoom + 1.0f;
    gb.begin(GL_QUADS);
    gb.color4f(1.0f,1.0f,1.0f,0.0f);
    gb.texCoord2f(0.0, 0.0);
    gb.vertex3f(smoke_size,-smoke_size,-smoke_size);
    gb.texCoord2f(1.0, 0.0);
    gb.vertex3f(smoke_size,smoke_size,-smoke_size);
    gb.texCoord2f(1.0, 1.0);
    gb.vertex3f(-smoke_size,smoke_size,-smoke_size);
    gb.texCoord2f(0.0, 1.0);
    gb.vertex3f(-smoke_size,-smoke_size,smoke_size);
    gb.end();
    gb.begin(GL_QUADS);
    gb.color4f(1.0f,1.0f,1.0f,0.0f);
    gb.texCoord2f(0.0, 0.0);
    gb.vertex3f(-smoke_size,-smoke_size,-smoke_size);
    gb.texCoord2f(1.0, 0.0);
    gb.vertex3f(-smoke_size,smoke_size,-smoke_size);
    gb.texCoord2f(1.0, 1.0);
    gb.vertex3f(smoke_size,smoke_size,smoke_size);
    gb.texCoord2f(0.0, 1.0);
    gb.vertex3f(smoke_size,-smoke_size,smoke_size);
    gb.end();
    glPopMatrix();
    glDisable( GL_BLEND );
    glDisable(GL_TEXTURE_2D);
}
void SCRenderer::drawModelWithChilds(
    RSEntity *object,
    size_t lodLevel,
    Vector3D position,
    Vector3D orientation,
    int wheel_index,
    int thrust,
    std::vector<std::tuple<Vector3D, RSEntity *>> weaps_load,
    bool afterburnerEngaged
) {
    if (object != nullptr) {
        glPushMatrix();
        Matrix rotation;
        rotation.Clear();
        rotation.Identity();
        rotation.translateM(position.x, position.y, position.z);
        rotation.rotateM(orientation.x, 0.0f, 1.0f, 0.0f);
        rotation.rotateM(orientation.y, 0.0f, 0.0f, 1.0f);
        rotation.rotateM(orientation.z, 1.0f, 0.0f, 0.0f);

        glMultMatrixf((float *)rotation.v);

        Renderer.drawModel(object, this->lodLevel);
        if (wheel_index) {
            if (object->chld.size() > wheel_index) {
                Renderer.drawModel(object->chld[wheel_index]->objct, this->lodLevel);
            }
        }
        if (thrust > 50) {
            if (object->chld.size() > 0) {
                glPushMatrix();
                Vector3D pos = {
                    (float) object->chld[0]->x,
                    (float) object->chld[0]->y,
                    (float) object->chld[0]->z
                };
                glTranslatef(pos.z/250 , pos.y /250 , pos.x /250);
                glScalef(1+thrust/100.0f,1,1);
                Renderer.drawModel(object->chld[0]->objct, this->lodLevel);
                glPopMatrix();
            }
        }
        // WC3 engine-thrust cones (SSHP>AFTB — see RSEntity.h/.cpp): the
        // named subobject (e.g. "AFBURN2") mounted at each of this ship's
        // engine positions, picking one of its 8 DETA levels by throttle.
        // LVL0-LVL6 are the 7 throttle-percentage thrust-glow states
        // (0%..100%); LVL7 is the afterburner-engaged flame, now driven by
        // the real AFTERBURNER key state (SCPlane::afterburner_engaged)
        // instead of the old thrust>=60 approximation.
        if (object->afterburner != nullptr && object->afterburner->objct != nullptr &&
            !object->afterburner->positions.empty() && !object->afterburner->objct->lods.empty()) {
            int clampedThrottle = thrust < 0 ? 0 : (thrust > 100 ? 100 : thrust);
            size_t maxLod = object->afterburner->objct->lods.size() - 1;
            size_t detaIndex;
            if (afterburnerEngaged) {
                detaIndex = (maxLod < 7) ? maxLod : 7;
            } else {
                size_t throttleMaxLod = (maxLod < 6) ? maxLod : 6;
                detaIndex = ((size_t)clampedThrottle * throttleMaxLod) / 100;
            }
            for (auto &mount : object->afterburner->positions) {
                glPushMatrix();
                // Same derivation as the capital-ship turret mount fix
                // (see the 4-arg drawModel(RSEntity*, size_t, Vector3D,
                // Vector3D) overload above): AFTB positions are parsed in
                // plain 1st->x, 2nd->y, 3rd->z order and already scaled to
                // world units at parse time (see
                // parseREAL_OBJT_SSHP_AFTB_DATA's /256.0f), so they need
                // the same axis remap as the hull's own vertices
                // (render_X = 2nd field, render_Y = 3rd field, render_Z =
                // 1st field) and no further scaling — this previously
                // divided by an extra 250 on top of the already-applied
                // /256 parse-time scale, and used a different (X/Z swap)
                // axis order that was never independently confirmed
                // correct.
                glTranslatef(mount.y, mount.z, mount.x);
                Renderer.drawModel(object->afterburner->objct, detaIndex, true);
                glPopMatrix();
            }
        }
        for (auto weaps_it: weaps_load) {
            float decy=0.5f;
            Vector3D weaps_pos = std::get<0>(weaps_it);
            RSEntity *weaps = std::get<1>(weaps_it);
            if (weaps == nullptr) {
                continue;  
            }
            
            glPushMatrix();
            glTranslatef(weaps_pos.x, weaps_pos.y, weaps_pos.z);
            Renderer.drawModel(weaps, this->lodLevel);
            glPopMatrix();            
        }
        glPopMatrix();
    }
}
void SCRenderer::drawModelColorPass(RSEntity *object, size_t lodLevel, std::vector<Vector3D> &vertexNormals, float ambientLamber, Vector3D lightEye, float *MV) {
    Lod *lod = &object->lods[lodLevel];
    static int interior_colorpass_debug = 0;
    static int hull_colorpass_debug = 0;
    bool isHull = !object->is_interior_geometry && object->flight_deck_entity != nullptr;
    static int dist_colorpass_debug = 0;
    bool debugColorPass = (object->is_interior_geometry && interior_colorpass_debug < 3) ||
                           (isHull && hull_colorpass_debug < 3) ||
                           (isHull && this->forceDebugDumpFrames > 0) ||
                           (object->entity_type == EntityType::dist && dist_colorpass_debug < 3);
    // FixEntityWinding's "away from the whole mesh's centroid" heuristic
    // isn't reliable for ANY model with concave detail relative to its own
    // centroid — not just large non-convex hulls/interiors, but ordinary
    // fighter models too (a canopy bubble, an intake recess): confirmed by
    // live testing (HELLCATP, used by both the player ship and Hobbes,
    // rendered with visible holes into its own interior from wrongly-culled
    // faces). Culling is unconditionally disabled below (see drawModel) for
    // every model now, so light every face two-sided here too rather than
    // trusting a winding-dependent normal that may point either way.
    bool forceTwoSidedLighting = true;
    int drawnTris = 0, skippedTris = 0, drawnQuads = 0, skippedQuads = 0;
    if (debugColorPass) {
        if (object->entity_type == EntityType::dist) { dist_colorpass_debug++; }
        else if (isHull) { hull_colorpass_debug++; } else { interior_colorpass_debug++; }
        printf("DEBUG ATTR KEYS (first 15 of %zu):", object->attrs.size());
        int n = 0;
        for (auto &kv : object->attrs) {
            if (n++ >= 15) break;
            printf(" %u(type=%c,id=%u)", kv.first, kv.second ? kv.second->type : '?', kv.second ? kv.second->id : 0);
        }
        printf("\n");
        printf("DEBUG LOD triangleIDs (first 15 of %u):", (unsigned)lod->numTriangles);
        for (int i = 0; i < lod->numTriangles && i < 15; i++) {
            printf(" %u", lod->triangleIDs[i]);
        }
        printf("\n");
        printf("DEBUG DIRECT LOOKUP for triangleIDs[0..14]:");
        for (int i = 0; i < lod->numTriangles && i < 15; i++) {
            uint16_t id = lod->triangleIDs[i];
            auto it = object->attrs.find(id);
            if (it == object->attrs.end()) {
                printf(" [%u:MISSING]", id);
            } else {
                Attr *a = it->second;
                printf(" [%u:type=%c,props1=%u,props2=%u,mappedId=%u]", id, a ? a->type : '?', a ? a->props1 : 0, a ? a->props2 : 0, a ? a->id : 0);
            }
        }
        printf("\n");
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Pass 1, draw color
    for (int i = 0; i < lod->numTriangles; i++) {
        uint16_t triangleID = lod->triangleIDs[i];
        if (object->attrs.size() > 0) {
            if (object->attrs[triangleID] == nullptr) {
                continue;
            }
            if (object->attrs[triangleID]->type == 'Q') {
                continue;
            }
            if (object->attrs[triangleID]->type == 'L') {
                continue;
            }
            triangleID = object->attrs[triangleID]->id;
        }
        if (triangleID >= object->triangles.size()) {
            continue;
        }
        Triangle *triangle = &object->triangles[triangleID];
        if (triangle->property == RSEntity::SC_TRANSPARENT) {
            continue;
        }
        float alpha = 1.0f;
        if (triangle->property == 6 && this->show_textured) {
            continue;

        }
        if (triangle->property == 9 && this->show_textured) {
            continue;
        }
        bool twoSided = forceTwoSidedLighting;
        if (triangle->flags[2] == 1) {
            twoSided = true; // If all vertices are at the same Z, we assume it's a 2D quad
        }
        if (twoSided) {
            glDisable(GL_CULL_FACE);
        }
        gb.begin(GL_TRIANGLES);
        for (int j = 0; j < 3; j++) {
            Vector3D vLocal = object->vertices[triangle->ids[j]];
            Vector3D nLocal = vertexNormals[triangle->ids[j]];

            float lambertianFactor = twoSided
                ? ComputeLambertAtTwoSided(vLocal, nLocal, MV, lightEye, ambientLamber)
                : ComputeLambertAt(vLocal, nLocal, MV, lightEye, ambientLamber);

            const Texel *texel = palette.GetRGBColor(triangle->color);
            gb.color4f(texel->r / 255.0f * lambertianFactor, texel->g / 255.0f * lambertianFactor,
                      texel->b / 255.0f * lambertianFactor, alpha);

            gb.vertex3f(vLocal.x, vLocal.y, vLocal.z);
        }
        gb.end();
        drawnTris++;
        // Culling is disabled for the whole model up in drawModel (every
        // model is now lit/rendered two-sided — see forceTwoSidedLighting),
        // so it must never be re-enabled mid-model here: doing so used to
        // reintroduce the exact "wrongly culled face" holes that disabling
        // it was meant to fix, once forceTwoSidedLighting stopped being
        // conditional on model type.
    }
    if (object->quads.size() > 0) {
        for (int i = 0; i < lod->numTriangles; i++) {
            uint16_t triangleID = lod->triangleIDs[i];
            int prop1 = 0;
            int prop2 = 0;
            if (object->attrs.size() > 0) {
                if (object->attrs[triangleID] == nullptr) {
                    continue;
                }
                if (object->attrs[triangleID]->type == 'T') {
                    continue;
                }
                if (object->attrs[triangleID]->type == 'L') {
                    continue;
                }
                prop1 = object->attrs[triangleID]->props1;
                prop2 = object->attrs[triangleID]->props2;
                triangleID = object->attrs[triangleID]->id;

            }
            if (triangleID >= object->quads.size()) {
                continue;
            }
            Quads *triangle = object->quads[triangleID];

            if (triangle->property == RSEntity::SC_TRANSPARENT) {
                continue;
            }
            float alpha = 1.0f;
            if (triangle->property == 6 && this->show_textured) {
                continue;

            }
            if (triangle->property == 9 && this->show_textured) {
                continue;

            }

            bool twoSided = forceTwoSidedLighting;


            twoSided = twoSided || (prop2 == 1);
            if (twoSided) {
                glDisable(GL_CULL_FACE);
            }
            gb.begin(GL_QUADS);
            for (int j = 0; j < 4; j++) {
                Vector3D vLocal = object->vertices[triangle->ids[j]];
                Vector3D nLocal = vertexNormals[triangle->ids[j]];

                float lambertianFactor = twoSided
                ? ComputeLambertAtTwoSided(vLocal, nLocal, MV, lightEye, ambientLamber)
                : ComputeLambertAt(vLocal, nLocal, MV, lightEye, ambientLamber);

                // No -1 here — matches this same function's triangle loop
                // above (palette.GetRGBColor(triangle->color), no offset)
                // and DebugObjectViewer.cpp's reference implementation
                // (palette.GetRGBColor(quad->color)). This quad loop was the
                // only one of the two using triangle->color-1, which for
                // color=0 wraps (uint8_t 0-1) to palette index 255 instead
                // of index 0.
                const Texel *texel = palette.GetRGBColor(triangle->color);
                gb.color4f(texel->r / 255.0f * lambertianFactor, texel->g / 255.0f * lambertianFactor,
                          texel->b / 255.0f * lambertianFactor, alpha);
                
                
                gb.vertex3f(vLocal.x, vLocal.y, vLocal.z);
            }
            gb.end();
            drawnQuads++;
            // Culling is disabled for the whole model up in drawModel
            // (every model is now rendered two-sided), so never re-enable
            // it mid-model here.
        }
    }
    if (debugColorPass) {
        printf("DEBUG COLORPASS: lod->numTriangles=%u drawnTris=%d drawnQuads=%d attrs.size=%zu triangles.size=%zu quads.size=%zu\n",
               (unsigned)lod->numTriangles, drawnTris, drawnQuads, object->attrs.size(), object->triangles.size(), object->quads.size());
    }
    glDisable(GL_BLEND);
}
void SCRenderer::drawModelTexturePass(RSEntity *object, size_t lodLevel, std::vector<Vector3D> &vertexNormals, float ambientLamber, Vector3D lightEye, float *MV) {
    static int interior_texpass_debug = 0;
    static int hull_texpass_debug = 0;
    bool isHullTex = !object->is_interior_geometry && object->flight_deck_entity != nullptr;
    static int dist_texpass_debug = 0;
    bool debugTexPass = (object->is_interior_geometry && interior_texpass_debug < 3) ||
                         (isHullTex && hull_texpass_debug < 3) ||
                         (isHullTex && this->forceDebugDumpFrames > 0) ||
                         (object->entity_type == EntityType::dist && dist_texpass_debug < 3);
    // Capital-ship hulls (flight_deck_entity != nullptr, e.g. VICTORY.IFF)
    // share the hangar-interior submesh's "packed texture atlas" shape: a
    // face's raw UV pixel coordinates only span a sub-rectangle of the
    // source image, not its full width/height. Dividing by the full
    // dimensions (the plain-fighter-model path below) under-fills the face
    // and samples past the sprite into the atlas's magenta colorkey
    // padding — confirmed by live testing (hull rendered as solid
    // magenta/black/white instead of its ship-panel texture). Per-face
    // min/max UV normalization (same fix already applied to the interior
    // model) fixes both the same way.
    bool usePerFaceUVNorm = object->is_interior_geometry || object->flight_deck_entity != nullptr;
    // FixEntityWinding's "away from the whole mesh's centroid" heuristic
    // isn't just unreliable for large non-convex hulls — it's also wrong
    // for ordinary fighter models with any concave detail relative to their
    // own centroid (a canopy bubble, an intake recess, a wingroot fillet):
    // confirmed by live testing (HELLCATP, used by both the player ship and
    // Hobbes, rendered with visible holes into its own interior — some
    // faces wrongly classified as backfaces and culled). Since culling is
    // now unconditionally disabled below (drawModel) for every model, not
    // just hull/interior, light every face two-sided here too rather than
    // trusting a winding-dependent normal that may point either way.
    bool forceTwoSidedLighting = true;
    // Some ships' texture atlases carry placeholder/reference images
    // literally named BACK/FRONT/BMD9/BSD1/BMDX5 (e.g. VICTORY.IFF's own
    // flight-deck-entrance opening) that were never meant to actually
    // render — confirmed against wc-iff-loader, which suppresses these same
    // named textures deliberately (isBackOrFrontLabel). Precompute which of
    // this model's own images match so both UV loops below can skip them.
    std::vector<bool> skipTextureImage(object->images.size(), false);
    for (size_t i = 0; i < object->images.size(); i++) {
        std::string imgName(object->images[i]->name, strnlen(object->images[i]->name, 8));
        while (!imgName.empty() && (imgName.back() == '\0' || imgName.back() == ' ')) {
            imgName.pop_back();
        }
        std::transform(imgName.begin(), imgName.end(), imgName.begin(), ::toupper);
        if (imgName == "BACK" || imgName == "FRONT" || imgName == "BMD9" || imgName == "BSD1" || imgName == "BMDX5") {
            skipTextureImage[i] = true;
        }
    }
    // drawModelColorPass and drawModelTransparentPass both only draw faces
    // that appear in the current LOD's own triangleIDs list; this pass used
    // to iterate object->uvs/qmapuvs directly instead, which cover every
    // UV-mapped face across the WHOLE model (all LOD levels' geometry
    // combined, not just this one) — so a multi-LOD capital ship like
    // VICTORY.IFF had every LOD's textured geometry drawn simultaneously
    // and overlapping, not just LOD0's. Confirmed by live testing (reported
    // as "3 linked cubes"/multiple LODs visibly overlapping) and by the
    // data itself: LOD0 here only selects 433 of the model's 623 total
    // triangle+quad entries, yet this pass drew UV data for all of them
    // unconditionally. Build the same LOD-membership set colorPass/
    // transparentPass already compute (raw triangleIDs -> attrs type/id
    // remap) and skip anything not actually part of this LOD.
    Lod *lod = &object->lods[lodLevel];
    std::vector<bool> triInLod(object->triangles.size(), false);
    std::vector<bool> quadInLod(object->quads.size(), false);
    for (int i = 0; i < lod->numTriangles; i++) {
        uint16_t id = lod->triangleIDs[i];
        if (object->attrs.size() > 0) {
            auto it = object->attrs.find(id);
            if (it == object->attrs.end() || it->second == nullptr) continue;
            if (it->second->type == 'T') {
                uint16_t realId = it->second->id;
                if (realId < triInLod.size()) triInLod[realId] = true;
            } else if (it->second->type == 'Q') {
                uint16_t realId = it->second->id;
                if (realId < quadInLod.size()) quadInLod[realId] = true;
            }
        } else {
            // No attrs metadata to disambiguate triangle vs quad — match
            // colorPass's own no-attrs fallback (raw id used directly as
            // the array index into whichever array is non-empty) by
            // marking both; permissive, but no worse than the old
            // fully-unfiltered behavior for models built this way.
            if (id < triInLod.size()) triInLod[id] = true;
            if (id < quadInLod.size()) quadInLod[id] = true;
        }
    }
    // Per-texture (not per-face) UV min/max, for usePerFaceUVNorm below.
    // Real user-reported bug (2026-07 session, VICTORY.IFF's hull): a
    // texture can legitimately be split across several adjacent faces —
    // e.g. one "40" marking image sliced in half across two neighboring
    // hull quads, confirmed by direct extraction (the two quads share an
    // edge in world space, and their raw UV ranges are non-overlapping,
    // contiguous halves of the same source image). Normalizing each face's
    // own min/max independently (the original per-FACE version of this)
    // stretches each half to fill its own face completely, so the same
    // image renders twice instead of split once — exactly the "texture
    // repeated twice" report. Aggregating the min/max across every face
    // that shares a texture ID fixes that (each half now normalizes against
    // the true combined span, so it lands at its real fractional position)
    // while leaving the original single-face "packed atlas" case (the one
    // usePerFaceUVNorm was first written for — a face using a small corner
    // of a much bigger shared sheet) unchanged, since a texture used by
    // only one face has an aggregate span identical to that face's own.
    std::unordered_map<uint16_t, std::pair<UV, UV>> texUVSpan;
    if (usePerFaceUVNorm) {
        auto foldSpan = [&](uint16_t texID, const UV* uvs, int count) {
            auto it = texUVSpan.find(texID);
            UV mn = uvs[0], mx = uvs[0];
            for (int k = 1; k < count; k++) {
                mn.u = std::min(mn.u, uvs[k].u); mx.u = std::max(mx.u, uvs[k].u);
                mn.v = std::min(mn.v, uvs[k].v); mx.v = std::max(mx.v, uvs[k].v);
            }
            if (it == texUVSpan.end()) {
                texUVSpan[texID] = {mn, mx};
            } else {
                it->second.first.u = std::min(it->second.first.u, mn.u);
                it->second.first.v = std::min(it->second.first.v, mn.v);
                it->second.second.u = std::max(it->second.second.u, mx.u);
                it->second.second.v = std::max(it->second.second.v, mx.v);
            }
        };
        for (int i = 0; i < object->NumUVs(); i++) {
            uvxyEntry* t = &object->uvs[i];
            if (t->triangleID >= triInLod.size() || !triInLod[t->triangleID]) continue;
            foldSpan(t->textureID, t->uvs, 3);
        }
        for (auto* quv : object->qmapuvs) {
            if (quv->triangleID >= quadInLod.size() || !quadInLod[quv->triangleID]) continue;
            foldSpan(quv->textureID, quv->uvs, 4);
        }
    }
    int skippedBadTexId = 0, skippedBadTriId = 0, drawnTexTris = 0, alphaZeroTris = 0, skippedNotInLodTris = 0;
    if (debugTexPass) {
        if (object->entity_type == EntityType::dist) { dist_texpass_debug++; }
        else if (isHullTex) { hull_texpass_debug++; } else { interior_texpass_debug++; }
        printf("DEBUG TEXPASS: NumUVs=%zu images.size=%zu triangles.size=%zu qmapuvs.size=%zu quads.size=%zu\n",
               object->NumUVs(), object->images.size(), object->triangles.size(), object->qmapuvs.size(), object->quads.size());
        for (int i = 0; i < object->NumUVs() && i < 15; i++) {
            uvxyEntry *t = &object->uvs[i];
            printf("DEBUG TEXPASS UV[%d]: triangleID=%u textureID=%u uv0=(%u,%u) uv1=(%u,%u) uv2=(%u,%u)\n",
                   i, t->triangleID, t->textureID, t->uvs[0].u, t->uvs[0].v, t->uvs[1].u, t->uvs[1].v, t->uvs[2].u, t->uvs[2].v);
        }
    }
    // Texture pass. This whole body used to be gated behind `lodLevel ==
    // 0` — a second, independent hardcoding of the same "every caller
    // always passes LOD 0" assumption drawModel(RSEntity*, size_t, bool)
    // used to bake in globally (see that function's own respectLodLevel
    // comment). Harmless as long as that was true, but once AFTB's
    // throttle/afterburner selection started actually requesting LOD 1-7
    // (respectLodLevel=true), this silently skipped the entire textured
    // draw for every one of those states — the engine-cone geometry was
    // still correctly selected/lit (drawModelColorPass has no such gate),
    // but property 6 covers 100% of this model's faces (user-reported,
    // 2026-07 session: confirmed via a live AFTB dump), and colorPass
    // defers every property-6/9 face to this pass — so with this pass
    // skipped, nothing drew at all for LOD>0. The triInLod/quadInLod
    // membership check below already correctly restricts rendering to
    // just the requested LOD's own faces, making this extra gate not just
    // wrong but redundant even for its original LOD-0-only use case.
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        for (int i = 0; i < object->NumUVs(); i++) {

            uvxyEntry *textInfo = &object->uvs[i];

            // Seems we have a textureID that we don't have :( !
            if (textInfo->textureID >= object->images.size()) {
                skippedBadTexId++;
                continue;
            }
            if (textInfo->triangleID >= object->triangles.size()) {
                skippedBadTriId++;
                continue;
            }
            if (!triInLod[textInfo->triangleID]) {
                skippedNotInLodTris++;
                continue;
            }
            if (skipTextureImage[textInfo->textureID]) {
                continue;
            }

            RSImage *image = object->images[textInfo->textureID];

            Texture *texture = image->GetTexture();
            Triangle *triangle = &object->triangles[textInfo->triangleID];
            drawnTexTris++;
            float alpha = 1.0f;
            bool twoSided = forceTwoSidedLighting;
            // property 6/9's alpha=0/depth-primed-duplicate treatment
            // assumes something else already drew depth at these faces
            // first (e.g. a canopy/glass overlay riding on the hull it
            // covers) — true for the rare ship faces this was built for,
            // but breaks any model where property 6/9 covers most/all of
            // its faces instead of a handful: nothing primes depth for
            // them, so they fail the depth test and never draw. Already
            // confirmed and bypassed for the hangar-interior submesh
            // (VICTEST3.IFF, 100% property 6) and capital-ship hulls; now
            // also confirmed for regular fighter models (HELLCATP: an
            // isolated flat-color test — bypassing this pass entirely —
            // rendered fine under the exact same transform, proving the
            // transform was never the problem) — bypass it everywhere
            // rather than guessing which models are "rare" enough to need
            // it.
            if (alpha == 0.0f) {
                alphaZeroTris++;
            }

            if (triangle->flags[2] == 1) {
                twoSided = true; // If all vertices are at the same Z, we assume it's a 2D quad
            }
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture->id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            // These two branches were swapped: alpha==1 (opaque, the normal
            // case — nearly every textured face) got GL_EQUAL with depth
            // writes off, which only passes where the depth buffer already
            // holds that exact value. Nothing primes it first (the color
            // pass defers property 6/9 faces to this pass instead of
            // drawing them), so every opaque textured triangle failed the
            // depth test and never drew — confirmed by live testing (valid
            // geometry/textures reached this code, zero visible pixels).
            // Opaque faces need depth writes on; only the alpha==0
            // (property 6/9) faces want the old EQUAL/no-write behavior, to
            // avoid disturbing depth already settled by whatever they're an
            // invisible/collision-only stand-in for. GL_LEQUAL (not
            // GL_LESS) for the opaque case: drawModelColorPass runs first
            // and already fills every non-6/9 face with a flat/solid color
            // at this exact same geometry (and therefore this exact same
            // depth) — models where most faces aren't property 6/9 (e.g.
            // VICTORY.IFF's actual hull, unlike the hangar-interior submesh
            // that happens to be 100% property 6) had their own texture
            // pass consistently lose the depth test against that identical
            // already-written depth, leaving only the flat color pass
            // visible — confirmed by live testing (hull rendered as
            // untextured solid-color polygons). LEQUAL lets the texture
            // pass legitimately win that tie instead of losing it.
            if (alpha == 0.0f) {
                glDepthFunc(GL_EQUAL);
                glDepthMask(GL_FALSE);
            } else {
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
            }

            if (twoSided) {
                glDisable(GL_CULL_FACE);
            }
            // For interior geometry, normalize this face's UV span, using
            // the min/max aggregated across every face that shares this
            // texture ID (texUVSpan, computed above) rather than just this
            // one face's own 3 corners — see texUVSpan's own comment for
            // why (a texture split across multiple faces must normalize
            // against the combined span, not fill each face independently).
            // Rather than dividing by the texture's full pixel dimensions:
            // a face's raw pixel coordinates only cover a sub-rectangle of
            // the source texture, so dividing by the full width/height
            // under-fills the face and the remaining gap repeats/tiles
            // under GL_REPEAT — confirmed by live testing (fine repeating
            // vertical stripes instead of one clean image spanning the
            // wall's full top-to-bottom extent).
            uint8_t triUMin = 255, triUMax = 0, triVMin = 255, triVMax = 0;
            if (usePerFaceUVNorm) {
                auto it = texUVSpan.find(textInfo->textureID);
                if (it != texUVSpan.end()) {
                    triUMin = it->second.first.u; triUMax = it->second.second.u;
                    triVMin = it->second.first.v; triVMax = it->second.second.v;
                }
            }
            gb.begin(GL_TRIANGLES);
            for (int j = 0; j < 3; j++) {
                Vector3D vLocal = object->vertices[triangle->ids[j]];
                Vector3D nLocal = vertexNormals[triangle->ids[j]];

                float lambertianFactor = twoSided
                ? ComputeLambertAtTwoSided(vLocal, nLocal, MV, lightEye, ambientLamber)
                : ComputeLambertAt(vLocal, nLocal, MV, lightEye, ambientLamber);

                const Texel *texel = palette.GetRGBColor(triangle->color-1);

                gb.color4f(lambertianFactor, lambertianFactor, lambertianFactor, 1.0f);

                if (usePerFaceUVNorm) {
                    float uRange = (triUMax > triUMin) ? (float)(triUMax - triUMin) : 1.0f;
                    float vRange = (triVMax > triVMin) ? (float)(triVMax - triVMin) : 1.0f;
                    float uNorm = (textInfo->uvs[j].u - triUMin) / uRange;
                    float vNorm = (textInfo->uvs[j].v - triVMin) / vRange;
                    if (isHullTex) {
                        // Capital-ship hulls need per-face UV min/max
                        // normalization (packed texture atlas — see
                        // usePerFaceUVNorm) but NOT the axis-swap rotation
                        // below: that rotation is specific to the
                        // hangar-interior model's own texture authoring,
                        // not a general "packed atlas" requirement.
                        // Confirmed by live testing: neither the interior's
                        // rotation direction nor its opposite looked right
                        // on VICTORY.IFF's hull; the plain (unrotated)
                        // mapping is what's actually needed here.
                        gb.texCoord2f(uNorm, vNorm);
                    } else {
                        // Hangar-interior model only (not the hull, not
                        // ship models — see isHullTex branch above and the
                        // plain-mapping else below). Rotation history (live
                        // user feedback, 2026-07 session, each step relative
                        // to the previous): started at (1-v, u) -> rotated
                        // 90 more to (1-u, 1-v) -> rotated 180 more, which
                        // lands back on the plain (u, v) mapping. Per-face
                        // min/max normalization (this whole usePerFaceUVNorm
                        // branch) still applies here even with no rotation —
                        // that's the separate "packed atlas" tiling fix, not
                        // this axis orientation.
                        gb.texCoord2f(uNorm, vNorm);
                    }
                } else {
                    float uNorm = textInfo->uvs[j].u / (float)texture->width;
                    float vNorm = textInfo->uvs[j].v / (float)texture->height;
                    gb.texCoord2f(uNorm, vNorm);
                }
                gb.vertex3f(vLocal.x, vLocal.y, vLocal.z);
            }
            gb.end();
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_TEXTURE_2D);
            // Culling is disabled for the whole model up in drawModel
            // (every model is now rendered two-sided), so never re-enable
            // it mid-model here.
        }
        if (debugTexPass) {
            printf("DEBUG TEXPASS TRI SUMMARY: drawnTexTris=%d skippedBadTexId=%d skippedBadTriId=%d alphaZeroTris=%d skippedNotInLodTris=%d\n",
                   drawnTexTris, skippedBadTexId, skippedBadTriId, alphaZeroTris, skippedNotInLodTris);
            int n = 0;
            for (auto quv : object->qmapuvs) {
                if (n++ >= 15) break;
                printf("DEBUG TEXPASS QUV[%d]: triangleID=%u textureID=%u uv0=(%u,%u) uv1=(%u,%u) uv2=(%u,%u) uv3=(%u,%u)\n",
                       n - 1, quv->triangleID, quv->textureID,
                       quv->uvs[0].u, quv->uvs[0].v, quv->uvs[1].u, quv->uvs[1].v,
                       quv->uvs[2].u, quv->uvs[2].v, quv->uvs[3].u, quv->uvs[3].v);
            }
        }
        int skippedBadQuadTexId = 0, skippedBadQuadTriId = 0, drawnTexQuads = 0, alphaZeroQuads = 0, skippedNotInLodQuads = 0;
        for (auto quv: object->qmapuvs) {
            if (quv->textureID >= object->images.size()) {
                skippedBadQuadTexId++;
                continue;
            }
            if (quv->triangleID >= object->quads.size()) {
                skippedBadQuadTriId++;
                continue;
            }
            if (!quadInLod[quv->triangleID]) {
                skippedNotInLodQuads++;
                continue;
            }
            if (skipTextureImage[quv->textureID]) {
                continue;
            }

            RSImage *image = object->images[quv->textureID];

            Texture *texture = image->GetTexture();
            Quads *triangle = object->quads[quv->triangleID];
            drawnTexQuads++;
            float alpha = 1.0f;
            // See the matching triangle-loop comment above — property 6/9's
            // depth-priming assumption is bypassed for every model now, not
            // just interiors/hulls.
            if (alpha == 0.0f) {
                alphaZeroQuads++;
            }

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture->id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            // Same swapped-branches/depth-race issue as the triangle loop
            // above — see that comment (GL_LEQUAL, not GL_LESS).
            if (alpha == 0.0f) {
                glDepthFunc(GL_EQUAL);
                glDepthMask(GL_FALSE);
            } else {
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
            }

            bool twoSided = forceTwoSidedLighting;
            for (auto attr : object->attrs) {
                if (attr.second->id == quv->triangleID && attr.second->type == 'Q') {
                    int prop1 = attr.second->props1;
                    int prop2 = attr.second->props2;

                    twoSided = twoSided || (prop2 == 1);
                    break;
                }
            }
            if (twoSided) {
                glDisable(GL_CULL_FACE);
            }
            // See texUVSpan's own comment (triangle loop above) — aggregated
            // across every face sharing this texture ID, not just this quad.
            uint8_t quadUMin = 255, quadUMax = 0, quadVMin = 255, quadVMax = 0;
            if (usePerFaceUVNorm) {
                auto it = texUVSpan.find(quv->textureID);
                if (it != texUVSpan.end()) {
                    quadUMin = it->second.first.u; quadUMax = it->second.second.u;
                    quadVMin = it->second.first.v; quadVMax = it->second.second.v;
                }
            }
            gb.begin(GL_QUADS);
            for (int j = 0; j < 4; j++) {
                Vector3D vLocal = object->vertices[triangle->ids[j]];
                Vector3D nLocal = vertexNormals[triangle->ids[j]];

                float lambertianFactor = twoSided
                ? ComputeLambertAtTwoSided(vLocal, nLocal, MV, lightEye, ambientLamber)
                : ComputeLambertAt(vLocal, nLocal, MV, lightEye, ambientLamber);

                const Texel *texel = palette.GetRGBColor(triangle->color-1);
                gb.color4f(lambertianFactor, lambertianFactor, lambertianFactor, 1.0f);
                if (usePerFaceUVNorm) {
                    float uRange = (quadUMax > quadUMin) ? (float)(quadUMax - quadUMin) : 1.0f;
                    float vRange = (quadVMax > quadVMin) ? (float)(quadVMax - quadVMin) : 1.0f;
                    float uNorm = (quv->uvs[j].u - quadUMin) / uRange;
                    float vNorm = (quv->uvs[j].v - quadVMin) / vRange;
                    if (isHullTex) {
                        // See the matching triangle-loop comment above —
                        // capital-ship hulls use the plain (unrotated)
                        // per-face-normalized mapping.
                        gb.texCoord2f(uNorm, vNorm);
                    } else {
                        // See the matching triangle-loop comment above.
                        gb.texCoord2f(uNorm, vNorm);
                    }
                } else {
                    float uNorm = quv->uvs[j].u / (float)texture->width;
                    float vNorm = quv->uvs[j].v / (float)texture->height;
                    gb.texCoord2f(uNorm, vNorm);
                }
                gb.vertex3f(vLocal.x, vLocal.y, vLocal.z);
            }
            gb.end();

            // Culling is disabled for the whole model up in drawModel
            // (every model is now rendered two-sided), so never re-enable
            // it mid-model here.
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_TEXTURE_2D);
        }
        if (debugTexPass) {
            printf("DEBUG TEXPASS QUAD SUMMARY: drawnTexQuads=%d skippedBadQuadTexId=%d skippedBadQuadTriId=%d alphaZeroQuads=%d skippedNotInLodQuads=%d\n",
                   drawnTexQuads, skippedBadQuadTexId, skippedBadQuadTriId, alphaZeroQuads, skippedNotInLodQuads);
        }
        glDisable(GL_BLEND);
    }
}
void SCRenderer::drawModelTransparentPass(RSEntity *object, size_t lodLevel, std::vector<Vector3D> &vertexNormals, float ambientLamber, Vector3D lightEye, float *MV) {
    // Pass 3: Let's draw the transparent stuff render RSEntity::SC_TRANSPARENT)
    Lod *lod = &object->lods[lodLevel];
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
#ifndef _WIN32
    glBlendEquation(GL_ADD);
#else
    typedef void(APIENTRY * PFNGLBLENDEQUATIONPROC)(GLenum mode);
    PFNGLBLENDEQUATIONPROC glBlendEquation = NULL;
    glBlendEquation = (PFNGLBLENDEQUATIONPROC)wglGetProcAddress("glBlendEquation");
    glBlendEquation(GL_FUNC_ADD);
#endif
    for (int i = 0; i < lod->numTriangles; i++) {
        uint16_t triangleID = lod->triangleIDs[i];
        if (object->attrs.size() > 0) {
            if (object->attrs[triangleID] == nullptr) {
                continue;
            }
            if (object->attrs[triangleID]->type == 'Q') {
                continue;
            }
            if (object->attrs[triangleID]->type == 'L') {
                continue;
            }
            triangleID = object->attrs[triangleID]->id;
        }
        if (triangleID >= object->triangles.size()) {
            continue;
        }
        Triangle *triangle = &object->triangles[triangleID];
        bool twoSided = false;
        if (triangle->property != RSEntity::SC_TRANSPARENT)
            continue;
        if (triangle->flags[2] == 1) {
            twoSided = true; // If all vertices are at the same Z, we assume it's a 2D quad
        }
        if (twoSided) {
            glDisable(GL_CULL_FACE);
        }
        gb.begin(GL_TRIANGLES);
        for (int j = 0; j < 3; j++) {
            Vector3D vLocal = object->vertices[triangle->ids[j]];
            Vector3D nLocal = vertexNormals[triangle->ids[j]];

            float lambertianFactor = twoSided
                ? ComputeLambertAtTwoSided(vLocal, nLocal, MV, lightEye, ambientLamber)
                : ComputeLambertAt(vLocal, nLocal, MV, lightEye, ambientLamber);

            const Texel *texel = palette.GetRGBColor(triangle->color-1);
            gb.color4f(texel->r / 255.0f * lambertianFactor, texel->g / 255.0f * lambertianFactor,
                      texel->b / 255.0f * lambertianFactor, texel->a);

            gb.vertex3f(vLocal.x, vLocal.y, vLocal.z);
        }
        gb.end();
        // Culling is disabled for the whole model up in drawModel, so never
        // re-enable it mid-model here.
    }

    if (object->quads.size() > 0) {
        for (int i = 0; i < lod->numTriangles; i++) {
            uint16_t triangleID = lod->triangleIDs[i];
            if (object->attrs.size() > 0) {
                if (object->attrs[triangleID]->type == 'T') {
                    continue;
                }
                if (object->attrs[triangleID]->type == 'L') {
                    continue;
                }
                triangleID = object->attrs[triangleID]->id;
            }
            if (triangleID >= object->triangles.size()) {
                continue;
            }
            if (triangleID >= object->quads.size()) {
                continue;
            }
            Quads *triangle = object->quads[triangleID];
            bool twoSided = false;
            if (object->attrs[triangleID]->props2 == 1) {
                twoSided = true;
            }
            if (triangle->property != RSEntity::SC_TRANSPARENT)
                continue;

            if (twoSided) {
                glDisable(GL_CULL_FACE);
            }
            gb.begin(GL_QUADS);
            for (int j = 0; j < 4; j++) {
                Vector3D vLocal = object->vertices[triangle->ids[j]];
                Vector3D nLocal = vertexNormals[triangle->ids[j]];

                float lambertianFactor = twoSided
                ? ComputeLambertAtTwoSided(vLocal, nLocal, MV, lightEye, ambientLamber)
                : ComputeLambertAt(vLocal, nLocal, MV, lightEye, ambientLamber);

                const Texel *texel = palette.GetRGBColor(triangle->color-1);
                gb.color4f(texel->r / 255.0f * lambertianFactor, texel->g / 255.0f * lambertianFactor,
                          texel->b / 255.0f * lambertianFactor, texel->a);

                gb.vertex3f(vLocal.x, vLocal.y, vLocal.z);
            }
            gb.end();
            // Culling is disabled for the whole model up in drawModel
            // (every model is now rendered two-sided), so never re-enable
            // it mid-model here.
        }    
    }
    
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}
void SCRenderer::drawModel(RSEntity *object, size_t lodLevel, bool respectLodLevel) {
    if (!initialized)
        return;

    // TEMP DEBUG: galaxy skybox (DIST) billboards confirmed positioned
    // correctly (magenta-quad test) but the real textured drawModel draw
    // still isn't visible — dump everything the LOD-membership filter in
    // drawModelColorPass/drawModelTexturePass actually needs, right here
    // (rate-limited per unique entity pointer, not a global call count —
    // the earlier unconditional version printed every frame for every one
    // of the 6 skybox entities and buried the one-time debugColorPass/
    // debugTexPass dumps, which never showed up in the captured output).
    static std::set<RSEntity*> distDebugSeen;
    if (object->entity_type == EntityType::dist && distDebugSeen.find(object) == distDebugSeen.end() && distDebugSeen.size() < 6) {
        distDebugSeen.insert(object);
        printf("DEBUG DIST: verts=%zu tris=%zu quads=%zu attrs=%zu images=%zu lods=%zu prepared=%d\n",
               object->vertices.size(), object->triangles.size(), object->quads.size(),
               object->attrs.size(), object->images.size(), object->lods.size(), object->prepared);
        // Which local-space plane the flat quad actually lies in by default
        // (which axis is ~constant across all 4 corners) — renderSkybox's
        // rotation table assumes it knows this without ever having checked.
        for (size_t vi = 0; vi < object->vertices.size(); vi++) {
            Vector3D &v = object->vertices[vi];
            printf("DEBUG DIST VERT[%zu]: (%.2f, %.2f, %.2f)\n", vi, v.x, v.y, v.z);
        }
        for (auto img : object->images) {
            printf("DEBUG DIST IMG: w=%zu h=%zu texId=%u\n", img->width, img->height, img->GetTexture()->getTextureID());
        }
        for (auto &kv : object->attrs) {
            printf("DEBUG DIST ATTR: key=%u type=%c id=%u props1=%u props2=%u\n",
                   kv.first, kv.second ? kv.second->type : '?', kv.second ? kv.second->id : 0,
                   kv.second ? kv.second->props1 : 0, kv.second ? kv.second->props2 : 0);
        }
        for (auto quv : object->qmapuvs) {
            printf("DEBUG DIST QMAPUV: triangleID(quadIdx)=%u textureID=%u\n", quv->triangleID, quv->textureID);
        }
        if (object->lods.size() > 0) {
            Lod &lod0 = object->lods[0];
            printf("DEBUG DIST LOD0: numTriangles(faceIDs)=%u ids=[", (unsigned)lod0.numTriangles);
            for (int i = 0; i < lod0.numTriangles; i++) printf("%u ", lod0.triangleIDs[i]);
            printf("]\n");
        }
    }

    if (object->vertices.size() == 0)
        return;

    // One-time per-entity setup (texture upload + winding fix) that every
    // caller of this, the real gameplay draw path, always skipped — only
    // the debug-only displayModel() ever called prepare()/FixEntityWinding.
    // Without a winding fix, raw WC3 model data whose triangle winding
    // doesn't already match glFrontFace(GL_CW) below gets entirely
    // backface-culled: confirmed by live testing — real geometry loaded
    // fine (vertices/LODs present, drawModel reached, no early return) but
    // nothing was visible, while simple non-mesh primitives (a debug
    // Renderer.drawPoint, the parametric background sphere) rendered fine.
    if (!object->IsPrepared()) {
        prepare(object);
    }

    static int interior_debug_count = 0;
    static int hull_debug_count = 0;
    bool isHullCall = !object->is_interior_geometry && object->flight_deck_entity != nullptr;
    bool debugThisCall = (object->is_interior_geometry && interior_debug_count < 3) ||
                          (isHullCall && hull_debug_count < 3) ||
                          (isHullCall && this->forceDebugDumpFrames > 0);
    if (debugThisCall) {
        if (isHullCall) { hull_debug_count++; } else { interior_debug_count++; }
        printf("DEBUG INTERIOR: prepared=%d verts=%zu tris=%zu quads=%zu attrs=%zu images=%zu lods=%zu\n",
               object->prepared, object->vertices.size(), object->triangles.size(), object->quads.size(),
               object->attrs.size(), object->images.size(), object->lods.size());
        for (auto img : object->images) {
            printf("DEBUG INTERIOR IMG: w=%zu h=%zu texId=%u\n",
                   img->width, img->height, img->GetTexture()->getTextureID());
        }
    }

    // Wing Commander 3 shipped in 1995; even its highest-detail LOD is
    // trivial for any modern GPU, so always use it rather than the
    // passed-in/config-driven level (ignoring `lodLevel` entirely here) —
    // unless the caller explicitly says `lodLevel` is a real state
    // selector, not a quality tier (see this overload's own header
    // comment).
    size_t currentLodLevel = respectLodLevel ? lodLevel : LOD_LEVEL_MAX;
    if (currentLodLevel >= object->NumLods()) {
        currentLodLevel = object->NumLods() - 1;
    }

    for (auto img: object->images) {
        if (img->nbframes > 1) {
            img->GetNextFrame();
        }
    }
    
    float ambientLamber = 0.4f;

    Lod *lod = &object->lods[currentLodLevel];
    std::vector<Vector3D> vertexNormals;
    ComputeVertexNormalsForLOD(object, currentLodLevel, camera.getPosition(), vertexNormals);

    // Prépare les matrices pour faire l’éclairage en espace œil
    float MV[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, MV);
    const float* V = camera.getViewMatrix()->ToGL();
    Vector3D lightWorld{light.x, light.y, light.z};
    Vector3D lightEye = TransformPointCM(V, lightWorld);

    glPolygonMode(GL_FRONT_AND_BACK, this->wireframeMode ? GL_LINE : GL_FILL);
    // FixEntityWinding's centroid-based "face outward" heuristic can't
    // reliably orient every face of a non-convex shape — true for a
    // hangar-bay interior corridor (viewed from inside its own volume) and
    // for capital-ship exterior hulls (hangar tunnel carved through them,
    // turret mounts), which is why culling was already disabled for those.
    // But it's ALSO unreliable for ordinary fighter models with any concave
    // detail relative to their own centroid (a canopy bubble, an intake
    // recess) — confirmed by live testing (HELLCATP, used by both the
    // player ship and Hobbes, rendered with visible holes into its own
    // interior: faces wrongly classified as backfaces and culled). Rather
    // than trying to special-case every concave shape, disable backface
    // culling unconditionally — every model here is now lit two-sided
    // (see forceTwoSidedLighting in drawModelColorPass/drawModelTexturePass)
    // so nothing depends on winding being correct anymore.
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);

    if (debugThisCall) {
        printf("DEBUG INTERIOR: currentLod=%zu lod->numTriangles=%u MV_translate=(%.1f,%.1f,%.1f) show_textured=%d glErr_before=0x%x\n",
               currentLodLevel, (unsigned)lod->numTriangles, MV[12], MV[13], MV[14], this->show_textured, glGetError());
    }

    drawModelColorPass(object, currentLodLevel, vertexNormals, ambientLamber, lightEye, MV);

    if (this->show_textured) {
        drawModelTexturePass(object, currentLodLevel, vertexNormals, ambientLamber, lightEye, MV);
    }

    drawModelTransparentPass(object, currentLodLevel, vertexNormals, ambientLamber, lightEye, MV);

    if (debugThisCall) {
        printf("DEBUG INTERIOR: glErr_after=0x%x\n", glGetError());
    }
    if (isHullCall && this->forceDebugDumpFrames > 0) {
        this->forceDebugDumpFrames--;
    }

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void SCRenderer::setLight(Point3D *l) { this->light = *l; }

void SCRenderer::prepare(RSEntity *object) {

    for (size_t i = 0; i < object->NumImages(); i++) {
        object->images[i]->SyncTexture();
    }
    FixEntityWinding(object);
    object->prepared = true;
}

void SCRenderer::displayModel(RSEntity *object, size_t lodLevel) {

    if (!initialized)
        return;

    if (object->IsPrepared())
        prepare(object);

    glMatrixMode(GL_PROJECTION);
    Matrix *projectionMatrix = camera.getProjectionMatrix();
    glLoadMatrixf(projectionMatrix->ToGL());

    running = true;
    float counter = 0;
    while (running) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        Matrix *modelViewMatrix;

        light.x = 20.0f * cosf(counter);
        light.y = 10.0f;
        light.z = 20.0f * sinf(counter);
        counter += 0.02f;

        // camera.SetPosition(position);

        modelViewMatrix = camera.getViewMatrix();
        glLoadMatrixf(modelViewMatrix->ToGL());

        drawModel(object, lodLevel);

        // Render light

        glPointSize(6);

        gb.begin(GL_POINTS);
        gb.color4f(1, 1, 0, 1);
        gb.vertex3f(light.x, light.y, light.z);
        gb.end();
    }
}

#define TEX_ZERO (0 / 64.0f)
#define TEX_ONE (64 / 64.0f)

// What is this offset ? It is used to get rid of the red delimitations
// in the 64x64 textures.
#define OFFSET (2.0f / 64.0f)
float textTrianCoo64[2][3][2] = {

    {{TEX_ZERO, TEX_ZERO + OFFSET},
     {TEX_ONE - 2 * OFFSET, TEX_ONE - OFFSET},
     {TEX_ZERO, TEX_ONE - OFFSET}}, // LOWER_TRIANGE

    {{TEX_ZERO + 2 * OFFSET, TEX_ZERO + OFFSET},
     {TEX_ONE, TEX_ZERO + OFFSET},
     {TEX_ONE, TEX_ONE - OFFSET}} // UPPER_TRIANGE
};

float textTrianCoo[2][3][2] = {

    {{TEX_ZERO, TEX_ZERO}, {TEX_ONE, TEX_ONE}, {TEX_ZERO, TEX_ONE}}, // LOWER_TRIANGE

    {{TEX_ZERO, TEX_ZERO}, {TEX_ONE, TEX_ZERO}, {TEX_ONE, TEX_ONE}} // UPPER_TRIANGE
};

#define LOWER_TRIANGE 0
#define UPPER_TRIANGE 1

void SCRenderer::renderTexturedTriangle(MapVertex *tri0, MapVertex *tri1, MapVertex *tri2, RSArea *area,
                                        int triangleType, RSImage *image) {

    int mainColor = 0;
    if (tri0->type != tri1->type || tri0->type != tri2->type) {
        mainColor = 1;
        if (tri1->type > tri0->type)
            if (tri1->type > tri2->type)
                gb.color4fv(tri1->color);
            else
                gb.color4fv(tri2->color);
        else if (tri0->type > tri2->type)
            gb.color4fv(tri0->color);
        else
            gb.color4fv(tri2->color);
    }

    if (image->width < 128) {
        gb.texCoord2fv(textTrianCoo64[triangleType][0]);
        if (!mainColor) {
            gb.color4fv(tri0->color);
        }
        gb.vertex3f(tri0->v.x, tri0->v.y, tri0->v.z);

        if (!mainColor) {
            gb.color4fv(tri1->color);
        }
        gb.texCoord2fv(textTrianCoo64[triangleType][1]);
        gb.vertex3f(tri1->v.x, tri1->v.y, tri1->v.z);

        if (!mainColor) {
            gb.color4fv(tri2->color);
        }
        gb.texCoord2fv(textTrianCoo64[triangleType][2]);
        gb.vertex3f(tri2->v.x, tri2->v.y, tri2->v.z);
    } else {
        gb.texCoord2fv(textTrianCoo[triangleType][0]);
        if (!mainColor) {
            gb.color4fv(tri0->color);
        }
        gb.vertex3f(tri0->v.x, tri0->v.y, tri0->v.z);

        gb.texCoord2fv(textTrianCoo[triangleType][1]);
        if (!mainColor) {
            gb.color4fv(tri1->color);
        }
        gb.vertex3f(tri1->v.x, tri1->v.y, tri1->v.z);

        gb.texCoord2fv(textTrianCoo[triangleType][2]);
        if (!mainColor) {
            gb.color4fv(tri2->color);
        }
        gb.vertex3f(tri2->v.x, tri2->v.y, tri2->v.z);
    }
}

bool SCRenderer::isTextured(MapVertex *tri0, MapVertex *tri1, MapVertex *tri2) {
    return
        // tri0->type != tri1->type ||
        // tri0->type != tri2->type ||
        tri0->upperImageID == 0xFF || tri0->lowerImageID == 0xFF;
}
void SCRenderer::renderColoredTriangle(MapVertex *tri0, MapVertex *tri1, MapVertex *tri2) {

    if (tri0->type != tri1->type || tri0->type != tri2->type

    ) {

        if (tri1->type > tri0->type)
            if (tri1->type > tri2->type)
                gb.color4fv(tri1->color);
            else
                gb.color4fv(tri2->color);
        else if (tri0->type > tri2->type)
            gb.color4fv(tri0->color);
        else
            gb.color4fv(tri2->color);

        gb.vertex3f(tri0->v.x, tri0->v.y, tri0->v.z);

        gb.vertex3f(tri1->v.x, tri1->v.y, tri1->v.z);

        gb.vertex3f(tri2->v.x, tri2->v.y, tri2->v.z);
    } else {
        gb.color4fv(tri0->color);
        gb.vertex3f(tri0->v.x, tri0->v.y, tri0->v.z);

        gb.color4fv(tri1->color);
        gb.vertex3f(tri1->v.x, tri1->v.y, tri1->v.z);

        gb.color4fv(tri2->color);
        gb.vertex3f(tri2->v.x, tri2->v.y, tri2->v.z);
    }
}

void SCRenderer::renderQuad(MapVertex *currentVertex, MapVertex *rightVertex, MapVertex *bottomRightVertex,
                            MapVertex *bottomVertex, RSArea *area) {

    
    if (currentVertex->lowerImageID == 0xFF || !this->show_textured) {
        // Render lower triangle
        renderColoredTriangle(currentVertex, bottomRightVertex, bottomVertex);
    }
    if (currentVertex->upperImageID == 0xFF || !this->show_textured) {
        // Render Upper triangles
        renderColoredTriangle(currentVertex, rightVertex, bottomRightVertex);
    }
    if (this->show_textured){
        if (currentVertex->lowerImageID != 0xFF) {
            VertexVector &vcache = textureSortedVertex[currentVertex->lowerImageID];
            VertexCache v = {currentVertex, bottomRightVertex, bottomVertex};
            v.lv1 = currentVertex;
            v.lv2 = bottomRightVertex;
            v.lv3 = bottomVertex;
            v.uv1 = v.uv2 = v.uv3 = NULL;
            vcache.push_back(v);
        }
        if (currentVertex->upperImageID != 0xFF) {
            VertexVector &vcache = textureSortedVertex[currentVertex->upperImageID];
            VertexCache v;
            v.uv1 = currentVertex;
            v.uv2 = rightVertex;
            v.uv3 = bottomRightVertex;
            v.lv1 = v.lv2 = v.lv3 = NULL;
            vcache.push_back(v);
        }
    }
}

void SCRenderer::renderBlock(RSArea *area, int LOD, int i, bool renderTexture,
                 const std::unordered_set<int>* skipRight,
                 const std::unordered_set<int>* skipBottom) {

    AreaBlock *block = area->GetAreaBlockByID(LOD, i);
    if (block == nullptr) {
        return;
    }
    uint32_t sideSize = block->sideSize;

    // printf("Rendering block %d at x %f,z %f\n", i, block->vertice[0].v.x, block->vertice[0].v.z);
    for (size_t x = 0; x < sideSize - 1; x++) {
        for (size_t y = 0; y < sideSize - 1; y++) {

            MapVertex *currentVertex = &block->vertice[x + y * sideSize];
            MapVertex *rightVertex = &block->vertice[(x + 1) + y * sideSize];
            MapVertex *bottomRightVertex = &block->vertice[(x + 1) + (y + 1) * sideSize];
            MapVertex *bottomVertex = &block->vertice[x + (y + 1) * sideSize];

            renderQuad(currentVertex, rightVertex, bottomRightVertex, bottomVertex, area);
        }
    }
    
    // Inter-block right side
    if (i % BLOCK_PER_MAP_SIDE != 17) {
        int rightId = static_cast<int>(i + 1);
        if (!skipRight || !skipRight->count(rightId)) {     // <-- ajout
            AreaBlock *currentBlock = area->GetAreaBlockByID(LOD, (size_t)i);
            AreaBlock *rightBlock   = area->GetAreaBlockByID(LOD, (size_t)rightId);
            for (uint32_t y = 0; y < sideSize - 1; y++) {
                MapVertex *currentVertex = currentBlock->GetVertice(currentBlock->sideSize - 1, y);
                MapVertex *rightVertex = rightBlock->GetVertice(0, y);
                MapVertex *bottomRightVertex = rightBlock->GetVertice(0, y + 1);
                MapVertex *bottomVertex = currentBlock->GetVertice(currentBlock->sideSize - 1, y + 1);

                renderQuad(currentVertex, rightVertex, bottomRightVertex, bottomVertex, area);
            }
        }
    }

    // Inter-block bottom side
    if (i / BLOCK_PER_MAP_SIDE != 17) {
    int bottomId = i + BLOCK_PER_MAP_SIDE;
        if (!skipBottom || !skipBottom->count(bottomId)) {  // <-- ajout
            AreaBlock *currentBlock = area->GetAreaBlockByID(LOD, i);
            AreaBlock *bottomBlock  = area->GetAreaBlockByID(LOD, bottomId);
            for (uint32_t x = 0; x < sideSize - 1; x++) {
                MapVertex *currentVertex = currentBlock->GetVertice(x, currentBlock->sideSize - 1);
                MapVertex *rightVertex = currentBlock->GetVertice(x + 1, currentBlock->sideSize - 1);
                MapVertex *bottomRightVertex = bottomBlock->GetVertice(x + 1, 0);
                MapVertex *bottomVertex = bottomBlock->GetVertice(x, 0);

                renderQuad(currentVertex, rightVertex, bottomRightVertex, bottomVertex, area);
            }
        }
    }

    // Inter bottom-right quad
    if (i % BLOCK_PER_MAP_SIDE != 17 && i / BLOCK_PER_MAP_SIDE != 17) {
        int rightId  = i + 1;
        int bottomId = i + BLOCK_PER_MAP_SIDE;
        bool skipR = skipRight  && skipRight->count(rightId);
        bool skipB = skipBottom && skipBottom->count(bottomId);
        if (!skipR && !skipB) { 
            AreaBlock *currentBlock = area->GetAreaBlockByID(LOD, i);
            AreaBlock *rightBlock = area->GetAreaBlockByID(LOD, i + 1);
            AreaBlock *rightBottonBlock = area->GetAreaBlockByID(LOD, i + 1 + BLOCK_PER_MAP_SIDE);
            AreaBlock *bottomBlock = area->GetAreaBlockByID(LOD, i + BLOCK_PER_MAP_SIDE);

            MapVertex *currentVertex = currentBlock->GetVertice(currentBlock->sideSize - 1, currentBlock->sideSize - 1);
            MapVertex *rightVertex = rightBlock->GetVertice(0, currentBlock->sideSize - 1);
            MapVertex *bottomRightVertex = rightBottonBlock->GetVertice(0, 0);
            MapVertex *bottomVertex = bottomBlock->GetVertice(currentBlock->sideSize - 1, 0);

            renderQuad(currentVertex, rightVertex, bottomRightVertex, bottomVertex, area);
        }
    }
    
}
void SCRenderer::renderSkydome(int rings, int slices) {
    // Le dôme ne doit JAMAIS recevoir le brouillard du terrain
    GLboolean fogWasEnabled = glIsEnabled(GL_FOG);
    glDisable(GL_FOG);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    const float radius = this->max_view_distance * 0.98f;
    Point3D cam = camera.getPosition();

    // ── Palette : horizon blanc → ciel bleu ─────────────────────────────
    // Clé de couleur : t=0 (équateur) … t=1 (zénith)
    static const float skyT[]   = { 0.00f, 0.08f, 0.25f, 0.60f, 1.00f };
    static const float skyRGB[][3] = {
        { 1.00f, 1.00f, 1.00f },   // horizon : filet blanc pur
        { 0.72f, 0.83f, 0.97f },   // juste au-dessus : bleu très pâle
        { 0.40f, 0.62f, 0.92f },   // ciel moyen
        { 0.18f, 0.44f, 0.82f },   // bleu soutenu
        { 0.08f, 0.28f, 0.72f },   // zénith : bleu profond
    };
    constexpr int N = 5;

    auto sampleSky = [&](float t, float& r, float& g, float& b) {
        if (t <= 0.f) { r=skyRGB[0][0]; g=skyRGB[0][1]; b=skyRGB[0][2]; return; }
        if (t >= 1.f) { r=skyRGB[N-1][0]; g=skyRGB[N-1][1]; b=skyRGB[N-1][2]; return; }
        for (int i = 0; i < N-1; ++i) {
            if (t >= skyT[i] && t <= skyT[i+1]) {
                float f = (t - skyT[i]) / (skyT[i+1] - skyT[i]);
                r = skyRGB[i][0] + f*(skyRGB[i+1][0]-skyRGB[i][0]);
                g = skyRGB[i][1] + f*(skyRGB[i+1][1]-skyRGB[i][1]);
                b = skyRGB[i][2] + f*(skyRGB[i+1][2]-skyRGB[i][2]);
                return;
            }
        }
    };

    // ── Hémisphère supérieure ────────────────────────────────────────────
    for (int r = 0; r < rings; ++r) {
        float t0  = (float)r       / rings;
        float t1  = (float)(r + 1) / rings;
        float phi0 = (float)M_PI_2 * t0;
        float phi1 = (float)M_PI_2 * t1;
        float r0,g0,b0, r1,g1,b1;
        sampleSky(t0, r0,g0,b0);
        sampleSky(t1, r1,g1,b1);

        gb.begin(GL_TRIANGLE_STRIP);
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f*(float)M_PI*s/slices;
            float cosT=cosf(theta), sinT=sinf(theta);
            gb.color3f(r0,g0,b0);
            gb.vertex3f(cam.x+radius*cosf(phi0)*cosT, cam.y+radius*sinf(phi0), cam.z+radius*cosf(phi0)*sinT);
            gb.color3f(r1,g1,b1);
            gb.vertex3f(cam.x+radius*cosf(phi1)*cosT, cam.y+radius*sinf(phi1), cam.z+radius*cosf(phi1)*sinT);
        }
        gb.end();
    }

    // ── Hémisphère inférieure (masque depth + sol) ───────────────────────
    static const float nadirRGB[3] = { 0.52f, 0.47f, 0.35f };
    for (int r = 0; r < rings; ++r) {
        float t0  = (float)r       / rings;
        float t1  = (float)(r + 1) / rings;
        float phi0 = -(float)M_PI_2 * t0;
        float phi1 = -(float)M_PI_2 * t1;
        float e0 = t0*t0, e1 = t1*t1; // ease-in quadratique

        gb.begin(GL_TRIANGLE_STRIP);
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f*(float)M_PI*s/slices;
            float cosT=cosf(theta), sinT=sinf(theta);
            gb.color3f(1.f+e0*(nadirRGB[0]-1.f), 1.f+e0*(nadirRGB[1]-1.f), 1.f+e0*(nadirRGB[2]-1.f));
            gb.vertex3f(cam.x+radius*cosf(phi0)*cosT, cam.y+radius*sinf(phi0), cam.z+radius*cosf(phi0)*sinT);
            gb.color3f(1.f+e1*(nadirRGB[0]-1.f), 1.f+e1*(nadirRGB[1]-1.f), 1.f+e1*(nadirRGB[2]-1.f));
            gb.vertex3f(cam.x+radius*cosf(phi1)*cosT, cam.y+radius*sinf(phi1), cam.z+radius*cosf(phi1)*sinT);
        }
        gb.end();
    }

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    if (fogWasEnabled) glEnable(GL_FOG);
}
void SCRenderer::renderEllipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int rings, int slices, float r, float g, float b, float baseAlpha) {
    for (int ring = 0; ring < rings; ++ring) {
        float phi0 = (float)M_PI * ring / rings - (float)M_PI_2;
        float phi1 = (float)M_PI * (ring + 1) / rings - (float)M_PI_2;

        gb.begin(GL_TRIANGLE_STRIP);
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f * (float)M_PI * s / slices;
            float cosT = cosf(theta), sinT = sinf(theta);

            for (int pass = 0; pass < 2; ++pass) {
                float phi = (pass == 0) ? phi0 : phi1;
                float cosPhi = cosf(phi), sinPhi = sinf(phi);
                // Dégradé : bas transparent, haut opaque
                float t = (sinPhi + 1.0f) * 0.5f;
                gb.color4f(r, g, b, baseAlpha * t);
                gb.vertex3f(cx + rx * cosPhi * cosT,
                           cy + ry * sinPhi,
                           cz + rz * cosPhi * sinT);
            }
        }
        gb.end();
    }
}
void SCRenderer::renderClouds(RSArea *area) {
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);  // ou GL_LEQUAL sans depth write
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Point3D cam = camera.getPosition();


    // Trier back-to-front pour l'alpha blending correct
    std::vector<const Cloud*> sorted;
    for (auto& c : area->clouds) {
        if (c.position.x < cam.x - this->max_view_distance || c.position.x > cam.x + this->max_view_distance ||
            c.position.z < cam.z - this->max_view_distance || c.position.z > cam.z + this->max_view_distance) {
            continue; // trop loin
        }
        sorted.push_back(&c);
    }
    std::sort(sorted.begin(), sorted.end(), [&](const Cloud* a, const Cloud* b) {
        float da = (a->position.x-cam.x)*(a->position.x-cam.x)
                 + (a->position.z-cam.z)*(a->position.z-cam.z);
        float db = (b->position.x-cam.x)*(b->position.x-cam.x)
                 + (b->position.z-cam.z)*(b->position.z-cam.z);
        return da > db; // plus loin d'abord
    });

    for (const Cloud* c : sorted) {
        for (const auto& p : c->puffs) {
            this->renderEllipsoid(
                c->position.x + p.ox,
                c->position.y + p.oy,
                c->position.z + p.oz,
                p.rx, p.ry, p.rz,
                6, 12,           // rings, slices (peu de polys = perf ok)
                1.0f, 1.0f, 1.0f, // blanc
                c->alpha
            );
        }
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}
void SCRenderer::renderWorldSkyAndGround() {
    if (this->show_starfield)
        this->renderStarfield();
    else
        this->renderSkydome(12, 48);
    this->renderSkybox();
    this->renderSpaceDust();
}

// WC3 space-mission galaxy skybox — 6 distant textured billboards (WRLD's
// own SKYS chunk: named DIST/DFLT entities + a direction vector each,
// reverse-engineered against WORLD.IFF; see RSWorld.h) positioned along
// their assigned axis at a fixed distance from the camera and reoriented to
// lie in the plane perpendicular to that axis.
//
// The mesh's actual default plane was previously guessed (comment used to
// claim face normal along +Y) rather than checked — live-dumping one
// galaxy entity's real object-space vertices showed every vertex has X=0,
// i.e. the quad lies flat in the Y-Z plane, default normal along +X, not Y.
// Combined with the specific (and non-obvious) axis remapping the 4-arg
// drawModel(...) overload applies — glRotatef(orientation.x,0,1,0) (Y-axis),
// then glRotatef(orientation.y,0,0,1) (Z-axis), then
// glRotatef(orientation.z,1,0,0) (X-axis), i.e. field x->Y-axis, field
// y->Z-axis, field z->X-axis, NOT a direct x/y/z-to-axis correspondence —
// the old table was wrong for all 6 directions: verified numerically
// (composing the exact same rotation order/axes in Python against the real
// default normal) that the old values sent 2 faces to face Y instead of X,
// and left the other 4 facing X permanently (an X-axis-only rotation can
// never redirect a vector already on the X axis) — every billboard ended
// up edge-on to its own placement direction regardless of which way the
// camera looked, which is why none of them were ever visible. Below is
// re-solved for the confirmed default normal (1,0,0) against this exact
// rotation order; each sends the normal to (or onto the same plane as)
// the real target direction (sign doesn't matter for visibility — culling
// is disabled — only which plane the quad ends up in).
//
// WRLD>SKYS's own (dirX,dirY,dirZ) is in the FILE's coordinate convention,
// not this engine's world axes directly — confirmed by live comparison
// against the original game: COMET (file dir (0,0,1)) belongs above the
// TCS Victory, not behind it, so file Y and file Z are swapped relative to
// this engine's (X=right, Y=up, Z=forward/back) — file X maps straight
// across, file Z is this engine's up/down (confirmed correct as-is: COMET,
// blkgal1), and file Y is this engine's forward/back but NEGATED (the
// front/behind pair — darkgal2/darkgal3 — came out swapped otherwise;
// confirmed by live testing). Both position and the orientation-selection
// below are driven from the same swapped engineDir so they can't drift out
// of sync with each other again.
void SCRenderer::renderSkybox() {
    if (this->skyFaces == nullptr || this->skyEntities == nullptr) return;
    Vector3D cameraPos = this->camera.getPosition();
    glDisable(GL_DEPTH_TEST);

    for (size_t i = 0; i < this->skyFaces->size() && i < this->skyEntities->size(); i++) {
        RSEntity* ent = (*this->skyEntities)[i];
        if (ent == nullptr) continue;
        WorldSkyFace& face = (*this->skyFaces)[i];
        // file Y <-> engine Z (negated), file Z <-> engine Y; file X
        // unchanged. COMET/blkgal1 (file Z, up/down) confirmed correct
        // as-is; darkgal2/darkgal3 (file Y, front/back) were swapped —
        // negated per live-testing feedback.
        Vector3D engineDir = { (float)face.dirX, (float)face.dirZ, -(float)face.dirY };
        Vector3D position = {
            cameraPos.x + engineDir.x * (float)face.distance,
            cameraPos.y + engineDir.y * (float)face.distance,
            cameraPos.z + engineDir.z * (float)face.distance
        };
        // orientation.z is a roll around the quad's own face-normal (see
        // drawModel's glRotatef(orientation.z,1,0,0) — the X-axis, this
        // mesh's default normal), i.e. purely an in-plane texture spin
        // that doesn't change which world direction the quad faces. Set
        // to 90 uniformly per live-testing feedback (the plane/position
        // fix above was right, but every texture read rotated 90 degrees).
        constexpr float kTextureRoll = -90.0f;
        Vector3D orientation = {0.0f, 0.0f, kTextureRoll};
        if (engineDir.x < 0)      orientation = {0.0f, 180.0f, kTextureRoll};
        else if (engineDir.x > 0) orientation = {0.0f, 0.0f, kTextureRoll};
        else if (engineDir.y < 0) orientation = {0.0f, 270.0f, kTextureRoll};
        else if (engineDir.y > 0) orientation = {0.0f, 90.0f, kTextureRoll};
        else if (engineDir.z < 0) orientation = {90.0f, 0.0f, kTextureRoll};
        else if (engineDir.z > 0) orientation = {90.0f, 180.0f, kTextureRoll};
        // Scaled proportionally to each face's own placement distance,
        // not a flat multiplier — WORLD.IFF's SKYS records don't all sit at
        // the same distance (COMET is at 12800, the other 5 galaxy faces
        // at 25600, exactly double), so a flat scale made COMET look right
        // while the other 5 — twice as far away, same apparent angular
        // size only if also scaled twice as large — read as too small.
        // 16x at the 12800 reference distance (COMET, confirmed a good
        // size at that scale) keeps every face's apparent screen size
        // consistent regardless of its individual distance.
        constexpr float kReferenceDistance = 12800.0f;
        constexpr float kReferenceScale = 16.0f;
        float scale = kReferenceScale * ((float)face.distance / kReferenceDistance);
        // The distance-proportional formula alone wasn't enough — darkgal1
        // (and the other 4 galaxy faces sharing its 25600 distance) still
        // read as tiny even at the resulting 32x. Bumped an extra 16x for
        // faces beyond the reference distance specifically. Applying that
        // same extra 16x to COMET too (reference distance) made it way too
        // big — reverted back to just the base 16x for COMET, which was
        // already confirmed a good size on its own.
        if (face.distance != (int32_t)kReferenceDistance) {
            scale *= 16.0f;
        }
        this->drawModel(ent, 0, position, orientation, Vector3D{0.0f, 0.0f, 0.0f}, scale);
    }
    glEnable(GL_DEPTH_TEST);
}

// Grey motion-dust particles (WRLD>DUST — see RSWorld.h/dustCount/
// dustSpawnRadius field comments) — spawn near the player and drift
// backward as they fly, confirmed against live gameplay. Particles are
// mostly static in world space (dustPositions persists across frames, not
// recomputed) — the player's own forward motion through them is what
// produces the "drifting backward" look — but now also partially follow
// the camera each frame (see dustFollowFactor) to slow that apparent drift
// down from the original 1:1 coupling with real ship speed, per user
// feedback. The other active logic here is respawning: once a particle
// falls outside dustSpawnRadius of the player's current position, it's
// teleported to a fresh random point within the radius so it reappears
// ahead instead of staying lost behind. Runs depth-tested (unlike the
// skybox/starfield, which render at "infinity" with depth testing
// disabled) since these particles are a real, near-field part of the scene
// and should be occluded by nearby ships/the carrier same as anything else.
void SCRenderer::renderSpaceDust() {
    if (!this->show_dust || this->dustCount <= 0) return;

    Vector3D camPos = this->camera.getPosition();
    float radiusSq = this->dustSpawnRadius * this->dustSpawnRadius;

    if ((int)this->dustPositions.size() != this->dustCount) {
        // Force every particle to look "expired" on this first pass so the
        // respawn loop below spreads them out immediately instead of
        // drawing dustCount particles piled at one spot for a frame.
        this->dustPositions.assign(this->dustCount,
            Vector3D{camPos.x + this->dustSpawnRadius * 10.0f, camPos.y, camPos.z});
        this->dustCamPosInitialized = false;
    }

    // Partially follow the camera to slow the apparent drift speed — see
    // dustFollowFactor's own comment. Skipped on the very first frame (no
    // prior camera position yet) so particles don't jump.
    if (!this->dustCamPosInitialized) {
        this->lastDustCamPos = camPos;
        this->dustCamPosInitialized = true;
    } else {
        Vector3D camDelta = {camPos.x - this->lastDustCamPos.x,
                              camPos.y - this->lastDustCamPos.y,
                              camPos.z - this->lastDustCamPos.z};
        Vector3D follow = {camDelta.x * this->dustFollowFactor,
                            camDelta.y * this->dustFollowFactor,
                            camDelta.z * this->dustFollowFactor};
        for (auto &p : this->dustPositions) {
            p.x += follow.x;
            p.y += follow.y;
            p.z += follow.z;
        }
        this->lastDustCamPos = camPos;
    }

    for (auto &p : this->dustPositions) {
        float dx = p.x - camPos.x, dy = p.y - camPos.y, dz = p.z - camPos.z;
        if (dx * dx + dy * dy + dz * dz > radiusSq) {
            float u = (float)rand() / (float)RAND_MAX;
            float v = (float)rand() / (float)RAND_MAX;
            float theta = 2.0f * 3.14159f * u;
            float phi = acosf(2.0f * v - 1.0f);
            // Never right on top of the player (0.3x-1.0x of the radius).
            float r = this->dustSpawnRadius * (0.3f + 0.7f * ((float)rand() / (float)RAND_MAX));
            p.x = camPos.x + sinf(phi) * cosf(theta) * r;
            p.y = camPos.y + sinf(phi) * sinf(theta) * r;
            p.z = camPos.z + cosf(phi) * r;
        }
    }

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_POINT_SMOOTH);
    glPointSize(6.0f);  // doubled from the original 3.0f per user feedback
    gb.color3f(0.6f, 0.6f, 0.6f); // grey, per live gameplay observation
    gb.begin(GL_POINTS);
    for (auto &p : this->dustPositions) {
        gb.vertex3f(p.x, p.y, p.z);
    }
    gb.end();
    glDisable(GL_POINT_SMOOTH);
}

// Builds one GL texture per WRLD>STAR glint sprite (see RSWorld.h) from its
// palette-indexed RLEShape pixels, mirroring the exact pattern used for
// SCCockpit's hi-res background texture: pull raw indices out via
// RLEShape::Expand(), wrap them in a throwaway RSImage against the current
// VGA palette, and let Texture::set/updateContent do the palette lookup.
// Rebuilt only when starSprites points somewhere new (e.g. a fresh mission).
//
// User-reported "blue pixels that move with the stars" / "starfield too
// dark, stars should be white" investigated by extracting a real WORLD.IFF
// (missions.tre) and its real PALT-format palette and decoding the STAR
// glint frames directly (see git history for the throwaway probe, since
// removed): the colorkey/background index (255) correctly resolves to
// alpha=0 (magenta, per this engine's transparency convention — see
// AlphaBleedRGBA8's comment) in every frame, so the alpha pipeline itself
// was never the bug. But the glint frames' real foreground pixels turned
// out to be a graduated, honestly pretty dim/blue-tinted palette (e.g.
// index 136 = rgb(69,74,80), index 28 = rgb(159,181,239) — nothing reaches
// pure white, brightest is ~239/255 on one channel), and every opaque texel
// has alpha=255 uniformly (no soft/graduated alpha edge) — so the shape's
// only brightness falloff comes from that dim, off-white RGB gradation.
// Drawn via GL_MODULATE (texture RGB * vertex RGB), a texture that's never
// actually white can't produce a white star no matter the vertex color, and
// the generally-dim palette explains "too dark". A first attempt fixed this
// by decolorizing every texel to a neutral grey (luminance only) before
// contrast-stretching — that fixed "too dark" but overcorrected "wrong
// colour": real stars (and this glint art) legitimately range from white to
// blue-white, and forcing everything to flat grey/white erased that
// variation entirely, which is its own "wrong colour" complaint. The fix
// now scales each frame's RGB *channels* up by the same per-frame factor
// (headroom to the brightest single channel value in that frame) instead of
// collapsing to luminance — this boosts overall brightness so the
// brightest texel reaches full-intensity on its dominant channel, while
// preserving each texel's original R:G:B ratio, so texels that were
// genuinely blue-tinted (e.g. index 28's rgb(159,181,239)) stay blue-tinted
// (scaled toward blue-white) rather than becoming neutral grey.
void SCRenderer::buildStarSpriteTextures() {
    for (auto* tex : this->starSpriteTextures) {
        delete tex;
    }
    this->starSpriteTextures.clear();
    this->starSpriteTexturesSource = this->starSprites;
    if (this->starSprites == nullptr) {
        return;
    }
    VGAPalette* palette = RSVGA::getInstance().getPalette();
    for (size_t i = 0; i < this->starSprites->GetNumImages(); i++) {
        RLEShape* shape = this->starSprites->GetShape(i);
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

        RSImage img;
        img.width = w;
        img.height = h;
        img.data = indexBuf.data();
        img.palette = palette;
        img.flags = 0;

        Texture* tex = new Texture();
        tex->set(&img);
        tex->updateContent(&img);
        // RSImage::~RSImage() frees `data`, which here just points into
        // indexBuf's vector-owned buffer — detach before img goes out of
        // scope to avoid a double free (see SCCockpit::RenderHighResBackground
        // for the same fix, same underlying gotcha).
        img.data = nullptr;

        // Hue-preserving contrast stretch — see this function's own doc
        // comment above for why the raw decoded RGB is too dim to reach
        // white on its own, and why a flat luminance desaturation (an
        // earlier attempt) was the wrong fix.
        if (tex->data != nullptr) {
            size_t pixelCount = (size_t)w * (size_t)h;
            uint8_t maxChannel = 0;
            for (size_t p = 0; p < pixelCount; p++) {
                uint8_t* px = tex->data + p * 4;
                if (px[3] == 0) continue;  // fully transparent, leave alone
                uint8_t m = std::max({px[0], px[1], px[2]});
                if (m > maxChannel) maxChannel = m;
            }
            if (maxChannel > 0) {
                for (size_t p = 0; p < pixelCount; p++) {
                    uint8_t* px = tex->data + p * 4;
                    if (px[3] == 0) continue;
                    for (int c = 0; c < 3; c++) {
                        px[c] = (uint8_t)std::min(255, (int)px[c] * 255 / maxChannel);
                    }
                }
            }
        }
        this->starSpriteTextures.push_back(tex);
    }
}

void SCRenderer::renderStarfield() {
    // Strip translation from modelview so stars are infinitely far — must
    // read the CURRENT modelview (the real camera view matrix, set by
    // bindCameraProjectionAndViewViewport just before renderWorldSolid
    // called into here) before touching it. This used to glLoadIdentity()
    // first and only then glGetFloatv the "current" matrix — capturing
    // identity, not the camera's real rotation — so the star sphere/skybox
    // were drawn in a fixed frame that never actually followed the ship's
    // orientation, leaving them out of the view frustum (and so invisible)
    // for most of the directions the player actually looked.
    glMatrixMode(GL_MODELVIEW);
    float camMv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, camMv);
    camMv[12] = camMv[13] = camMv[14] = 0.0f;
    glPushMatrix();
    glLoadMatrixf(camMv);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);

    // Background sphere — WRLD>BACK's palette-indexed color (a dark navy
    // blue for most missions, not pure black; see spaceBackgroundColor).
    float radius = 50000.0f;
    gb.color3f(this->spaceBackgroundColor.x, this->spaceBackgroundColor.y, this->spaceBackgroundColor.z);
    int rings = 8, slices = 16;
    for (int i = 0; i < rings; i++) {
        float phi0 = 3.14159f * (-0.5f + (float)i / rings);
        float phi1 = 3.14159f * (-0.5f + (float)(i + 1) / rings);
        gb.begin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            float theta = 2.0f * 3.14159f * (float)j / slices;
            float x0 = cosf(phi0) * cosf(theta), y0 = sinf(phi0), z0 = cosf(phi0) * sinf(theta);
            float x1 = cosf(phi1) * cosf(theta), y1 = sinf(phi1), z1 = cosf(phi1) * sinf(theta);
            gb.vertex3f(x0 * radius, y0 * radius, z0 * radius);
            gb.vertex3f(x1 * radius, y1 * radius, z1 * radius);
        }
        gb.end();
    }

    bool useSprites = this->starSprites != nullptr && this->starSprites->GetNumImages() > 0;
    if (useSprites && this->starSpriteTexturesSource != this->starSprites) {
        this->buildStarSpriteTextures();
    }
    useSprites = useSprites && !this->starSpriteTextures.empty();

    if (useSprites) {
        // WRLD>STAR's own diamond/glint sprites (see RSWorld.h), drawn as
        // camera-facing billboards instead of flat GL_POINTS — same
        // deterministic LCG position/brightness sequence as the fallback
        // below (so star *placement* is unchanged either way), bucketed by
        // brightness into one of the (2-3) available glint sizes. One pass
        // per sprite texture so each can be bound once and drawn in a single
        // GL_QUADS block rather than rebinding mid-star.
        //
        // Camera-facing basis: camMv has translation stripped (see above),
        // so its rows give the camera's own right/up axes in world space —
        // the standard technique for billboarding without per-star matrix
        // math.
        float rightX = camMv[0], rightY = camMv[4], rightZ = camMv[8];
        float upX = camMv[1], upY = camMv[5], upZ = camMv[9];

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        size_t numSprites = this->starSpriteTextures.size();
        // World-space size of one screen pixel at the star sphere's render
        // distance, derived from the actual fovy/window height instead of a
        // fixed fraction of `radius` — a prior attempt tried radius*0.006f
        // (~1800-unit stars, 90% the width of the whole galaxy skybox
        // backdrop) then radius*0.0003f (so small it visually disappeared
        // entirely), both uncalibrated guesses confirmed wrong by live
        // testing. Computing directly from the projection means the
        // per-bucket pixel sizes below are exactly what they say, regardless
        // of `radius` or fovy changing.
        float starSphereDistance = radius * 0.99f;
        float fovyRad = this->camera.fovy * 3.14159f / 180.0f;
        int screenH = RSVGA::getInstance().GetWindowHeight();
        if (screenH <= 0) screenH = 1080;
        float worldPerPixel = 2.0f * starSphereDistance * tanf(fovyRad * 0.5f) / (float)screenH;
        // Dimmest bucket -> minPixelSize, brightest -> maxPixelSize, linearly
        // interpolated across however many glint sizes this WRLD's STAR
        // chunk actually provided (2-3 in practice) rather than a fixed
        // per-bucket step, so the range is exactly [min,max] regardless of
        // numSprites.
        constexpr float minPixelSize = 1.0f;   // halved again from 2.0f per user feedback
        constexpr float maxPixelSize = 3.5f;   // halved again from 7.0f per user feedback
        for (size_t spriteIdx = 0; spriteIdx < numSprites; spriteIdx++) {
            Texture* tex = this->starSpriteTextures[spriteIdx];
            if (!tex->initialized) {
                glGenTextures(1, &tex->id);
                glBindTexture(GL_TEXTURE_2D, tex->id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)tex->width, (GLsizei)tex->height, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, tex->data);
                tex->initialized = true;
            } else {
                glBindTexture(GL_TEXTURE_2D, tex->id);
            }
            // Bigger/brighter sprites (index 0) drawn visibly larger: full
            // quad width ranges [minPixelSize,maxPixelSize] screen pixels
            // across the available buckets — see the screen-space
            // derivation above.
            float t = (numSprites > 1) ? (float)spriteIdx / (float)(numSprites - 1) : 0.0f;
            float sizePixels = maxPixelSize - (maxPixelSize - minPixelSize) * t;
            float halfSize = 0.5f * sizePixels * worldPerPixel;

            uint32_t seed = 0x12345678;
            gb.begin(GL_QUADS);
            for (int i = 0; i < 800; i++) {
                seed = seed * 1103515245 + 12345;
                float u = (float)(seed & 0xFFFF) / 65535.0f;
                seed = seed * 1103515245 + 12345;
                float v = (float)(seed & 0xFFFF) / 65535.0f;
                seed = seed * 1103515245 + 12345;
                float brightness = 0.4f + 0.6f * ((float)(seed & 0xFF) / 255.0f);

                float bucketWidth = 1.0f / (float)numSprites;
                size_t bucket = (size_t)((1.0f - (brightness - 0.4f) / 0.6f) / bucketWidth);
                if (bucket >= numSprites) bucket = numSprites - 1;
                if (bucket != spriteIdx) continue;

                float theta = 2.0f * 3.14159f * u;
                float phi = acosf(2.0f * v - 1.0f);
                float cx = sinf(phi) * cosf(theta) * radius * 0.99f;
                float cy = sinf(phi) * sinf(theta) * radius * 0.99f;
                float cz = cosf(phi) * radius * 0.99f;

                gb.color4f(brightness, brightness, brightness * 0.95f, 1.0f);
                gb.texCoord2f(0, 1);
                gb.vertex3f(cx - rightX * halfSize - upX * halfSize, cy - rightY * halfSize - upY * halfSize,
                           cz - rightZ * halfSize - upZ * halfSize);
                gb.texCoord2f(1, 1);
                gb.vertex3f(cx + rightX * halfSize - upX * halfSize, cy + rightY * halfSize - upY * halfSize,
                           cz + rightZ * halfSize - upZ * halfSize);
                gb.texCoord2f(1, 0);
                gb.vertex3f(cx + rightX * halfSize + upX * halfSize, cy + rightY * halfSize + upY * halfSize,
                           cz + rightZ * halfSize + upZ * halfSize);
                gb.texCoord2f(0, 0);
                gb.vertex3f(cx - rightX * halfSize + upX * halfSize, cy - rightY * halfSize + upY * halfSize,
                           cz - rightZ * halfSize + upZ * halfSize);
            }
            gb.end();
        }
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
    } else {
        // Fallback (Strike Commander, or a WC3 mission whose WRLD didn't
        // resolve): stars as flat-colored points at the same fixed
        // deterministic positions.
        glEnable(GL_POINT_SMOOTH);
        glPointSize(8.0f); // mid-point of the sprite path's 4-14px range (doubled from 4.0f)
        gb.begin(GL_POINTS);
        uint32_t seed = 0x12345678;
        for (int i = 0; i < 800; i++) {
            // Simple LCG for deterministic positions
            seed = seed * 1103515245 + 12345;
            float u = (float)(seed & 0xFFFF) / 65535.0f;
            seed = seed * 1103515245 + 12345;
            float v = (float)(seed & 0xFFFF) / 65535.0f;
            seed = seed * 1103515245 + 12345;
            float brightness = 0.4f + 0.6f * ((float)(seed & 0xFF) / 255.0f);

            float theta = 2.0f * 3.14159f * u;
            float phi = acosf(2.0f * v - 1.0f);
            float x = sinf(phi) * cosf(theta);
            float y = sinf(phi) * sinf(theta);
            float z = cosf(phi);

            gb.color3f(brightness, brightness, brightness * 0.95f);
            gb.vertex3f(x * radius * 0.99f, y * radius * 0.99f, z * radius * 0.99f);
        }
        gb.end();
        glDisable(GL_POINT_SMOOTH);
    }

    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

void SCRenderer::renderWorldSolid(RSArea *area, int LOD, int verticesPerBlock) {
    if (this->show_starfield) {
        glClearColor(this->spaceBackgroundColor.x, this->spaceBackgroundColor.y, this->spaceBackgroundColor.z, 1.0f);
    }

    GLfloat fogColor[4] = {0.89f, 0.89f, 0.98f, 1.0f};
    if (this->show_fog && this->show_ground) {
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fogColor);
        glFogf(GL_FOG_START, this->max_view_distance * 0.40f);
        glFogf(GL_FOG_END,   this->max_view_distance * 0.93f);
        glHint(GL_FOG_HINT, GL_DONT_CARE);
        glEnable(GL_FOG);
    } else {
        glDisable(GL_FOG);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    this->renderWorldSkyAndGround();

    // area is null for missions with no terrain (e.g. pure-space training
    // missions whose world has no `tera` file, see [[project_wc3_sim_missions]])
    // — every block/cloud/overlay path below assumes a real RSArea, so treat
    // a missing area the same as "nothing to draw" rather than crashing.
    if (!this->show_ground || area == nullptr) {
        glEnable(GL_DEPTH_TEST);
        return;
    }
    if (this->show_clouds) {
        this->renderClouds(area);
    }
    
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    textureSortedVertex.clear();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    Plane frustum[6];
    extractFrustumPlanes(frustum);

    Vector3D pos = camera.getPosition();
    std::vector<int> visibleHiLOD;

    for (int i = 0; i < BLOCKS_PER_MAP; ++i) {
        int bx = i % BLOCK_PER_MAP_SIDE;
        int by = i / BLOCK_PER_MAP_SIDE;
        float block_cx = ((float)bx - (float)BLOCK_PER_MAP_SIDE_DIV_2) * (float)BLOCK_WIDTH + (float)BLOCK_WIDTH * 0.5f;
        float block_cz = ((float)by - (float)BLOCK_PER_MAP_SIDE_DIV_2) * (float)BLOCK_WIDTH + (float)BLOCK_WIDTH * 0.5f;
        float dx = pos.x - block_cx;
        float dz = pos.z - block_cz;
        if (dx*dx + dz*dz > (float)this->max_view_distance * (float)this->max_view_distance)
            continue;
        const AABB& box = computeBlockAABB(area, LOD, i);
        if (isAABBVisible(box, frustum))
            visibleHiLOD.push_back(i);
    }


    GLenum terrainFront = GL_CCW;
    int refId = -1; 
    bool refHi = false;
    if (!visibleHiLOD.empty()) {
        AreaBlock* refBlk = area->GetAreaBlockByID(LOD, visibleHiLOD[0]);
        terrainFront = DetectTerrainFrontFace(refBlk);
    }
    if (refId >= 0) {
        AreaBlock* refBlk = area->GetAreaBlockByID(refHi ? LOD : (LOD+1), refId);
        terrainFront = DetectTerrainFrontFace(refBlk);
    }
    glFrontFace(terrainFront);
    // Passe géométrie (non texturée)
    gb.begin(GL_TRIANGLES);
    textureSortedVertex.clear();
    for (int id : visibleHiLOD) {
        renderBlock(area, LOD, id, true);
    }
    gb.end();

    // Passe textures: réutilise les mêmes listes visibles (pas de culling)
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

    for (auto const &x : textureSortedVertex) {
        RSImage *image = area->GetImageByID(x.first);
        if (!image) {
            printf("This should never happen: Put a break point here.\n");
            continue;
        }
        glBindTexture(GL_TEXTURE_2D, image->GetTexture()->getTextureID());
        gb.begin(GL_TRIANGLES);
        for (int i = 0; i < (int)x.second.size(); i++) {
            VertexCache v = x.second.at(i);
            if (v.lv1 != NULL && v.lv1->lowerImageID == x.first) {
                renderTexturedTriangle(v.lv1, v.lv2, v.lv3, area, LOWER_TRIANGE, image);
            }
            if (v.uv1 != NULL && v.uv1->upperImageID == x.first) {
                renderTexturedTriangle(v.uv1, v.uv2, v.uv3, area, UPPER_TRIANGE, image);
            }
        }
        gb.end();
    }
    glDisable(GL_TEXTURE_2D);
    // Overlay: inversion de Z -> winding mirroir, on inverse temporairement la front face
    
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glFrontFace(GL_CCW);
    const auto& skirts = area->GetSkirts();
    gb.begin(GL_TRIANGLES);
    for (const auto& tri : skirts.tris) {
        gb.color3f(tri.v[0].r, tri.v[0].g, tri.v[0].b);
        gb.vertex3f(tri.v[0].x, tri.v[0].y, tri.v[0].z);
        gb.color3f(tri.v[1].r, tri.v[1].g, tri.v[1].b);
        gb.vertex3f(tri.v[1].x, tri.v[1].y, tri.v[1].z);
        gb.color3f(tri.v[2].r, tri.v[2].g, tri.v[2].b);
        gb.vertex3f(tri.v[2].x, tri.v[2].y, tri.v[2].z);
    }
    gb.end();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthFunc(GL_LESS);
    glFrontFace(terrainFront);

    renderMapOverlay(area);

    //glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glFrontFace(GL_CCW);
}

void SCRenderer::renderObjects(RSArea *area, size_t blockID) {
    float y = 0;
    for (auto object : area->objects) {

        glPushMatrix();

        glTranslatef(object.position.x, object.position.y, object.position.z);
        if (object.entity != nullptr) {
            drawModel(object.entity, this->lodLevel);
        } else {
            printf("OBJECT [%s] NOT FOUND\n", object.name);
            gb.begin(GL_POINTS);
            gb.color3f(0, 1, 0);
            gb.vertex3d(0, 0, 0);
            gb.end();
        }

        glPopMatrix();
    }
}
void SCRenderer::renderWorldToTexture(RSArea *area) {
    

    Vector3D pos = camera.getPosition();
    int centerX = BLOCK_WIDTH * BLOCK_PER_MAP_SIDE_DIV_2;
    int centerY = BLOCK_WIDTH * BLOCK_PER_MAP_SIDE_DIV_2;
    int blocX = (int)(pos.x + centerX) / BLOCK_WIDTH;
    int blocY = (int)(pos.z + centerY) / BLOCK_WIDTH;

    
    int block_id = blocY * BLOCK_PER_MAP_SIDE + blocX;
    
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    this->renderWorldSkyAndGround();

    // Space missions have no terrain block grid (area is null — see
    // is_space_mission in WC3Mission::loadMission()); mirrors the identical
    // show_ground guard in the main (non-render-to-texture) world renderer.
    if (!this->show_ground || area == nullptr) {
        return;
    }

    /**/
    bool save_is_textured = this->show_textured;
    this->show_textured = false;
    textureSortedVertex.clear();
    // Render your scene here
    gb.begin(GL_TRIANGLES);
    renderBlock(area, 0, block_id, false);
    renderBlock(area, 0, block_id+1, false);
    renderBlock(area, 0, block_id-1, false);
    renderBlock(area, 0, block_id+1*18, false);
    renderBlock(area, 0, block_id-1*18, false);
    renderBlock(area, 0, block_id+1+1*18, false);
    renderBlock(area, 0, block_id+1-1*18, false);
    renderBlock(area, 0, block_id-1+1*18, false);
    renderBlock(area, 0, block_id-1-1*18, false);
    gb.end();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    renderBlock(area, 0, block_id, true);
    for (auto const &x : textureSortedVertex) {
        RSImage *image = NULL;
        image = area->GetImageByID(x.first);
        if (image == NULL) {
            printf("This should never happen: Put a break point here.\n");
            return;
        }
        glBindTexture(GL_TEXTURE_2D, image->GetTexture()->getTextureID());

        gb.begin(GL_TRIANGLES);
        for (int i = 0; i < x.second.size(); i++) {
            VertexCache v = x.second.at(i);
            if (v.lv1 != NULL && v.lv1->lowerImageID == x.first) {
                renderTexturedTriangle(v.lv1, v.lv2, v.lv3, area, LOWER_TRIANGE, image);
            }
            if (v.uv1 != NULL && v.uv1->upperImageID == x.first) {
                renderTexturedTriangle(v.uv1, v.uv2, v.uv3, area, UPPER_TRIANGE, image);
            }
        }
        gb.end();
    }
    glDisable(GL_TEXTURE_2D);
    this->show_textured = save_is_textured;
}
void SCRenderer::initRenderToTexture() {
    if (this->texture == 0) {
        glGenTextures(1, &this->texture);
        glBindTexture(GL_TEXTURE_2D, this->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 107, 75, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glViewport(0, 0, 107, 75);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void SCRenderer::getRenderToTexture() {
    glBindTexture(GL_TEXTURE_2D, this->texture);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 107, 75, 0);
}
void SCRenderer::initRenderCameraView(){ 
    Matrix *projectionMatrix = camera.getProjectionMatrix();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glTranslatef(0.0f, verticalOffset, 0.0f);
    glMultMatrixf(projectionMatrix->ToGL());
    glMatrixMode(GL_MODELVIEW);
    Matrix *modelViewMatrix = camera.getViewMatrix();
    glLoadMatrixf(modelViewMatrix->ToGL());
}
void SCRenderer::renderMissionObjects(RSMission *mission) {

    float y = 0;
    for (auto object : mission->mission_data.parts) {
        if (object == mission->getPlayerCoord()) {
            continue;
        }
        glPushMatrix();

        glTranslatef(static_cast<GLfloat>(object->position.x), static_cast<GLfloat>(object->position.y),
                     static_cast<GLfloat>(object->position.z));

        float model_view_mat[4][4];
        glGetFloatv(GL_MODELVIEW_MATRIX, (GLfloat *)model_view_mat);
        glRotatef((360.0f - (float)object->azymuth + 90.0f), 0, 1, 0);
        glRotatef((float)object->pitch, 0, 0, 1);
        glRotatef(-(float)object->roll, 1, 0, 0);
        if (object->entity != NULL) {
            drawModel(object->entity, this->lodLevel);
        } else {
            printf("OBJECT [%s] NOT FOUND\n", object->member_name.c_str());
            gb.begin(GL_POINTS);
            gb.color3f(0, 1, 0);
            gb.vertex3d(0, 0, 0);
            gb.end();
        }
        glPopMatrix();
    }
}
void SCRenderer::renderLineCube(Vector3D position, int32_t size) {
    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);
    glScalef((float)size, (float)size, (float)size);
    gb.begin(GL_LINES);
    // Front face
    gb.color3f(1.0f, 0.0f, 0.0f); // Red
    gb.vertex3f(-0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f,  0.5f,  0.5f);
    gb.vertex3f( 0.5f,  0.5f,  0.5f);
    gb.vertex3f(-0.5f,  0.5f,  0.5f);
    gb.vertex3f(-0.5f,  0.5f,  0.5f);
    gb.vertex3f(-0.5f, -0.5f,  0.5f);

    // Back face
    gb.color3f(0.0f, 1.0f, 0.0f); // Green
    gb.vertex3f(-0.5f, -0.5f, -0.5f);
    gb.vertex3f(-0.5f,  0.5f, -0.5f);
    gb.vertex3f(-0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f, -0.5f, -0.5f);
    gb.vertex3f( 0.5f, -0.5f, -0.5f);
    gb.vertex3f(-0.5f, -0.5f, -0.5f);

    // Edges
    gb.color3f(0.0f, 0.0f, 1.0f); // Blue
    gb.vertex3f(-0.5f, -0.5f, -0.5f);
    gb.vertex3f(-0.5f, -0.5f,  0.5f);
    gb.vertex3f(-0.5f,  0.5f, -0.5f);
    gb.vertex3f(-0.5f,  0.5f,  0.5f);
    gb.vertex3f( 0.5f, -0.5f, -0.5f);
    gb.vertex3f( 0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f,  0.5f,  0.5f);

    gb.end();
    glPopMatrix();
}
void SCRenderer::renderBBox(Vector3D position, Point3D min, Point3D max) {
    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);
    glScalef(max.x - min.x, max.y - min.y, max.z - min.z);
    gb.begin(GL_LINES);
    // Front face
    gb.color3f(1.0f, 0.0f, 0.0f); // Red
    gb.vertex3f(-0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f,  0.5f,  0.5f);
    gb.vertex3f( 0.5f,  0.5f,  0.5f);
    gb.vertex3f(-0.5f,  0.5f,  0.5f);
    gb.vertex3f(-0.5f,  0.5f,  0.5f);
    gb.vertex3f(-0.5f, -0.5f,  0.5f);

    // Back face
    gb.color3f(0.0f, 1.0f, 0.0f); // Green
    gb.vertex3f(-0.5f, -0.5f, -0.5f);
    gb.vertex3f(-0.5f,  0.5f, -0.5f);
    gb.vertex3f(-0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f, -0.5f, -0.5f);
    gb.vertex3f( 0.5f, -0.5f, -0.5f);
    gb.vertex3f(-0.5f, -0.5f, -0.5f);

    // Edges
    gb.color3f(0.0f, 0.0f, 1.0f); // Blue
    gb.vertex3f(-0.5f, -0.5f, -0.5f);
    gb.vertex3f(-0.5f, -0.5f,  0.5f);
    gb.vertex3f(-0.5f,  0.5f, -0.5f);
    gb.vertex3f(-0.5f,  0.5f,  0.5f);
    gb.vertex3f( 0.5f, -0.5f, -0.5f);
    gb.vertex3f( 0.5f, -0.5f,  0.5f);
    gb.vertex3f( 0.5f,  0.5f, -0.5f);
    gb.vertex3f( 0.5f,  0.5f,  0.5f);

    gb.end();
    glPopMatrix();
}
void SCRenderer::renderMapOverlay(RSArea *area) {

    glDepthMask(GL_FALSE);           // ne pas écrire dans le depth buffer
    glDepthFunc(GL_LEQUAL);          // accepter l'égalité de profondeur
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    for (int i = 0; i < area->objectOverlay.size(); i++) {
        AoVPoints *v = area->objectOverlay[i].vertices;
        for (int j = 0; j < area->objectOverlay[i].nbTriangles; j++) {
            AoVPoints v1, v2, v3;
            v1 = area->objectOverlay[i].vertices[area->objectOverlay[i].trianles[j].verticesIdx[0]];
            v2 = area->objectOverlay[i].vertices[area->objectOverlay[i].trianles[j].verticesIdx[1]];
            v3 = area->objectOverlay[i].vertices[area->objectOverlay[i].trianles[j].verticesIdx[2]];
            gb.begin(GL_TRIANGLES);
            const Texel *texel = palette.GetRGBColor(area->objectOverlay[i].trianles[j].color);
            gb.color4f(texel->r / 255.0f, texel->g / 255.0f, texel->b / 255.0f, 1);
            gb.vertex3f((GLfloat)v1.x, (GLfloat)v1.y, (GLfloat)-v1.z);
            gb.vertex3f((GLfloat)v2.x, (GLfloat)v2.y, (GLfloat)-v2.z);
            gb.vertex3f((GLfloat)v3.x, (GLfloat)v3.y, (GLfloat)-v3.z);
            gb.end();
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}
void SCRenderer::renderWorldByID(RSArea *area, int LOD, int verticesPerBlock, int blockId) {
    textureSortedVertex.clear();
    printf("X:%f,Y:%f", area->GetAreaBlockByID(LOD, blockId)->vertice[0].v.x,
           area->GetAreaBlockByID(LOD, blockId)->vertice[0].v.z);
    glPushMatrix();
    glScalef(1 / 100.0f, 1 / 100.0f, 1 / 100.0f);
    glTranslatef(-area->GetAreaBlockByID(LOD, blockId)->vertice[0].v.x - 12500, 0,
                 -area->GetAreaBlockByID(LOD, blockId)->vertice[0].v.z - 12500);
    //
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    gb.begin(GL_TRIANGLES);
    renderBlock(area, LOD, blockId, false);
    gb.end();

    renderBlock(area, LOD, blockId, true);
    glEnable(GL_TEXTURE_2D);
    for (auto const &x : textureSortedVertex) {

        RSImage *image = NULL;

        image = area->GetImageByID(x.first);
        if (image == NULL) {
            printf("This should never happen: Put a break point here.\n");
            return;
        }
        glBindTexture(GL_TEXTURE_2D, image->GetTexture()->getTextureID());

        gb.begin(GL_TRIANGLES);
        for (int i = 0; i < x.second.size(); i++) {
            VertexCache v = x.second.at(i);
            if (v.lv1 != NULL && v.lv1->lowerImageID == x.first) {
                renderTexturedTriangle(v.lv1, v.lv2, v.lv3, area, LOWER_TRIANGE, image);
            }
            if (v.uv1 != NULL && v.uv1->upperImageID == x.first) {
                renderTexturedTriangle(v.uv1, v.uv2, v.uv3, area, UPPER_TRIANGE, image);
            }
        }
        gb.end();
    }
    glDisable(GL_TEXTURE_2D);
    renderObjects(area, blockId);
    glPopMatrix();
}

void SCRenderer::drawBillboard(Vector3D pos, Texture *tex, float size, float alpha) {
    if (!initialized || tex == nullptr)
        return;
    
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    
    // Initialize texture if needed
    if (!tex->initialized) {
        glGenTextures(1, &tex->id);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)tex->width, (GLsizei)tex->height, 
                     0, GL_RGBA, GL_UNSIGNED_BYTE, tex->data);
        
        tex->initialized = true;
    } else {
        glBindTexture(GL_TEXTURE_2D, tex->id);
    }
    
    // Get camera position
    Point3D cameraPos = camera.getPosition();
    
    // Calculate billboard vectors
    Vector3D look, right, up;
    
    // Look vector points from billboard to camera
    look.x = cameraPos.x - pos.x;
    look.y = cameraPos.y - pos.y;
    look.z = cameraPos.z - pos.z;
    look.Normalize();
    
    // Right vector is perpendicular to look and global up
    Vector3D globalUp = {0.0f, 1.0f, 0.0f};
    right = globalUp.CrossProduct(&look);
    right.Normalize();
    
    // Up vector completes the orthogonal basis
    up = look.CrossProduct(&right);
    
    // Calculate the corners of the billboard quad
    float halfSize = size * 0.5f;
    
    Vector3D bottomLeft, bottomRight, topRight, topLeft;
    
    bottomLeft.x = pos.x - right.x * halfSize - up.x * halfSize;
    bottomLeft.y = pos.y - right.y * halfSize - up.y * halfSize;
    bottomLeft.z = pos.z - right.z * halfSize - up.z * halfSize;
    
    bottomRight.x = pos.x + right.x * halfSize - up.x * halfSize;
    bottomRight.y = pos.y + right.y * halfSize - up.y * halfSize;
    bottomRight.z = pos.z + right.z * halfSize - up.z * halfSize;
    
    topRight.x = pos.x + right.x * halfSize + up.x * halfSize;
    topRight.y = pos.y + right.y * halfSize + up.y * halfSize;
    topRight.z = pos.z + right.z * halfSize + up.z * halfSize;
    
    topLeft.x = pos.x - right.x * halfSize + up.x * halfSize;
    topLeft.y = pos.y - right.y * halfSize + up.y * halfSize;
    topLeft.z = pos.z - right.z * halfSize + up.z * halfSize;
    
    // Draw the billboard quad
    gb.begin(GL_QUADS);
    gb.color4f(1.0f, 1.0f, 1.0f, alpha);
    
    gb.texCoord2f(0.0f, 0.0f);
    gb.vertex3f(bottomLeft.x, bottomLeft.y, bottomLeft.z);
    
    gb.texCoord2f(1.0f, 0.0f);
    gb.vertex3f(bottomRight.x, bottomRight.y, bottomRight.z);
    
    gb.texCoord2f(1.0f, 1.0f);
    gb.vertex3f(topRight.x, topRight.y, topRight.z);
    
    gb.texCoord2f(0.0f, 1.0f);
    gb.vertex3f(topLeft.x, topLeft.y, topLeft.z);
    gb.end();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
}