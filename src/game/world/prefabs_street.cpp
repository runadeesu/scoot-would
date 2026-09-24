// scoot would - street detail kit: parked cars, decals, traffic signs, parking meters, utility poles
// and cables, awnings, sidewalks with curbs, crosswalks, parking bays and shopfront buildings with
// recessed windows (interior mapped glass), cornices, sign bands and roof equipment.
// Conventions as in prefabs.cpp: origin at the footprint centre on the ground, +X along the object.
#include "core/log.h"
#include "game/world/prefab_kit.h"

#include <cmath>

namespace sw::kit {

namespace {

// smooth tube through cross sections (superellipse rings); rings run along `axis`
struct Ring {
    Vec3 c;
    float halfW, halfH;  // along side / up
    float sq = 2.0f;
};

Vec2 superEllipse(float th, float a, float b, float sq) {
    float cs = std::cos(th), sn = std::sin(th), e = 2.0f / sq;
    return {signf(cs) * std::pow(std::fabs(cs), e) * a, signf(sn) * std::pow(std::fabs(sn), e) * b};
}

// loft along +X (car bodies): side = Z, up = Y
void loftX(MeshBuilder& b, const std::vector<Ring>& rings, int seg, bool capFront, bool capBack) {
    size_t n = rings.size();
    std::vector<std::vector<Vec3>> P(n, std::vector<Vec3>(size_t(seg)));
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < seg; ++k) {
            float th = float(k) / float(seg) * kTwoPi;
            Vec2 e = superEllipse(th, rings[i].halfW, rings[i].halfH, rings[i].sq);
            P[i][size_t(k)] = rings[i].c + Vec3(0, e.y, e.x);
        }
    auto normalAt = [&](size_t i, int k) {
        const Vec3& a = P[i][size_t((k + 1) % seg)];
        const Vec3& c = P[i][size_t((k + seg - 1) % seg)];
        const Vec3& f = P[std::min(i + 1, n - 1)][size_t(k)];
        const Vec3& g = P[i == 0 ? 0 : i - 1][size_t(k)];
        Vec3 nn = cross(a - c, f - g);
        Vec3 out = P[i][size_t(k)] - rings[i].c;
        if (dot(nn, out) < 0) nn = -nn;
        return nn.normalized();
    };
    std::vector<std::vector<uint32_t>> id(n, std::vector<uint32_t>(size_t(seg) + 1));
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k <= seg; ++k) {
            int kk = k % seg;
            id[i][size_t(k)] = b.addVertex(P[i][size_t(kk)], normalAt(i, kk), Vec2(float(k) / float(seg), P[i][size_t(kk)].x));
        }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 0; k < seg; ++k) {
            uint32_t a = id[i][size_t(k)], bb = id[i][size_t(k) + 1], c = id[i + 1][size_t(k) + 1], d = id[i + 1][size_t(k)];
            // winding: outward faces
            Vec3 fn = cross(P[i + 1][size_t(k)] - P[i][size_t(k)], P[i][size_t((k + 1) % seg)] - P[i][size_t(k)]);
            Vec3 out = P[i][size_t(k)] - rings[i].c;
            if (dot(fn, out) > 0) b.addQuad(a, d, c, bb);
            else b.addQuad(a, bb, c, d);
        }
    auto cap = [&](size_t i, float dir) {
        uint32_t c = b.addVertex(rings[i].c, Vec3(dir, 0, 0), Vec2(0.5f, 0.5f));
        std::vector<uint32_t> ring;
        for (int k = 0; k < seg; ++k) ring.push_back(b.addVertex(P[i][size_t(k)], Vec3(dir, 0, 0), Vec2(0, 0)));
        for (int k = 0; k < seg; ++k) {
            uint32_t a = ring[size_t(k)], bb = ring[size_t((k + 1) % seg)];
            Vec3 fn = cross(P[i][size_t(k)] - rings[i].c, P[i][size_t((k + 1) % seg)] - rings[i].c);
            if (fn.x * dir > 0) b.addTriangle(c, a, bb);
            else b.addTriangle(c, bb, a);
        }
    };
    if (capFront) cap(n - 1, 1.0f);
    if (capBack) cap(0, -1.0f);
}

void quadUV(MeshBuilder& b, Vec3 a, Vec3 bb, Vec3 c, Vec3 d, Vec2 uv0 = Vec2(0, 0), Vec2 uv1 = Vec2(1, 1)) {
    Vec3 n = cross(bb - a, d - a).normalized();
    uint32_t i0 = b.addVertex(a, n, Vec2(uv0.x, uv1.y)), i1 = b.addVertex(bb, n, Vec2(uv1.x, uv1.y));
    uint32_t i2 = b.addVertex(c, n, Vec2(uv1.x, uv0.y)), i3 = b.addVertex(d, n, Vec2(uv0.x, uv0.y));
    b.addQuad(i0, i1, i2, i3);
}

