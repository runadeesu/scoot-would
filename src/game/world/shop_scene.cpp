#include "game/world/shop_scene.h"
#include "assets/asset_manager.h"
#include "core/log.h"
#include "game/player/scooter_model.h"
#include "render/mesh_builder.h"
#include "scene/scene.h"

namespace sw {

namespace {
// room (local space: origin on the floor in the middle of the shop, the counter is towards -Z)
constexpr float kHalfW = 4.2f, kBack = -3.0f, kFront = 3.4f, kHeight = 3.0f;
constexpr float kCounterX0 = -1.5f, kCounterX1 = 1.9f, kCounterZ = -1.2f, kCounterDepth = 0.7f;
constexpr float kCounterTop = 0.99f;
const ScooterDims kDims;
// the display scooter stands on its wheels on the counter, front wheel to the right (+X)
const Vec3 kDisplayPos(0.2f, kCounterTop + 0.006f + 0.055f, kCounterZ);
const float kDisplayYaw = -90.0f * kDeg2Rad;

std::shared_ptr<GpuMesh> upload(MeshBuilder& b) {
    MeshData md = b.build();
    return createGpuMesh(md, false);
}
std::shared_ptr<GpuMesh> upload(MeshData md) { return createGpuMesh(md, false); }

// textured quad with 0..1 UVs (signs, banners, screens): centre, half extents along right / up
void texturedQuad(MeshBuilder& b, const Vec3& c, const Vec3& right, const Vec3& up) {
    Vec3 n = cross(right, up).normalized();
    uint32_t a = b.addVertex(c - right - up, n, {0, 1}), bb = b.addVertex(c + right - up, n, {1, 1});
    uint32_t cc = b.addVertex(c + right + up, n, {1, 0}), d = b.addVertex(c - right + up, n, {0, 0});
    b.addQuad(a, bb, cc, d);
}

// rotation taking the (orthonormal) source axes onto the destination axes, then `anchor` onto `pos`
Mat4 align(const Vec3& srcX, const Vec3& srcY, const Vec3& dstX, const Vec3& dstY, const Vec3& anchor, const Vec3& pos) {
    Vec3 sx = srcX.normalized(), sy = srcY.normalized(), sz = cross(sx, sy).normalized();
    sy = cross(sz, sx);
    Vec3 dx = dstX.normalized(), dy = dstY.normalized(), dz = cross(dx, dy).normalized();
    dy = cross(dz, dx);
    Mat4 r;
    // R = D * S^T
    const Vec3 S[3] = {sx, sy, sz}, D[3] = {dx, dy, dz};
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row) {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k) {
                float dk[3] = {D[k].x, D[k].y, D[k].z}, sk[3] = {S[k].x, S[k].y, S[k].z};
                v += dk[row] * sk[col];
            }
            r.m[col * 4 + row] = v;
        }
    Vec3 moved = r.transformPoint(anchor);
    r.m[12] = pos.x - moved.x;
    r.m[13] = pos.y - moved.y;
    r.m[14] = pos.z - moved.z;
    return r;
}

MaterialPtr mat(const char* n) { return assets().material(n); }
}  // namespace

// --------------------------------------------------------------------------------------------------

RenderScene::Handle ShopScene::add(std::shared_ptr<GpuMesh> mesh, const std::vector<MaterialPtr>& mats, const Mat4& local, bool shadows,
                                   const Vec4& tint) {
    RenderObject o;
    o.mesh = std::move(mesh);
    o.materials = mats;
    o.world = Mat4::translation(origin_) * local;
    o.castShadows = shadows;
    o.isStatic = false;
    o.visible = active_;
    o.tint = tint;
    o.lodBias = 2.0f;
    RenderScene::Handle h = rs_->add(o);
    objects_.push_back(h);
    return h;
}

