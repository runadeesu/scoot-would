// scoot would - level kit: parametric prefab generators used by scene JSON files and the editor.
// Conventions: Y up, local origin at the centre of the footprint on the ground (y = 0).
// Ramps rise towards +X. Widths run along Z.
#include "assets/asset_manager.h"
#include "core/log.h"
#include "game/world/prefab_kit.h"
#include "render/mesh_builder.h"
#include "scene/scene.h"

#include <cmath>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace sw {

using namespace kit;

namespace {

// ---------------------------------------------------------------------------
void groundPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(40, 0.2f, 40));
    int div = Pi(j, "divisions", 4);
    MeshBuilder b("ground");
    if (size.y > 0.001f) {
        b.boxFaces(Vec3(0, -size.y * 0.5f, 0), size, false, false, true);
        b.grid(Vec3(0), size.x, size.z, std::max(1, int(size.x / 10.0f) * div / 4), std::max(1, int(size.z / 10.0f) * div / 4));
    } else {
        b.grid(Vec3(0), size.x, size.z, div, div);
    }
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_worn")};
    out.castShadows = false;
}

void slabPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(4, 0.4f, 4));
    MeshBuilder b("slab");
    b.box(Vec3(0, size.y * 0.5f, 0), size);
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_smooth")};
    if (Pb(j, "grindEdges", false)) {
        float hx = size.x * 0.5f, hz = size.z * 0.5f, y = size.y;
        addRail(out, {{-hx, y, hz}, {hx, y, hz}}, RailType::Ledge);
        addRail(out, {{-hx, y, -hz}, {hx, y, -hz}}, RailType::Ledge);
    }
}

void roadPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float length = P(j, "length", 40), width = P(j, "width", 10);
    bool center = Pb(j, "centerLine", true);
    float curb = P(j, "curbHeight", 0.14f);
    float sidewalk = P(j, "sidewalk", 3.0f);
    MeshBuilder b("road");
    b.setMaterial(0);
    // asphalt = false: markings only (roads painted onto a shared asphalt ground)
    if (Pb(j, "asphalt", true)) b.grid(Vec3(0, 0, 0), length, width, std::max(1, int(length / 8)), 2);
    b.setMaterial(1);
    // lane markings: dashed centre line + solid edge lines
    float y = 0.004f;
    if (center)
        for (float x = -length * 0.5f + 1.0f; x < length * 0.5f - 1.0f; x += 6.0f)
            b.quad(Vec3(x, y, 0.07f), Vec3(x + 3.0f, y, 0.07f), Vec3(x + 3.0f, y, -0.07f), Vec3(x, y, -0.07f));
    b.setMaterial(2);
    for (float s : {-1.0f, 1.0f}) {
        float z = s * (width * 0.5f - 0.35f);
        b.quad(Vec3(-length * 0.5f, y, z + 0.06f), Vec3(length * 0.5f, y, z + 0.06f), Vec3(length * 0.5f, y, z - 0.06f), Vec3(-length * 0.5f, y, z - 0.06f));
    }
    if (sidewalk > 0.0f) {
        b.setMaterial(3);
        for (float s : {-1.0f, 1.0f}) {
            float zc = s * (width * 0.5f + sidewalk * 0.5f);
            b.box(Vec3(0, curb * 0.5f, zc), Vec3(length, curb, sidewalk));
        }
        // curb edges are grindable ledges
        for (float s : {-1.0f, 1.0f})
            addRail(out, {{-length * 0.5f, curb, s * width * 0.5f}, {length * 0.5f, curb, s * width * 0.5f}}, RailType::Ledge);
    }
    out.mesh = b.build();
    out.materials = {M(mat, "asphalt"), "road_line_white", "road_line_white", "pavement"};
    out.castShadows = false;
}

void wallPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(6, 2.5f, 0.3f));
    MeshBuilder b("wall");
    b.box(Vec3(0, size.y * 0.5f, 0), size);
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_wall")};
    if (Pb(j, "grindTop", false)) addRail(out, {{-size.x * 0.5f, size.y, 0}, {size.x * 0.5f, size.y, 0}}, RailType::Ledge);
}

void buildingPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(16, 12, 12));
    float floorH = P(j, "floorHeight", 3.3f);
    float winSpacing = P(j, "windowSpacing", 3.0f);
    bool shops = Pb(j, "shops", true);
    bool parapet = Pb(j, "parapet", true);
    bool lit = Pb(j, "litWindows", true);
    std::string roofMat = Ps(j, "roofMaterial", "roof_grey");
    float hx = size.x * 0.5f, hz = size.z * 0.5f;
    MeshBuilder b("building");
    // slot 0 walls, 1 glass, 2 frames/trim, 3 roof, 4 lit windows
    b.setMaterial(0);
    b.boxFaces(Vec3(0, size.y * 0.5f, 0), size, false, false, true);
    b.setMaterial(3);
    b.quad(Vec3(-hx, size.y, hz), Vec3(hx, size.y, hz), Vec3(hx, size.y, -hz), Vec3(-hx, size.y, -hz));
    if (parapet) {
        b.setMaterial(2);
        float t = 0.3f, h = 0.9f;
        b.box(Vec3(0, size.y + h * 0.5f, hz - t * 0.5f), Vec3(size.x, h, t));
        b.box(Vec3(0, size.y + h * 0.5f, -hz + t * 0.5f), Vec3(size.x, h, t));
        b.box(Vec3(hx - t * 0.5f, size.y + h * 0.5f, 0), Vec3(t, h, size.z - 2 * t));
        b.box(Vec3(-hx + t * 0.5f, size.y + h * 0.5f, 0), Vec3(t, h, size.z - 2 * t));
    }
    // floors bands + windows on each facade
    int floors = std::max(1, int(size.y / floorH));
    Rng rng(uint64_t(size.x * 131 + size.y * 71 + size.z * 17));
    struct Face {
        Vec3 origin, along, normal;
        float width;
    };
    Face faces[4] = {{{-hx, 0, hz}, {1, 0, 0}, {0, 0, 1}, size.x},
                     {{hx, 0, -hz}, {-1, 0, 0}, {0, 0, -1}, size.x},
                     {{hx, 0, hz}, {0, 0, -1}, {1, 0, 0}, size.z},
                     {{-hx, 0, -hz}, {0, 0, 1}, {-1, 0, 0}, size.z}};
    for (const Face& f : faces) {
        int cols = std::max(1, int(f.width / winSpacing));
        float colW = f.width / float(cols);
        for (int fl = 0; fl < floors; ++fl) {
            float y0 = float(fl) * floorH;
            // trim band
            b.setMaterial(2);
            Vec3 bandC = f.origin + f.along * (f.width * 0.5f) + Vec3(0, y0 + floorH - 0.12f, 0) + f.normal * 0.05f;
            Vec3 bandSize = vabs(f.along) * f.width + Vec3(0, 0.18f, 0) + vabs(f.normal) * 0.1f;
            b.box(bandC, bandSize);
            bool ground = fl == 0 && shops;
            for (int c = 0; c < cols; ++c) {
                float cx = (float(c) + 0.5f) * colW;
                float ww = ground ? colW * 0.8f : std::min(1.4f, colW * 0.55f);
                float wh = ground ? floorH * 0.72f : floorH * 0.5f;
                float wy = ground ? y0 + 0.15f : y0 + floorH * 0.28f;
                Vec3 base = f.origin + f.along * cx + f.normal * 0.03f;
                Vec3 a = base - f.along * (ww * 0.5f) + Vec3(0, wy, 0);
                Vec3 bb = base + f.along * (ww * 0.5f) + Vec3(0, wy, 0);
                Vec3 cc = bb + Vec3(0, wh, 0), d = a + Vec3(0, wh, 0);
                bool isLit = lit && !ground && rng.uniform() < 0.35f;
                b.setMaterial(isLit ? 4 : 1);
                b.quad(a, bb, cc, d);
                // frame
                b.setMaterial(2);
                float ft = 0.07f;
                Vec3 up(0, 1, 0);
                Vec3 depth = f.normal * 0.06f;
                Vec3 alongAbs = vabs(f.along);
                b.box(base + Vec3(0, wy - ft * 0.5f, 0) + depth * 0.5f, alongAbs * (ww + ft * 2) + Vec3(0, ft, 0) + vabs(f.normal) * 0.08f);
                b.box(base + Vec3(0, wy + wh + ft * 0.5f, 0) + depth * 0.5f, alongAbs * (ww + ft * 2) + Vec3(0, ft, 0) + vabs(f.normal) * 0.08f);
                b.box(a + up * (wh * 0.5f) - f.along * (ft * 0.5f) + depth * 0.5f, alongAbs * ft + Vec3(0, wh, 0) + vabs(f.normal) * 0.08f);
                b.box(bb + up * (wh * 0.5f) + f.along * (ft * 0.5f) + depth * 0.5f, alongAbs * ft + Vec3(0, wh, 0) + vabs(f.normal) * 0.08f);
                if (ground) {
                    // window sill = grindable ledge on shop fronts
                    b.box(base + Vec3(0, wy - 0.08f, 0) + f.normal * 0.18f, alongAbs * (ww + 0.2f) + Vec3(0, 0.16f, 0) + vabs(f.normal) * 0.36f);
                }
            }
        }
    }
    out.mesh = b.build();
    out.materials = {M(mat, "brick_red"), "glass", "concrete_wall", roofMat, "window_lit"};
    // collision: box + parapet walls
    MeshBuilder c("building_col");
    c.box(Vec3(0, size.y * 0.5f, 0), size);
    if (parapet) {
        float t = 0.3f, h = 0.9f;
        c.box(Vec3(0, size.y + h * 0.5f, hz - t * 0.5f), Vec3(size.x, h, t));
        c.box(Vec3(0, size.y + h * 0.5f, -hz + t * 0.5f), Vec3(size.x, h, t));
        c.box(Vec3(hx - t * 0.5f, size.y + h * 0.5f, 0), Vec3(t, h, size.z - 2 * t));
        c.box(Vec3(-hx + t * 0.5f, size.y + h * 0.5f, 0), Vec3(t, h, size.z - 2 * t));
        // parapet tops are grindable ledges
        float y = size.y + h;
        addRail(out, {{-hx, y, hz - t * 0.5f}, {hx, y, hz - t * 0.5f}}, RailType::Ledge);
        addRail(out, {{-hx, y, -hz + t * 0.5f}, {hx, y, -hz + t * 0.5f}}, RailType::Ledge);
    }
    out.collisionMesh = c.build();
}