// ---------------------------------------------------------------------------------------------
// parked car: rounded body lofted through cross sections with wheel arches, glasshouse, roof,
// pillars, wheels, lights, plates and mirrors. Front towards +X.
void carPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    std::string type = Ps(j, "type", "sedan");
    std::string paint = Ps(j, "paint", "red");
    MeshBuilder b("car");
    std::vector<Ring> body, glass, roof;
    float wheelX = 1.36f, wheelZ = 0.76f, wr = 0.33f;
    auto R = [](float x, float y0, float y1, float hw, float sq) { return Ring{Vec3(x, (y0 + y1) * 0.5f, 0), hw, (y1 - y0) * 0.5f, sq}; };
    auto archRings = [&](std::vector<Ring>& out, float wx, float topAt) {
        // lower edge follows the wheel arch
        const float offs[] = {0.44f, 0.30f, 0.16f, 0.0f, -0.16f, -0.30f, -0.44f};
        const float lift[] = {0.0f, 0.25f, 0.36f, 0.40f, 0.36f, 0.25f, 0.0f};
        for (int i = 0; i < 7; ++i) {
            float x = wx + offs[i];
            float y0 = 0.30f + lift[i];
            out.push_back(R(x, y0, topAt, 0.90f, 4.0f));
        }
    };
    if (type == "van") {
        wheelX = 1.5f;
        wr = 0.34f;
        body = {R(2.45f, 0.42f, 0.75f, 0.84f, 3.0f), R(2.36f, 0.32f, 0.92f, 0.92f, 3.5f), R(2.10f, 0.30f, 1.02f, 0.95f, 4.0f)};
        std::vector<Ring> a1;
        archRings(a1, wheelX, 1.06f);
        body.insert(body.end(), a1.begin(), a1.end());
        body.push_back(R(0.0f, 0.30f, 1.08f, 0.95f, 4.0f));
        std::vector<Ring> a2;
        archRings(a2, -wheelX, 1.08f);
        body.insert(body.end(), a2.begin(), a2.end());
        body.push_back(R(-2.25f, 0.30f, 1.08f, 0.95f, 4.0f));
        body.push_back(R(-2.42f, 0.40f, 1.02f, 0.9f, 3.5f));
        glass = {R(1.95f, 1.05f, 1.08f, 0.9f, 3.5f), R(1.55f, 1.05f, 1.62f, 0.86f, 3.5f), R(1.2f, 1.06f, 1.95f, 0.84f, 4.0f),
                 R(-2.3f, 1.06f, 1.96f, 0.84f, 4.0f), R(-2.4f, 1.05f, 1.85f, 0.86f, 4.0f)};
        roof = {R(1.18f, 1.9f, 1.99f, 0.83f, 4.0f), R(-2.32f, 1.9f, 1.99f, 0.83f, 4.0f)};
    } else {
        bool hatch = type == "hatch";
        float rear = hatch ? -1.95f : -2.32f;
        body = {R(2.32f, 0.40f, 0.66f, 0.78f, 3.0f), R(2.24f, 0.30f, 0.76f, 0.86f, 3.5f), R(2.00f, 0.30f, 0.82f, 0.89f, 4.0f)};
        std::vector<Ring> a1;
        archRings(a1, wheelX, 0.9f);
        body.insert(body.end(), a1.begin(), a1.end());
        body.push_back(R(0.80f, 0.30f, 0.94f, 0.90f, 4.0f));
        std::vector<Ring> a2;
        archRings(a2, -(hatch ? 1.2f : 1.32f), 0.98f);
        body.insert(body.end(), a2.begin(), a2.end());
        body.push_back(R(rear + 0.37f, 0.30f, 1.0f, 0.89f, 4.0f));
        body.push_back(R(rear + 0.1f, 0.32f, 0.96f, 0.86f, 3.5f));
        body.push_back(R(rear, 0.42f, 0.86f, 0.80f, 3.0f));
        if (hatch) {
            glass = {R(0.82f, 0.93f, 0.95f, 0.84f, 3.0f), R(0.55f, 0.93f, 1.16f, 0.8f, 3.0f), R(0.2f, 0.94f, 1.38f, 0.74f, 3.0f),
                     R(-1.2f, 0.96f, 1.40f, 0.73f, 3.0f), R(-1.72f, 0.98f, 1.30f, 0.76f, 3.0f), R(-1.9f, 0.99f, 1.02f, 0.8f, 3.0f)};
            roof = {R(0.22f, 1.36f, 1.40f, 0.72f, 3.5f), R(0.1f, 1.37f, 1.445f, 0.715f, 3.5f), R(-1.3f, 1.37f, 1.43f, 0.715f, 3.5f),
                    R(-1.5f, 1.35f, 1.39f, 0.72f, 3.5f)};
        } else {
            glass = {R(0.82f, 0.93f, 0.95f, 0.84f, 3.0f), R(0.55f, 0.93f, 1.16f, 0.8f, 3.0f), R(0.2f, 0.94f, 1.38f, 0.74f, 3.0f),
                     R(-0.2f, 0.95f, 1.43f, 0.72f, 3.0f), R(-0.8f, 0.96f, 1.42f, 0.72f, 3.0f), R(-1.2f, 0.97f, 1.30f, 0.76f, 3.0f),
                     R(-1.55f, 0.98f, 1.02f, 0.82f, 3.0f)};
            roof = {R(0.22f, 1.36f, 1.40f, 0.72f, 3.5f), R(0.1f, 1.37f, 1.445f, 0.715f, 3.5f), R(-0.8f, 1.37f, 1.445f, 0.715f, 3.5f),
                    R(-0.92f, 1.35f, 1.42f, 0.72f, 3.5f)};
        }
    }
    // loft needs rings ordered back to front along +X
    auto sortRings = [](std::vector<Ring>& r) { std::sort(r.begin(), r.end(), [](const Ring& a, const Ring& c) { return a.c.x < c.c.x; }); };
    sortRings(body);
    sortRings(glass);
    sortRings(roof);
    b.setMaterial(0);
    loftX(b, body, 28, true, true);
    loftX(b, roof, 24, true, true);
    b.setMaterial(1);
    loftX(b, glass, 24, true, true);
    // pillars (paint)
    b.setMaterial(0);
    float gFront = glass.back().c.x, gBack = glass.front().c.x;
    float roofY = roof.back().c.y + roof.back().halfH;
    for (float s : {-1.0f, 1.0f}) {
        b.tube({Vec3(gFront - 0.02f, 0.95f, s * 0.82f), Vec3(roof.back().c.x, roofY - 0.04f, s * 0.71f)}, 0.045f, 8, true);
        b.tube({Vec3(gBack + 0.03f, glass.front().c.y, s * 0.8f), Vec3(roof.front().c.x, roofY - 0.05f, s * 0.71f)}, 0.06f, 8, true);
        float bx = (roof.back().c.x + roof.front().c.x) * 0.5f + 0.1f;
        b.box(Vec3(bx, (0.95f + roofY) * 0.5f, s * 0.73f), Vec3(0.09f, roofY - 0.95f, 0.06f));
        // mirrors
        b.box(Vec3(gFront - 0.12f, 1.02f, s * 0.96f), Vec3(0.12f, 0.1f, 0.14f));
        // door handles
        b.setMaterial(4);
        b.box(Vec3(0.2f, 0.86f, s * 0.905f), Vec3(0.14f, 0.025f, 0.02f));
        b.box(Vec3(-0.75f, 0.88f, s * 0.905f), Vec3(0.14f, 0.025f, 0.02f));
        // side trim strip
        b.box(Vec3(0.0f, 0.42f, s * 0.9f), Vec3(3.2f, 0.06f, 0.02f));
        b.setMaterial(0);
    }
    // lights, grille, plates, bumper trim
    float front = body.back().c.x, back = body.front().c.x;
    b.setMaterial(5);
    for (float s : {-1.0f, 1.0f}) b.box(Vec3(front - 0.06f, 0.72f, s * 0.62f), Vec3(0.08f, 0.1f, 0.28f));
    b.setMaterial(6);
    for (float s : {-1.0f, 1.0f}) b.box(Vec3(back + 0.05f, 0.82f, s * 0.66f), Vec3(0.07f, 0.12f, 0.26f));
    b.setMaterial(4);
    b.box(Vec3(front + 0.0f, 0.56f, 0), Vec3(0.06f, 0.12f, 0.72f));
    b.box(Vec3(front - 0.02f, 0.38f, 0), Vec3(0.08f, 0.08f, 1.5f));
    b.box(Vec3(back + 0.02f, 0.40f, 0), Vec3(0.08f, 0.08f, 1.5f));
    b.setMaterial(7);
    b.box(Vec3(front + 0.02f, 0.46f, 0), Vec3(0.02f, 0.11f, 0.52f));
    b.box(Vec3(back - 0.01f, 0.62f, 0), Vec3(0.02f, 0.11f, 0.52f));
    // wheels: tyre (lathe around the axle) + rim with spokes
    auto wheel = [&](float x, float z) {
        float side = signf(z);
        Mat4 m = Mat4::trs(Vec3(x, wr, z), Quat::angleAxis(kHalfPi, Vec3(1, 0, 0)), Vec3(1));
        b.setTransform(m);
        b.setMaterial(2);
        std::vector<Vec2> tyre = {{wr * 0.72f, -0.105f}, {wr * 0.93f, -0.108f}, {wr, -0.085f}, {wr, 0.085f}, {wr * 0.93f, 0.108f}, {wr * 0.72f, 0.105f}};
        b.lathe(tyre, 24, true);
        b.setMaterial(3);
        float face = 0.095f * side;  // rim face towards the outside (local +y maps to world +z)
        b.cylinder(Vec3(0, face - 0.01f, 0), wr * 0.72f, 0.02f, 20, true);
        b.cylinder(Vec3(0, face - 0.03f, 0), wr * 0.18f, 0.04f, 12, true);
        for (int k = 0; k < 5; ++k) {
            float a = float(k) / 5.0f * kTwoPi;
            Vec3 d(std::cos(a), 0, std::sin(a));
            b.box(Vec3(0, face - 0.005f, 0) + d * (wr * 0.42f), Vec3(std::fabs(d.x) * wr * 0.5f + 0.04f, 0.025f, std::fabs(d.z) * wr * 0.5f + 0.04f));
        }
        b.resetTransform();
    };
    for (float sx : {1.0f, -1.0f})
        for (float sz : {1.0f, -1.0f}) wheel(sx > 0 ? wheelX : -(type == "hatch" ? 1.2f : (type == "van" ? wheelX : 1.32f)), sz * wheelZ);
    out.mesh = b.build();
    out.materials = {M(mat, "car_paint_" + paint), "car_glass", "car_tire", "car_rim", "car_trim", "car_light_front", "car_light_rear", "car_plate"};
    out.meshKey = keyOf("car", j, mat);
    out.collider = ColliderComponent::Kind::Box;
    AABB bb = out.mesh.bounds;
    out.boxHalfExtents = (bb.max - bb.min) * 0.5f;
    out.boxCenter = (bb.max + bb.min) * 0.5f;
    out.surface = "metal";
}