void ShopScene::addModel(const char* name, const Vec3& pos, float yawDeg, float scale, const char* include, const char* exclude) {
    std::string path = std::string("assets/models/props/") + name + "/" + name + ".gltf";
    Json p = {{"model", path}, {"include", include}, {"exclude", exclude}, {"scale", scale}, {"collider", "none"}, {"recenter", true}};
    PrefabBuild b;
    if (!prefabs().build("model", p, "", b) || b.mesh.vertices.empty()) {
        LOG_WARN("shop: prop '%s' missing", name);
        return;
    }
    std::vector<MaterialPtr> mats;
    for (auto& m : b.materials) mats.push_back(assets().material(m));
    add(createGpuMesh(b.mesh, true), mats, Mat4::translation(pos) * Mat4::rotation(Quat::angleAxis(yawDeg * kDeg2Rad, Vec3(0, 1, 0))));
}

void ShopScene::addLight(const Vec3& localPos, const Vec3& color, float intensity, float radius) {
    LightProxy l;
    l.position = origin_ + localPos;
    l.color = color;
    l.intensity = intensity;
    l.radius = radius;
    l.enabled = active_;
    lights_.push_back(rs_->addLight(l));
}

void ShopScene::reset() {
    objects_.clear();
    lights_.clear();
    built_ = false;
    active_ = false;
    rs_ = nullptr;
}

void ShopScene::setActive(bool on) {
    active_ = on;
    if (!rs_) return;
    for (auto h : objects_) rs_->setVisible(h, on);
    for (auto h : lights_)
        if (LightProxy* l = rs_->light(h)) l->enabled = on;
}

Transform ShopScene::displayTransform() const {
    Transform t;
    t.position = origin_ + kDisplayPos;
    t.rotation = Quat::angleAxis(kDisplayYaw, Vec3(0, 1, 0));
    return t;
}

Vec3 ShopScene::clampToRoom(const Vec3& p) const {
    Vec3 l = p - origin_;
    l.x = clampf(l.x, -kHalfW + 0.35f, kHalfW - 0.35f);
    l.y = clampf(l.y, 0.35f, kHeight - 0.3f);
    l.z = clampf(l.z, kBack + 0.35f, kFront - 0.35f);
    return origin_ + l;
}

Environment ShopScene::environment() const {
    Environment e;
    e.name = "shop";
    e.hdri = "assets/hdri/shop.hdr";  // Poly Haven "Gear Store" (CC0): a real shop interior lights the scene
    e.rotation = 0.0f;
    e.skyIntensity = 1.0f;
    e.iblIntensity = 1.0f;
    e.autoSun = false;
    e.sunDirection = Vec3(0.18f, 1.0f, 0.42f).normalized();  // overhead panels, slightly from the front
    e.sunColor = Vec3(1.0f, 0.95f, 0.88f);
    e.sunIntensity = 0.55f;
    e.exposure = 1.15f;
    e.fogDensity = 0.0f;
    e.bloomThreshold = 1.4f;
    e.bloomStrength = 0.05f;
    e.contrast = 1.04f;
    e.saturation = 1.03f;
    e.temperature = 0.04f;
    e.vignette = 0.28f;
    e.lampsOn = false;
    e.urbanReflection = 0.0f;
    e.groundFill = false;
    return e;
}

void ShopScene::ensureBuilt(RenderScene& rs) {
    if (built_ && rs_ == &rs) return;
    rs_ = &rs;
    objects_.clear();
    lights_.clear();
    buildRoom();
    buildCounter();
    buildMerchandise();
    built_ = true;
    LOG_INFO("shop: interior built (%zu objects, %zu lights)", objects_.size(), lights_.size());
}

// --- room --------------------------------------------------------------------------------------------

