//
//  gfx.h
//  iff
//
//  Created by Fabien Sanglard on 12/21/2013.
//  Copyright (c) 2013 Fabien Sanglard. All rights reserved.
//

#pragma once
#include <unordered_map>
#include <unordered_set>
#include "Camera.h"
#include "../realspace/AssetManager.h"
#include "../realspace/RSMission.h"
#include "../realspace/RSImage.h"
#include "../realspace/RSImageSet.h"
#include "../realspace/RSWorld.h"
#include "Texture.h"
#ifdef _WIN32
#include <GL/GL.h>
#include <Windows.h>
#include <algorithm>
#endif
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#define MAP_SIZE 200000
class RSArea;
class RSEntity;
struct MapVertex;
struct Triangle;
typedef struct VertexCache {
    MapVertex *lv1;
    MapVertex *lv2;
    MapVertex *lv3;
    MapVertex *uv1;
    MapVertex *uv2;
    MapVertex *uv3;

} VertexCache;


typedef std::vector<VertexCache> VertexVector;
typedef std::unordered_map<int, VertexVector> TextureVertexMap;
// Petit utilitaire pour le culling
struct Plane {
    float a, b, c, d; // ax + by + cz + d = 0
};
struct AABB {
    Vector3D min;
    Vector3D max;
};
class SCRenderer {
private:
    inline static std::unique_ptr<SCRenderer> s_instance{};
    bool initialized;
    int scale;
    VGAPalette palette;
    bool running;
    bool paused;
    uint32_t counter;
    
    
    Point3D playerPosition;
    TextureVertexMap textureSortedVertex;
    GLuint framebuffer;
    
    std::unordered_map<uint64_t, AABB> blockAABBCache_;
    struct AreaAABBCache {
        std::unordered_map<uint64_t, AABB> byKey;
    };
    std::unordered_map<const RSArea*, AreaAABBCache> aabbCache_;

    void extractFrustumPlanes(Plane planes[6]) const;
    static bool isAABBVisible(const AABB& box, const Plane planes[6]);
    
    const AABB &computeBlockAABB(RSArea *area, int LOD, int blockId);
    void getNormal(RSEntity *object, Triangle *triangle, Vector3D *normal);
    void renderWorldSkyAndGround();

public:
    // Was private (grouped with renderWorldSkyAndGround, its only caller
    // until now) — SCNavMap::render3DScene() needs to call it directly for
    // the SC1 nav-map skybox fallback (no skyFaces/skyEntities exist for
    // SC1 missions), mirroring renderWorldSkyAndGround's own
    // show_starfield-vs-skydome branch rather than duplicating it.
    void renderSkydome(int rings, int slices);
    Camera camera;
    // Renders every model as wireframe instead of filled polygons — useful
    // for diagnosing missing/incorrect geometry (a gap with no wireframe
    // lines through it means nothing is actually drawn there, vs. lines
    // present but unlit/untextured).
    bool wireframeMode{false};
    // Renders turret mount/gun meshes as flat solid magenta (no texture/
    // lighting) instead of their real appearance — a quick visibility check
    // for turret positioning, independent of texture/lighting correctness.
    bool debugMagentaTurrets{false};
    // Diagnostic: disables backface culling for every model (not just
    // capital-ship hulls/interiors). Ruled out by live testing — fighter
    // ships (Hobbes, the player) stayed invisible with culling off too, so
    // this isn't a winding/culling problem after all.
    bool debugDisableAllCulling{false};
    // When > 0, forces drawModel's detailed per-face debug prints
    // (attrs/property/alpha breakdown) for the next N calls regardless of
    // the fixed "first 3 calls" budget elsewhere, which is long exhausted
    // by the time a player actually flies far enough to reach whatever's
    // being diagnosed. Decremented once per drawModel call while set.
    int forceDebugDumpFrames{0};
    static SCRenderer& getInstance() {
        if (!SCRenderer::hasInstance()) {
            SCRenderer::setInstance(std::make_unique<SCRenderer>());
        }
        SCRenderer& instance = SCRenderer::instance();
        return instance;
    };
    static SCRenderer& instance() {
        return *s_instance;
    }
    static void setInstance(std::unique_ptr<SCRenderer> inst) {
        s_instance = std::move(inst);
    }