// ---------------------------------------------------------------------------------------------
// decal: textured, alpha blended quad laid 6 mm above the ground (or in front of a wall, facing +Z)
void decalPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec2 size = jvec2(j, "size", Vec2(2, 2));
    bool wall = Ps(j, "orient", "ground") == "wall";
    float off = P(j, "offset", 0.006f);
    MeshBuilder b("decal");
    float hx = size.x * 0.5f, hy = size.y * 0.5f;
    if (wall)
        quadUV(b, Vec3(-hx, 0, off), Vec3(hx, 0, off), Vec3(hx, size.y, off), Vec3(-hx, size.y, off));
    else
        quadUV(b, Vec3(-hx, off, hy), Vec3(hx, off, hy), Vec3(hx, off, -hy), Vec3(-hx, off, -hy));
    out.mesh = b.build();
    out.materials = {M(mat, "decal_crack_1")};
    out.meshKey = keyOf("decal", j, mat);
    out.collider = ColliderComponent::Kind::None;
    out.castShadows = false;
}

// traffic sign on a galvanized post
void trafficSignPrefab(const Json& j, const std::string&, PrefabBuild& out) {
    std::string sign = Ps(j, "sign", "stop");
    float h = P(j, "height", 2.6f);
    Vec2 plate(0.75f, 0.75f);
    if (sign == "oneway" || sign == "street_1" || sign == "street_2") plate = Vec2(0.9f, 0.22f);
    if (sign == "speed") plate = Vec2(0.6f, 0.6f);
    MeshBuilder b("sign");
    b.setMaterial(0);
    b.cylinder(Vec3(0), 0.03f, h + (plate.y > 0.5f ? 0.0f : 0.25f), 10, true);
    float cy = h - plate.y * 0.5f + 0.1f;
    b.setMaterial(1);
    quadUV(b, Vec3(-plate.x * 0.5f, cy - plate.y * 0.5f, 0.04f), Vec3(plate.x * 0.5f, cy - plate.y * 0.5f, 0.04f), Vec3(plate.x * 0.5f, cy + plate.y * 0.5f, 0.04f),
           Vec3(-plate.x * 0.5f, cy + plate.y * 0.5f, 0.04f));
    b.setMaterial(2);
    quadUV(b, Vec3(plate.x * 0.5f, cy - plate.y * 0.5f, 0.032f), Vec3(-plate.x * 0.5f, cy - plate.y * 0.5f, 0.032f), Vec3(-plate.x * 0.5f, cy + plate.y * 0.5f, 0.032f),
           Vec3(plate.x * 0.5f, cy + plate.y * 0.5f, 0.032f));
    b.setMaterial(0);
    b.box(Vec3(0, cy, 0.02f), Vec3(0.05f, 0.08f, 0.03f));
    if (sign == "street_1") {
        // second street name plate crossing the first one
        b.setMaterial(3);
        float y2 = cy + plate.y + 0.02f;
        quadUV(b, Vec3(0.04f, y2 - plate.y * 0.5f, plate.x * 0.5f), Vec3(0.04f, y2 - plate.y * 0.5f, -plate.x * 0.5f), Vec3(0.04f, y2 + plate.y * 0.5f, -plate.x * 0.5f),
               Vec3(0.04f, y2 + plate.y * 0.5f, plate.x * 0.5f));
        b.setMaterial(2);
        quadUV(b, Vec3(-0.0f, y2 - plate.y * 0.5f, -plate.x * 0.5f), Vec3(-0.0f, y2 - plate.y * 0.5f, plate.x * 0.5f), Vec3(-0.0f, y2 + plate.y * 0.5f, plate.x * 0.5f),
               Vec3(-0.0f, y2 + plate.y * 0.5f, -plate.x * 0.5f));
    }
    out.mesh = b.build();
    out.materials = {"metal_galvanized", "sign_" + sign, "metal_galvanized", "sign_street_2"};
    out.meshKey = keyOf("traffic_sign", j, "");
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.05f, h * 0.5f, 0.05f);
    out.boxCenter = Vec3(0, h * 0.5f, 0);
    out.surface = "metal";
}