void ShopScene::buildRoom() {
    const float W = kHalfW, B = kBack, F = kFront, H = kHeight;
    {
        MeshBuilder b("shop floor");
        b.quad(Vec3(-W, 0, F), Vec3(W, 0, F), Vec3(W, 0, B), Vec3(-W, 0, B));
        add(upload(b), {mat("shop_floor")}, Mat4::identity(), false);
    }
    {
        MeshBuilder b("shop walls");
        b.quad(Vec3(-W, 0, B), Vec3(W, 0, B), Vec3(W, H, B), Vec3(-W, H, B));  // back
        b.quad(Vec3(W, 0, F), Vec3(-W, 0, F), Vec3(-W, H, F), Vec3(W, H, F));  // front
        b.quad(Vec3(-W, 0, F), Vec3(-W, 0, B), Vec3(-W, H, B), Vec3(-W, H, F));  // left
        b.quad(Vec3(W, 0, B), Vec3(W, 0, F), Vec3(W, H, F), Vec3(W, H, B));  // right
        add(upload(b), {mat("shop_wall")}, Mat4::identity(), false);
    }
    {
        MeshBuilder b("shop ceiling");
        b.quad(Vec3(-W, H, B), Vec3(W, H, B), Vec3(W, H, F), Vec3(-W, H, F));
        add(upload(b), {mat("shop_ceiling")}, Mat4::identity(), false);
    }
    {
        // skirting boards + a shadow gap under the ceiling
        MeshBuilder b("shop skirting");
        const float t = 0.015f, h = 0.09f;
        b.box(Vec3(0, h * 0.5f, B + t * 0.5f), Vec3(2 * W, h, t));
        b.box(Vec3(0, h * 0.5f, F - t * 0.5f), Vec3(2 * W, h, t));
        b.box(Vec3(-W + t * 0.5f, h * 0.5f, (B + F) * 0.5f), Vec3(t, h, F - B));
        b.box(Vec3(W - t * 0.5f, h * 0.5f, (B + F) * 0.5f), Vec3(t, h, F - B));
        add(upload(b), {mat("shop_skirting")}, Mat4::identity(), false);
    }
    {
        // recessed LED panels with aluminium trims
        MeshBuilder lit("shop panels"), trim("shop panel trims");
        for (float x : {-2.4f, 0.0f, 2.4f})
            for (float z : {-1.7f, 1.3f}) {
                lit.box(Vec3(x, H - 0.012f, z), Vec3(0.58f, 0.02f, 0.58f));
                trim.box(Vec3(x, H - 0.006f, z - 0.305f), Vec3(0.64f, 0.012f, 0.03f));
                trim.box(Vec3(x, H - 0.006f, z + 0.305f), Vec3(0.64f, 0.012f, 0.03f));
                trim.box(Vec3(x - 0.305f, H - 0.006f, z), Vec3(0.03f, 0.012f, 0.58f));
                trim.box(Vec3(x + 0.305f, H - 0.006f, z), Vec3(0.03f, 0.012f, 0.58f));
            }
        add(upload(lit), {mat("shop_light_panel")}, Mat4::identity(), false);
        add(upload(trim), {mat("shop_alu")}, Mat4::identity(), false);
    }

    // back wall: back lit sign with neon outline, wooden fascia with the brand banners
    {
        const float cy = 1.8f, hw = 1.1f, hh = 0.55f;
        MeshBuilder frame("shop sign frame");
        frame.box(Vec3(0, cy, B + 0.025f), Vec3(2 * hw + 0.1f, 2 * hh + 0.1f, 0.05f));
        add(upload(frame), {mat("shop_black_metal")}, Mat4::identity(), false);
        MeshBuilder face("shop sign");
        texturedQuad(face, Vec3(0, cy, B + 0.0515f), Vec3(hw, 0, 0), Vec3(0, hh, 0));
        add(upload(face), {mat("shop_panel")}, Mat4::identity(), false);
        MeshBuilder neon("shop neon");
        const float o = 0.11f, z = B + 0.06f;
        std::vector<Vec3> loop = {{-hw - o, cy - hh - o, z}, {hw + o, cy - hh - o, z}, {hw + o, cy + hh + o, z}, {-hw - o, cy + hh + o, z},
                                  {-hw - o, cy - hh - o, z}};
        neon.tube(loop, 0.009f, 8, false);
        add(upload(neon), {mat("shop_neon")}, Mat4::identity(), false);
        addLight(Vec3(0, cy, B + 0.5f), Vec3(0.25f, 0.8f, 1.0f), 3.0f, 2.8f);
    }
    {
        MeshBuilder band("shop fascia");
        band.box(Vec3(0, 2.72f, B + 0.02f), Vec3(2 * W - 0.04f, 0.5f, 0.04f));
        add(upload(band), {mat("plywood")}, Mat4::identity(), false);
        const char* banners[3] = {"shop_banner_1", "shop_banner_2", "shop_banner_3"};
        for (int i = 0; i < 3; ++i) {
            float x = -2.3f + 2.3f * float(i);
            MeshBuilder back("banner backing");
            back.box(Vec3(x, 2.72f, B + 0.047f), Vec3(1.0f, 0.39f, 0.014f));
            add(upload(back), {mat("shop_black_metal")}, Mat4::identity(), false);
            MeshBuilder q("banner");
            texturedQuad(q, Vec3(x, 2.72f, B + 0.0545f), Vec3(0.48f, 0, 0), Vec3(0, 0.18f, 0));
            add(upload(q), {mat(banners[i])}, Mat4::identity(), false);
        }
    }
    // front wall: the roller shutter of the (closed) shop front
    {
        MeshBuilder s("shop shutter");
        s.quad(Vec3(1.6f, 0, F - 0.02f), Vec3(-1.6f, 0, F - 0.02f), Vec3(-1.6f, 2.5f, F - 0.02f), Vec3(1.6f, 2.5f, F - 0.02f));
        add(upload(s), {mat("shutter")}, Mat4::identity(), false);
        MeshBuilder f("shutter frame");
        f.box(Vec3(-1.66f, 1.28f, F - 0.04f), Vec3(0.12f, 2.56f, 0.08f));
        f.box(Vec3(1.66f, 1.28f, F - 0.04f), Vec3(0.12f, 2.56f, 0.08f));
        f.box(Vec3(0, 2.62f, F - 0.08f), Vec3(3.44f, 0.24f, 0.16f));
        add(upload(f), {mat("shop_black_metal")}, Mat4::identity(), false);
    }
    // framed poster on the right wall
    {
        MeshBuilder fr("poster frame");
        fr.box(Vec3(kHalfW - 0.015f, 1.62f, 1.45f), Vec3(0.03f, 0.98f, 0.68f));
        add(upload(fr), {mat("shop_black_metal")}, Mat4::identity(), false);
        MeshBuilder q("poster");
        texturedQuad(q, Vec3(kHalfW - 0.0305f, 1.62f, 1.45f), Vec3(0, 0, 0.3f), Vec3(0, 0.45f, 0));
        add(upload(q), {mat("shop_poster")}, Mat4::identity(), false);
    }

    // lighting: pendants over the counter, fill from the ceiling panels, cyan sign glow (above)
    const float lampScale = 0.78f;
    for (float x : {-0.85f, 0.25f, 1.35f}) {
        addModel("modern_ceiling_lamp_01", Vec3(x, kHeight - 0.952f * lampScale, kCounterZ), 0.0f, lampScale);
        addLight(Vec3(x, kHeight - 0.62f, kCounterZ), Vec3(1.0f, 0.8f, 0.58f), 3.2f, 3.6f);
    }
    // ceiling panels wash the walls; low warm lights stand in for the bounce off the wooden floor
    for (float x : {-2.4f, 2.4f}) addLight(Vec3(x, kHeight - 0.35f, 0.0f), Vec3(1.0f, 0.96f, 0.9f), 2.0f, 6.0f);
    addLight(Vec3(-3.4f, kHeight - 0.4f, -0.3f), Vec3(1.0f, 0.96f, 0.9f), 1.6f, 4.5f);
    addLight(Vec3(3.4f, kHeight - 0.4f, -0.8f), Vec3(1.0f, 0.96f, 0.9f), 1.6f, 4.5f);
    addLight(Vec3(0.0f, kHeight - 0.4f, 2.5f), Vec3(1.0f, 0.96f, 0.9f), 1.4f, 4.5f);
    for (float x : {-2.0f, 2.0f}) addLight(Vec3(x, 1.1f, 1.0f), Vec3(1.0f, 0.84f, 0.66f), 0.9f, 5.5f);
    // the opal glass globes of the pendants glow
    for (auto h : objects_) {
        const RenderObject* o = rs_->get(h);
        if (!o) continue;
        for (const MaterialPtr& m : o->materials)
            if (m && m->name.find("modern_ceiling_lamp_01_glass") != std::string::npos) {
                m->emissiveFactor = Vec3(1.0f, 0.86f, 0.66f);
                m->emissiveStrength = 2.2f;
            }
    }
}