// --- skatepark obstacles ------------------------------------------------------
// Dimensions follow skatepark practice and ASTM F2334 (public skatepark facilities): 2 3/8 in (60.3 mm) steel
// coping set about 1/8 in (3 mm) proud of the deck and of the transition, radius a little larger than the height
// on park quarters (4 ft / 6 ft, 6 ft / 8 ft), guardrails on decks 38 in (0.97 m) or higher.
constexpr float kCopingR = 0.0302f;   // 60.3 mm steel pipe
constexpr float kCopingProud = 0.003f;

// coping along a lip at (lipX, h) whose deck continues towards +x (dir = +1) or -x (dir = -1)
void coping(MeshBuilder& b, PrefabBuild& out, float lipX, float h, float dir, float z0, float z1) {
    Vec2 c(lipX + dir * (kCopingR - kCopingProud), h - (kCopingR - kCopingProud));
    b.tube({{c.x, c.y, z0}, {c.x, c.y, z1}}, kCopingR, 12, true);
    addRail(out, {{c.x, h + kCopingProud, z0}, {c.x, h + kCopingProud, z1}}, RailType::Coping, kCopingR);
}

// guardrail at the back of a high deck: 42 in (1.07 m) tall, vertical bars only (nothing to climb), ends 24 in
// (0.61 m) short of the deck's open sides
void guardrail(MeshBuilder& b, float x, float y, float z0, float z1) {
    float zs = z0 + 0.61f, ze = z1 - 0.61f;
    if (ze - zs < 0.3f) return;
    float top = y + 1.07f;
    b.tube({{x, top, zs}, {x, top, ze}}, 0.024f, 10, true);          // 48 mm top rail
    b.tube({{x, y + 0.1f, zs}, {x, y + 0.1f, ze}}, 0.017f, 8, true);  // foot rail
    int posts = std::max(2, int(std::ceil((ze - zs) / 1.6f)) + 1);
    for (int i = 0; i < posts; ++i) {
        float z = lerpf(zs, ze, float(i) / float(posts - 1));
        b.tube({{x, y, z}, {x, top, z}}, 0.024f, 10, false);
    }
    for (float z = zs + 0.1f; z < ze - 0.05f; z += 0.1f) b.tube({{x, y + 0.1f, z}, {x, top, z}}, 0.008f, 6, false);  // 100 mm spacing
}

void quarterPipePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float h = P(j, "height", 1.8f), r = P(j, "radius", 2.4f), w = P(j, "width", 6.0f), deck = P(j, "deck", 1.2f);
    auto prof = profiles::quarterPipe(h, r, deck, std::max(8, int(r * 6)));
    float len = profileLength(prof);
    prof = shifted(prof, -len * 0.5f);
    float lipX = prof[3].x;  // (xTop, height) point in the profile order
    float backX = prof[2].x;
    MeshBuilder b("quarter");
    b.setMaterial(0);
    b.extrude(prof, -w * 0.5f, w * 0.5f, true, true);
    b.setMaterial(1);
    if (Pb(j, "coping", true)) coping(b, out, lipX, h, 1.0f, -w * 0.5f, w * 0.5f);
    if (h >= 0.97f && Pb(j, "guardrail", true)) guardrail(b, backX - 0.06f, h, -w * 0.5f, w * 0.5f);
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park"), "coping"};
}

void halfPipePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float h = P(j, "height", 2.4f), r = P(j, "radius", 2.8f), w = P(j, "width", 8.0f), flat = P(j, "flat", 4.0f), deck = P(j, "deck", 1.5f);
    auto prof = profiles::quarterPipe(h, r, deck, std::max(10, int(r * 6)));
    float len = profileLength(prof);
    float lipLocal = prof[3].x;
    MeshBuilder b("halfpipe");
    b.setMaterial(0);
    // right quarter
    auto right = shifted(prof, flat * 0.5f);
    b.extrude(right, -w * 0.5f, w * 0.5f, true, true);
    // left quarter: mirrored
    std::vector<Vec2> left;
    for (auto it = prof.rbegin(); it != prof.rend(); ++it) left.push_back(Vec2(-(it->x + flat * 0.5f), it->y));
    b.extrude(left, -w * 0.5f, w * 0.5f, true, true);
    // flat bottom
    b.box(Vec3(0, 0.02f, 0), Vec3(flat, 0.04f, w));
    b.setMaterial(1);
    for (float s : {-1.0f, 1.0f}) {
        coping(b, out, s * (flat * 0.5f + lipLocal), h, s, -w * 0.5f, w * 0.5f);
        if (h >= 0.97f && Pb(j, "guardrail", true)) guardrail(b, s * (flat * 0.5f + lipLocal + deck - 0.06f), h, -w * 0.5f, w * 0.5f);
    }
    (void)len;
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park"), "coping"};
}

void spinePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float h = P(j, "height", 1.6f), r = P(j, "radius", 2.0f), w = P(j, "width", 6.0f);
    auto prof = profiles::spine(h, r, std::max(8, int(r * 6)));
    prof = shifted(prof, -profileLength(prof) * 0.5f);
    MeshBuilder b("spine");
    b.setMaterial(0);
    b.extrude(prof, -w * 0.5f, w * 0.5f, true, true);
    b.setMaterial(1);
    // spine: one pipe over the ridge, its top 1/8 in above the two transitions
    b.tube({{0, h - kCopingR + kCopingProud, -w * 0.5f}, {0, h - kCopingR + kCopingProud, w * 0.5f}}, kCopingR, 12, true);
    addRail(out, {{0, h + kCopingProud, -w * 0.5f}, {0, h + kCopingProud, w * 0.5f}}, RailType::Coping, kCopingR);
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park"), "coping"};
}

void kickerPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 2.2f), h = P(j, "height", 0.8f), w = P(j, "width", 2.0f);
    auto prof = profiles::kicker(l, h, 10);
    prof = shifted(prof, -l * 0.5f);
    MeshBuilder b("kicker");
    b.extrude(prof, -w * 0.5f, w * 0.5f, true, true);
    if (Pb(j, "metalLip", true)) {
        // steel plate over the lip
        b.setMaterial(1);
        auto lp = profiles::kicker(l, h, 10);
        Vec2 top = lp[2] + Vec2(-l * 0.5f, 0);
        Vec2 prev = lp[3] + Vec2(-l * 0.5f, 0);
        Vec3 a(prev.x, prev.y + 0.004f, -w * 0.5f), bb(top.x, top.y + 0.004f, -w * 0.5f);
        b.quad(Vec3(a.x, a.y, w * 0.5f), Vec3(bb.x, bb.y, w * 0.5f), bb, a);
    }
    out.mesh = b.build();
    out.materials = {M(mat, "plywood"), "metal_plate"};
}

void bankPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 3.0f), h = P(j, "height", 1.2f), w = P(j, "width", 4.0f), deck = P(j, "deck", 1.0f);
    std::vector<Vec2> prof = {{0, 0}, {l + deck, 0}, {l + deck, h}, {l, h}};
    if (deck <= 0.001f) prof = profiles::bank(l, h);
    prof = shifted(prof, -(l + deck) * 0.5f);
    MeshBuilder b("bank");
    b.extrude(prof, -w * 0.5f, w * 0.5f, true, false);
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park")};
    if (deck > 0.001f && Pb(j, "grindEdge", true)) {
        float x = l - (l + deck) * 0.5f;
        addRail(out, {{x, h, -w * 0.5f}, {x, h, w * 0.5f}}, RailType::Ledge);
    }
}

void funboxPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float rl = P(j, "rampLength", 1.8f), tl = P(j, "topLength", 2.5f), h = P(j, "height", 0.6f), w = P(j, "width", 3.0f);
    auto prof = profiles::funbox(rl, tl, h);
    prof = shifted(prof, -(rl * 2 + tl) * 0.5f);
    MeshBuilder b("funbox");
    b.setMaterial(0);
    b.extrude(prof, -w * 0.5f, w * 0.5f, true, false);
    // steel angle on the top edges
    b.setMaterial(1);
    for (float s : {-1.0f, 1.0f}) {
        b.box(Vec3(0, h - 0.015f, s * (w * 0.5f - 0.015f)), Vec3(tl, 0.03f, 0.03f));
        addRail(out, {{-tl * 0.5f, h, s * w * 0.5f}, {tl * 0.5f, h, s * w * 0.5f}}, RailType::Ledge);
    }
    if (Pb(j, "rail", true)) {
        b.setMaterial(2);
        float rh = h + P(j, "railHeight", 0.35f);
        std::vector<Vec3> path = {{-tl * 0.5f - rl * 0.7f, rh - (rh - h) * 0.0f, 0}, {tl * 0.5f + rl * 0.7f, rh, 0}};
        path[0].y = rh;
        b.tube(path, 0.025f, 12, true);
        for (float x : {-tl * 0.5f + 0.2f, tl * 0.5f - 0.2f}) b.cylinder(Vec3(x, h, 0), 0.022f, rh - h, 8, false);
        addRail(out, {{path[0].x, rh + 0.02f, 0}, {path[1].x, rh + 0.02f, 0}}, RailType::Round);
    }
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park"), "coping", "rail_steel"};
}

void manualPadPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 5.0f), w = P(j, "width", 1.8f), h = P(j, "height", 0.25f);
    MeshBuilder b("manualpad");
    b.setMaterial(0);
    b.box(Vec3(0, h * 0.5f, 0), Vec3(l, h, w));
    b.setMaterial(1);
    for (float s : {-1.0f, 1.0f}) b.box(Vec3(0, h - 0.012f, s * (w * 0.5f - 0.012f)), Vec3(l, 0.025f, 0.025f));
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_smooth"), "coping"};
    for (float s : {-1.0f, 1.0f}) addRail(out, {{-l * 0.5f, h, s * w * 0.5f}, {l * 0.5f, h, s * w * 0.5f}}, RailType::Ledge);
}

void ledgePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 6.0f), w = P(j, "width", 0.6f), h = P(j, "height", 0.45f);
    bool both = Pb(j, "bothEdges", true);
    MeshBuilder b("ledge");
    b.setMaterial(0);
    b.box(Vec3(0, h * 0.5f, 0), Vec3(l, h, w));
    b.setMaterial(1);
    b.box(Vec3(0, h - 0.015f, w * 0.5f - 0.015f), Vec3(l, 0.03f, 0.03f));
    if (both) b.box(Vec3(0, h - 0.015f, -w * 0.5f + 0.015f), Vec3(l, 0.03f, 0.03f));
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_smooth"), "coping"};
    addRail(out, {{-l * 0.5f, h, w * 0.5f}, {l * 0.5f, h, w * 0.5f}}, RailType::Ledge);
    if (both) addRail(out, {{-l * 0.5f, h, -w * 0.5f}, {l * 0.5f, h, -w * 0.5f}}, RailType::Ledge);
}

// stairs going down towards +X, optional handrails and hubba ledges on the sides
void stairsPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    int steps = Pi(j, "steps", 6);
    float rise = P(j, "rise", 0.17f), run = P(j, "run", 0.33f), w = P(j, "width", 4.0f), top = P(j, "landingTop", 2.5f);
    bool rail = Pb(j, "handrail", true);
    bool railBoth = Pb(j, "handrailBoth", false);
    bool hubba = Pb(j, "hubba", false);
    float H = float(steps) * rise;
    auto prof = profiles::stairs(steps, rise, run, top, 0);
    float len = top + float(steps) * run;
    prof = shifted(prof, -len * 0.5f);
    MeshBuilder b("stairs");
    b.setMaterial(0);
    b.extrude(prof, -w * 0.5f, w * 0.5f, true, false);
    float x0 = -len * 0.5f + top;          // top of the flight
    float x1 = -len * 0.5f + len;          // bottom of the flight
    if (hubba) {
        // sloped concrete ledges on both sides of the steps
        float hw = 0.5f, extra = 0.45f;
        b.setMaterial(1);
        for (float s : {-1.0f, 1.0f}) {
            float zc = s * (w * 0.5f + hw * 0.5f);
            std::vector<Vec2> hp = {{-len * 0.5f, 0}, {x1 + 0.3f, 0}, {x1 + 0.3f, extra}, {x0, H + extra}, {-len * 0.5f, H + extra}};
            b.extrude(hp, zc - hw * 0.5f, zc + hw * 0.5f, true, false);
            float ze = zc - s * hw * 0.5f;
            addRail(out, {{-len * 0.5f + 0.3f, H + extra, ze}, {x0, H + extra, ze}, {x1 + 0.3f, extra, ze}}, RailType::Ledge);
        }
    }
    if (rail) {
        b.setMaterial(2);
        float rh = P(j, "railHeight", 0.9f);
        for (float s : {1.0f, -1.0f}) {
            if (s < 0 && !railBoth) continue;
            float z = s * (w * 0.5f - 0.3f);
            if (hubba) z = s * (w * 0.5f - 0.35f);
            std::vector<Vec3> path = {{x0 - 0.9f, H + rh, z}, {x0 + 0.1f, H + rh, z}, {x1 - 0.1f, rh, z}, {x1 + 0.7f, rh, z}};
            // posts at the kinks follow the stair height
            b.tube(path, 0.025f, 12, true);
            for (size_t i = 0; i < path.size(); ++i) {
                Vec3 p = path[i];
                float groundY = p.x < x0 ? H : (p.x > x1 ? 0.0f : H - (p.x - x0) / (x1 - x0) * H);
                if (i == 0 || i == path.size() - 1 || i == 1 || i == 2)
                    b.cylinder(Vec3(p.x, groundY, p.z), 0.022f, p.y - groundY, 8, false);
            }
            std::vector<Vec3> rp = path;
            for (auto& p : rp) p.y += 0.02f;
            addRail(out, rp, RailType::Round);
        }
    }
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_worn"), "concrete_smooth", "rail_steel"};
}

void railPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 5.0f), h = P(j, "height", 0.45f);
    float drop = P(j, "drop", 0.0f);  // height difference start -> end (down rails)
    bool square = Ps(j, "type", "round") == "square";
    int kinks = Pi(j, "segments", 1);
    std::vector<Vec3> path;
    for (int i = 0; i <= kinks; ++i) {
        float t = float(i) / float(kinks);
        path.push_back(Vec3(-l * 0.5f + l * t, h + drop * (1.0f - t), 0));
    }
    MeshBuilder b("rail");
    float radius = square ? 0.025f : 0.024f;
    railGeometry(b, path, radius, kinks > 1 ? 1 : 0, square);
    out.mesh = b.build();
    out.materials = {M(mat, square ? "rail_black" : "rail_steel")};
    std::vector<Vec3> rp = path;
    for (auto& p : rp) p.y += radius;
    addRail(out, rp, square ? RailType::Square : RailType::Round, radius);
    out.meshKey = keyOf("rail", j, out.materials[0]);
}

void bowlPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float radius = P(j, "radius", 7.0f), depth = P(j, "depth", 2.2f), trans = P(j, "transition", 2.6f), deck = P(j, "deck", 2.0f);
    int seg = Pi(j, "segments", 48);
    float floorR = std::max(0.5f, radius - trans);
    // inner surface: flat floor -> circular transition up to the lip
    std::vector<Vec2> inner;
    inner.push_back({0.0f, 0.0f});
    inner.push_back({floorR, 0.0f});
    int ts = 12;
    float vert = std::max(0.0f, depth - trans);
    float curveH = depth - vert;
    float thetaMax = std::acos(clampf(1.0f - curveH / trans, -1.0f, 1.0f));
    for (int i = 1; i <= ts; ++i) {
        float th = thetaMax * float(i) / float(ts);
        inner.push_back({floorR + trans * std::sin(th), trans * (1.0f - std::cos(th))});
    }
    if (vert > 0) inner.push_back({inner.back().x, depth});
    float lipR = inner.back().x;
    MeshBuilder b("bowl");
    b.setMaterial(0);
    b.lathe(inner, seg, true, true);
    // deck ring + outer wall
    b.lathe({{lipR + deck, depth}, {lipR, depth}}, seg, false, false);
    b.lathe({{lipR + deck, 0.0f}, {lipR + deck, depth}}, seg, false, false);
    // coping ring
    b.setMaterial(1);
    // pool coping style ring, set 1/8 in proud of the deck and the wall like the straight copings
    std::vector<Vec3> ring;
    float cr = lipR + (kCopingR - kCopingProud);
    for (int i = 0; i <= seg; ++i) {
        float a = float(i) / float(seg) * kTwoPi;
        ring.push_back(Vec3(std::cos(a) * cr, depth - (kCopingR - kCopingProud), -std::sin(a) * cr));
    }
    b.tube(ring, kCopingR, 12, false);
    std::vector<Vec3> rr = ring;
    for (auto& p : rr) p.y = depth + kCopingProud;
    addRail(out, rr, RailType::Coping, kCopingR);
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park"), "coping"};
}