    static bool hasInstance() { return (bool)s_instance; }
    float verticalOffset{0.45f};
    float fov{30.0f};
    int max_view_distance{160000};
    bool show_fog{true};
    bool show_textured{true};
    bool show_clouds{false};
    bool show_ground{true};
    bool show_starfield{false};
    // WC3 space-mission skybox (WRLD>SKYS/STAR — see RSWorld.h), set once by
    // WC3Strike::setMission(); non-owning, points into the current
    // SCMission's own world/skyEntities. Null for missions without a loaded
    // WRLD (or for Strike Commander, which never sets these).
    std::vector<WorldSkyFace>* skyFaces{nullptr};
    std::vector<RSEntity*>* skyEntities{nullptr};
    RSImageSet* starSprites{nullptr};
    // GL textures built (lazily, once per starSprites pointer) from the
    // above's palette-indexed RLEShape glints — see buildStarSpriteTextures.
    std::vector<Texture*> starSpriteTextures;
    RSImageSet* starSpriteTexturesSource{nullptr};
    // Space background clear color — fixed RGB(8,16,36) (user-confirmed,
    // 2026-07 session), set by WC3Strike::setMission(). Previously
    // resolved dynamically from WRLD>BACK's palette index (RSWorld::
    // backgroundColorIndex), which didn't match the real color.
    Vector3D spaceBackgroundColor{8.0f / 255.0f, 16.0f / 255.0f, 36.0f / 255.0f};
    // Grey motion-dust particles (WRLD>DUST — see RSWorld.h) that spawn
    // near the player in open space and drift backward as they fly,
    // confirmed against live gameplay. show_dust is toggled every frame by
    // SCStrike::runFrame() (show_starfield && not currently docked in a
    // hangar bay) rather than staying on for the whole mission — the
    // effect shouldn't appear while still inside the TCS Victory's hangar.
    // dustCount/dustSpawnRadius default to WRLD>DUST's own fixed values
    // (identical in every real mission file) but WC3Strike::setMission()
    // still copies them from the mission's own RSWorld rather than
    // hardcoding here, in case some future/unseen WRLD ever differs.
    // dustPositions is persistent world-space state (not recomputed every
    // frame) — particles are static in world space; the player's own
    // motion through them is what produces the "drifting backward" look,
    // and this vector only gets touched to respawn a particle once it
    // falls outside dustSpawnRadius of the player.
    bool show_dust{false};
    int dustCount{50};
    float dustSpawnRadius{256.0f};
    std::vector<Vector3D> dustPositions;
    // Dust particles are static in world space (see dustPositions' own
    // comment above) — their apparent drift speed past the camera is
    // therefore a direct 1:1 readout of the player's real velocity, with no
    // separate "animation speed" of its own to tune. To slow the perceived
    // drift without decoupling from real ship speed (which would look
    // physically wrong), dust particles now partially follow the camera
    // each frame: dustFollowFactor is the fraction of the camera's own
    // frame-to-frame displacement that gets added to every particle's
    // position too, so only the remaining (1 - dustFollowFactor) fraction
    // of relative motion is visible. 0 = fully static (fastest apparent
    // drift, prior behavior); 1 = fully attached to the camera (no visible
    // drift at all). 0.5 halves the apparent drift speed. lastDustCamPos/
    // dustCamPosInitialized track the previous frame's camera position so
    // the delta can be computed; initialized lazily on first use rather
    // than in the constructor since the camera isn't valid that early.
    float dustFollowFactor{0.5f};
    Vector3D lastDustCamPos{0.0f, 0.0f, 0.0f};
    bool dustCamPosInitialized{false};
    int lodLevel{0};
    GLuint texture{0};
    // Multiplies every model-draw alpha (drawModelColorPass/
    // drawModelTexturePass/drawModelTransparentPass — see each pass' own
    // `float alpha = 1.0f;`) for the duration of one drawModelWithChilds
    // call. Callers set it right before drawing a cloaking actor's ship
    // (see SCPlane::cloak_factor) and must restore it to 1.0f right after
    // — a single global toggle rather than threading an alpha parameter
    // through every draw-pass signature, since blending (GL_BLEND) is
    // already unconditionally enabled for every model draw regardless.
    float modelAlphaMultiplier{1.0f};
    
    SCRenderer();
    ~SCRenderer();

    void init(int width, int height);

    void clear(void);
    void resetCameraPerspective();
    void drawParticle(Vector3D pos, float alpha);