// --- counter --------------------------------------------------------------------------------------

void ShopScene::buildCounter() {
    const float x0 = kCounterX0, x1 = kCounterX1, cx = (x0 + x1) * 0.5f, len = x1 - x0;
    const float zc = kCounterZ, d = kCounterDepth;
    {
        MeshBuilder body("counter body");
        body.box(Vec3(cx, 0.1f + 0.425f, zc), Vec3(len, 0.85f, d - 0.04f));
        add(upload(body), {mat("shop_counter_body")}, Mat4::identity());
        MeshBuilder kick("counter kick");
        kick.box(Vec3(cx, 0.05f, zc - 0.03f), Vec3(len - 0.08f, 0.1f, d - 0.14f));
        // panel seams on the customer side
        for (float x = x0 + 0.425f; x < x1 - 0.1f; x += 0.425f) kick.box(Vec3(x, 0.525f, zc + (d - 0.04f) * 0.5f), Vec3(0.006f, 0.83f, 0.004f));
        add(upload(kick), {mat("shop_skirting")}, Mat4::identity());
        MeshBuilder top("counter top");
        top.box(Vec3(cx, 0.97f, zc), Vec3(len + 0.06f, 0.04f, d + 0.06f));
        add(upload(top), {mat("shop_counter_top")}, Mat4::identity());
    }
    // rubber display mat + a rear wheel stand under the display scooter
    {
        MeshBuilder m("display mat");
        m.box(Vec3(kDisplayPos.x, kCounterTop + 0.003f, zc), Vec3(0.84f, 0.006f, 0.34f));
        add(upload(m), {mat("shop_mat")}, Mat4::identity());
        Vec3 rear = kDisplayPos + Quat::angleAxis(kDisplayYaw, Vec3(0, 1, 0)) * kDims.rearAxle();
        MeshBuilder s("wheel stand");
        float base = kCounterTop + 0.006f;
        // base plate with two chocks holding the rear wheel
        s.box(Vec3(rear.x, base + 0.003f, zc), Vec3(0.2f, 0.006f, 0.1f));
        for (float side : {-1.0f, 1.0f}) s.box(Vec3(rear.x + side * 0.047f, base + 0.018f, zc), Vec3(0.03f, 0.024f, 0.07f), Quat::angleAxis(side * 0.35f, Vec3(0, 0, 1)));
        add(upload(s), {mat("shop_black_metal")}, Mat4::identity());
    }
    // laptop, turned towards the customer
    {
        Mat4 place = Mat4::translation(Vec3(-1.05f, kCounterTop, zc - 0.02f)) * Mat4::rotation(Quat::angleAxis(28.0f * kDeg2Rad, Vec3(0, 1, 0)));
        MeshBuilder base("laptop base");
        base.box(Vec3(0, 0.008f, 0), Vec3(0.33f, 0.016f, 0.23f));
        MeshBuilder keys("laptop keys");
        keys.box(Vec3(0, 0.0165f, -0.02f), Vec3(0.28f, 0.002f, 0.1f));
        keys.box(Vec3(0, 0.0165f, 0.075f), Vec3(0.1f, 0.002f, 0.06f));
        // lid hinged on the back edge, opened to 108 degrees
        Mat4 lid = Mat4::translation(Vec3(0, 0.016f, -0.115f)) * Mat4::rotation(Quat::angleAxis(18.0f * kDeg2Rad, Vec3(-1, 0, 0)));
        MeshBuilder shell("laptop lid");
        shell.setTransform(lid);
        shell.box(Vec3(0, 0.11f, -0.004f), Vec3(0.33f, 0.22f, 0.008f));
        MeshBuilder screen("laptop screen");
        screen.setTransform(lid);
        texturedQuad(screen, Vec3(0, 0.115f, 0.0005f), Vec3(0.15f, 0, 0), Vec3(0, 0.094f, 0));
        add(upload(base), {mat("shop_alu")}, place);
        add(upload(keys), {mat("shop_mat")}, place);
        add(upload(shell), {mat("shop_alu")}, place);
        add(upload(screen), {mat("shop_laptop_screen")}, place, false);
    }
    addModel("boombox", Vec3(1.5f, kCounterTop, zc - 0.12f), -16.0f, 0.62f);
    addModel("bar_chair_round_01", Vec3(-0.45f, 0.0f, zc - 0.95f), 20.0f);
}