// In-ground concrete pool / flow bowl: a rounded rectangle whose walls are one transition swept round the
// coping line, so straight walls and corner pockets are one continuous surface. The floor is at y = 0 and the
// deck at y = depth: place it depth below grade and the deck is the ground (no guardrail needed at grade).
// length/width run coping to coping (X/Z), cornerRadius is the plan radius of the coping line at the
// corners (length = width = 2 * cornerRadius is a round pool). Coping "steel" is 2 3/8 in pipe; "pool" is
// concrete bullnose blocks with a band of tile underneath, the classic backyard pool.
void poolPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float L = P(j, "length", 12.0f), W = P(j, "width", 8.0f), depth = P(j, "depth", 1.8f), trans = P(j, "transition", 2.4f);
    float deck = P(j, "deck", 1.5f), rc = P(j, "cornerRadius", 3.0f);
    bool poolCoping = Ps(j, "coping", "steel") == "pool";
    bool tile = Pb(j, "tile", poolCoping);
    const float kNoseR = 0.045f, kBlock = 0.30f;  // bullnose radius, coping block depth (12 in)
    // riding surface profile (u = distance behind the coping line: negative towards the middle, y up)
    float vert = std::max(0.0f, depth - trans);
    float curveH = depth - vert;
    float thetaMax = std::acos(clampf(1.0f - curveH / trans, -1.0f, 1.0f));
    float xTop = trans * std::sin(thetaMax);
    float a = L * 0.5f, bz = W * 0.5f;
    rc = clampf(rc, std::min(xTop + 0.1f, std::min(a, bz)), std::min(a, bz));
    float topY = poolCoping ? depth + kCopingProud - 2.0f * kNoseR : depth;  // where the wall meets the coping
    float tileY = tile ? topY - 0.15f : topY;
    std::vector<Vec2> prof;  // (u, y) from the floor edge up to under the coping
    int ts = std::max(10, int(trans * 7.0f));
    for (int i = 0; i <= ts; ++i) {
        float th = thetaMax * float(i) / float(ts);
        prof.push_back({trans * std::sin(th) - xTop, trans * (1.0f - std::cos(th))});
    }
    if (vert > 0.0f) prof.push_back({0.0f, depth});
    // cut the profile at the tile line and the coping underside
    auto cutAt = [&](float y) {
        for (size_t i = 0; i + 1 < prof.size(); ++i) {
            if (prof[i].y < y - 1e-4f && prof[i + 1].y > y + 1e-4f) {
                float t = (y - prof[i].y) / (prof[i + 1].y - prof[i].y);
                prof.insert(prof.begin() + long(i) + 1, prof[i] + (prof[i + 1] - prof[i]) * t);
                return;
            }
        }
    };
    cutAt(tileY);
    cutAt(topY);
    while (prof.size() > 2 && prof.back().y > topY + 1e-4f) prof.pop_back();
    // coping line: rounded rectangle, outward normals
    struct PathPt {
        Vec2 p, n;
        float s;
    };
    // corner c holds path[c * (cseg + 1) .. c * (cseg + 1) + cseg]; the straight walls are the segments between
    // corners (zero length on a round pool: skipped)
    std::vector<PathPt> path;
    int cseg = std::max(6, int(rc * kHalfPi / 0.22f));
    Vec2 centres[4] = {{a - rc, bz - rc}, {-(a - rc), bz - rc}, {-(a - rc), -(bz - rc)}, {a - rc, -(bz - rc)}};
    for (int c = 0; c < 4; ++c) {
        float a0 = float(c) * kHalfPi;
        for (int i = 0; i <= cseg; ++i) {
            float th = a0 + kHalfPi * float(i) / float(cseg);
            Vec2 n(std::cos(th), std::sin(th));
            path.push_back({centres[c] + n * rc, n, 0.0f});
        }
    }
    path.push_back(path.front());
    for (size_t i = 1; i < path.size(); ++i) path[i].s = path[i - 1].s + (path[i].p - path[i - 1].p).length();
    auto at = [](const PathPt& pp, float u, float y) { return Vec3(pp.p.x + pp.n.x * u, y, pp.p.y + pp.n.y * u); };
    auto dedupe = [](std::vector<Vec3> v, bool closed) {
        std::vector<Vec3> o;
        for (auto& p : v)
            if (o.empty() || (o.back() - p).lengthSq() > 1e-8f) o.push_back(p);
        if (closed && o.size() > 1 && (o.back() - o.front()).lengthSq() < 1e-8f) o.pop_back();
        return o;
    };
    // sweep a (u, y) profile round the path; the surface faces +y / the middle of the pool
    MeshBuilder b("pool");
    auto sweep = [&](const std::vector<Vec2>& pr, auto matOf) {
        std::vector<float> ps(pr.size(), 0.0f);
        for (size_t k = 1; k < pr.size(); ++k) ps[k] = ps[k - 1] + (pr[k] - pr[k - 1]).length();
        auto pn = [&](size_t k) {  // profile normal (u, y): rotate the tangent +90 deg
            Vec2 t0 = k > 0 ? (pr[k] - pr[k - 1]).normalized() : Vec2(0, 0);
            Vec2 t1 = k + 1 < pr.size() ? (pr[k + 1] - pr[k]).normalized() : Vec2(0, 0);
            Vec2 t = (t0 + t1);
            if (t.lengthSq() < 1e-8f) t = t1.lengthSq() > 0 ? t1 : t0;
            t = t.normalized();
            return Vec2(-t.y, t.x);
        };
        for (size_t k = 0; k + 1 < pr.size(); ++k) {
            b.setMaterial(matOf((pr[k].y + pr[k + 1].y) * 0.5f));
            Vec2 n0 = pn(k), n1 = pn(k + 1);
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                const PathPt &A = path[i], &B = path[i + 1];
                if ((B.p - A.p).lengthSq() < 1e-8f) continue;
                Vec3 p00 = at(A, pr[k].x, pr[k].y), p01 = at(B, pr[k].x, pr[k].y);
                Vec3 p10 = at(A, pr[k + 1].x, pr[k + 1].y), p11 = at(B, pr[k + 1].x, pr[k + 1].y);
                auto nrm = [](const PathPt& pp, Vec2 q) { return Vec3(pp.n.x * q.x, q.y, pp.n.y * q.x).normalized(); };
                uint32_t v00 = b.addVertex(p00, nrm(A, n0), Vec2(A.s, ps[k])), v01 = b.addVertex(p01, nrm(B, n0), Vec2(B.s, ps[k]));
                uint32_t v10 = b.addVertex(p10, nrm(A, n1), Vec2(A.s, ps[k + 1])), v11 = b.addVertex(p11, nrm(B, n1), Vec2(B.s, ps[k + 1]));
                Vec3 face = cross(p01 - p00, p10 - p00);
                if (dot(face, nrm(A, n0) + nrm(B, n1)) >= 0.0f) {
                    b.addTriangle(v00, v01, v11);
                    b.addTriangle(v00, v11, v10);
                } else {
                    b.addTriangle(v00, v11, v01);
                    b.addTriangle(v00, v10, v11);
                }
            }
        }
    };
    sweep(prof, [&](float y) { return tile && y > tileY ? 2 : 0; });
    // flat floor inside the transitions
    b.setMaterial(0);
    std::vector<Vec3> floorPts;
    for (size_t i = path.size() - 1; i-- > 0;) floorPts.push_back(at(path[i], -xTop, 0.0f));  // CCW seen from above
    b.polygon(dedupe(floorPts, true), Vec3(0, 1, 0));
    // coping
    float backU = 0.0f;  // where the deck starts behind the coping line
    b.setMaterial(1);
    if (poolCoping) {
        std::vector<Vec2> cp;  // bullnose: under the block, round the front, over the top
        Vec2 c(-0.035f + kNoseR, depth + kCopingProud - kNoseR);
        cp.push_back({0.0f, topY});
        for (int i = 0; i <= 8; ++i) {
            float th = -kHalfPi - kPi * float(i) / 8.0f;
            cp.push_back(c + Vec2(std::cos(th), std::sin(th)) * kNoseR);
        }
        cp.push_back({kBlock - 0.004f, depth + kCopingProud});
        cp.push_back({kBlock, depth});
        cp.erase(cp.begin() + 1);  // the first nose point sits under the block, on the wall line
        sweep(cp, [](float) { return 1; });
        backU = kBlock;
        std::vector<Vec3> rail;
        for (auto& pp : path) rail.push_back(at(pp, c.x, depth + kCopingProud));
        rail = dedupe(rail, false);
        addRail(out, rail, RailType::Coping, kNoseR);
    } else {
        std::vector<Vec3> tubePts, rail;
        for (auto& pp : path) {
            tubePts.push_back(at(pp, kCopingR - kCopingProud, depth - (kCopingR - kCopingProud)));
            rail.push_back(at(pp, kCopingR - kCopingProud, depth + kCopingProud));
        }
        b.tube(dedupe(tubePts, false), kCopingR, 12, false);
        addRail(out, dedupe(rail, false), RailType::Coping, kCopingR);
        backU = 2.0f * kCopingR - kCopingProud;
    }
    // deck out to the outer rectangle: four pieces split at the corner diagonals
    b.setMaterial(0);
    float A = a + deck, B = bz + deck;
    Vec2 outer[4] = {{A, B}, {-A, B}, {-A, -B}, {A, -B}};
    size_t perCorner = size_t(cseg + 1);
    for (int c = 0; c < 4; ++c) {
        // from the middle of corner c's arc (it faces outer[c]) along the wall to the middle of the next corner
        size_t mid = size_t(c) * perCorner + size_t(cseg / 2);
        size_t midNext = size_t((c + 1) % 4) * perCorner + size_t(cseg / 2);
        std::vector<Vec3> poly;
        poly.push_back(Vec3(outer[c].x, depth, outer[c].y));
        for (size_t i = mid;; i = (i + 1) % (path.size() - 1)) {
            poly.push_back(at(path[i], backU, depth));
            if (i == midNext) break;
        }
        poly.push_back(Vec3(outer[(c + 1) % 4].x, depth, outer[(c + 1) % 4].y));
        b.polygon(dedupe(poly, true), Vec3(0, 1, 0));
    }
    // outer walls (hidden below grade, closes the solid when it stands on its own)
    b.quad(Vec3(A, 0, B), Vec3(A, 0, -B), Vec3(A, depth, -B), Vec3(A, depth, B));
    b.quad(Vec3(-A, 0, -B), Vec3(-A, 0, B), Vec3(-A, depth, B), Vec3(-A, depth, -B));
    b.quad(Vec3(-A, 0, B), Vec3(A, 0, B), Vec3(A, depth, B), Vec3(-A, depth, B));
    b.quad(Vec3(A, 0, -B), Vec3(-A, 0, -B), Vec3(-A, depth, -B), Vec3(A, depth, -B));
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_park"), poolCoping ? "pool_coping" : "coping", "pool_tile"};
}