void parkingMeterPrefab(const Json&, const std::string&, PrefabBuild& out) {
    MeshBuilder b("meter");
    b.setMaterial(0);
    b.cylinder(Vec3(0), 0.035f, 1.1f, 10, true);
    b.cylinder(Vec3(0), 0.09f, 0.02f, 12, true);
    b.setMaterial(1);
    b.box(Vec3(0, 1.28f, 0), Vec3(0.22f, 0.36f, 0.16f));
    b.lathe({{0.0f, 1.46f}, {0.11f, 1.46f}, {0.1f, 1.52f}, {0.0f, 1.54f}}, 16, true);
    b.setMaterial(2);
    quadUV(b, Vec3(-0.07f, 1.26f, 0.081f), Vec3(0.07f, 1.26f, 0.081f), Vec3(0.07f, 1.38f, 0.081f), Vec3(-0.07f, 1.38f, 0.081f));
    out.mesh = b.build();
    out.materials = {"metal_galvanized", "painted_dark_grey", "car_glass"};
    out.meshKey = "parking_meter";
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.1f, 0.78f, 0.1f);
    out.boxCenter = Vec3(0, 0.78f, 0);
    out.surface = "metal";
}

// timber utility pole with a cross arm and insulators; cable anchors at +-1.1 m on the arm
void utilityPolePrefab(const Json& j, const std::string&, PrefabBuild& out) {
    float h = P(j, "height", 9.0f);
    MeshBuilder b("pole");
    b.setMaterial(0);
    b.lathe({{0.0f, 0.0f}, {0.15f, 0.0f}, {0.12f, h}, {0.0f, h + 0.02f}}, 14, true);
    b.setMaterial(1);
    b.box(Vec3(0, h - 0.5f, 0), Vec3(2.4f, 0.12f, 0.12f));
    b.box(Vec3(0.35f, h - 0.8f, 0), Vec3(0.06f, 0.6f, 0.06f));
    b.setMaterial(2);
    for (float x : {-1.1f, -0.4f, 0.4f, 1.1f}) b.cylinder(Vec3(x, h - 0.44f, 0), 0.045f, 0.14f, 10, true);
    b.setMaterial(1);
    b.box(Vec3(0, h - 2.0f, 0.2f), Vec3(0.5f, 0.7f, 0.4f));  // transformer box
    out.mesh = b.build();
    out.materials = {"wood_pole", "metal_galvanized", "insulator"};
    out.meshKey = keyOf("utility_pole", j, "");
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(0.16f, h * 0.5f, 0.16f);
    out.boxCenter = Vec3(0, h * 0.5f, 0);
}

// sagging cable along +X between (-length/2, 0, 0) and (length/2, 0, 0)
void cablePrefab(const Json& j, const std::string&, PrefabBuild& out) {
    float l = P(j, "length", 30.0f), sag = P(j, "sag", 0.6f), r = P(j, "radius", 0.012f);
    std::vector<Vec3> path;
    for (int i = 0; i <= 24; ++i) {
        float t = float(i) / 24.0f;
        path.push_back(Vec3(-l * 0.5f + l * t, -sag * 4.0f * t * (1.0f - t), 0));
    }
    MeshBuilder b("cable");
    b.tube(path, r, 5, false);
    out.mesh = b.build();
    out.materials = {"cable_black"};
    out.meshKey = keyOf("cable", j, "");
    out.collider = ColliderComponent::Kind::None;
}

