#include "game/ui/part_thumbs.h"
#include "game/player/scooter_model.h"

#include <cstdio>

namespace sw {

namespace {

struct SlotLook {
    bool tinted = false;
    float metal = 0.0f, rough = 0.5f;
    Vec3 base = Vec3(0.5f);
};

struct PartInstance {
    const MeshData* mesh;
    Mat4 xf;
    std::vector<SlotLook> slots;
};

const SlotLook kHardware{false, 1.0f, 0.3f, {0.56f, 0.57f, 0.6f}};
const SlotLook kBlackAlu{false, 1.0f, 0.34f, {0.035f, 0.035f, 0.04f}};
const SlotLook kBlackRubber{false, 0.0f, 0.8f, {0.035f, 0.035f, 0.035f}};
const SlotLook kTintAlu{true, 1.0f, 0.3f, Vec3(1.0f)};
const SlotLook kRawAlu{false, 1.0f, 0.28f, {0.8f, 0.81f, 0.83f}};

float smooth01(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// soft studio: large overhead softbox, key strip from the upper left, fill card on the right
struct Studio {
    Vec3 up, key, fill, rim, view;
    float env(const Vec3& r, float rough) const {
        float spread = 1.0f + rough * 6.0f;
        float e = 0.16f + 0.75f * smooth01(-0.25f, 0.95f, dot(r, up));
        e += 2.6f * std::pow(std::max(dot(r, key), 0.0f), 40.0f / spread) * (1.0f - rough * 0.5f);
        e += 1.1f * std::pow(std::max(dot(r, fill), 0.0f), 16.0f / spread);
        e += 0.9f * std::pow(std::max(dot(r, rim), 0.0f), 24.0f / spread);
        return e;
    }
};

Vec3 tonemap(Vec3 c) {
    auto f = [](float x) {
        x = std::max(x, 0.0f) * 1.1f;
        float t = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);  // ACES fit
        t = clampf(t, 0.0f, 1.0f);
        return t <= 0.0031308f ? t * 12.92f : 1.055f * std::pow(t, 1.0f / 2.4f) - 0.055f;
    };
    return Vec3(f(c.x), f(c.y), f(c.z));
}

}  // namespace

void PartThumbnails::clear() {
    shades_.clear();
    textures_.clear();
}

PartThumbnails::Shade PartThumbnails::render(Kind kind, int variant) const {
    static ScooterMeshSet sets[4];
    static bool built[4] = {false, false, false, false};
    int v = std::clamp(variant, 0, 3);
    const ScooterDims dims;
    if (!built[v]) {
        ScooterModelOptions o;
        o.deck = o.bars = v;
        o.wheels = std::min(v, 2);
        o.clamp = std::min(v, 1);
        sets[v] = buildScooterModel(dims, o);
        built[v] = true;
    }
    const ScooterMeshSet& s = sets[v];
    std::vector<PartInstance> parts;
    Vec3 viewDir;  // from the subject towards the eye
    switch (kind) {
        case Deck:
            parts.push_back({&s.deck, Mat4::identity(), {kTintAlu, kHardware, kBlackAlu}});
            parts.push_back({&s.grip, Mat4::identity(), {{false, 0.0f, 0.95f, {0.045f, 0.045f, 0.048f}}, kTintAlu}});
            parts.push_back({&s.brake, Mat4::identity(), {{false, 0.7f, 0.42f, {0.03f, 0.03f, 0.035f}}}});
            viewDir = Vec3(1.0f, 0.95f, 0.3f);
            break;
        case Bars:
            parts.push_back({&s.bars, Mat4::identity(), {kTintAlu, {false, 0.0f, 0.6f, Vec3(0.03f)}}});
            parts.push_back({&s.grips, Mat4::identity(), {kBlackRubber}});
            viewDir = Vec3(0.55f, 0.2f, -1.0f);
            break;
        case Clamp:
            parts.push_back({&s.clamp, Mat4::identity(), {kTintAlu, kHardware}});
            viewDir = Vec3(0.75f, 0.55f, 0.75f);
            break;
        case Wheel:
            parts.push_back({&s.tyre, Mat4::identity(), {{false, 0.0f, 0.5f, {0.9f, 0.9f, 0.88f}}}});
            parts.push_back({&s.core, Mat4::identity(), {kTintAlu, {false, 0.9f, 0.3f, Vec3(0.08f)}, kHardware}});
            viewDir = Vec3(1.0f, 0.22f, 0.38f);
            break;
        case Urethane:
            parts.push_back({&s.tyre, Mat4::identity(), {{true, 0.0f, 0.5f, Vec3(1.0f)}}});
            parts.push_back({&s.core, Mat4::identity(), {kRawAlu, {false, 0.9f, 0.3f, Vec3(0.08f)}, kHardware}});
            viewDir = Vec3(1.0f, 0.22f, 0.38f);
            break;
        case Grips:
        default:
            parts.push_back({&s.grips, Mat4::identity(), {{true, 0.0f, 0.82f, Vec3(1.0f)}}});
            parts.push_back({&s.bars, Mat4::identity(), {kBlackAlu, {false, 0.0f, 0.6f, Vec3(0.03f)}}});
            viewDir = Vec3(0.55f, 0.35f, -1.0f);
            break;
    }
    const Vec3 dirV = viewDir.normalized();
    const Vec3 fwd = -dirV;
    const Vec3 right = cross(fwd, Vec3(0, 1, 0)).normalized();
    const Vec3 up = cross(right, fwd).normalized();
    Studio st;
    st.up = up;
    st.view = dirV;
    st.key = (right * -0.55f + up * 0.75f + dirV * 0.45f).normalized();
    st.fill = (right * 0.85f + up * 0.05f + dirV * 0.5f).normalized();
    st.rim = (up * 0.55f - dirV * 0.85f + right * 0.2f).normalized();

    // fit: orthographic projection of every vertex
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const PartInstance& p : parts)
        for (const Vertex& vt : p.mesh->vertices) {
            Vec3 w = p.xf.transformPoint(vt.position);
            float px = dot(w, right), py = dot(w, up);
            x0 = std::min(x0, px);
            x1 = std::max(x1, px);
            y0 = std::min(y0, py);
            y1 = std::max(y1, py);
        }
    const int S = kSize * 2;  // 2x2 supersampling
    float extent = std::max(x1 - x0, y1 - y0);
    float scale = float(S) * 0.84f / std::max(extent, 1e-4f);
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;