// descending slope towards +X that meets the ground tangentially (roll-ins, landings).
// Returns the CCW outline and the x where the surface reaches the ground.
std::vector<Vec2> slopeProfile(float height, float length, float radius, float& groundX) {
    float alpha = std::atan2(height, length);
    radius = std::min(radius, height / std::max(1e-3f, 1.0f - std::cos(alpha)) * 0.8f);
    float xa = radius * std::sin(alpha) + (height - radius * (1.0f - std::cos(alpha))) / std::tan(alpha);
    groundX = xa;
    std::vector<Vec2> p;
    p.push_back({0, 0});
    p.push_back({xa, 0});
    int n = 14;
    for (int i = 1; i <= n; ++i) {
        float th = alpha * float(i) / float(n);
        p.push_back({xa - radius * std::sin(th), radius * (1.0f - std::cos(th))});
    }
    p.push_back({0, height});
    return p;
}

// complete mega ramp: roll-in tower, kicker, gap, landing, final quarter. Runs along +X.
void megaRampPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float towerH = P(j, "towerHeight", 11.0f), rollL = P(j, "rollInLength", 20.0f), w = P(j, "width", 7.0f);
    float kickH = P(j, "kickerHeight", 3.2f), kickL = P(j, "kickerLength", 7.0f), gap = P(j, "gap", 14.0f);
    float landH = P(j, "landingHeight", 5.5f), landL = P(j, "landingLength", 20.0f), qpH = P(j, "quarterHeight", 6.0f);
    MeshBuilder b("megaramp");
    b.setMaterial(0);
    float deckL = 4.0f;
    // tower with deck, then the roll in
    b.box(Vec3(deckL * 0.5f, towerH * 0.5f, 0), Vec3(deckL, towerH, w));
    float x = deckL;
    float gx = 0;
    b.extrude(shifted(slopeProfile(towerH, rollL, 12.0f, gx), x), -w * 0.5f, w * 0.5f, true, true);
    x += gx + 10.0f;  // flat run up to the kicker
    b.extrude(shifted(profiles::kicker(kickL, kickH, 16), x), -w * 0.5f, w * 0.5f, true, true);
    x += kickL + gap;
    // landing: table then descending slope
    b.box(Vec3(x + 1.0f, landH * 0.5f, 0), Vec3(2.0f, landH, w));
    x += 2.0f;
    b.extrude(shifted(slopeProfile(landH, landL, 14.0f, gx), x), -w * 0.5f, w * 0.5f, true, true);
    x += gx + 14.0f;  // run out
    auto qp = profiles::quarterPipe(qpH, qpH * 1.1f, 2.0f, 20);
    b.extrude(shifted(qp, x), -w * 0.5f, w * 0.5f, true, true);
    b.setMaterial(1);
    coping(b, out, x + qp[3].x, qpH, 1.0f, -w * 0.5f, w * 0.5f);
    // guard rails on the tower
    b.setMaterial(2);
    for (float s : {-1.0f, 1.0f}) b.tube({{0.2f, towerH + 1.0f, s * (w * 0.5f - 0.1f)}, {deckL - 0.2f, towerH + 1.0f, s * (w * 0.5f - 0.1f)}}, 0.03f, 8, true);
    out.mesh = b.build();
    out.materials = {M(mat, "plywood"), "coping", "rail_steel"};
}

// drainage ditch: flat channel with banked sides, runs along X
void ditchPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 60.0f), bottom = P(j, "bottom", 4.0f), bankW = P(j, "bankWidth", 3.0f), depth = P(j, "depth", 2.0f), top = P(j, "top", 2.0f);
    MeshBuilder b("ditch");
    b.setMaterial(0);
    b.box(Vec3(0, 0.02f, 0), Vec3(l, 0.04f, bottom));
    // banks as extrusions along X: profile in (z,y) built as XY then rotated
    Mat4 rot = Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 1, 0)));
    for (float s : {-1.0f, 1.0f}) {
        std::vector<Vec2> prof;
        // slightly curved bank (transition at the bottom)
        float r = 2.0f;
        prof.push_back({0, 0});
        prof.push_back({bankW + top, 0});
        prof.push_back({bankW + top, depth});
        prof.push_back({bankW, depth});
        int n = 8;
        float slope = std::atan2(depth, bankW);
        for (int i = n - 1; i >= 1; --i) {
            float t = float(i) / float(n);
            float xx = bankW * t;
            float yy = depth * t;
            float bend = std::sin(t * kPi) * 0.12f * r * (1.0f - t);
            prof.push_back({xx + bend * std::sin(slope), yy - bend * std::cos(slope)});
        }
        MeshBuilder tmp;
        (void)tmp;
        Mat4 m = rot;
        if (s < 0) m = Mat4::rotation(Quat::angleAxis(kHalfPi, Vec3(0, 1, 0)));
        Mat4 place = Mat4::translation(Vec3(0, 0, s * bottom * 0.5f)) * m;
        b.setTransform(place);
        b.extrude(prof, -l * 0.5f, l * 0.5f, true, true);
        b.resetTransform();
        addRail(out, {{-l * 0.5f, depth, s * (bottom * 0.5f + bankW)}, {l * 0.5f, depth, s * (bottom * 0.5f + bankW)}}, RailType::Ledge);
    }
    out.mesh = b.build();
    out.materials = {M(mat, "gravel_concrete")};
}

void containerPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    bool longC = Pb(j, "long", false);
    Vec3 size(longC ? 12.19f : 6.06f, 2.59f, 2.44f);
    MeshBuilder b("container");
    b.setMaterial(0);
    b.box(Vec3(0, size.y * 0.5f, 0), size);
    b.setMaterial(1);
    // corner posts / rails
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f}) b.box(Vec3(sx * (size.x * 0.5f - 0.05f), size.y * 0.5f, sz * (size.z * 0.5f - 0.05f)), Vec3(0.14f, size.y + 0.02f, 0.14f));
    for (float sz : {-1.0f, 1.0f}) b.box(Vec3(0, size.y - 0.05f, sz * (size.z * 0.5f - 0.02f)), Vec3(size.x, 0.12f, 0.1f));
    out.mesh = b.build();
    out.materials = {M(mat, "container_blue"), "metal_rust"};
    out.meshKey = keyOf("container", j, out.materials[0]);
    for (float sz : {-1.0f, 1.0f}) addRail(out, {{-size.x * 0.5f, size.y, sz * size.z * 0.5f}, {size.x * 0.5f, size.y, sz * size.z * 0.5f}}, RailType::Ledge);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = size * 0.5f;
    out.boxCenter = Vec3(0, size.y * 0.5f, 0);
}

void loadingDockPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 16.0f), d = P(j, "depth", 4.0f), h = P(j, "height", 1.2f);
    MeshBuilder b("dock");
    b.setMaterial(0);
    b.box(Vec3(0, h * 0.5f, 0), Vec3(l, h, d));
    b.setMaterial(1);
    b.box(Vec3(0, h - 0.04f, d * 0.5f - 0.04f), Vec3(l, 0.08f, 0.08f));
    // rubber bumpers
    b.setMaterial(2);
    for (float x = -l * 0.5f + 2.0f; x < l * 0.5f - 1.0f; x += 4.0f) b.box(Vec3(x, h * 0.6f, d * 0.5f + 0.06f), Vec3(0.3f, 0.35f, 0.12f));
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_wall"), "metal_plate", "plastic_black"};
    addRail(out, {{-l * 0.5f, h, d * 0.5f}, {l * 0.5f, h, d * 0.5f}}, RailType::Ledge);
}

void pipePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float r = P(j, "radius", 0.5f), l = P(j, "length", 10.0f), h = P(j, "height", 0.8f);
    MeshBuilder b("pipe");
    b.setMaterial(0);
    b.tube({{-l * 0.5f, h, 0}, {l * 0.5f, h, 0}}, r, 20, true);
    b.setMaterial(1);
    for (float x = -l * 0.5f + 1.0f; x < l * 0.5f; x += 3.0f) b.box(Vec3(x, (h - r) * 0.5f, 0), Vec3(0.2f, h - r, r * 1.4f));
    out.mesh = b.build();
    out.materials = {M(mat, "metal_rust"), "concrete_wall"};
    addRail(out, {{-l * 0.5f, h + r, 0}, {l * 0.5f, h + r, 0}}, RailType::Round, r);
}


// --- imported models ----------------------------------------------------------------
// glTF prop (scanned / modelled assets in assets/models). Poly Haven style files often lay out
// several variants side by side, so nodes can be picked by name: "include" / "exclude" are comma
// separated substrings. The selection is merged into one static mesh, moved so its footprint is
// centred on the origin with the lowest point at y = 0, and shared between instances.
bool nameMatches(const std::string& name, const std::string& list) {
    if (list.empty()) return false;
    std::stringstream ss(list);
    std::string tok;
    while (std::getline(ss, tok, ','))
        if (!tok.empty() && name.find(tok) != std::string::npos) return true;
    return false;
}

void modelPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    std::string path = Ps(j, "model", "");
    std::string include = Ps(j, "include", ""), exclude = Ps(j, "exclude", "");
    float scale = P(j, "scale", 1.0f);
    std::string collider = Ps(j, "collider", "box");
    std::string key = keyOf("model", j, mat);
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, std::pair<MeshData, std::vector<std::string>>> cache;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            out.mesh = it->second.first;
            out.materials = it->second.second;
        }
    }
    if (out.mesh.vertices.empty()) {
        ModelPtr m = path.empty() ? nullptr : assets().model(path);
        if (!m || m->meshes.empty()) {
            LOG_WARN("prefab: model '%s' not available", path.c_str());
            MeshBuilder b("missing model");
            b.box(Vec3(0, 0.25f, 0), Vec3(0.5f));
            out.mesh = b.build();
            out.materials = {"editor_helper"};
            return;
        }
        auto nodeWorld = [&](int n) {
            Mat4 w = Mat4::identity();
            while (n >= 0) {
                w = m->nodes[size_t(n)].local.matrix() * w;
                n = m->nodes[size_t(n)].parent;
            }
            return w;
        };
        MeshData merged;
        merged.name = path;
        std::vector<std::string> mats;
        for (const ModelMesh& mm : m->meshes) {
            if (!mm.data.skin.empty()) continue;
            std::string nodeName = mm.node >= 0 ? m->nodes[size_t(mm.node)].name : mm.name;
            if (!include.empty() && !nameMatches(nodeName, include)) continue;
            if (nameMatches(nodeName, exclude)) continue;
            int offset = int(mats.size());
            for (auto& mp : mm.materials) mats.push_back(mp ? mp->name : std::string("default"));
            if (mm.materials.empty()) mats.push_back("default");
            Mat4 w = mm.node >= 0 ? nodeWorld(mm.node) : Mat4::identity();
            merged.append(mm.data, Mat4::scale(Vec3(scale)) * w, offset);
        }
        merged.computeBounds();
        if (Pb(j, "recenter", true) && merged.bounds.valid()) {
            Vec3 c = merged.bounds.center();
            Mat4 shift = Mat4::translation(Vec3(-c.x, -merged.bounds.min.y, -c.z));
            MeshData moved;
            moved.name = merged.name;
            moved.append(merged, shift, 0);
            merged = std::move(moved);
            merged.computeBounds();
        }
        out.mesh = merged;
        out.materials = mats;
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[key] = {merged, mats};
    }
    if (!mat.empty())
        for (auto& mname : out.materials) mname = mat;
    out.meshKey = key;
    out.cullDistance = P(j, "cullDistance", 0.0f);
    AABB b = out.mesh.bounds;
    if (collider == "none") {
        out.collider = ColliderComponent::Kind::None;
    } else if (collider == "mesh") {
        out.collider = ColliderComponent::Kind::Mesh;
    } else {
        out.collider = ColliderComponent::Kind::Box;
        out.boxHalfExtents = (b.max - b.min) * 0.5f;
        out.boxCenter = (b.max + b.min) * 0.5f;
    }
    out.surface = Ps(j, "surface", "metal");
}

// --- props ----------------------------------------------------------------------
void benchPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 2.0f);
    MeshBuilder b("bench");
    b.setMaterial(0);
    for (int i = 0; i < 4; ++i) b.box(Vec3(0, 0.44f, -0.2f + float(i) * 0.13f), Vec3(l, 0.04f, 0.11f));
    for (int i = 0; i < 2; ++i) b.box(Vec3(0, 0.62f + float(i) * 0.14f, 0.28f), Vec3(l, 0.1f, 0.035f));
    b.setMaterial(1);
    for (float x : {-l * 0.5f + 0.2f, l * 0.5f - 0.2f}) {
        b.box(Vec3(x, 0.21f, -0.15f), Vec3(0.05f, 0.42f, 0.05f));
        b.box(Vec3(x, 0.4f, 0.28f), Vec3(0.05f, 0.8f, 0.05f));
        b.box(Vec3(x, 0.4f, 0.0f), Vec3(0.05f, 0.04f, 0.6f));
    }
    out.mesh = b.build();
    out.materials = {M(mat, "wood_deck"), "rail_black"};
    out.meshKey = keyOf("bench", j, out.materials[0]);
    addRail(out, {{-l * 0.5f, 0.46f, -0.26f}, {l * 0.5f, 0.46f, -0.26f}}, RailType::Ledge);
    out.cullDistance = 220.0f;
}

void streetLampPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float h = P(j, "height", 6.5f);
    MeshBuilder b("lamp");
    b.setMaterial(0);
    b.lathe({{0.14f, 0.0f}, {0.12f, 0.4f}, {0.07f, 0.5f}, {0.06f, h}, {0.0f, h + 0.02f}}, 12, true);
    b.squareTube({{0, h - 0.1f, 0}, {0, h, 0}, {1.3f, h + 0.1f, 0}}, 0.04f, 0.04f, true);
    b.box(Vec3(1.45f, h + 0.05f, 0), Vec3(0.6f, 0.12f, 0.28f));
    b.setMaterial(1);
    b.quad(Vec3(1.18f, h - 0.012f, 0.12f), Vec3(1.72f, h - 0.012f, 0.12f), Vec3(1.72f, h - 0.012f, -0.12f), Vec3(1.18f, h - 0.012f, -0.12f));
    out.mesh = b.build();
    out.materials = {M(mat, "rail_black"), "lamp_emissive"};
    out.meshKey = keyOf("street_lamp", j, out.materials[0]);
    LightComponent lc;
    lc.light.type = LightProxy::Type::Spot;
    lc.light.position = Vec3(1.45f, h - 0.1f, 0);
    lc.light.direction = Vec3(-0.15f, -1, 0).normalized();
    lc.light.color = Vec3(1.0f, 0.82f, 0.6f);
    lc.light.intensity = 60.0f;
    lc.light.radius = 16.0f;
    lc.light.innerAngle = 0.55f;
    lc.light.outerAngle = 1.05f;
    lc.nightOnly = true;
    out.lights.push_back(lc);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.12f, h * 0.5f, 0.12f);
    out.boxCenter = Vec3(0, h * 0.5f, 0);
    out.cullDistance = 350.0f;
}

void fencePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 6.0f), h = P(j, "height", 1.2f);
    MeshBuilder b("fence");
    b.squareTube({{-l * 0.5f, h, 0}, {l * 0.5f, h, 0}}, 0.025f, 0.025f, true);
    b.squareTube({{-l * 0.5f, 0.12f, 0}, {l * 0.5f, 0.12f, 0}}, 0.02f, 0.02f, true);
    for (float x = -l * 0.5f; x <= l * 0.5f + 0.01f; x += 0.14f) b.box(Vec3(x, h * 0.5f, 0), Vec3(0.018f, h, 0.018f));
    for (float x = -l * 0.5f; x <= l * 0.5f + 0.01f; x += 2.0f) b.box(Vec3(x, h * 0.5f + 0.05f, 0), Vec3(0.06f, h + 0.1f, 0.06f));
    out.mesh = b.build();
    out.materials = {M(mat, "rail_black")};
    out.meshKey = keyOf("fence", j, out.materials[0]);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(l * 0.5f, h * 0.5f, 0.05f);
    out.boxCenter = Vec3(0, h * 0.5f, 0);
    addRail(out, {{-l * 0.5f, h + 0.025f, 0}, {l * 0.5f, h + 0.025f, 0}}, RailType::Square, 0.025f);
}

void barrierPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 3.0f);
    std::vector<Vec2> prof = {{-0.3f, 0}, {0.3f, 0}, {0.3f, 0.08f}, {0.12f, 0.3f}, {0.08f, 0.81f}, {-0.08f, 0.81f}, {-0.12f, 0.3f}, {-0.3f, 0.08f}};
    MeshBuilder b("barrier");
    Mat4 rot = Mat4::rotation(Quat::angleAxis(kHalfPi, Vec3(0, 1, 0)));
    b.setTransform(rot);
    // profile is in (z, y): extrude along local Z then rotate to lie along X
    std::vector<Vec2> ccw = prof;
    b.extrude(ccw, -l * 0.5f, l * 0.5f, true, false);
    b.resetTransform();
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_smooth")};
    out.meshKey = keyOf("barrier", j, out.materials[0]);
    addRail(out, {{-l * 0.5f, 0.81f, 0}, {l * 0.5f, 0.81f, 0}}, RailType::Ledge);
}

void conePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    MeshBuilder b("cone");
    b.setMaterial(0);
    b.lathe({{0.17f, 0.04f}, {0.02f, 0.72f}, {0.0f, 0.73f}}, 16, true);
    b.setMaterial(1);
    b.box(Vec3(0, 0.02f, 0), Vec3(0.4f, 0.04f, 0.4f));
    out.mesh = b.build();
    out.materials = {"rail_red", "plastic_black"};
    out.meshKey = "cone";
    auto rb = std::make_unique<RigidBodyComponent>();
    rb->mass = 3.0f;
    rb->shape = ShapeKind::Box;
    rb->halfExtents = Vec3(0.17f, 0.36f, 0.17f);
    out.boxCenter = Vec3(0, 0.36f, 0);
    out.rigidBody = std::move(rb);
    out.isStatic = false;
    out.cullDistance = 200.0f;
    (void)j;
    (void)mat;
}

void bollardPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    MeshBuilder b("bollard");
    b.lathe({{0.1f, 0.0f}, {0.1f, 0.85f}, {0.07f, 0.95f}, {0.0f, 0.97f}}, 12, true);
    out.mesh = b.build();
    out.materials = {M(mat, "rail_black")};
    out.meshKey = keyOf("bollard", j, out.materials[0]);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.1f, 0.48f, 0.1f);
    out.boxCenter = Vec3(0, 0.48f, 0);
}

void planterPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(3, 0.5f, 1.2f));
    MeshBuilder b("planter");
    b.setMaterial(0);
    float t = 0.15f;
    b.box(Vec3(0, size.y * 0.5f, size.z * 0.5f - t * 0.5f), Vec3(size.x, size.y, t));
    b.box(Vec3(0, size.y * 0.5f, -size.z * 0.5f + t * 0.5f), Vec3(size.x, size.y, t));
    b.box(Vec3(size.x * 0.5f - t * 0.5f, size.y * 0.5f, 0), Vec3(t, size.y, size.z - 2 * t));
    b.box(Vec3(-size.x * 0.5f + t * 0.5f, size.y * 0.5f, 0), Vec3(t, size.y, size.z - 2 * t));
    b.setMaterial(1);
    b.box(Vec3(0, size.y * 0.4f, 0), Vec3(size.x - 2 * t, size.y * 0.8f, size.z - 2 * t));
    b.setMaterial(2);
    Rng rng(uint64_t(size.x * 100));
    for (int i = 0; i < int(size.x * 1.5f); ++i) {
        Vec3 c(rng.range(-size.x * 0.4f, size.x * 0.4f), size.y * 0.8f + 0.15f, rng.range(-size.z * 0.25f, size.z * 0.25f));
        b.sphere(c, rng.range(0.25f, 0.4f), 6, 8);
    }
    out.mesh = b.build();
    out.materials = {M(mat, "concrete_smooth"), "dirt", "foliage"};
    out.meshKey = keyOf("planter", j, out.materials[0]);
    for (float s : {-1.0f, 1.0f}) addRail(out, {{-size.x * 0.5f, size.y, s * size.z * 0.5f}, {size.x * 0.5f, size.y, s * size.z * 0.5f}}, RailType::Ledge);
}

void signPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float h = P(j, "height", 2.6f);
    MeshBuilder b("sign");
    b.setMaterial(0);
    b.cylinder(Vec3(0), 0.035f, h, 8, true);
    b.setMaterial(1);
    b.box(Vec3(0, h - 0.3f, 0.05f), Vec3(0.7f, 0.5f, 0.03f));
    out.mesh = b.build();
    out.materials = {"rail_steel", M(mat, "sign_green")};
    out.meshKey = keyOf("sign", j, out.materials[1]);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.04f, h * 0.5f, 0.04f);
    out.boxCenter = Vec3(0, h * 0.5f, 0);
}

void trashCanPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    MeshBuilder b("trash");
    b.lathe({{0.28f, 0.0f}, {0.3f, 0.95f}, {0.31f, 1.0f}, {0.0f, 1.02f}}, 16, true);
    out.mesh = b.build();
    out.materials = {M(mat, "metal_rust")};
    out.meshKey = keyOf("trash_can", j, out.materials[0]);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.3f, 0.5f, 0.3f);
    out.boxCenter = Vec3(0, 0.5f, 0);
}

void picnicTablePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    MeshBuilder b("picnic");
    b.setMaterial(0);
    b.box(Vec3(0, 0.75f, 0), Vec3(1.8f, 0.05f, 0.8f));
    for (float s : {-1.0f, 1.0f}) b.box(Vec3(0, 0.45f, s * 0.7f), Vec3(1.8f, 0.05f, 0.3f));
    b.setMaterial(1);
    for (float x : {-0.7f, 0.7f}) {
        b.box(Vec3(x, 0.37f, 0), Vec3(0.06f, 0.74f, 0.06f));
        b.box(Vec3(x, 0.42f, 0), Vec3(0.06f, 0.05f, 1.7f));
    }
    out.mesh = b.build();
    out.materials = {M(mat, "wood_deck"), "rail_black"};
    out.meshKey = keyOf("picnic_table", j, out.materials[0]);
    addRail(out, {{-0.9f, 0.775f, 0.4f}, {0.9f, 0.775f, 0.4f}}, RailType::Ledge);
    addRail(out, {{-0.9f, 0.775f, -0.4f}, {0.9f, 0.775f, -0.4f}}, RailType::Ledge);
}

void bikeRackPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 2.4f);
    int stands = Pi(j, "stands", 0);
    MeshBuilder b("bikerack");
    if (stands <= 0) {
        // one long hoop: a grindable bar
        b.tube({{-l * 0.5f, 0.02f, 0}, {-l * 0.5f, 0.8f, 0}, {l * 0.5f, 0.8f, 0}, {l * 0.5f, 0.02f, 0}}, 0.03f, 10, true);
        addRail(out, {{-l * 0.5f, 0.83f, 0}, {l * 0.5f, 0.83f, 0}}, RailType::Round, 0.03f);
    } else {
        // row of "Sheffield" stands along X: inverted U hoops across Z with bent corners + base plates
        float hw = 0.36f, h = 0.8f, bend = 0.16f;
        for (int i = 0; i < stands; ++i) {
            float x = stands == 1 ? 0.0f : -l * 0.5f + l * float(i) / float(stands - 1);
            std::vector<Vec3> path = {{x, 0.0f, -hw}, {x, h - bend, -hw}};
            for (int k = 1; k < 6; ++k) {
                float a = float(k) / 6.0f * kHalfPi;
                path.push_back({x, h - bend + std::sin(a) * bend, -hw + bend - std::cos(a) * bend});
            }
            path.push_back({x, h, -hw + bend});
            path.push_back({x, h, hw - bend});
            for (int k = 1; k < 6; ++k) {
                float a = float(k) / 6.0f * kHalfPi;
                path.push_back({x, h - bend + std::cos(a) * bend, hw - bend + std::sin(a) * bend});
            }
            path.push_back({x, h - bend, hw});
            path.push_back({x, 0.0f, hw});
            b.tube(path, 0.024f, 10, false);
            for (float z : {-hw, hw}) b.cylinder(Vec3(x, 0, z), 0.05f, 0.01f, 12, true);
        }
    }
    out.mesh = b.build();
    out.materials = {M(mat, "rail_steel")};
    out.meshKey = keyOf("bike_rack", j, out.materials[0]);
    out.surface = "metal";
}