// canvas awning over a shop window (front edge towards +Z)
void awningPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float w = P(j, "width", 4.0f), d = P(j, "depth", 1.2f), drop = P(j, "drop", 0.5f), valance = P(j, "valance", 0.25f);
    MeshBuilder b("awning");
    b.setMaterial(0);
    int n = 6;
    for (int i = 0; i < n; ++i) {
        float t0 = float(i) / float(n), t1 = float(i + 1) / float(n);
        auto P3 = [&](float x, float t) { return Vec3(x, -drop * t - std::sin(t * kPi) * 0.04f, d * t); };
        quadUV(b, P3(-w * 0.5f, t1), P3(w * 0.5f, t1), P3(w * 0.5f, t0), P3(-w * 0.5f, t0), Vec2(0, t0), Vec2(w, t1));
    }
    quadUV(b, Vec3(-w * 0.5f, -drop - valance, d), Vec3(w * 0.5f, -drop - valance, d), Vec3(w * 0.5f, -drop, d), Vec3(-w * 0.5f, -drop, d), Vec2(0, 0), Vec2(w, 0.25f));
    b.setMaterial(1);
    for (float s : {-1.0f, 1.0f}) b.tube({Vec3(s * w * 0.5f, 0, 0), Vec3(s * w * 0.5f, -drop, d)}, 0.015f, 6, true);
    b.tube({Vec3(-w * 0.5f, -drop, d), Vec3(w * 0.5f, -drop, d)}, 0.015f, 6, true);
    out.mesh = b.build();
    out.materials = {M(mat, "awning_green"), "metal_galvanized"};
    out.meshKey = keyOf("awning", j, mat);
    out.collider = ColliderComponent::Kind::None;
}

// sidewalk slab with a granite curb along the +Z edge (grindable), optional curb on -Z
void sidewalkPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float l = P(j, "length", 20.0f), w = P(j, "width", 4.0f), h = P(j, "height", 0.15f);
    bool back = Pb(j, "backCurb", false);
    float cw = 0.3f;
    MeshBuilder b("sidewalk");
    b.setMaterial(0);
    b.box(Vec3(0, h * 0.5f, -cw * 0.5f * (back ? 0.0f : 1.0f)), Vec3(l, h, w - cw * (back ? 2.0f : 1.0f)));
    b.setMaterial(1);
    // curb stone with a small chamfer
    std::vector<Vec2> prof = {{0, 0}, {cw, 0}, {cw, h - 0.02f}, {cw - 0.02f, h}, {0, h}};
    auto curb = [&](float zSide) {
        Mat4 m = Mat4::trs(Vec3(0, 0, zSide > 0 ? w * 0.5f - cw : -w * 0.5f + cw), Quat::angleAxis(zSide > 0 ? kHalfPi : -kHalfPi, Vec3(0, 1, 0)), Vec3(1));
        b.setTransform(m);
        b.extrude(prof, -l * 0.5f, l * 0.5f, true, false);
        b.resetTransform();
        addRail(out, {{-l * 0.5f, h, zSide * (w * 0.5f - 0.01f)}, {l * 0.5f, h, zSide * (w * 0.5f - 0.01f)}}, RailType::Ledge);
    };
    curb(1.0f);
    if (back) curb(-1.0f);
    out.mesh = b.build();
    out.materials = {M(mat, "sidewalk"), "curb_granite"};
    out.surface = "concrete";
}

void crosswalkPrefab(const Json& j, const std::string&, PrefabBuild& out) {
    float l = P(j, "length", 10.0f), w = P(j, "width", 3.0f);
    MeshBuilder b("crosswalk");
    for (float x = -l * 0.5f + 0.3f; x < l * 0.5f - 0.3f; x += 1.0f)
        quadUV(b, Vec3(x, 0.006f, w * 0.5f), Vec3(x + 0.5f, 0.006f, w * 0.5f), Vec3(x + 0.5f, 0.006f, -w * 0.5f), Vec3(x, 0.006f, -w * 0.5f), Vec2(0, 0),
               Vec2(0.5f, w));
    out.mesh = b.build();
    out.materials = {"road_paint_worn"};
    out.meshKey = keyOf("crosswalk", j, "");
    out.collider = ColliderComponent::Kind::None;
    out.castShadows = false;
}

void parkingLinesPrefab(const Json& j, const std::string&, PrefabBuild& out) {
    int n = Pi(j, "bays", 5);
    float wBay = P(j, "bayWidth", 2.6f), len = P(j, "bayLength", 5.2f);
    MeshBuilder b("parking");
    for (int i = 0; i <= n; ++i) {
        float x = -float(n) * wBay * 0.5f + float(i) * wBay;
        quadUV(b, Vec3(x - 0.06f, 0.006f, len * 0.5f), Vec3(x + 0.06f, 0.006f, len * 0.5f), Vec3(x + 0.06f, 0.006f, -len * 0.5f), Vec3(x - 0.06f, 0.006f, -len * 0.5f),
               Vec2(0, 0), Vec2(0.12f, len));
    }
    out.mesh = b.build();
    out.materials = {"road_paint_worn"};
    out.meshKey = keyOf("parking_lines", j, "");
    out.collider = ColliderComponent::Kind::None;
    out.castShadows = false;
}