    std::vector<float> depth(size_t(S) * S, -1e30f);
    std::vector<Vec3> normal(size_t(S) * S);
    std::vector<const SlotLook*> look(size_t(S) * S, nullptr);
    for (const PartInstance& p : parts) {
        const MeshData& m = *p.mesh;
        std::vector<Vec3> sp(m.vertices.size()), wn(m.vertices.size());
        std::vector<float> sd(m.vertices.size());
        for (size_t i = 0; i < m.vertices.size(); ++i) {
            Vec3 w = p.xf.transformPoint(m.vertices[i].position);
            sp[i] = Vec3((dot(w, right) - cx) * scale + float(S) * 0.5f, (cy - dot(w, up)) * scale + float(S) * 0.5f, 0.0f);
            sd[i] = dot(w, dirV);
            wn[i] = p.xf.transformDir(m.vertices[i].normal).normalized();
        }
        for (const SubMesh& sm : m.submeshes) {
            const SlotLook* lk = sm.material >= 0 && size_t(sm.material) < p.slots.size() ? &p.slots[size_t(sm.material)] : &p.slots.back();
            for (uint32_t t = sm.firstIndex; t + 2 < sm.firstIndex + sm.indexCount; t += 3) {
                uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
                Vec3 a = sp[i0], b = sp[i1], c = sp[i2];
                float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (std::fabs(area) < 1e-8f) continue;
                int bx0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
                int bx1 = std::min(S - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
                int by0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
                int by1 = std::min(S - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
                float inv = 1.0f / area;
                for (int y = by0; y <= by1; ++y)
                    for (int x = bx0; x <= bx1; ++x) {
                        float px = float(x) + 0.5f, py = float(y) + 0.5f;
                        float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * inv;
                        float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * inv;
                        float w2 = 1.0f - w0 - w1;
                        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                        float z = sd[i0] * w0 + sd[i1] * w1 + sd[i2] * w2;
                        size_t idx = size_t(y) * S + x;
                        if (z <= depth[idx]) continue;
                        depth[idx] = z;
                        normal[idx] = wn[i0] * w0 + wn[i1] * w1 + wn[i2] * w2;
                        look[idx] = lk;
                    }
            }
        }
    }

    // shade (tint independent) and resolve 2x2
    Shade out;
    size_t n = size_t(kSize) * kSize;
    out.a.assign(n, Vec3(0));
    out.b.assign(n, Vec3(0));
    out.alpha.assign(n, 0.0f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            size_t idx = size_t(y) * S + x;
            const SlotLook* lk = look[idx];
            if (!lk) continue;
            Vec3 N = normal[idx].normalized();
            if (dot(N, dirV) < 0.0f) N = N - dirV * (2.0f * dot(N, dirV));
            Vec3 R = dirV * -1.0f + N * (2.0f * dot(N, dirV));
            float NoV = clampf(dot(N, dirV), 0.0f, 1.0f);
            float fres = std::pow(1.0f - NoV, 5.0f);
            float diffuse = 0.3f + 0.12f * dot(N, up) + 1.05f * std::max(dot(N, st.key), 0.0f) + 0.4f * std::max(dot(N, st.fill), 0.0f) +
                            0.25f * std::max(dot(N, st.rim), 0.0f);
            float refl = st.env(R, lk->rough);
            Vec3 A(0), B(0);
            if (lk->metal > 0.5f) {
                // anodised / polished metal: the tint colours the reflection
                Vec3 spec(refl * (0.72f + 0.28f * fres));
                if (lk->tinted) {
                    A = spec + Vec3(diffuse * 0.08f);
                    B = Vec3(refl * fres * 0.12f);
                } else {
                    B = lk->base * (refl * 0.9f + diffuse * 0.08f) + Vec3(refl * fres * 0.1f);
                }
            } else {
                float spec = (0.04f + 0.96f * fres) * refl * (1.0f - lk->rough * 0.7f);
                if (lk->tinted) {
                    A = Vec3(diffuse * 0.62f);
                    B = Vec3(spec);
                } else {
                    B = lk->base * (diffuse * 0.62f) + Vec3(spec);
                }
            }
            size_t o = size_t(y / 2) * kSize + size_t(x / 2);
            out.a[o] += A * 0.25f;
            out.b[o] += B * 0.25f;
            out.alpha[o] += 0.25f;
        }
    return out;
}

const PartThumbnails::Shade* PartThumbnails::shade(Kind kind, int variant) {
    int key = int(kind) * 16 + variant;
    auto it = shades_.find(key);
    if (it != shades_.end()) return &it->second;
    if (renderBudget_ <= 0) return nullptr;
    --renderBudget_;
    return &(shades_[key] = render(kind, variant));
}

Texture* PartThumbnails::get(Kind kind, int variant, const Vec3& color) {
    char key[96];
    std::snprintf(key, sizeof(key), "%d:%d:%.3f,%.3f,%.3f", int(kind), variant, color.x, color.y, color.z);
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second.get();
    if (uploadBudget_ <= 0) return nullptr;
    const Shade* s = shade(kind, variant);
    if (!s) return nullptr;
    --uploadBudget_;
    std::vector<uint8_t> px(size_t(kSize) * kSize * 4);
    for (size_t i = 0; i < size_t(kSize) * kSize; ++i) {
        float a = s->alpha[i];
        Vec3 c(0);
        if (a > 0.0f) c = tonemap((color * s->a[i] + s->b[i]) / a);
        px[i * 4 + 0] = uint8_t(clampf(c.x, 0.0f, 1.0f) * 255.0f + 0.5f);
        px[i * 4 + 1] = uint8_t(clampf(c.y, 0.0f, 1.0f) * 255.0f + 0.5f);
        px[i * 4 + 2] = uint8_t(clampf(c.z, 0.0f, 1.0f) * 255.0f + 0.5f);
        px[i * 4 + 3] = uint8_t(clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    TexturePtr t = createTextureRGBA8(std::string("thumb ") + key, kSize, kSize, px.data(), false, false);
    textures_[key] = t;
    return t.get();
}

}  // namespace sw