// --- merchandise -----------------------------------------------------------------------------------

void ShopScene::buildMerchandise() {
    const Vec3 anodised[] = {{0.03f, 0.03f, 0.035f}, {0.85f, 0.85f, 0.88f}, {0.75f, 0.06f, 0.05f}, {0.05f, 0.2f, 0.75f},
                             {0.9f, 0.65f, 0.2f},    {0.05f, 0.55f, 0.5f},  {0.35f, 0.08f, 0.6f}, {0.95f, 0.35f, 0.03f}};
    MaterialPtr hw = mat("scooter_hardware");
    std::vector<MaterialPtr> deckMats = {mat("scooter_deck"), hw, mat("scooter_headset")};
    std::vector<MaterialPtr> gripMats = {mat("scooter_griptape")};
    std::vector<MaterialPtr> barMats = {mat("scooter_bars"), mat("scooter_barend")};
    std::vector<MaterialPtr> handleMats = {mat("scooter_grips")};
    std::vector<MaterialPtr> tyreMats = {mat("scooter_wheel")};
    std::vector<MaterialPtr> coreMats = {mat("scooter_core"), mat("scooter_bearing"), hw};
    ScooterModelOptions opt[3];
    ScooterMeshSet sets[3];
    for (int i = 0; i < 3; ++i) {
        opt[i].deck = i;
        opt[i].bars = i;
        opt[i].wheels = i;
        sets[i] = buildScooterModel(kDims, opt[i]);
    }
    std::shared_ptr<GpuMesh> decks[3], grips[3], bars[3], handles[3], tyre[3], core[3];
    for (int i = 0; i < 3; ++i) {
        decks[i] = upload(sets[i].deck);
        grips[i] = upload(sets[i].grip);
        bars[i] = upload(sets[i].bars);
        handles[i] = upload(sets[i].grips);
        tyre[i] = upload(sets[i].tyre);
        core[i] = upload(sets[i].core);
    }

    // left wall: slatwall with decks hanging nose up, griptape towards the room
    {
        MeshBuilder slat("slatwall");
        const float z0 = -2.0f, z1 = 1.7f, y0 = 0.55f, y1 = 2.3f;
        slat.box(Vec3(-kHalfW + 0.01f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f), Vec3(0.02f, y1 - y0, z1 - z0));
        MeshBuilder grooves("slatwall grooves");
        for (float y = y0 + 0.1f; y < y1 - 0.05f; y += 0.1524f) grooves.box(Vec3(-kHalfW + 0.021f, y, (z0 + z1) * 0.5f), Vec3(0.004f, 0.012f, z1 - z0));
        add(upload(slat), {mat("shop_slatwall")}, Mat4::identity(), false);
        add(upload(grooves), {mat("shop_black_metal")}, Mat4::identity(), false);
        for (int i = 0; i < 7; ++i) {
            int v = i % 3;
            float z = -1.6f + 0.5f * float(i);
            Vec3 anchor(0, 0.0f, 0.0f);
            Mat4 m = align(Vec3(0, 1, 0), Vec3(0, 0, -1), Vec3(1, 0, 0), Vec3(0, 1, 0), anchor, Vec3(-kHalfW + 0.075f, 1.32f, z));
            Vec4 tint(anodised[size_t(i) % 8], 0.0f);
            add(decks[v], deckMats, m, true, tint);
            add(grips[v], gripMats, m, true);
        }
        // hooks
        MeshBuilder hooks("hooks");
        for (int i = 0; i < 7; ++i) {
            float z = -1.6f + 0.5f * float(i);
            hooks.tube({{-kHalfW + 0.02f, 1.08f, z}, {-kHalfW + 0.1f, 1.08f, z}, {-kHalfW + 0.11f, 1.1f, z}}, 0.004f, 6, true);
        }
        add(upload(hooks), {mat("shop_alu")}, Mat4::identity(), false);
    }
    // right wall: bars hanging above two cubby shelves full of wheels
    {
        Vec3 steer = kDims.steerAxis();
        for (int i = 0; i < 5; ++i) {
            int v = i % 3;
            float z = -2.35f + 0.52f * float(i);
            Vec3 top = kDims.barCenter();
            // hang from the crossbar: steer axis vertical, crossbar along the wall
            Mat4 m = align(Vec3(1, 0, 0), steer, Vec3(0, 0, 1), Vec3(0, 1, 0), top, Vec3(kHalfW - 0.12f, 2.45f, z));
            Vec4 tint(anodised[size_t(i + 3) % 8], 0.0f);
            add(bars[v], barMats, m, true, tint);
            add(handles[v], handleMats, m, true, Vec4(i % 2 ? Vec3(0.05f) : Vec3(0.8f, 0.1f, 0.1f), 0.0f));
        }
        for (float z : {-1.9f, -0.8f}) addModel("wooden_display_shelves_01", Vec3(kHalfW - 0.2f, 0.0f, z), 180.0f);
        // wheels standing in the cubbies (3 x 4 compartments per unit, ~0.35 m pitch)
        int n = 0;
        for (float zc : {-1.9f, -0.8f})
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) {
                    if ((row + col + n) % 4 == 3) continue;
                    float y = 0.52f + 0.345f * float(row) + 0.056f;
                    float z = zc - 0.34f + 0.34f * float(col);
                    Mat4 m = Mat4::translation(Vec3(kHalfW - 0.2f, y, z));  // axle along X: faces the room
                    int v = (row + col) % 3;
                    add(tyre[v], tyreMats, m, true, Vec4(n % 3 == 0 ? Vec3(0.05f) : Vec3(0.92f), 0.0f));
                    add(core[v], coreMats, m, true, Vec4(anodised[size_t(n) % 8], 0.0f));
                    ++n;
                }
        addModel("wall_clock", Vec3(kHalfW - 0.02f, 2.25f, -0.2f), -90.0f);
    }
    // back wall: steel shelving with boxed parts and spare wheels
    for (float x : {-2.75f, 2.75f}) {
        addModel("steel_frame_shelves_01", Vec3(x, 0.0f, kBack + 0.27f), 0.0f, 0.1f);
        addModel("cardboard_box_01", Vec3(x - 0.25f, 0.6f, kBack + 0.27f), 8.0f, 0.9f);
        addModel("cardboard_box_01", Vec3(x + 0.25f, 1.1f, kBack + 0.27f), -5.0f, 0.8f);
        addModel("plastic_crate_01", Vec3(x + 0.1f, 0.1f, kBack + 0.27f), 0.0f, 0.9f);
        for (int k = 0; k < 4; ++k) {
            Mat4 m = Mat4::translation(Vec3(x - 0.36f + 0.24f * float(k), 1.57f + 0.056f, kBack + 0.3f)) *
                     Mat4::rotation(Quat::angleAxis(90.0f * kDeg2Rad, Vec3(0, 1, 0)));  // axle along Z: faces the counter
            add(tyre[k % 3], tyreMats, m, true, Vec4(k % 2 ? Vec3(0.92f) : Vec3(0.05f), 0.0f));
            add(core[k % 3], coreMats, m, true, Vec4(anodised[size_t(k + 2) % 8], 0.0f));
        }
    }
    // lounge corner, plants
    addModel("modern_arm_chair_01", Vec3(3.3f, 0.0f, 2.4f), -140.0f);
    addModel("pachira_aquatica_01", Vec3(-3.6f, 0.0f, -2.45f), 30.0f, 1.0f, "bark_a,leaves_a");
    addModel("potted_plant_02", Vec3(-3.6f, 0.0f, 2.8f), 0.0f);
}

}  // namespace sw