// ---------------------------------------------------------------------------------------------
// shopfront building: facade towards +Z with a glazed shop on the ground floor (sign band, door,
// optional awning), recessed upper windows with frames and sills, a cornice, roof equipment and a
// downspout. Window glass carries per-window UVs (column + u, floor + v) for interior mapping.
void shopfrontPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(14, 13, 12));
    float groundH = P(j, "groundFloor", 4.2f), floorH = P(j, "floorHeight", 3.3f);
    std::string shop = Ps(j, "shop", "shop_coffee");
    int seed = Pi(j, "seed", 1);
    float hx = size.x * 0.5f, hz = size.z * 0.5f;
    Rng rng(uint64_t(seed) * 7919u + 13u);
    MeshBuilder b("shopfront");
    b.setUvScale(1.0f);
    // side + back walls (plain with stuck-on windows further down), roof
    b.setMaterial(0);
    b.quad(Vec3(hx, 0, hz), Vec3(hx, 0, -hz), Vec3(hx, size.y, -hz), Vec3(hx, size.y, hz));
    b.quad(Vec3(-hx, 0, -hz), Vec3(-hx, 0, hz), Vec3(-hx, size.y, hz), Vec3(-hx, size.y, -hz));
    b.quad(Vec3(hx, 0, -hz), Vec3(-hx, 0, -hz), Vec3(-hx, size.y, -hz), Vec3(hx, size.y, -hz));
    b.setMaterial(4);
    b.quad(Vec3(-hx, size.y, hz), Vec3(hx, size.y, hz), Vec3(hx, size.y, -hz), Vec3(-hx, size.y, -hz));
    // front facade: ground floor storefront + upper floors with recessed windows
    float fz = hz;
    int floors = std::max(1, int((size.y - groundH) / floorH));
    float upperTop = groundH + float(floors) * floorH;
    int cols = std::max(2, int(size.x / 2.8f));
    float colW = size.x / float(cols);
    float winW = std::min(1.35f, colW * 0.56f), winH = floorH * 0.55f, sillY = floorH * 0.3f, recess = 0.16f;
    for (int fl = 0; fl < floors; ++fl) {
        float y0 = groundH + float(fl) * floorH;
        for (int c = 0; c < cols; ++c) {
            float x0 = -hx + float(c) * colW, x1 = x0 + colW;
            float cx = (x0 + x1) * 0.5f;
            float wx0 = cx - winW * 0.5f, wx1 = cx + winW * 0.5f, wy0 = y0 + sillY, wy1 = wy0 + winH;
            b.setMaterial(0);
            // wall pieces around the opening
            b.quad(Vec3(x0, y0, fz), Vec3(x1, y0, fz), Vec3(x1, wy0, fz), Vec3(x0, wy0, fz));
            b.quad(Vec3(x0, wy1, fz), Vec3(x1, wy1, fz), Vec3(x1, y0 + floorH, fz), Vec3(x0, y0 + floorH, fz));
            b.quad(Vec3(x0, wy0, fz), Vec3(wx0, wy0, fz), Vec3(wx0, wy1, fz), Vec3(x0, wy1, fz));
            b.quad(Vec3(wx1, wy0, fz), Vec3(x1, wy0, fz), Vec3(x1, wy1, fz), Vec3(wx1, wy1, fz));
            // reveals
            b.setMaterial(3);
            b.quad(Vec3(wx0, wy1, fz - recess), Vec3(wx1, wy1, fz - recess), Vec3(wx1, wy1, fz), Vec3(wx0, wy1, fz));
            b.quad(Vec3(wx0, wy0, fz), Vec3(wx1, wy0, fz), Vec3(wx1, wy0, fz - recess), Vec3(wx0, wy0, fz - recess));
            b.quad(Vec3(wx0, wy0, fz), Vec3(wx0, wy0, fz - recess), Vec3(wx0, wy1, fz - recess), Vec3(wx0, wy1, fz));
            b.quad(Vec3(wx1, wy0, fz - recess), Vec3(wx1, wy0, fz), Vec3(wx1, wy1, fz), Vec3(wx1, wy1, fz - recess));
            // glass with per-window UV for interior mapping
            b.setMaterial(1);
            float u0 = float(c + fl * 17 + seed * 5), v0 = float(fl);
            quadUV(b, Vec3(wx0, wy0, fz - recess), Vec3(wx1, wy0, fz - recess), Vec3(wx1, wy1, fz - recess), Vec3(wx0, wy1, fz - recess), Vec2(u0, v0 + 1.0f),
                   Vec2(u0 + 1.0f, v0));
            // frame + mullion + sill
            b.setMaterial(2);
            float ft = 0.05f;
            b.box(Vec3(cx, wy0 + ft * 0.5f, fz - recess + 0.03f), Vec3(winW, ft, 0.06f));
            b.box(Vec3(cx, wy1 - ft * 0.5f, fz - recess + 0.03f), Vec3(winW, ft, 0.06f));
            b.box(Vec3(wx0 + ft * 0.5f, (wy0 + wy1) * 0.5f, fz - recess + 0.03f), Vec3(ft, winH, 0.06f));
            b.box(Vec3(wx1 - ft * 0.5f, (wy0 + wy1) * 0.5f, fz - recess + 0.03f), Vec3(ft, winH, 0.06f));
            b.box(Vec3(cx, (wy0 + wy1) * 0.5f, fz - recess + 0.03f), Vec3(0.04f, winH, 0.05f));
            b.setMaterial(3);
            b.box(Vec3(cx, wy0 - 0.03f, fz + 0.04f), Vec3(winW + 0.16f, 0.06f, 0.14f));
            // occasional AC unit hanging under a window
            if (rng.uniform() < 0.18f) {
                b.setMaterial(5);
                b.box(Vec3(cx + 0.1f, wy0 - 0.35f, fz + 0.25f), Vec3(0.7f, 0.42f, 0.4f));
            }
        }
        // floor band
        b.setMaterial(3);
        b.box(Vec3(0, y0 + 0.04f, fz + 0.04f), Vec3(size.x + 0.08f, 0.08f, 0.08f));
    }
    if (upperTop < size.y) {
        b.setMaterial(0);
        b.quad(Vec3(-hx, upperTop, fz), Vec3(hx, upperTop, fz), Vec3(hx, size.y, fz), Vec3(-hx, size.y, fz));
    }
    // storefront: bulkhead, big shop glass (interior mapped, deeper room), door, sign band
    float shopRecess = 0.35f, bulk = 0.5f, signH = 0.7f;
    float glassTop = groundH - signH - 0.15f;
    b.setMaterial(0);
    b.quad(Vec3(-hx, groundH - signH - 0.15f, fz), Vec3(hx, groundH - signH - 0.15f, fz), Vec3(hx, groundH, fz), Vec3(-hx, groundH, fz));
    b.quad(Vec3(-hx, 0, fz), Vec3(-hx + 0.5f, 0, fz), Vec3(-hx + 0.5f, glassTop, fz), Vec3(-hx, glassTop, fz));
    b.quad(Vec3(hx - 0.5f, 0, fz), Vec3(hx, 0, fz), Vec3(hx, glassTop, fz), Vec3(hx - 0.5f, glassTop, fz));
    float sx0 = -hx + 0.5f, sx1 = hx - 0.5f;
    float doorW = 1.1f, doorX = sx0 + (sx1 - sx0) * (0.2f + 0.6f * rng.uniform());
    b.setMaterial(3);
    // recess reveals + ceiling
    b.quad(Vec3(sx0, glassTop, fz - shopRecess), Vec3(sx1, glassTop, fz - shopRecess), Vec3(sx1, glassTop, fz), Vec3(sx0, glassTop, fz));
    b.quad(Vec3(sx0, 0, fz), Vec3(sx0, 0, fz - shopRecess), Vec3(sx0, glassTop, fz - shopRecess), Vec3(sx0, glassTop, fz));
    b.quad(Vec3(sx1, 0, fz - shopRecess), Vec3(sx1, 0, fz), Vec3(sx1, glassTop, fz), Vec3(sx1, glassTop, fz - shopRecess));
    b.setMaterial(7);
    b.box(Vec3((sx0 + doorX - doorW * 0.5f) * 0.5f, bulk * 0.5f, fz - shopRecess + 0.05f), Vec3(doorX - doorW * 0.5f - sx0, bulk, 0.1f));
    b.box(Vec3((doorX + doorW * 0.5f + sx1) * 0.5f, bulk * 0.5f, fz - shopRecess + 0.05f), Vec3(sx1 - doorX - doorW * 0.5f, bulk, 0.1f));
    b.setMaterial(6);
    float gz = fz - shopRecess;
    quadUV(b, Vec3(sx0, bulk, gz), Vec3(doorX - doorW * 0.5f, bulk, gz), Vec3(doorX - doorW * 0.5f, glassTop, gz), Vec3(sx0, glassTop, gz), Vec2(float(seed) * 3.0f, 1),
           Vec2(float(seed) * 3.0f + 1.0f, 0));
    quadUV(b, Vec3(doorX + doorW * 0.5f, bulk, gz), Vec3(sx1, bulk, gz), Vec3(sx1, glassTop, gz), Vec3(doorX + doorW * 0.5f, glassTop, gz),
           Vec2(float(seed) * 3.0f + 1.0f, 1), Vec2(float(seed) * 3.0f + 2.0f, 0));
    // door: glass pane in a frame
    quadUV(b, Vec3(doorX - doorW * 0.5f + 0.08f, 0.1f, gz - 0.02f), Vec3(doorX + doorW * 0.5f - 0.08f, 0.1f, gz - 0.02f),
           Vec3(doorX + doorW * 0.5f - 0.08f, 2.2f, gz - 0.02f), Vec3(doorX - doorW * 0.5f + 0.08f, 2.2f, gz - 0.02f), Vec2(float(seed) * 3.0f + 2.0f, 1),
           Vec2(float(seed) * 3.0f + 2.6f, 0));
    b.setMaterial(2);
    for (float x : {sx0 + 0.04f, doorX - doorW * 0.5f, doorX + doorW * 0.5f, sx1 - 0.04f})
        b.box(Vec3(x, glassTop * 0.5f, gz + 0.04f), Vec3(0.07f, glassTop, 0.08f));
    b.box(Vec3((sx0 + sx1) * 0.5f, glassTop - 0.04f, gz + 0.04f), Vec3(sx1 - sx0, 0.08f, 0.08f));
    b.box(Vec3((sx0 + sx1) * 0.5f, bulk, gz + 0.04f), Vec3(sx1 - sx0, 0.05f, 0.08f));
    b.box(Vec3(doorX, 2.24f, gz + 0.03f), Vec3(doorW, 0.08f, 0.08f));
    b.box(Vec3(doorX + doorW * 0.3f, 1.05f, gz + 0.06f), Vec3(0.03f, 0.35f, 0.03f));
    // step at the door
    b.setMaterial(3);
    b.box(Vec3(doorX, 0.05f, fz - shopRecess * 0.5f), Vec3(doorW + 0.4f, 0.1f, shopRecess));
    // sign band
    b.setMaterial(8);
    float signW = std::min(size.x - 1.4f, 6.5f);
    quadUV(b, Vec3(-signW * 0.5f, groundH - signH, fz + 0.06f), Vec3(signW * 0.5f, groundH - signH, fz + 0.06f), Vec3(signW * 0.5f, groundH - 0.1f, fz + 0.06f),
           Vec3(-signW * 0.5f, groundH - 0.1f, fz + 0.06f));
    b.setMaterial(2);
    b.box(Vec3(0, groundH - signH * 0.5f - 0.05f, fz + 0.03f), Vec3(signW + 0.1f, signH + 0.1f, 0.05f));
    // cornice + parapet cap
    b.setMaterial(3);
    b.box(Vec3(0, size.y - 0.2f, fz + 0.15f), Vec3(size.x + 0.3f, 0.4f, 0.3f));
    b.box(Vec3(0, size.y + 0.4f, fz - 0.1f), Vec3(size.x, 0.8f, 0.2f));
    b.box(Vec3(0, size.y + 0.4f, -hz + 0.1f), Vec3(size.x, 0.8f, 0.2f));
    b.box(Vec3(hx - 0.1f, size.y + 0.4f, 0), Vec3(0.2f, 0.8f, size.z - 0.4f));
    b.box(Vec3(-hx + 0.1f, size.y + 0.4f, 0), Vec3(0.2f, 0.8f, size.z - 0.4f));
    // roof equipment
    b.setMaterial(5);
    for (int k = 0; k < 3; ++k) {
        Vec3 p(rng.range(-hx + 2.0f, hx - 2.0f), size.y, rng.range(-hz + 2.0f, hz - 2.0f));
        b.box(p + Vec3(0, 0.5f, 0), Vec3(rng.range(1.0f, 1.8f), 1.0f, rng.range(0.8f, 1.4f)));
    }
    b.cylinder(Vec3(hx - 1.5f, size.y, -hz + 1.5f), 0.3f, 1.4f, 14, true);
    // downspout on a front corner
    b.setMaterial(2);
    float dx = (seed % 2 ? hx : -hx) - (seed % 2 ? 0.15f : -0.15f);
    b.cylinder(Vec3(dx, 0.1f, fz + 0.1f), 0.05f, size.y - 0.2f, 8, false);
    // stuck-on windows on the side walls (frames + interior glass)
    for (int side = -1; side <= 1; side += 2) {
        int scols = std::max(1, int(size.z / 3.2f) - 1);
        for (int fl = 0; fl < floors; ++fl)
            for (int c = 0; c < scols; ++c) {
                float z = -hz + (float(c) + 1.0f) * size.z / float(scols + 1);
                float y0 = groundH + float(fl) * floorH + sillY;
                float x = float(side) * (hx + 0.02f);
                b.setMaterial(1);
                float u0 = float(40 + c + fl * 7 + seed * 3 + side * 11), v0 = float(fl);
                Vec3 a(x, y0, z + float(side) * winW * 0.5f), bb(x, y0, z - float(side) * winW * 0.5f);
                quadUV(b, a, bb, bb + Vec3(0, winH, 0), a + Vec3(0, winH, 0), Vec2(u0, v0 + 1), Vec2(u0 + 1, v0));
                // frame bars, mullion and a concrete sill (the glass stays visible)
                b.setMaterial(2);
                float xf = float(side) * (hx + 0.05f);
                b.box(Vec3(xf, y0 + winH + 0.03f, z), Vec3(0.06f, 0.06f, winW + 0.12f));
                b.box(Vec3(xf, y0 - 0.03f, z), Vec3(0.06f, 0.06f, winW + 0.12f));
                b.box(Vec3(xf, y0 + winH * 0.5f, z - winW * 0.5f - 0.03f), Vec3(0.06f, winH, 0.06f));
                b.box(Vec3(xf, y0 + winH * 0.5f, z + winW * 0.5f + 0.03f), Vec3(0.06f, winH, 0.06f));
                b.box(Vec3(xf, y0 + winH * 0.5f, z), Vec3(0.05f, winH, 0.04f));
                b.setMaterial(3);
                b.box(Vec3(float(side) * (hx + 0.07f), y0 - 0.09f, z), Vec3(0.14f, 0.06f, winW + 0.24f));
            }
    }
    out.mesh = b.build();
    out.materials = {M(mat, "brick_painted"), "window_interior", "frame_dark", "trim_concrete", "roof_grey", "metal_galvanized", "shop_interior",
                     "painted_dark_grey", shop};
    MeshBuilder c("shopfront_col");
    c.box(Vec3(0, size.y * 0.5f, 0), size);
    out.collisionMesh = c.build();
    out.surface = "concrete";
    // sills + window ledges of the shop are grindable
    addRail(out, {{sx0, bulk, fz - shopRecess + 0.1f}, {doorX - doorW * 0.5f, bulk, fz - shopRecess + 0.1f}}, RailType::Ledge);
}

}  // namespace