// --- helpers (editor only) --------------------------------------------------------
void spawnPrefab(const Json& j, const std::string&, PrefabBuild& out) {
    MeshBuilder b("spawn");
    b.cylinder(Vec3(0), 0.4f, 0.05f, 16, true);
    b.box(Vec3(0, 0.1f, -0.5f), Vec3(0.1f, 0.1f, 0.6f));
    out.mesh = b.build();
    out.materials = {"editor_helper"};
    out.meshKey = "spawn_helper";
    out.collider = ColliderComponent::Kind::None;
    out.editorHelper = true;
    auto s = std::make_unique<SpawnComponent>();
    s->label = Ps(j, "label", "Spawn");
    s->isDefault = Pb(j, "default", false);
    out.spawn = std::move(s);
}

void zonePrefab(const Json& j, const std::string&, PrefabBuild& out) {
    Vec3 he = jvec3(j, "halfExtents", Vec3(10, 5, 10));
    MeshBuilder b("zone");
    b.box(Vec3(0, he.y, 0), Vec3(0.3f, 0.3f, 0.3f));
    out.mesh = b.build();
    out.materials = {"editor_helper"};
    out.meshKey = "zone_helper";
    out.collider = ColliderComponent::Kind::None;
    out.editorHelper = true;
    auto z = std::make_unique<ZoneComponent>();
    z->area = Ps(j, "area", "Area");
    z->halfExtents = he;
    z->kind = Ps(j, "kind", "area");
    z->score = Pi(j, "score", 0);
    out.zone = std::move(z);
}

void lightPrefab(const Json& j, const std::string&, PrefabBuild& out) {
    MeshBuilder b("light");
    b.sphere(Vec3(0), 0.12f, 6, 8);
    out.mesh = b.build();
    out.materials = {"editor_helper"};
    out.meshKey = "light_helper";
    out.collider = ColliderComponent::Kind::None;
    out.editorHelper = true;
    LightComponent lc;
    lc.light.type = Ps(j, "type", "point") == "spot" ? LightProxy::Type::Spot : LightProxy::Type::Point;
    lc.light.color = jcolor(j, "color", Vec3(1, 0.85f, 0.65f));
    lc.light.intensity = P(j, "intensity", 40.0f);
    lc.light.radius = P(j, "radius", 12.0f);
    lc.nightOnly = Pb(j, "nightOnly", true);
    out.lights.push_back(lc);
}

void audioPrefab(const Json& j, const std::string&, PrefabBuild& out) {
    MeshBuilder b("audio");
    b.box(Vec3(0), Vec3(0.2f));
    out.mesh = b.build();
    out.materials = {"editor_helper"};
    out.meshKey = "audio_helper";
    out.collider = ColliderComponent::Kind::None;
    out.editorHelper = true;
    auto a = std::make_unique<AudioSourceComponent>();
    a->sound = Ps(j, "sound", "ambience_city");
    a->volume = P(j, "volume", 0.6f);
    a->minDistance = P(j, "minDistance", 5.0f);
    a->maxDistance = P(j, "maxDistance", 60.0f);
    out.audio = std::move(a);
}

}  // namespace

void registerBuiltinPrefabs() {
    PrefabRegistry& r = prefabs();
    r.add("ground", "Ground", groundPrefab, {{"size", {40, 0.2, 40}}, {"divisions", 4}});
    r.add("slab", "Ground", slabPrefab, {{"size", {4, 0.4, 4}}, {"grindEdges", false}});
    r.add("road", "Ground", roadPrefab, {{"length", 40}, {"width", 9}, {"sidewalk", 3.0}, {"curbHeight", 0.14}, {"centerLine", true}, {"asphalt", true}});
    r.add("wall", "Structure", wallPrefab, {{"size", {6, 2.5, 0.3}}, {"grindTop", false}});
    r.add("building", "Structure", buildingPrefab,
          {{"size", {16, 12, 12}}, {"floorHeight", 3.3}, {"windowSpacing", 3.0}, {"shops", true}, {"parapet", true}, {"litWindows", true}, {"roofMaterial", "roof_grey"}});
    r.add("quarter_pipe", "Skatepark", quarterPipePrefab, {{"height", 1.8}, {"radius", 2.4}, {"width", 6.0}, {"deck", 1.2}, {"coping", true}});
    r.add("half_pipe", "Skatepark", halfPipePrefab, {{"height", 2.4}, {"radius", 2.8}, {"width", 8.0}, {"flat", 4.0}, {"deck", 1.5}});
    r.add("spine", "Skatepark", spinePrefab, {{"height", 1.6}, {"radius", 2.0}, {"width", 6.0}});
    r.add("kicker", "Skatepark", kickerPrefab, {{"length", 2.2}, {"height", 0.8}, {"width", 2.0}, {"metalLip", true}});
    r.add("bank", "Skatepark", bankPrefab, {{"length", 3.0}, {"height", 1.2}, {"width", 4.0}, {"deck", 1.0}, {"grindEdge", true}});
    r.add("funbox", "Skatepark", funboxPrefab, {{"rampLength", 1.8}, {"topLength", 2.5}, {"height", 0.6}, {"width", 3.0}, {"rail", true}, {"railHeight", 0.35}});
    r.add("manual_pad", "Skatepark", manualPadPrefab, {{"length", 5.0}, {"width", 1.8}, {"height", 0.25}});
    r.add("ledge", "Street", ledgePrefab, {{"length", 6.0}, {"width", 0.6}, {"height", 0.45}, {"bothEdges", true}});
    r.add("stairs", "Street", stairsPrefab,
          {{"steps", 6}, {"rise", 0.17}, {"run", 0.33}, {"width", 4.0}, {"landingTop", 2.5}, {"handrail", true}, {"handrailBoth", false}, {"hubba", false}, {"railHeight", 0.9}});
    r.add("rail", "Street", railPrefab, {{"length", 5.0}, {"height", 0.45}, {"drop", 0.0}, {"type", "round"}, {"segments", 1}});
    r.add("bowl", "Skatepark", bowlPrefab, {{"radius", 7.0}, {"depth", 2.2}, {"transition", 2.6}, {"deck", 2.0}, {"segments", 48}});
    r.add("pool", "Skatepark", poolPrefab,
          {{"length", 12.0}, {"width", 8.0}, {"cornerRadius", 3.0}, {"depth", 1.8}, {"transition", 2.4}, {"deck", 1.5}, {"coping", "steel"}, {"tile", false}});
    r.add("mega_ramp", "Mega Park", megaRampPrefab,
          {{"towerHeight", 11.0}, {"rollInLength", 20.0}, {"width", 7.0}, {"kickerHeight", 3.2}, {"kickerLength", 7.0}, {"gap", 14.0},
           {"landingHeight", 5.5}, {"landingLength", 20.0}, {"quarterHeight", 6.0}});
    r.add("ditch", "Ditch", ditchPrefab, {{"length", 60.0}, {"bottom", 4.0}, {"bankWidth", 3.0}, {"depth", 2.0}, {"top", 2.0}});
    r.add("container", "Industrial", containerPrefab, {{"long", false}});
    r.add("loading_dock", "Industrial", loadingDockPrefab, {{"length", 16.0}, {"depth", 4.0}, {"height", 1.2}});
    r.add("pipe", "Industrial", pipePrefab, {{"radius", 0.5}, {"length", 10.0}, {"height", 0.8}});
    r.add("bench", "Props", benchPrefab, {{"length", 2.0}});
    r.add("street_lamp", "Props", streetLampPrefab, {{"height", 6.5}});
    r.add("fence", "Props", fencePrefab, {{"length", 6.0}, {"height", 1.2}});
    r.add("barrier", "Props", barrierPrefab, {{"length", 3.0}});
    r.add("cone", "Props", conePrefab, Json::object());
    r.add("bollard", "Props", bollardPrefab, Json::object());
    r.add("planter", "Props", planterPrefab, {{"size", {3, 0.5, 1.2}}});
    r.add("sign", "Props", signPrefab, {{"height", 2.6}});
    r.add("trash_can", "Props", trashCanPrefab, Json::object());
    r.add("picnic_table", "Props", picnicTablePrefab, Json::object());
    r.add("bike_rack", "Props", bikeRackPrefab, {{"length", 2.4}, {"stands", 0}});
    r.add("model", "Props", modelPrefab, {{"model", "assets/models/props/fire_hydrant/fire_hydrant.gltf"}, {"include", ""}, {"exclude", "_aged"},
                                          {"scale", 1.0}, {"collider", "box"}, {"recenter", true}});
    registerStreetPrefabs(r);
    registerNaturePrefabs(r);
    r.add("spawn", "Gameplay", spawnPrefab, {{"label", "Spawn"}, {"default", false}});
    r.add("zone", "Gameplay", zonePrefab, {{"halfExtents", {10, 5, 10}}, {"area", "Area"}, {"kind", "area"}, {"score", 0}});
    r.add("light", "Gameplay", lightPrefab, {{"type", "point"}, {"intensity", 40.0}, {"radius", 12.0}, {"nightOnly", true}});
    r.add("audio", "Gameplay", audioPrefab, {{"sound", "ambience_city"}, {"volume", 0.6}, {"minDistance", 5.0}, {"maxDistance", 60.0}});
}

}  // namespace sw