    void drawModel(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation, Vector3D ajustement,
                   float scale);
    void drawModel(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation, Vector3D ajustement);
    // respectLodLevel: see the 2-arg drawModel(RSEntity*, size_t, bool)
    // overload's own comment — forwarded straight through.
    void drawModel(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation,
                    bool respectLodLevel = false);
    void drawModel(RSEntity *object, Vector3D position, Vector3D orientation);
    void drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation);
    void drawLine(Vector3D start, Vector3D end, Vector3D color, Vector3D orientation, Vector3D position);
    void drawLine(Vector3D start, Vector3D end, Vector3D color);
    // Untextured tapered cross-quad energy bolt (geometry from Energy_bolt.obj,
    // axes remapped so its long axis lies along -Z, matching this engine's
    // model-forward convention -- see SCPlane::forward). Used for gun weapons
    // with no real mesh (see GunSimulatedObject::Render()); orientation is in
    // radians, same convention as drawModel(RSEntity*, Vector3D, Vector3D).
    void drawBolt(Vector3D position, Vector3D orientation, Vector3D color);
    void drawPoint(Vector3D point, Vector3D color, Vector3D pos, Vector3D orientation);
    void drawSprite(Vector3D pos, Texture *tex, float zoom);
    void drawModelWithChilds(RSEntity *object, size_t lodLevel, Vector3D position, Vector3D orientation,
                             int wheel_index, int thrust, std::vector<std::tuple<Vector3D, RSEntity *>> weaps_load,
                             bool afterburnerEngaged = false);
    // respectLodLevel: false (default) keeps the existing "always render
    // at max detail" behavior for ordinary distance-based mesh LODs (see
    // this function's own comment — WC3's LODs are all trivial for a
    // modern GPU). Pass true when `lodLevel` isn't a quality tier at all
    // but a discrete visual STATE selector, unrelated to distance/detail —
    // e.g. AFTB engine-thrust-cone submodels, whose 8 "LOD" levels are
    // really 8 throttle-percentage/afterburner states (see
    // drawModelWithChilds' own AFTB block). Without this, that block's
    // whole throttle/afterburner selection silently did nothing — every
    // state rendered as LOD 0 regardless of detaIndex, since this
    // function ignored its own lodLevel parameter.
    void drawModel(RSEntity *object, size_t lodLevel, bool respectLodLevel = false);
    // Debug aid: draws every triangle/quad of the given LOD as flat, untextured
    // solid color (no lighting) — used to check turret mount/gun positioning
    // independent of texture/lighting correctness.
    void drawModelFlatColor(RSEntity *object, size_t lodLevel, Vector3D color);
    void drawModelColorPass(RSEntity *object, size_t lodLevel, std::vector<Vector3D> &vertexNormals, float ambientLamber, Vector3D lightEye, float *MV);
    void drawModelTexturePass(RSEntity *object, size_t lodLevel, std::vector<Vector3D> &vertexNormals, float ambientLamber, Vector3D lightEye, float *MV);
    void drawModelTransparentPass(RSEntity *object, size_t lodLevel, std::vector<Vector3D> &vertexNormals, float ambientLamber, Vector3D lightEye, float *MV);
    void drawBillboard(Vector3D pos, Texture *tex, float size, float alpha = 1.0f);
    // Full-viewport solid-color overlay, drawn in normalized screen space
    // (independent of the 3D camera) so it always covers everything
    // rendered earlier this frame — used for the large-capital-ship death
    // whiteout (SCStrike::checkExplosionScreenEffects). Call last, after
    // both the 3D scene and the cockpit HUD have been drawn.
    void renderFullscreenFlash(float r, float g, float b, float alpha);
    void displayModel(RSEntity *object, size_t lodLevel);
     
    void createTextureInGPU(Texture *texture);
    void uploadTextureContentToGPU(Texture *texture);
    void deleteTextureInGPU(Texture *texture);
    void drawTexturedQuad(Vector3D pos, Vector3D orientation,  std::vector<Vector3D> quad, Texture *tex);
    void renderEllipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int rings, int slices, float r, float g, float b, float baseAlpha);
    VGAPalette *getPalette(void);
    void renderClouds(RSArea *area);
    // Map Rendering

    void renderTexturedTriangle(MapVertex *tri0, MapVertex *tri1, MapVertex *tri2, RSArea *area, int triangleType,
                                RSImage *image);
    void renderColoredTriangle(MapVertex *tri0, MapVertex *tri1, MapVertex *tri2);
    bool isTextured(MapVertex *tri0, MapVertex *tri1, MapVertex *tri2);
    void renderQuad(MapVertex *currentVertex, MapVertex *rightVertex, MapVertex *bottomRightVertex,
                    MapVertex *bottomVertex, RSArea *area);

    void renderBlock(RSArea *area, int LOD, int i, bool renderTexture,
                 const std::unordered_set<int>* skipRight  = nullptr,
                 const std::unordered_set<int>* skipBottom = nullptr);
    void renderStarfield();
    void renderSkybox();
    void renderSpaceDust();
    void buildStarSpriteTextures();
    void renderWorldSolid(RSArea *area, int LOD, int verticesPerBlock);
    void renderWorldByID(RSArea *area, int LOD, int verticesPerBlock, int blockId);
    void renderObjects(RSArea *area, size_t blockID);
    void renderLineCube(Vector3D position, int32_t size);
    void renderBBox(Vector3D position, Point3D min, Point3D max);
    
    Camera *getCamera(void);

    void setLight(Point3D *position);

    void setClearColor(uint8_t red, uint8_t green, uint8_t blue);

    void prepare(RSEntity *object);
    void renderMissionObjects(RSMission *mission);
    void renderMapOverlay(RSArea *area);
    void setPlayerPosition(Point3D *position);
    void renderWorldToTexture(RSArea *area);

    void initRenderToTexture();
    void getRenderToTexture();
    void initRenderCameraView();
    
    // Invalidation cache AABB
    void InvalidateAABBCache();
    void InvalidateAABBCache(RSArea* area);
    void InvalidateAABBForBlock(RSArea* area, int LOD, int blockId);

    // Pré-calcul (optionnel) sur une plage de LOD
    void PrecomputeAABBs(RSArea* area, int minLOD, int maxLOD);
    void bindCameraProjectionAndView(float verticalOffset);
    void bindCameraProjectionAndViewViewport(int32_t viewportW, int32_t viewportH, float verticalOffset);
    int32_t width;
    int32_t height;
    Point3D light;

     
};