void registerStreetPrefabs(PrefabRegistry& r) {
    r.add("car", "Street", carPrefab, {{"type", "sedan"}, {"paint", "red"}});
    r.add("decal", "Street", decalPrefab, {{"size", {2, 2}}, {"orient", "ground"}, {"offset", 0.006}});
    r.add("traffic_sign", "Street", trafficSignPrefab, {{"sign", "stop"}, {"height", 2.6}});
    r.add("parking_meter", "Street", parkingMeterPrefab, Json::object());
    r.add("utility_pole", "Street", utilityPolePrefab, {{"height", 9.0}});
    r.add("cable", "Street", cablePrefab, {{"length", 30.0}, {"sag", 0.6}, {"radius", 0.012}});
    r.add("awning", "Street", awningPrefab, {{"width", 4.0}, {"depth", 1.2}, {"drop", 0.5}, {"valance", 0.25}});
    r.add("sidewalk", "Ground", sidewalkPrefab, {{"length", 20.0}, {"width", 4.0}, {"height", 0.15}, {"backCurb", false}});
    r.add("crosswalk", "Ground", crosswalkPrefab, {{"length", 10.0}, {"width", 3.0}});
    r.add("parking_lines", "Ground", parkingLinesPrefab, {{"bays", 5}, {"bayWidth", 2.6}, {"bayLength", 5.2}});
    r.add("shopfront", "Structure", shopfrontPrefab,
          {{"size", {14, 13, 12}}, {"groundFloor", 4.2}, {"floorHeight", 3.3}, {"shop", "shop_coffee"}, {"seed", 1}});
}

}  // namespace sw::kit
