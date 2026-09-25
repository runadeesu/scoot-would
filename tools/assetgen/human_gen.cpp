// scoot would asset tools - realistic rider built on the MakeHuman "hm08" base mesh (CC0 assets,
// vendored in third_party/makehuman, see LICENSE.ASSETS.md there).
//
// Pipeline:
//   1. load the base mesh, apply shape targets (young athletic male, mixed ethnicity)
//   2. convert to game space (metres, facing -Z, left = -X), normalise height, feet on y = 0
//   3. re-pose the A-pose arms into our rest pose with MakeHuman's own skin weights (LBS)
//   4. build the game skeleton (core rider joints + clavicles, toes, fingers, jaw, eyes, lids) from
//      the MakeHuman joint helpers and remap the MakeHuman weights onto it by bone ancestry
//   5. fit the textured high-poly eyes (mhclo proxy), bake skin / hair textures in UV space,
//      derive clothing, shoes and headwear from the body surface
//   6. write rider.glb with the shared animation clips (rider_clips.cpp)
#include "assetgen.h"
#include "gltf_writer.h"
#include "rider_clips.h"

#include "core/json.h"
#include "game/player/rider_blueprint.h"
#include "render/mesh.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace sw::tools {
namespace {

// ---------------------------------------------------------------------------------------------
// MakeHuman data

struct ObjFace {
    int v[4], t[4];
    int n = 3;
    int group = -1;
};
struct ObjData {
    std::vector<Vec3> v;
    std::vector<Vec2> vt;
    std::vector<ObjFace> f;
    std::vector<std::string> groups;
};

bool loadObj(const std::string& path, ObjData& o) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    int group = -1;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            Vec3 p;
            std::sscanf(line.c_str() + 2, "%f %f %f", &p.x, &p.y, &p.z);
            o.v.push_back(p);
        } else if (line[0] == 'v' && line[1] == 't') {
            Vec2 t;
            std::sscanf(line.c_str() + 3, "%f %f", &t.x, &t.y);
            o.vt.push_back(t);
        } else if (line[0] == 'g' && line[1] == ' ') {
            std::string g = line.substr(2);
            while (!g.empty() && (g.back() == '\r' || g.back() == ' ')) g.pop_back();
            o.groups.push_back(g);
            group = int(o.groups.size()) - 1;
        } else if (line[0] == 'f' && line[1] == ' ') {
            ObjFace f;
            f.group = group;
            std::istringstream ss(line.substr(2));
            std::string tok;
            int k = 0;
            while (ss >> tok && k < 4) {
                int vi = 0, ti = 0;
                if (std::sscanf(tok.c_str(), "%d/%d", &vi, &ti) < 1) continue;
                f.v[k] = vi - 1;
                f.t[k] = ti > 0 ? ti - 1 : -1;
                ++k;
            }
            f.n = k;
            if (k >= 3) o.f.push_back(f);
        }
    }
    return !o.v.empty();
}

bool applyTarget(const std::string& path, float w, std::vector<Vec3>& v) {
    std::ifstream in(path);
    if (!in) {
        std::printf("human: missing target %s\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        int i;
        Vec3 d;
        if (std::sscanf(line.c_str(), "%d %f %f %f", &i, &d.x, &d.y, &d.z) == 4 && i >= 0 && size_t(i) < v.size()) v[size_t(i)] += d * w;
    }
    return true;
}

struct MHBone {
    std::string name, parent, headJoint, tailJoint;
    int parentIdx = -1;
    Vec3 head, tail;
};
struct MHSkel {
    std::vector<MHBone> bones;  // parents before children
    std::map<std::string, int> index;
    std::map<std::string, std::vector<int>> joints;
    int find(const std::string& n) const {
        auto it = index.find(n);
        return it == index.end() ? -1 : it->second;
    }
};

Vec3 jointPos(const MHSkel& s, const std::string& j, const std::vector<Vec3>& v) {
    auto it = s.joints.find(j);
    if (it == s.joints.end() || it->second.empty()) return Vec3(0);
    Vec3 c(0);
    for (int i : it->second) c += v[size_t(i)];
    return c / float(it->second.size());
}

bool loadSkel(const std::string& path, MHSkel& s) {
    auto j = loadJsonFile(path);
    if (!j) return false;
    for (auto& [name, list] : (*j)["joints"].items()) {
        std::vector<int> idx;
        for (auto& x : list) idx.push_back(x.get<int>());
        s.joints[name] = idx;
    }
    // topological order: repeatedly add bones whose parent is placed
    std::map<std::string, Json> raw;
    for (auto& [name, b] : (*j)["bones"].items()) raw[name] = b;
    std::set<std::string> placed;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& [name, b] : raw) {
            if (placed.count(name)) continue;
            std::string parent = b["parent"].is_string() ? b["parent"].get<std::string>() : "";
            if (!parent.empty() && !placed.count(parent)) continue;
            MHBone bone;
            bone.name = name;
            bone.parent = parent;
            bone.headJoint = b["head"].get<std::string>();
            bone.tailJoint = b["tail"].get<std::string>();
            bone.parentIdx = parent.empty() ? -1 : s.index[parent];
            s.index[name] = int(s.bones.size());
            s.bones.push_back(bone);
            placed.insert(name);
            progress = true;
        }
    }
    return !s.bones.empty();
}

using MHWeights = std::map<std::string, std::vector<std::pair<int, float>>>;
bool loadWeights(const std::string& path, MHWeights& w) {
    auto j = loadJsonFile(path);
    if (!j) return false;
    for (auto& [bone, list] : (*j)["weights"].items()) {
        auto& dst = w[bone];
        for (auto& e : list) dst.push_back({e[0].get<int>(), e[1].get<float>()});
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// game skeleton

struct GJoint {
    std::string name, parent, mhBone;
    Vec3 pos;
    int parentIdx = -1;
};

std::vector<GJoint> gameJoints() {
    std::vector<GJoint> j = {
        {"root", "", ""},          {"pelvis", "root", "root"},   {"spine", "pelvis", "spine04"}, {"chest", "spine", "spine02"},
        {"neck", "chest", "neck01"}, {"head", "neck", "head"},     {"jaw", "head", "jaw"},
    };
    for (const char* s : {"l", "r"}) {
        std::string S = s[0] == 'l' ? "L" : "R";
        auto add = [&](const std::string& n, const std::string& p, const std::string& mh) { j.push_back({n + "_" + s, p.empty() ? "" : p, mh + "." + S}); };
        add("eye", "", "eye");
        j.back().parent = "head";
        add("lid_upper", "", "orbicularis03");
        j.back().parent = "head";
        add("lid_lower", "", "orbicularis04");
        j.back().parent = "head";
        add("clavicle", "", "clavicle");
        j.back().parent = "chest";
        add("upperarm", std::string("clavicle_") + s, "upperarm01");
        add("lowerarm", std::string("upperarm_") + s, "lowerarm01");
        add("hand", std::string("lowerarm_") + s, "wrist");
        const char* fingers[5] = {"thumb", "index", "middle", "ring", "pinky"};
        for (int f = 0; f < 5; ++f)
            for (int k = 1; k <= 3; ++k) {
                std::string n = std::string(fingers[f]) + std::to_string(k);
                std::string parent = k == 1 ? std::string("hand_") + s : std::string(fingers[f]) + std::to_string(k - 1) + "_" + s;
                add(n, parent, "finger" + std::to_string(f + 1) + "-" + std::to_string(k));
            }
        add("thigh", "pelvis", "upperleg01");
        add("shin", std::string("thigh_") + s, "lowerleg01");
        add("foot", std::string("shin_") + s, "foot");
        add("toes", std::string("foot_") + s, "toe3-1");
    }
    return j;
}

// MakeHuman bone -> game joint for the key bones; every other bone inherits from its nearest mapped
// ancestor (face details -> head, lower lip -> jaw, metacarpals -> hand, ...)
std::string keyMapping(const std::string& b) {
    auto side = [&]() { return b.size() > 2 && b[b.size() - 2] == '.' ? std::string(b.back() == 'L' ? "_l" : "_r") : std::string(); };
    std::string base = b.substr(0, b.find('.'));
    if (base == "root" || base == "spine05" || base == "pelvis") return "pelvis";
    if (base == "spine04" || base == "spine03") return "spine";
    if (base == "spine02" || base == "spine01" || base == "breast") return "chest";
    if (base == "neck01" || base == "neck02" || base == "neck03") return "neck";
    if (base == "head") return "head";
    if (base == "jaw") return "jaw";
    if (base == "eye") return "eye" + side();
    if (base == "orbicularis03") return "lid_upper" + side();
    if (base == "orbicularis04") return "lid_lower" + side();
    if (base == "clavicle" || base == "shoulder01") return "clavicle" + side();
    if (base == "upperarm01" || base == "upperarm02") return "upperarm" + side();
    if (base == "lowerarm01" || base == "lowerarm02") return "lowerarm" + side();
    if (base == "wrist" || base.rfind("metacarpal", 0) == 0) return "hand" + side();
    if (base.rfind("finger", 0) == 0) {
        const char* names[5] = {"thumb", "index", "middle", "ring", "pinky"};
        int f = base[6] - '1', k = base[8] - '0';
        return std::string(names[std::clamp(f, 0, 4)]) + std::to_string(std::clamp(k, 1, 3)) + side();
    }
    if (base == "upperleg01" || base == "upperleg02") return "thigh" + side();
    if (base == "lowerleg01" || base == "lowerleg02") return "shin" + side();
    if (base == "foot") return "foot" + side();
    if (base.rfind("toe", 0) == 0) return "toes" + side();
    return "";
}

// ---------------------------------------------------------------------------------------------
// output helpers

struct Skinned {
    uint8_t j[4] = {0, 0, 0, 0};
    float w[4] = {1, 0, 0, 0};
};

Skinned topFour(const std::vector<std::pair<int, float>>& in) {
    std::vector<std::pair<int, float>> v = in;
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    Skinned s;
    float sum = 0.0f;
    for (size_t k = 0; k < 4 && k < v.size(); ++k) sum += v[k].second;
    for (size_t k = 0; k < 4; ++k) {
        if (k < v.size() && sum > 0.0f) {
            s.j[k] = uint8_t(v[k].first);
            s.w[k] = v[k].second / sum;
        } else {
            s.j[k] = k < v.size() ? uint8_t(v[k].first) : 0;
            s.w[k] = (k == 0 && sum <= 0.0f) ? 1.0f : 0.0f;
        }
    }
    return s;
}

// triangle soup with per corner position index (for welded normals) + uv
struct Part {
    std::vector<Vec3> p, n;
    std::vector<Vec2> uv;
    std::vector<Skinned> sk;
    std::vector<uint32_t> idx;
};

void addPart(GMesh& gm, int material, Part& part) {
    MeshData md;
    md.vertices.resize(part.p.size());
    for (size_t i = 0; i < part.p.size(); ++i) {
        md.vertices[i].position = part.p[i];
        md.vertices[i].normal = part.n[i];
        md.vertices[i].uv = part.uv[i];
    }
    md.indices = part.idx;
    md.computeTangents();
    GPrimitive prim;
    prim.material = material;
    for (size_t i = 0; i < md.vertices.size(); ++i) {
        GVertex gv;
        gv.pos = md.vertices[i].position;
        gv.normal = md.vertices[i].normal;
        gv.tangent = md.vertices[i].tangent;
        gv.uv = md.vertices[i].uv;
        for (int k = 0; k < 4; ++k) {
            gv.joints[k] = part.sk[i].j[k];
            gv.weights[k] = part.sk[i].w[k];
        }
        prim.vertices.push_back(gv);
    }
    prim.indices = md.indices;
    gm.primitives.push_back(std::move(prim));
    gm.skinned = true;
}

std::vector<Vec3> smoothNormals(const std::vector<Vec3>& v, const std::vector<std::array<int, 3>>& tris) {
    std::vector<Vec3> n(v.size(), Vec3(0));
    for (auto& t : tris) {
        Vec3 fn = cross(v[size_t(t[1])] - v[size_t(t[0])], v[size_t(t[2])] - v[size_t(t[0])]);
        for (int k = 0; k < 3; ++k) n[size_t(t[size_t(k)])] += fn;
    }
    for (auto& x : n) x = x.lengthSq() > 1e-20f ? x.normalized() : Vec3(0, 1, 0);
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------------------------

struct HumanModel {
    ObjData obj;
    std::vector<Vec3> v;                 // game space, re-posed
    std::vector<Vec3> nrm;               // smooth normals (vertex level)
    std::vector<std::array<int, 3>> tris;        // body triangles (vertex indices)
    std::vector<std::array<int, 3>> triUV;       // matching uv indices
    std::vector<int> triGroup;
    std::vector<GJoint> joints;
    std::map<std::string, int> jointIndex;
    std::vector<std::vector<std::pair<int, float>>> weights;  // per vertex (game joints)
    std::vector<int> dominant;           // dominant game joint per vertex
    MHSkel skel;
    int J(const std::string& n) const {
        auto it = jointIndex.find(n);
        return it == jointIndex.end() ? -1 : it->second;
    }
    Vec3 jp(const std::string& n) const {
        int i = J(n);
        return i >= 0 ? joints[size_t(i)].pos : Vec3(0);
    }
};

bool buildHuman(const std::string& mhDir, HumanModel& H, float targetHeight) {
    if (!loadObj(mhDir + "/base.obj", H.obj)) {
        std::printf("human: cannot load %s/base.obj\n", mhDir.c_str());
        return false;
    }
    std::vector<Vec3> v = H.obj.v;
    // young athletic male of mixed ethnicity (skin tone is customisable at runtime)
    const std::string T = mhDir + "/targets/macrodetails/";
    applyTarget(T + "caucasian-male-young.target", 0.55f, v);
    applyTarget(T + "african-male-young.target", 0.2f, v);
    applyTarget(T + "asian-male-young.target", 0.25f, v);
    applyTarget(T + "universal-male-young-maxmuscle-averageweight.target", 0.38f, v);
    applyTarget(T + "universal-male-young-averagemuscle-minweight.target", 0.18f, v);
    applyTarget(T + "male-young-averagemuscle-averageweight-maxheight.target", 0.2f, v);
    applyTarget(T + "male-young-averagemuscle-averageweight-idealproportions.target", 0.6f, v);

    if (!loadSkel(mhDir + "/rigs/default.mhskel", H.skel)) return false;
    MHWeights mw;
    if (!loadWeights(mhDir + "/rigs/default_weights.mhw", mw)) return false;

    // game space: metres, facing -Z (MakeHuman faces +Z), character left = -X
    for (auto& p : v) p = Vec3(-p.x, p.y, -p.z) * 0.1f;
    // body extents (body group only)
    int bodyGroup = -1;
    for (size_t g = 0; g < H.obj.groups.size(); ++g)
        if (H.obj.groups[g] == "body") bodyGroup = int(g);
    std::vector<char> isBody(v.size(), 0);
    for (auto& f : H.obj.f)
        if (f.group == bodyGroup)
            for (int k = 0; k < f.n; ++k) isBody[size_t(f.v[k])] = 1;
    float ymin = 1e9f, ymax = -1e9f;
    for (size_t i = 0; i < v.size(); ++i)
        if (isBody[i]) {
            ymin = std::min(ymin, v[i].y);
            ymax = std::max(ymax, v[i].y);
        }
    float s = targetHeight / (ymax - ymin);
    Vec3 pel = jointPos(H.skel, H.skel.bones[size_t(H.skel.find("root"))].headJoint.empty() ? "" : H.skel.bones[size_t(H.skel.find("root"))].headJoint, v);
    for (auto& p : v) p = Vec3((p.x - pel.x) * s, (p.y - ymin) * s, (p.z - pel.z) * s);
    for (auto& b : H.skel.bones) {
        b.head = jointPos(H.skel, b.headJoint, v);
        b.tail = jointPos(H.skel, b.tailJoint, v);
    }

    // re-pose: arms from the A-pose down to the rest pose of the rider clips (upper arm hangs ~12
    // degrees out, elbow slightly bent forward), using MakeHuman's skin weights
    const RiderJointDef* bp = riderJoints();
    std::map<std::string, Quat> rot;
    for (int side = 0; side < 2; ++side) {
        std::string S = side == 0 ? ".L" : ".R";
        int ua = side == 0 ? RJ_UpperArmL : RJ_UpperArmR, la = side == 0 ? RJ_LowerArmL : RJ_LowerArmR, ha = side == 0 ? RJ_HandL : RJ_HandR;
        Vec3 tUp = (bp[la].restWorld - bp[ua].restWorld).normalized();
        Vec3 tFore = (bp[ha].restWorld - bp[la].restWorld).normalized();
        const MHBone& up = H.skel.bones[size_t(H.skel.find("upperarm01" + S))];
        const MHBone& lo = H.skel.bones[size_t(H.skel.find("lowerarm01" + S))];
        const MHBone& wr = H.skel.bones[size_t(H.skel.find("wrist" + S))];
        Vec3 dUp = (lo.head - up.head).normalized(), dFore = (wr.head - lo.head).normalized();
        Quat rUp = Quat::fromTo(dUp, tUp);
        Quat rFore = Quat::fromTo(dFore, rUp.conjugate() * tFore);
        rot["upperarm01" + S] = rUp;
        rot["lowerarm01" + S] = rFore;
        // legs: straight down under the hip joints (MakeHuman stands with the feet apart)
        int th = side == 0 ? RJ_ThighL : RJ_ThighR, sh = side == 0 ? RJ_ShinL : RJ_ShinR, ft = side == 0 ? RJ_FootL : RJ_FootR;
        Vec3 tTh = (bp[sh].restWorld - bp[th].restWorld).normalized();
        Vec3 tSh = (bp[ft].restWorld - bp[sh].restWorld).normalized();
        const MHBone& ul = H.skel.bones[size_t(H.skel.find("upperleg01" + S))];
        const MHBone& ll = H.skel.bones[size_t(H.skel.find("lowerleg01" + S))];
        const MHBone& fo = H.skel.bones[size_t(H.skel.find("foot" + S))];
        Quat rTh = Quat::fromTo((ll.head - ul.head).normalized(), tTh);
        Quat rSh = Quat::fromTo((fo.head - ll.head).normalized(), rTh.conjugate() * tSh);
        rot["upperleg01" + S] = rTh;
        rot["lowerleg01" + S] = rSh;
    }
    std::vector<Mat4> W(H.skel.bones.size(), Mat4::identity());
    for (size_t b = 0; b < H.skel.bones.size(); ++b) {
        const MHBone& bone = H.skel.bones[b];
        Mat4 parent = bone.parentIdx >= 0 ? W[size_t(bone.parentIdx)] : Mat4::identity();
        auto it = rot.find(bone.name);
        Mat4 local = it == rot.end() ? Mat4::identity()
                                     : Mat4::translation(bone.head) * Mat4::rotation(it->second) * Mat4::translation(-bone.head);
        W[b] = parent * local;
    }
    std::vector<Vec3> acc(v.size(), Vec3(0));
    std::vector<float> wsum(v.size(), 0.0f);
    for (auto& [bone, list] : mw) {
        int bi = H.skel.find(bone);
        if (bi < 0) continue;
        for (auto& [vi, w] : list) {
            if (vi < 0 || size_t(vi) >= v.size()) continue;
            acc[size_t(vi)] += W[size_t(bi)].transformPoint(v[size_t(vi)]) * w;
            wsum[size_t(vi)] += w;
        }
    }
    for (size_t i = 0; i < v.size(); ++i)
        if (wsum[i] > 1e-6f) v[i] = acc[i] / wsum[i];
    for (size_t b = 0; b < H.skel.bones.size(); ++b) {
        MHBone& bone = H.skel.bones[b];
        Mat4 parent = bone.parentIdx >= 0 ? W[size_t(bone.parentIdx)] : Mat4::identity();
        bone.head = parent.transformPoint(bone.head);
        bone.tail = W[b].transformPoint(bone.tail);
    }
    H.v = v;

    // game skeleton from the (re-posed) MakeHuman bones
    H.joints = gameJoints();
    for (size_t i = 0; i < H.joints.size(); ++i) H.jointIndex[H.joints[i].name] = int(i);
    for (auto& j : H.joints) {
        j.parentIdx = j.parent.empty() ? -1 : H.jointIndex[j.parent];
        if (j.mhBone.empty()) {
            j.pos = Vec3(0);
            continue;
        }
        int b = H.skel.find(j.mhBone);
        j.pos = b >= 0 ? H.skel.bones[size_t(b)].head : Vec3(0);
    }
    // toes: ball of the foot = mean of the toe 1..5 bases
    for (const char* s2 : {"l", "r"}) {
        std::string S = s2[0] == 'l' ? ".L" : ".R";
        Vec3 c(0);
        for (int t = 1; t <= 5; ++t) c += H.skel.bones[size_t(H.skel.find("toe" + std::to_string(t) + "-1" + S))].head;
        H.joints[size_t(H.J(std::string("toes_") + s2))].pos = c / 5.0f;
    }

    // weights: MakeHuman bone -> nearest mapped ancestor -> game joint
    std::vector<int> boneToJoint(H.skel.bones.size(), -1);
    for (size_t b = 0; b < H.skel.bones.size(); ++b) {
        int cur = int(b);
        while (cur >= 0) {
            std::string m = keyMapping(H.skel.bones[size_t(cur)].name);
            if (!m.empty() && H.J(m) >= 0) {
                boneToJoint[b] = H.J(m);
                break;
            }
            cur = H.skel.bones[size_t(cur)].parentIdx;
        }
        if (boneToJoint[b] < 0) boneToJoint[b] = H.J("pelvis");
    }
    std::vector<std::map<int, float>> wm(v.size());
    for (auto& [bone, list] : mw) {
        int bi = H.skel.find(bone);
        if (bi < 0) continue;
        for (auto& [vi, w] : list)
            if (vi >= 0 && size_t(vi) < v.size()) wm[size_t(vi)][boneToJoint[size_t(bi)]] += w;
    }
    H.weights.resize(v.size());
    H.dominant.assign(v.size(), H.J("pelvis"));
    for (size_t i = 0; i < v.size(); ++i) {
        float best = -1.0f;
        for (auto& [j, w] : wm[i]) {
            H.weights[i].push_back({j, w});
            if (w > best) {
                best = w;
                H.dominant[i] = j;
            }
        }
        if (H.weights[i].empty()) H.weights[i].push_back({H.J("pelvis"), 1.0f});
    }

    // body triangles
    for (auto& f : H.obj.f) {
        if (f.group != bodyGroup) continue;
        for (int k = 1; k + 1 < f.n; ++k) {
            H.tris.push_back({f.v[0], f.v[k], f.v[k + 1]});
            H.triUV.push_back({f.t[0], f.t[k], f.t[k + 1]});
        }
    }
    H.nrm = smoothNormals(H.v, H.tris);
    std::printf("human: %zu verts, %zu body triangles, %zu joints, height %.2f m\n", v.size(), H.tris.size(), H.joints.size(), targetHeight);
    return true;
}

namespace {

// eyes: MakeHuman high-poly eye proxy fitted onto the base mesh helper (mhclo: 3 reference vertices
// with barycentric weights + a scaled offset per proxy vertex)
bool fitEyes(const std::string& mhDir, const HumanModel& H, Part& out, int eyeL, int eyeR) {
    ObjData eo;
    if (!loadObj(mhDir + "/eyes/high-poly.obj", eo)) return false;
    std::ifstream in(mhDir + "/eyes/high-poly.mhclo");
    if (!in) return false;
    struct Ref {
        int v[3];
        float w[3];
        Vec3 d;
    };
    std::vector<Ref> refs;
    float scale[3] = {1, 1, 1};
    bool inVerts = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        int a, b;
        float d;
        char axis;
        if (std::sscanf(line.c_str(), "%c_scale %d %d %f", &axis, &a, &b, &d) == 4) {
            int k = axis == 'x' ? 0 : axis == 'y' ? 1 : 2;
            // distance between the two reference vertices now vs. in the reference mesh (in game
            // metres vs. MakeHuman decimetres: the ratio carries the unit change)
            float cur = std::fabs(H.v[size_t(a)][k] - H.v[size_t(b)][k]);
            scale[k] = cur / d;
            continue;
        }
        if (line.rfind("verts", 0) == 0) {
            inVerts = true;
            continue;
        }
        if (!inVerts) continue;
        Ref r;
        if (std::sscanf(line.c_str(), "%d %d %d %f %f %f %f %f %f", &r.v[0], &r.v[1], &r.v[2], &r.w[0], &r.w[1], &r.w[2], &r.d.x, &r.d.y, &r.d.z) == 9)
            refs.push_back(r);
        else if (!refs.empty())
            break;
    }
    if (refs.size() != eo.v.size()) {
        std::printf("human: eye proxy mismatch (%zu refs, %zu verts)\n", refs.size(), eo.v.size());
        return false;
    }
    std::vector<Vec3> pv(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        const Ref& r = refs[i];
        Vec3 p = H.v[size_t(r.v[0])] * r.w[0] + H.v[size_t(r.v[1])] * r.w[1] + H.v[size_t(r.v[2])] * r.w[2];
        // offsets are in MakeHuman axes: x and z flip in game space
        p += Vec3(-r.d.x * scale[0], r.d.y * scale[1], -r.d.z * scale[2]);
        pv[i] = p;
    }
    std::vector<std::array<int, 3>> tris, tuv;
    for (auto& f : eo.f) {
        // skip the transparent cornea shell (mapped to the white disc in the texture corner): the
        // eyeball itself is rendered glossy instead
        bool cornea = true;
        for (int k = 0; k < f.n; ++k) {
            Vec2 t = f.t[k] >= 0 ? eo.vt[size_t(f.t[k])] : Vec2(0);
            if (!(t.x > 0.84f && t.y < 0.16f)) cornea = false;
        }
        if (cornea) continue;
        for (int k = 1; k + 1 < f.n; ++k) {
            tris.push_back({f.v[0], f.v[k], f.v[k + 1]});
            tuv.push_back({f.t[0], f.t[k], f.t[k + 1]});
        }
    }
    auto n = smoothNormals(pv, tris);
    std::map<std::pair<int, int>, uint32_t> remap;
    for (size_t t = 0; t < tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            auto key = std::make_pair(tris[t][size_t(k)], tuv[t][size_t(k)]);
            auto it = remap.find(key);
            if (it == remap.end()) {
                uint32_t id = uint32_t(out.p.size());
                int vi = key.first;
                out.p.push_back(pv[size_t(vi)]);
                out.n.push_back(n[size_t(vi)]);
                Vec2 uv = key.second >= 0 ? eo.vt[size_t(key.second)] : Vec2(0);
                out.uv.push_back(Vec2(uv.x, 1.0f - uv.y));
                Skinned sk;
                sk.j[0] = uint8_t(pv[size_t(vi)].x < 0.0f ? eyeL : eyeR);
                out.sk.push_back(sk);
                it = remap.emplace(key, id).first;
            }
            out.idx.push_back(it->second);
        }
    return true;
}

}  // namespace

namespace {

// ---------------------------------------------------------------------------------------------
// texture baking helpers

struct Tex {
    int w = 0, h = 0;
    std::vector<Vec4> px;
    std::vector<uint8_t> set;
    Tex(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * size_t(h_), Vec4(0)), set(size_t(w_) * size_t(h_), 0) {}
    Vec4& at(int x, int y) { return px[size_t(y) * size_t(w) + size_t(x)]; }
    // fill unset texels from set neighbours (padding across UV seams, mip safe)
    void dilate(int passes) {
        for (int p = 0; p < passes; ++p) {
            std::vector<Vec4> np = px;
            std::vector<uint8_t> ns = set;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    size_t i = size_t(y) * size_t(w) + size_t(x);
                    if (set[i]) continue;
                    Vec4 sum(0);
                    int n = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                            size_t j = size_t(yy) * size_t(w) + size_t(xx);
                            if (set[j]) {
                                sum = Vec4(sum.x + px[j].x, sum.y + px[j].y, sum.z + px[j].z, sum.w + px[j].w);
                                ++n;
                            }
                        }
                    if (n) {
                        float inv = 1.0f / float(n);
                        np[i] = Vec4(sum.x * inv, sum.y * inv, sum.z * inv, sum.w * inv);
                        ns[i] = 1;
                    }
                }
            px.swap(np);
            set.swap(ns);
        }
    }
    bool save(const std::string& path, bool srgb, int channels) const {
        std::vector<uint8_t> out(size_t(w) * size_t(h) * size_t(channels));
        for (size_t i = 0; i < px.size(); ++i)
            for (int c = 0; c < channels; ++c) {
                float v = clampf(px[i][c], 0.0f, 1.0f);
                if (srgb && c < 3) v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
                out[i * size_t(channels) + size_t(c)] = uint8_t(std::lround(v * 255.0f));
            }
        return stbi_write_png(path.c_str(), w, h, channels, out.data(), w * channels) != 0;
    }
};

// rasterise a UV triangle, calling fn(x, y, l0, l1, l2) for covered texel centres (+ a 1 texel
// conservative margin so edges are fully covered)
template <typename F>
void rasterUV(int W, int Hh, Vec2 a, Vec2 b, Vec2 c, F&& fn) {
    Vec2 A(a.x * float(W), a.y * float(Hh)), B(b.x * float(W), b.y * float(Hh)), C(c.x * float(W), c.y * float(Hh));
    float area = (B.x - A.x) * (C.y - A.y) - (C.x - A.x) * (B.y - A.y);
    if (std::fabs(area) < 1e-9f) return;
    int x0 = std::max(0, int(std::floor(std::min({A.x, B.x, C.x}))) - 1), x1 = std::min(W - 1, int(std::ceil(std::max({A.x, B.x, C.x}))) + 1);
    int y0 = std::max(0, int(std::floor(std::min({A.y, B.y, C.y}))) - 1), y1 = std::min(Hh - 1, int(std::ceil(std::max({A.y, B.y, C.y}))) + 1);
    float eps = 1.2f / std::sqrt(std::fabs(area));  // ~1 texel margin in barycentric units
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            Vec2 P(float(x) + 0.5f, float(y) + 0.5f);
            float l1 = ((P.x - A.x) * (C.y - A.y) - (C.x - A.x) * (P.y - A.y)) / area;
            float l2 = ((B.x - A.x) * (P.y - A.y) - (P.x - A.x) * (B.y - A.y)) / area;
            float l0 = 1.0f - l1 - l2;
            if (l0 < -eps || l1 < -eps || l2 < -eps) continue;
            // clamp the margin samples back onto the triangle
            l0 = std::max(l0, 0.0f);
            l1 = std::max(l1, 0.0f);
            l2 = std::max(l2, 0.0f);
            float sum = l0 + l1 + l2;
            fn(x, y, l0 / sum, l1 / sum, l2 / sum);
        }
}

float hash31(Vec3 p) {
    float h = std::sin(p.x * 127.1f + p.y * 311.7f + p.z * 74.7f) * 43758.5453f;
    return h - std::floor(h);
}
float vnoise3(Vec3 p) {
    Vec3 i(std::floor(p.x), std::floor(p.y), std::floor(p.z));
    Vec3 f = p - i;
    f = Vec3(f.x * f.x * (3 - 2 * f.x), f.y * f.y * (3 - 2 * f.y), f.z * f.z * (3 - 2 * f.z));
    auto h = [&](float dx, float dy, float dz) { return hash31(i + Vec3(dx, dy, dz)); };
    float x00 = lerpf(h(0, 0, 0), h(1, 0, 0), f.x), x10 = lerpf(h(0, 1, 0), h(1, 1, 0), f.x);
    float x01 = lerpf(h(0, 0, 1), h(1, 0, 1), f.x), x11 = lerpf(h(0, 1, 1), h(1, 1, 1), f.x);
    return lerpf(lerpf(x00, x10, f.y), lerpf(x01, x11, f.y), f.z);
}
float fbm3(Vec3 p, int oct = 4) {
    float s = 0, a = 0.5f, n = 0;
    for (int o = 0; o < oct; ++o) {
        s += a * vnoise3(p);
        n += a;
        p = p * 2.03f + Vec3(17.1f, 9.3f, 3.7f);
        a *= 0.5f;
    }
    return s / n;
}
// sparse dots (freckles, stubble, pores): 1 inside a random dot of the cell grid
float dots(Vec3 p, float cell, float radius, float density, float seed) {
    Vec3 q = p / cell;
    Vec3 i(std::floor(q.x), std::floor(q.y), std::floor(q.z));
    float best = 0.0f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                Vec3 c = i + Vec3(float(dx), float(dy), float(dz));
                if (hash31(c + Vec3(seed)) > density) continue;
                Vec3 o = c + Vec3(hash31(c + Vec3(1.3f + seed)), hash31(c + Vec3(7.1f + seed)), hash31(c + Vec3(3.7f + seed)));
                float d = (q - o).length() * cell;
                best = std::max(best, 1.0f - smoothstep(radius * 0.5f, radius, d));
            }
    return best;
}

// uniform grid over triangles for occlusion rays
struct TriGrid {
    Vec3 mn, mx, cs;
    int n = 48;
    std::vector<std::vector<int>> cells;
    const std::vector<Vec3>* v = nullptr;
    const std::vector<std::array<int, 3>>* t = nullptr;
    void build(const std::vector<Vec3>& verts, const std::vector<std::array<int, 3>>& tris) {
        v = &verts;
        t = &tris;
        mn = Vec3(1e9f);
        mx = Vec3(-1e9f);
        for (auto& tr : tris)
            for (int k = 0; k < 3; ++k) {
                mn = vmin(mn, verts[size_t(tr[size_t(k)])]);
                mx = vmax(mx, verts[size_t(tr[size_t(k)])]);
            }
        mn -= Vec3(0.01f);
        mx += Vec3(0.01f);
        cs = (mx - mn) / float(n);
        cells.assign(size_t(n * n * n), {});
        for (size_t ti = 0; ti < tris.size(); ++ti) {
            Vec3 a = verts[size_t(tris[ti][0])], b = verts[size_t(tris[ti][1])], c = verts[size_t(tris[ti][2])];
            Vec3 lo = vmin(a, vmin(b, c)), hi = vmax(a, vmax(b, c));
            int x0 = cellOf(lo.x, 0), x1 = cellOf(hi.x, 0), y0 = cellOf(lo.y, 1), y1 = cellOf(hi.y, 1), z0 = cellOf(lo.z, 2), z1 = cellOf(hi.z, 2);
            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) cells[size_t((z * n + y) * n + x)].push_back(int(ti));
        }
    }
    int cellOf(float p, int axis) const { return std::clamp(int((p - mn[axis]) / cs[axis]), 0, n - 1); }
    // any hit within maxT (Moller-Trumbore), marching the ray through cells
    bool occluded(Vec3 o, Vec3 d, float maxT, int skipA) const {
        float step = std::min(cs.x, std::min(cs.y, cs.z)) * 0.5f;
        int last = -1;
        for (float s = 0.0f; s < maxT + step; s += step) {
            Vec3 p = o + d * s;
            if (p.x < mn.x || p.y < mn.y || p.z < mn.z || p.x > mx.x || p.y > mx.y || p.z > mx.z) return false;
            int ci = (cellOf(p.z, 2) * n + cellOf(p.y, 1)) * n + cellOf(p.x, 0);
            if (ci == last) continue;
            last = ci;
            for (int ti : cells[size_t(ci)]) {
                const auto& tr = (*t)[size_t(ti)];
                if (tr[0] == skipA || tr[1] == skipA || tr[2] == skipA) continue;
                Vec3 a = (*v)[size_t(tr[0])], b = (*v)[size_t(tr[1])], c = (*v)[size_t(tr[2])];
                Vec3 e1 = b - a, e2 = c - a, pv = cross(d, e2);
                float det = dot(e1, pv);
                if (std::fabs(det) < 1e-12f) continue;
                float inv = 1.0f / det;
                Vec3 tv = o - a;
                float u = dot(tv, pv) * inv;
                if (u < 0 || u > 1) continue;
                Vec3 qv = cross(tv, e1);
                float w = dot(d, qv) * inv;
                if (w < 0 || u + w > 1) continue;
                float tt = dot(e2, qv) * inv;
                if (tt > 1e-4f && tt < maxT) return true;
            }
        }
        return false;
    }
};

std::vector<float> vertexAO(const HumanModel& H, int rays, float maxDist) {
    TriGrid g;
    g.build(H.v, H.tris);
    std::vector<char> used(H.v.size(), 0);
    for (auto& t : H.tris)
        for (int k = 0; k < 3; ++k) used[size_t(t[size_t(k)])] = 1;
    std::vector<float> ao(H.v.size(), 1.0f);
    for (size_t i = 0; i < H.v.size(); ++i) {
        if (!used[i]) continue;
        Vec3 n = H.nrm[i];
        Vec3 t = anyPerpendicular(n).normalized(), b = cross(n, t);
        int hit = 0;
        for (int r = 0; r < rays; ++r) {
            // cosine weighted hemisphere (Hammersley)
            float u1 = (float(r) + 0.5f) / float(rays);
            uint32_t bits = uint32_t(r);
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            float u2 = float(bits) * 2.3283064365386963e-10f + hash31(H.v[i] * 97.0f) ;
            u2 -= std::floor(u2);
            float rr = std::sqrt(u1), ph = kTwoPi * u2;
            Vec3 d = (t * (rr * std::cos(ph)) + b * (rr * std::sin(ph)) + n * std::sqrt(std::max(0.0f, 1.0f - u1))).normalized();
            if (g.occluded(H.v[i] + n * 0.0015f, d, maxDist, int(i))) ++hit;
        }
        ao[i] = 1.0f - float(hit) / float(rays);
    }
    return ao;
}

std::vector<Vec3> groupVerts(const HumanModel& H, const std::string& name) {
    std::set<int> idx;
    for (auto& f : H.obj.f)
        if (f.group >= 0 && H.obj.groups[size_t(f.group)] == name)
            for (int k = 0; k < f.n; ++k) idx.insert(f.v[k]);
    std::vector<Vec3> out;
    for (int i : idx) out.push_back(H.v[size_t(i)]);
    return out;
}
Vec3 centroid(const std::vector<Vec3>& p) {
    Vec3 c(0);
    for (auto& x : p) c += x;
    return p.empty() ? c : c / float(p.size());
}

struct Landmarks {
    Vec3 eye[2], eyeMid, mouth, noseTip, chin, headC;
    float mouthHalfW = 0.022f, lipTop = 0.0f, lipBottom = 0.0f;
    float io = 0.063f;
    std::vector<Vec3> lash[2];
    Vec3 nipple[2];
    Vec3 palmN[2], handC[2];
    Vec3 tip[2][5], knuckle[2][5];
    float headHalfW = 0.075f;
};

Landmarks landmarks(const HumanModel& H) {
    Landmarks L;
    L.eye[0] = H.jp("eye_l");
    L.eye[1] = H.jp("eye_r");
    L.eyeMid = (L.eye[0] + L.eye[1]) * 0.5f;
    L.io = (L.eye[0] - L.eye[1]).length();
    // lips from the MakeHuman lip bones (upper lip oris05, lower lip oris01, corners oris03)
    auto bone = [&](const char* n) -> const MHBone& { return H.skel.bones[size_t(H.skel.find(n))]; };
    Vec3 upper = bone("oris05").tail, lower = bone("oris01").tail;
    L.mouth = Vec3(0.0f, (upper.y + lower.y) * 0.5f, std::min(upper.z, lower.z));
    L.mouthHalfW = std::fabs(bone("oris03.L").tail.x) + 0.004f;
    L.lipTop = bone("oris05").head.y + 0.002f;
    L.lipBottom = bone("oris01").head.y - 0.001f;
    L.headC = H.jp("head");
    L.noseTip = L.eyeMid;
    L.chin = bone("jaw").tail;
    float bestN = 1e9f, maxX = 0.0f;
    std::vector<char> used(H.v.size(), 0);
    for (auto& t : H.tris)
        for (int k = 0; k < 3; ++k) used[size_t(t[size_t(k)])] = 1;
    for (size_t i = 0; i < H.v.size(); ++i) {
        if (!used[i]) continue;
        const Vec3& p = H.v[i];
        if (std::fabs(p.x) < 0.008f && p.y < L.eyeMid.y - 0.01f && p.y > L.lipTop + 0.006f && p.z < bestN) {
            bestN = p.z;
            L.noseTip = p;
        }
        if (std::fabs(p.y - L.eyeMid.y) < 0.02f && std::fabs(p.z - L.headC.z) < 0.06f) maxX = std::max(maxX, std::fabs(p.x));
    }
    L.headHalfW = maxX > 0.05f ? maxX : 0.075f;
    std::printf("human: landmarks eyes y %.3f io %.3f | nose tip y %.3f z %.3f | mouth y %.3f z %.3f | chin y %.3f | head half width %.3f\n", L.eyeMid.y, L.io,
                L.noseTip.y, L.noseTip.z, L.mouth.y, L.mouth.z, L.chin.y, L.headHalfW);
    for (int s = 0; s < 2; ++s) {
        std::string g = s == 0 ? "helper-l-eyelashes-" : "helper-r-eyelashes-";
        for (const char* k : {"1", "2"}) {
            auto pts = groupVerts(H, g + k);
            L.lash[s].insert(L.lash[s].end(), pts.begin(), pts.end());
        }
        std::string S = s == 0 ? ".L" : ".R";
        // MakeHuman .L is the character's left = game -X; lash helpers are named by the same side
        L.nipple[s] = jointPos(H.skel, "breast" + S + "____tail", H.v);
        for (int f = 0; f < 5; ++f) {
            const MHBone& b3 = H.skel.bones[size_t(H.skel.find("finger" + std::to_string(f + 1) + "-3" + S))];
            const MHBone& b1 = H.skel.bones[size_t(H.skel.find("finger" + std::to_string(f + 1) + "-1" + S))];
            L.tip[s][f] = b3.tail;
            L.knuckle[s][f] = b1.head;
        }
        Vec3 wrist = H.skel.bones[size_t(H.skel.find("wrist" + S))].head;
        L.handC[s] = (wrist + L.knuckle[s][2]) * 0.5f;
        Vec3 n = cross(L.knuckle[s][1] - L.knuckle[s][4], L.tip[s][2] - wrist).normalized();
        if (dot(n, Vec3(-L.handC[s].x, 0, 0)) < 0.0f) n = -n;  // palm faces the body
        L.palmN[s] = n;
    }
    return L;
}

// skin albedo multiplier (the runtime tint sets the tone) + roughness, from the rest position
struct SkinSample {
    Vec3 col{1, 1, 1};
    float rough = 0.52f;
};

SkinSample skinAt(const Landmarks& L, Vec3 p, Vec3 n) {
    SkinSample s;
    Vec3 c(1.0f);
    auto redden = [&](float a) { c = lerp(c, Vec3(c.x * 1.04f, c.y * 0.86f, c.z * 0.86f), clampf(a, 0.0f, 1.0f)); };
    auto darken = [&](Vec3 tint, float a) { c = lerp(c, Vec3(c.x * tint.x, c.y * tint.y, c.z * tint.z), clampf(a, 0.0f, 1.0f)); };
    // large scale mottling + slight redness variation, fine mottle
    float m = fbm3(p * 9.0f);
    c *= 0.95f + 0.1f * m;
    redden((fbm3(p * 22.0f + Vec3(3.1f)) - 0.45f) * 0.5f);
    // freckles / small moles, sparse
    darken(Vec3(0.78f, 0.68f, 0.62f), dots(p, 0.012f, 0.0014f, 0.08f, 1.0f) * 0.6f);
    darken(Vec3(0.55f, 0.42f, 0.38f), dots(p, 0.06f, 0.0022f, 0.05f, 5.0f) * 0.8f);

    bool head = p.y > L.mouth.y - 0.09f && (p - L.headC).length() < 0.16f;
    if (head) {
        Vec3 e = L.eyeMid;
        float front = clampf(-n.z * 1.4f, 0.0f, 1.0f);
        // lips: ellipse around the mouth, front facing; darker line where they meet
        Vec3 dm = p - L.mouth;
        float lipC = (L.lipTop + L.lipBottom) * 0.5f, lipH = (L.lipTop - L.lipBottom) * 0.5f;
        float lip = 1.0f - smoothstep(0.78f, 1.05f, std::sqrt(sqr(dm.x / L.mouthHalfW) + sqr((p.y - lipC) / lipH)));
        lip *= front * smoothstep(-0.02f, -0.004f, -(p.z - L.mouth.z));
        c = lerp(c, Vec3(c.x * 0.98f, c.y * 0.66f, c.z * 0.68f), lip * 0.85f);
        c *= 1.0f - 0.5f * lip * (1.0f - smoothstep(0.0005f, 0.0016f, std::fabs(dm.y))) * (1.0f - smoothstep(L.mouthHalfW - 0.006f, L.mouthHalfW, std::fabs(dm.x)));
        s.rough = lerpf(s.rough, 0.34f, lip);
        // cheeks, nose, ears: warmer
        for (int k = 0; k < 2; ++k) {
            Vec3 ck = Vec3(L.eye[k].x * 1.15f, e.y - 0.034f, 0) - Vec3(p.x, p.y, 0);
            redden((1.0f - smoothstep(0.008f, 0.03f, ck.length())) * 0.55f * front);
        }
        redden((1.0f - smoothstep(0.004f, 0.02f, (p - L.noseTip).length())) * 0.5f);
        float ear = smoothstep(L.headHalfW - 0.005f, L.headHalfW + 0.012f, std::fabs(p.x)) * (1.0f - smoothstep(0.03f, 0.05f, std::fabs(p.y - (e.y - 0.015f))));
        redden(ear * 0.6f);
        // eye area: slightly darker / pinker lids, dark lash line along the lid margins
        for (int k = 0; k < 2; ++k) {
            float de = (p - L.eye[k]).length();
            darken(Vec3(0.93f, 0.86f, 0.87f), (1.0f - smoothstep(0.012f, 0.024f, de)) * 0.7f);
            if (de < 0.03f) {
                float dl = 1e9f;
                for (auto& q : L.lash[k]) dl = std::min(dl, (p - q).length());
                darken(Vec3(0.18f, 0.14f, 0.13f), (1.0f - smoothstep(0.0008f, 0.0022f, dl)) * 0.9f);
            }
        }
        // eyebrows: arcs above the eyes with hair strokes along the brow
        for (int k = 0; k < 2; ++k) {
            float sgn = L.eye[k].x < 0.0f ? -1.0f : 1.0f;  // outward direction
            float t = (p.x - L.eye[k].x) * sgn;          // -inner .. +outer
            float u = clampf((t + 0.018f) / 0.047f, 0.0f, 1.0f);
            float yc = L.eye[k].y + 0.019f + 0.009f * std::sin(u * kPi * 0.85f) - 0.004f * u;
            float thick = lerpf(0.0048f, 0.0026f, u);
            float inside = (t > -0.019f && t < 0.03f) ? 1.0f : 0.0f;
            float brow = inside * (1.0f - smoothstep(thick * 0.55f, thick, std::fabs(p.y - yc))) * front;
            brow *= smoothstep(-0.019f, -0.012f, t) * (1.0f - smoothstep(0.022f, 0.03f, t));
            // strokes: stretched noise along the brow direction (up and outwards)
            Vec3 q(p.x * sgn * 420.0f + p.y * 180.0f, p.y * 1300.0f - p.x * sgn * 500.0f, 0.0f);
            float stroke = smoothstep(0.35f, 0.75f, vnoise3(q + Vec3(float(k) * 13.0f, 0, 0)));
            c = lerp(c, Vec3(c.x * 0.17f, c.y * 0.14f, c.z * 0.13f), brow * (0.45f + 0.55f * stroke));
        }
        // beard shadow / stubble: jaw, chin, upper lip, lower cheeks
        float below = smoothstep(L.noseTip.y - 0.002f, L.noseTip.y - 0.012f, p.y);
        float aboveNeck = smoothstep(L.chin.y - 0.03f, L.chin.y - 0.008f, p.y);
        float faceSide = 1.0f - smoothstep(L.headC.z - 0.005f, L.headC.z + 0.02f, p.z);
        float cheekLine = 1.0f - smoothstep(0.0f, 0.012f, p.y - (L.noseTip.y - 0.004f - std::fabs(p.x) * 0.25f));
        float beard = below * aboveNeck * faceSide * cheekLine * (1.0f - lip);
        float stub = dots(p, 0.0011f, 0.00045f, 0.75f, 9.0f);
        c = lerp(c, Vec3(c.x * 0.72f, c.y * 0.72f, c.z * 0.76f), beard * (0.25f + 0.45f * stub));
        s.rough = lerpf(s.rough, 0.6f, beard * 0.6f);
        // T-zone sheen
        float tzone = front * (1.0f - smoothstep(0.012f, 0.03f, std::fabs(p.x))) * smoothstep(L.noseTip.y - 0.01f, L.noseTip.y + 0.01f, p.y);
        s.rough = lerpf(s.rough, 0.42f, tzone);
        // scalp (buzz cut under the hair shell)
        float az = std::atan2(p.x, -(p.z - L.headC.z));
        float hairline = e.y + lerpf(0.046f, -0.07f, smoothstep(0.9f, 2.6f, std::fabs(az)));
        hairline -= 0.03f * (1.0f - smoothstep(0.0f, 0.18f, std::fabs(std::fabs(az) - 1.35f)));  // sideburns
        float scalp = smoothstep(hairline - 0.004f, hairline + 0.01f, p.y) * (1.0f - ear);
        float hair = dots(p, 0.0009f, 0.0004f, 0.9f, 21.0f);
        c = lerp(c, Vec3(c.x * 0.35f, c.y * 0.3f, c.z * 0.28f), scalp * (0.55f + 0.35f * hair));
    }
    // nipples
    for (int k = 0; k < 2; ++k) {
        float d = (p - L.nipple[k]).length();
        c = lerp(c, Vec3(c.x * 0.86f, c.y * 0.66f, c.z * 0.64f), (1.0f - smoothstep(0.008f, 0.013f, d)) * 0.85f);
    }
    // hands: pinker palms, darker knuckles, nails
    for (int k = 0; k < 2; ++k) {
        if ((p - L.handC[k]).length() > 0.16f) continue;
        float palm = smoothstep(0.1f, 0.5f, dot(n, L.palmN[k]));
        c = lerp(c, Vec3(std::min(1.12f, c.x * 1.08f), c.y * 0.93f, c.z * 0.92f), palm * 0.6f);
        for (int f = 0; f < 5; ++f) {
            float dk = (p - L.knuckle[k][f]).length();
            darken(Vec3(0.88f, 0.8f, 0.8f), (1.0f - smoothstep(0.006f, 0.014f, dk)) * (1.0f - palm) * 0.7f);
            float dt = (p - L.tip[k][f]).length();
            float nail = (1.0f - smoothstep(0.0095f, 0.0125f, dt)) * smoothstep(0.1f, 0.45f, -dot(n, L.palmN[k]) + (f == 0 ? 0.35f : 0.0f));
            // nails: light pink with a paler free edge; independent of the skin tone tint -> push up
            c = lerp(c, Vec3(1.25f, 1.02f, 1.0f), nail * 0.9f);
            s.rough = lerpf(s.rough, 0.22f, nail);
        }
        // subtle veins on the back of the hand
        float vein = smoothstep(0.62f, 0.7f, vnoise3(p * Vec3(90.0f, 30.0f, 90.0f))) * (1.0f - palm);
        darken(Vec3(0.94f, 0.95f, 1.03f), vein * 0.8f);
    }
    s.col = c;
    return s;
}

}  // namespace

namespace {

// ---------------------------------------------------------------------------------------------
// garments: regions of the body surface offset outwards, smoothed into fabric, draped, with hems

struct Garment {
    std::vector<int> body;                           // garment vertex -> body vertex (-1: added)
    std::vector<Vec3> pos;
    std::vector<std::vector<std::pair<int, float>>> w;
    std::vector<std::array<int, 3>> tris;
    std::vector<std::array<Vec2, 3>> uvs;
    std::vector<std::vector<int>> adj;
    std::vector<char> boundary;
};

Garment garmentFromMask(const HumanModel& H, const std::vector<char>& mask) {
    Garment g;
    std::unordered_map<int, int> local;
    for (size_t t = 0; t < H.tris.size(); ++t) {
        const auto& tr = H.tris[t];
        if (!mask[size_t(tr[0])] || !mask[size_t(tr[1])] || !mask[size_t(tr[2])]) continue;
        if (tr[0] == tr[1] || tr[1] == tr[2] || tr[0] == tr[2]) continue;  // degenerate (collapsed quad corner)
        std::array<int, 3> gt;
        std::array<Vec2, 3> uv;
        for (int k = 0; k < 3; ++k) {
            int bv = tr[size_t(k)];
            auto it = local.find(bv);
            if (it == local.end()) {
                it = local.emplace(bv, int(g.pos.size())).first;
                g.body.push_back(bv);
                g.pos.push_back(H.v[size_t(bv)]);
                g.w.push_back(H.weights[size_t(bv)]);
            }
            gt[size_t(k)] = it->second;
            int ti = H.triUV[t][size_t(k)];
            Vec2 u = ti >= 0 ? H.obj.vt[size_t(ti)] : Vec2(0);
            uv[size_t(k)] = Vec2(u.x, 1.0f - u.y);
        }
        g.tris.push_back(gt);
        g.uvs.push_back(uv);
    }
    g.adj.assign(g.pos.size(), {});
    std::map<std::pair<int, int>, int> edges;
    for (auto& t : g.tris)
        for (int k = 0; k < 3; ++k) {
            int a = t[size_t(k)], b = t[size_t((k + 1) % 3)];
            g.adj[size_t(a)].push_back(b);
            edges[{std::min(a, b), std::max(a, b)}]++;
        }
    for (auto& a : g.adj) {
        std::sort(a.begin(), a.end());
        a.erase(std::unique(a.begin(), a.end()), a.end());
    }
    g.boundary.assign(g.pos.size(), 0);
    for (auto& [e, c] : edges)
        if (c == 1) g.boundary[size_t(e.first)] = g.boundary[size_t(e.second)] = 1;
    return g;
}

Vec3 bodyN(const HumanModel& H, const Garment& g, size_t i) { return g.body[i] >= 0 ? H.nrm[size_t(g.body[i])] : Vec3(0, 1, 0); }
Vec3 bodyP(const HumanModel& H, const Garment& g, size_t i) { return g.body[i] >= 0 ? H.v[size_t(g.body[i])] : g.pos[i]; }

// Laplacian smoothing that never lets the fabric sink under `minOff` above the skin
void smoothGarment(const HumanModel& H, Garment& g, int iters, float lambda, float minOff, bool keepBoundary) {
    for (int it = 0; it < iters; ++it) {
        std::vector<Vec3> np = g.pos;
        for (size_t i = 0; i < g.pos.size(); ++i) {
            if (g.adj[i].empty()) continue;
            Vec3 c(0);
            int n = 0;
            for (int j : g.adj[i]) {
                if (keepBoundary && g.boundary[i] && !g.boundary[size_t(j)]) continue;
                c += g.pos[size_t(j)];
                ++n;
            }
            // a hem vertex slides along the hem line only between exactly two hem neighbours
            if (!n || (keepBoundary && g.boundary[i] && n != 2)) continue;
            np[i] = lerp(g.pos[i], c / float(n), lambda);
        }
        for (size_t i = 0; i < np.size(); ++i) {
            if (g.body[i] < 0) continue;
            Vec3 n = bodyN(H, g, i);
            float d = dot(np[i] - bodyP(H, g, i), n);
            if (d < minOff) np[i] += n * (minOff - d);
        }
        g.pos = np;
    }
}

void smoothWeights(Garment& g, int iters) {
    for (int it = 0; it < iters; ++it) {
        std::vector<std::vector<std::pair<int, float>>> nw(g.w.size());
        for (size_t i = 0; i < g.w.size(); ++i) {
            std::map<int, float> acc;
            for (auto& [j, w] : g.w[i]) acc[j] += w * 2.0f;
            float tot = 2.0f;
            for (int n : g.adj[i]) {
                for (auto& [j, w] : g.w[size_t(n)]) acc[j] += w;
                tot += 1.0f;
            }
            for (auto& [j, w] : acc) nw[i].push_back({j, w / tot});
        }
        g.w = nw;
    }
}

// drop influences of joints the garment must not follow (e.g. a shirt hem following the thighs)
void filterWeights(const HumanModel& H, Garment& g, const std::function<bool(const std::string&)>& allowed, const std::string& fallback) {
    for (auto& wl : g.w) {
        std::vector<std::pair<int, float>> keep;
        float sum = 0.0f;
        for (auto& [j, w] : wl)
            if (allowed(H.joints[size_t(j)].name)) {
                keep.push_back({j, w});
                sum += w;
            }
        if (sum <= 1e-5f) keep = {{H.J(fallback), 1.0f}};
        else
            for (auto& k : keep) k.second /= sum;
        wl = keep;
    }
}

// hem: the boundary gets a folded edge of the given thickness (outer edge -> inside)
void addHem(const HumanModel& H, Garment& g, float thickness) {
    std::map<std::pair<int, int>, int> count;
    std::map<std::pair<int, int>, std::pair<int, Vec2>> owner;  // edge -> (third vertex, uv of a)
    for (size_t t = 0; t < g.tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            int a = g.tris[t][size_t(k)], b = g.tris[t][size_t((k + 1) % 3)];
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            count[key]++;
            owner[key] = {g.tris[t][size_t((k + 2) % 3)], g.uvs[t][size_t(k)]};
        }
    std::map<int, int> inner;
    auto innerOf = [&](int v) {
        auto it = inner.find(v);
        if (it != inner.end()) return it->second;
        int id = int(g.pos.size());
        g.pos.push_back(g.pos[size_t(v)] - bodyN(H, g, size_t(v)) * thickness);
        g.body.push_back(g.body[size_t(v)]);
        g.w.push_back(g.w[size_t(v)]);
        g.adj.push_back({});
        g.boundary.push_back(0);
        inner[v] = id;
        return id;
    };
    for (auto& [e, c] : count) {
        if (c != 1) continue;
        int a = e.first, b = e.second;
        int ia = innerOf(a), ib = innerOf(b);
        Vec2 uv = owner[e].second;
        // orient: the strip faces away from the garment interior (third vertex of the triangle)
        Vec3 mid = (g.pos[size_t(a)] + g.pos[size_t(b)]) * 0.5f, third = g.pos[size_t(owner[e].first)];
        Vec3 fn = cross(g.pos[size_t(b)] - g.pos[size_t(a)], g.pos[size_t(ib)] - g.pos[size_t(a)]);
        bool flip = dot(fn, mid - third) < 0.0f;
        std::array<int, 3> t1 = flip ? std::array<int, 3>{a, ib, b} : std::array<int, 3>{a, b, ib};
        std::array<int, 3> t2 = flip ? std::array<int, 3>{a, ia, ib} : std::array<int, 3>{a, ib, ia};
        g.tris.push_back(t1);
        g.uvs.push_back({uv, uv + Vec2(0.002f, 0), uv + Vec2(0, 0.002f)});
        g.tris.push_back(t2);
        g.uvs.push_back({uv, uv + Vec2(0, 0.002f), uv + Vec2(0.002f, 0.002f)});
    }
}

Part garmentPart(const Garment& g) {
    std::vector<Vec3> n(g.pos.size(), Vec3(0));
    for (auto& t : g.tris) {
        Vec3 fn = cross(g.pos[size_t(t[1])] - g.pos[size_t(t[0])], g.pos[size_t(t[2])] - g.pos[size_t(t[0])]);
        for (int k = 0; k < 3; ++k) n[size_t(t[size_t(k)])] += fn;
    }
    for (auto& x : n) x = x.lengthSq() > 1e-20f ? x.normalized() : Vec3(0, 1, 0);
    Part P;
    std::map<std::tuple<int, int, int>, uint32_t> remap;
    for (size_t t = 0; t < g.tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            int v = g.tris[t][size_t(k)];
            Vec2 uv = g.uvs[t][size_t(k)];
            auto key = std::make_tuple(v, int(std::lround(uv.x * 65536.0f)), int(std::lround(uv.y * 65536.0f)));
            auto it = remap.find(key);
            if (it == remap.end()) {
                uint32_t id = uint32_t(P.p.size());
                P.p.push_back(g.pos[size_t(v)]);
                P.n.push_back(n[size_t(v)]);
                P.uv.push_back(uv);
                P.sk.push_back(topFour(g.w[size_t(v)]));
                it = remap.emplace(key, id).first;
            }
            P.idx.push_back(it->second);
        }
    return P;
}

float wOf(const HumanModel& H, int v, const std::string& j) {
    int ji = H.J(j);
    for (auto& [k, w] : H.weights[size_t(v)])
        if (k == ji) return w;
    return 0.0f;
}
bool domIs(const HumanModel& H, int v, std::initializer_list<const char*> names) {
    const std::string& n = H.joints[size_t(H.dominant[size_t(v)])].name;
    for (const char* x : names)
        if (n.rfind(x, 0) == 0) return true;
    return false;
}
// parameter along a limb polyline a -> b -> c: 0 at a, 1 at b, 2 at c; also the axis point
float limbParam(Vec3 p, Vec3 a, Vec3 b, Vec3 c, Vec3* axis = nullptr) {
    auto seg = [&](Vec3 s0, Vec3 s1, float& t, Vec3& q) {
        Vec3 d = s1 - s0;
        t = clampf(dot(p - s0, d) / std::max(dot(d, d), 1e-8f), 0.0f, 1.0f);
        q = s0 + d * t;
        return (p - q).lengthSq();
    };
    float t0, t1;
    Vec3 q0, q1;
    float d0 = seg(a, b, t0, q0), d1 = seg(b, c, t1, q1);
    if (d0 <= d1) {
        if (axis) *axis = q0;
        return t0;
    }
    if (axis) *axis = q1;
    return 1.0f + t1;
}

// luminance-only fabric texture (weave + variation) so the runtime tint sets the colour
bool fabricLuminance(const std::string& src, const std::string& dst, float mean) {
    int w, h, c;
    stbi_uc* px = stbi_load(src.c_str(), &w, &h, &c, 3);
    if (!px) return false;
    std::vector<float> l(size_t(w) * size_t(h));
    double sum = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        auto lin = [](stbi_uc v) {
            float f = float(v) / 255.0f;
            return f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f);
        };
        l[i] = 0.2126f * lin(px[i * 3]) + 0.7152f * lin(px[i * 3 + 1]) + 0.0722f * lin(px[i * 3 + 2]);
        sum += l[i];
    }
    stbi_image_free(px);
    float m = float(sum / double(l.size()));
    Tex t(w, h);
    for (size_t i = 0; i < l.size(); ++i) {
        float v = clampf(mean + (l[i] - m) / std::max(m, 1e-3f) * mean * 0.6f, 0.0f, 1.0f);
        t.px[i] = Vec4(v, v, v, 1.0f);
    }
    return t.save(dst, true, 3);
}

}  // namespace

namespace {

struct Wardrobe {
    std::map<std::string, std::vector<std::pair<int, Part>>> meshes;  // mesh name -> (material, part)
    void add(const std::string& mesh, int mat, Part p) { meshes[mesh].push_back({mat, std::move(p)}); }
};

// sweep a superellipse section along a path into a Part (hood roll, drawstrings, straps)
Part sweepPart(const std::vector<Vec3>& path, const std::vector<Vec2>& half, Vec3 sideHint, int seg, int joint, bool caps) {
    Part P;
    size_t n = path.size();
    std::vector<std::vector<uint32_t>> ring(n);
    for (size_t i = 0; i < n; ++i) {
        Vec3 t = (path[std::min(i + 1, n - 1)] - path[i == 0 ? 0 : i - 1]).normalized();
        Vec3 sd = (sideHint - t * dot(sideHint, t));
        sd = sd.lengthSq() < 1e-8f ? anyPerpendicular(t).normalized() : sd.normalized();
        Vec3 u = cross(t, sd).normalized();
        for (int k = 0; k <= seg; ++k) {
            float a = float(k) / float(seg) * kTwoPi;
            Vec3 d = sd * (std::cos(a) * half[i].x) + u * (std::sin(a) * half[i].y);
            ring[i].push_back(uint32_t(P.p.size()));
            P.p.push_back(path[i] + d);
            P.n.push_back((sd * (std::cos(a) / std::max(half[i].x, 1e-4f)) + u * (std::sin(a) / std::max(half[i].y, 1e-4f))).normalized());
            P.uv.push_back(Vec2(float(k) / float(seg), float(i) / float(n)));
            Skinned sk;
            sk.j[0] = uint8_t(joint);
            P.sk.push_back(sk);
        }
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 0; k < seg; ++k) {
            uint32_t a = ring[i][size_t(k)], b = ring[i][size_t(k) + 1], c = ring[i + 1][size_t(k) + 1], d = ring[i + 1][size_t(k)];
            Vec3 fn = cross(P.p[b] - P.p[a], P.p[c] - P.p[a]);
            if (dot(fn, P.n[a]) >= 0.0f) P.idx.insert(P.idx.end(), {a, b, c, a, c, d});
            else P.idx.insert(P.idx.end(), {a, c, b, a, d, c});
        }
    if (caps)
        for (size_t e : {size_t(0), n - 1}) {
            Vec3 t = (e == 0 ? path[0] - path[1] : path[n - 1] - path[n - 2]).normalized();
            uint32_t c = uint32_t(P.p.size());
            P.p.push_back(path[e]);
            P.n.push_back(t);
            P.uv.push_back(Vec2(0.5f));
            P.sk.push_back(P.sk[ring[e][0]]);
            for (int k = 0; k < seg; ++k) {
                uint32_t a = ring[e][size_t(k)], b = ring[e][size_t(k) + 1];
                Vec3 fn = cross(P.p[a] - P.p[c], P.p[b] - P.p[c]);
                if (dot(fn, t) >= 0.0f) P.idx.insert(P.idx.end(), {c, a, b});
                else P.idx.insert(P.idx.end(), {c, b, a});
            }
        }
    return P;
}

// head shell (helmet / cap crown): ellipsoid rows interpolated from the top down to a cut line
// that depends on the azimuth; outer + inner surface + rim
Part headShell(Vec3 c, Vec3 r, float thick, const std::function<float(float)>& edgeY, int joint, int cols, int rows) {
    Part P;
    auto pointAt = [&](float az, float el, float shrink) {
        // az: 0 = front (-Z); el: 0 = top .. pi
        Vec3 d(std::sin(el) * std::sin(az), std::cos(el), -std::sin(el) * std::cos(az));
        return c + Vec3(d.x * (r.x - shrink), d.y * (r.y - shrink), d.z * (r.z - shrink));
    };
    auto edgeEl = [&](float az) {
        float target = edgeY(az), lo = 0.0f, hi = kPi * 0.95f;
        for (int i = 0; i < 30; ++i) {
            float m = 0.5f * (lo + hi);
            if (pointAt(az, m, 0.0f).y > target) lo = m;
            else hi = m;
        }
        return 0.5f * (lo + hi);
    };
    std::vector<float> ee(size_t(cols) + 1);
    for (int i = 0; i <= cols; ++i) ee[size_t(i)] = edgeEl(float(i) / float(cols) * kTwoPi);
    auto layer = [&](float shrink, bool inward) {
        std::vector<std::vector<uint32_t>> id(size_t(rows) + 1, std::vector<uint32_t>(size_t(cols) + 1));
        for (int rr = 0; rr <= rows; ++rr)
            for (int cc = 0; cc <= cols; ++cc) {
                float az = float(cc) / float(cols) * kTwoPi;
                float el = std::max(0.02f, ee[size_t(cc)] * float(rr) / float(rows));
                Vec3 p = pointAt(az, el, shrink);
                Vec3 nn = Vec3((p.x - c.x) / sqr(r.x), (p.y - c.y) / sqr(r.y), (p.z - c.z) / sqr(r.z)).normalized();
                id[size_t(rr)][size_t(cc)] = uint32_t(P.p.size());
                P.p.push_back(p);
                P.n.push_back(inward ? -nn : nn);
                P.uv.push_back(Vec2(float(cc) / float(cols), float(rr) / float(rows)));
                Skinned sk;
                sk.j[0] = uint8_t(joint);
                P.sk.push_back(sk);
            }
        for (int rr = 0; rr < rows; ++rr)
            for (int cc = 0; cc < cols; ++cc) {
                uint32_t a = id[size_t(rr)][size_t(cc)], b = id[size_t(rr)][size_t(cc) + 1], cI = id[size_t(rr) + 1][size_t(cc) + 1], d = id[size_t(rr) + 1][size_t(cc)];
                Vec3 fn = cross(P.p[b] - P.p[a], P.p[cI] - P.p[a]);
                if ((dot(fn, P.n[a]) >= 0.0f)) P.idx.insert(P.idx.end(), {a, b, cI, a, cI, d});
                else P.idx.insert(P.idx.end(), {a, cI, b, a, d, cI});
            }
        return id;
    };
    auto outer = layer(0.0f, false);
    auto inner = layer(thick, true);
    // rim
    for (int cc = 0; cc < cols; ++cc) {
        uint32_t a = outer[size_t(rows)][size_t(cc)], b = outer[size_t(rows)][size_t(cc) + 1], d = inner[size_t(rows)][size_t(cc)], e = inner[size_t(rows)][size_t(cc) + 1];
        Vec3 down = Vec3(0, -1, 0);
        uint32_t base = uint32_t(P.p.size());
        for (uint32_t v : {a, b, e, d}) {
            P.p.push_back(P.p[v]);
            P.n.push_back(down);
            P.uv.push_back(P.uv[v]);
            P.sk.push_back(P.sk[v]);
        }
        Vec3 fn = cross(P.p[base + 1] - P.p[base], P.p[base + 2] - P.p[base]);
        if (dot(fn, down) >= 0.0f) P.idx.insert(P.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else P.idx.insert(P.idx.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    return P;
}

void buildWardrobe(const HumanModel& H, const Landmarks& L, Wardrobe& W, const std::map<std::string, int>& M) {
    auto mat = [&](const char* n) { return M.at(n); };
    std::vector<char> used(H.v.size(), 0);
    for (auto& t : H.tris)
        for (int k = 0; k < 3; ++k) used[size_t(t[size_t(k)])] = 1;
    const Vec3 P = H.jp("pelvis"), N = H.jp("neck"), C = H.jp("chest");
    Vec3 sh[2] = {H.jp("upperarm_l"), H.jp("upperarm_r")}, el[2] = {H.jp("lowerarm_l"), H.jp("lowerarm_r")}, wr[2] = {H.jp("hand_l"), H.jp("hand_r")};
    Vec3 hip[2] = {H.jp("thigh_l"), H.jp("thigh_r")}, knee[2] = {H.jp("shin_l"), H.jp("shin_r")}, ank[2] = {H.jp("foot_l"), H.jp("foot_r")};
    float chestY = (L.nipple[0].y + L.nipple[1].y) * 0.5f;
    // chest front / shoulder blade profiles per x (1 cm bins) for the drape
    std::map<int, float> front, back;
    for (size_t i = 0; i < H.v.size(); ++i) {
        if (!used[i] || !domIs(H, int(i), {"chest", "spine", "clavicle"})) continue;
        const Vec3& p = H.v[i];
        int xb = int(std::floor(p.x * 100.0f));
        if (std::fabs(p.y - chestY) < 0.035f) front[xb] = std::min(front.count(xb) ? front[xb] : 1e9f, p.z);
        if (p.y > chestY && p.y < chestY + 0.13f) back[xb] = std::max(back.count(xb) ? back[xb] : -1e9f, p.z);
    }
    auto prof = [&](const std::map<int, float>& m, float x, float def) {
        int xb = int(std::floor(x * 100.0f));
        auto it = m.find(xb);
        return it == m.end() ? def : it->second;
    };
    auto side = [](const Vec3& p) { return p.x < 0.0f ? 0 : 1; };

    // ---- tops ------------------------------------------------------------------------------
    struct TopSpec {
        const char* mesh;
        int material;
        float hemY, neckExtra, sleeveT, offset, sleeveR0, sleeveR1, cuffT;
        bool crewDip, tank;
    };
    const TopSpec tops[3] = {
        {"top_0", mat("top"), P.y - 0.085f, -0.004f, 0.5f, 0.006f, 0.054f, 0.052f, 9.0f, true, false},
        {"top_1", mat("hoodie"), P.y - 0.115f, 0.03f, 1.93f, 0.012f, 0.062f, 0.052f, 1.78f, false, false},
        {"top_2", mat("top"), P.y - 0.08f, -0.004f, -1.0f, 0.006f, 0.0f, 0.0f, 9.0f, true, true},
    };
    for (const TopSpec& T : tops) {
        std::vector<char> m(H.v.size(), 0);
        for (size_t i = 0; i < H.v.size(); ++i) {
            if (!used[i]) continue;
            const Vec3& p = H.v[i];
            int v = int(i);
            if (p.y < T.hemY) continue;
            // never the crotch / inner thighs: tops end in a clean loop around the hips
            if (p.y < P.y - 0.01f && (H.nrm[i].y < -0.35f || (std::fabs(p.x) < 0.1f && std::fabs(H.nrm[i].x) > 0.45f &&
                                                                  H.nrm[i].x * p.x < 0.0f)))
                continue;
            if (wOf(H, v, "thigh_l") + wOf(H, v, "thigh_r") > 0.35f) continue;
            if (wOf(H, v, "neck") * 0.6f + wOf(H, v, "head") + wOf(H, v, "jaw") > 0.3f) continue;
            if (p.y > N.y + T.neckExtra) continue;
            if (T.crewDip && H.nrm[i].z < -0.2f && std::fabs(p.x) < 0.075f && p.y > N.y - 0.045f) continue;
            if (T.tank) {
                if (H.nrm[i].z < -0.1f && std::fabs(p.x) < 0.085f && p.y > chestY + 0.06f) continue;  // scoop neck
                if (H.nrm[i].z > 0.1f && std::fabs(p.x) < 0.075f && p.y > chestY + 0.11f) continue;
                if ((p - sh[side(p)]).length() < 0.105f || domIs(H, v, {"upperarm"})) continue;           // arm holes
            }
            bool arm = domIs(H, v, {"upperarm", "lowerarm"});
            if (arm) {
                int sd = side(p);
                if (limbParam(p, sh[sd], el[sd], wr[sd]) > T.sleeveT) continue;
            } else if (!domIs(H, v, {"pelvis", "spine", "chest", "clavicle", "neck"})) {
                continue;
            }
            m[i] = 1;
        }
        Garment g = garmentFromMask(H, m);
        for (size_t i = 0; i < g.pos.size(); ++i) {
            Vec3 p = bodyP(H, g, i), n = bodyN(H, g, i);
            int v = g.body[i];
            if (domIs(H, v, {"upperarm", "lowerarm"})) {
                int sd = side(p);
                Vec3 axis;
                float t = limbParam(p, sh[sd], el[sd], wr[sd], &axis);
                Vec3 r = p - axis;
                float R = std::max(r.length(), 1e-4f);
                float target = lerpf(T.sleeveR0, T.sleeveR1, clampf(t / 1.9f, 0.0f, 1.0f));
                target = std::min(target, R + 0.014f);                                   // loose, not flared
                target = lerpf(R + T.offset, target, smoothstep(0.05f, 0.3f, t));        // eases in from the shoulder
                if (t > T.cuffT) target = lerpf(target, R + 0.006f, smoothstep(T.cuffT, T.cuffT + 0.08f, t));
                g.pos[i] = axis + r / R * std::max(R + T.offset, target);
            } else {
                float off = T.offset;
                // waistband of the hoodie hugs the hips
                if (T.cuffT < 5.0f) off = lerpf(0.009f, off, smoothstep(T.hemY + 0.02f, T.hemY + 0.07f, p.y));
                g.pos[i] = p + n * off;
                // drape: the fabric hangs from the chest / shoulder blades instead of following the belly
                if (p.y < chestY) {
                    float drop = chestY - p.y;
                    float fz = prof(front, p.x, p.z) - T.offset - 0.002f + drop * (T.cuffT < 5.0f ? 0.06f : 0.1f);
                    float bz = prof(back, p.x, p.z) + T.offset + 0.002f - drop * 0.08f;
                    float wf = smoothstep(0.0f, 0.5f, -n.z), wb = smoothstep(0.0f, 0.5f, n.z);
                    g.pos[i].z = lerpf(g.pos[i].z, std::min(g.pos[i].z, fz), wf * 0.45f);
                    g.pos[i].z = lerpf(g.pos[i].z, std::max(g.pos[i].z, bz), wb * 0.45f);
                }
            }
        }
        smoothGarment(H, g, T.cuffT < 5.0f ? 20 : 12, 0.5f, T.offset * 0.55f, true);
        // soft folds at the elbows / waist so the fabric is not perfectly smooth
        for (size_t i = 0; i < g.pos.size(); ++i) {
            Vec3 p = bodyP(H, g, i), n = bodyN(H, g, i);
            float f = fbm3(p * 38.0f) - 0.5f;
            g.pos[i] += n * (f * 0.005f);
        }
        smoothWeights(g, 3);
        filterWeights(H, g, [](const std::string& n) { return n.rfind("thigh", 0) != 0 && n.rfind("shin", 0) != 0 && n.rfind("foot", 0) != 0; }, "pelvis");
        addHem(H, g, T.cuffT < 5.0f ? 0.009f : 0.005f);
        W.add(T.mesh, T.material, garmentPart(g));
        if (T.cuffT < 5.0f) {
            // hood rolled around the back of the neck + drawstrings
            float neckR = 0.075f;
            std::vector<Vec3> path;
            std::vector<Vec2> half;
            for (int k = 0; k <= 20; ++k) {
                float a = (-105.0f + float(k) / 20.0f * 210.0f) * kDeg2Rad;  // 0 = back
                float bk = std::cos(a * 0.5f * 180.0f / 105.0f);
                Vec3 q(std::sin(a) * (neckR + 0.012f), N.y + 0.035f - 0.06f * std::max(0.0f, std::cos(a)), N.z + 0.012f + std::cos(a) * (neckR + 0.035f));
                path.push_back(q);
                half.push_back(Vec2(0.018f + 0.035f * std::max(0.0f, bk), 0.02f + 0.028f * std::max(0.0f, bk)));
            }
            W.add(T.mesh, T.material, sweepPart(path, half, Vec3(0, 1, 0), 14, H.J("chest"), true));
            for (float sx : {-0.032f, 0.032f}) {
                Vec3 top(sx, N.y - 0.012f, prof(front, sx, N.z - 0.09f) - T.offset - 0.012f);
                std::vector<Vec3> sp = {top, top + Vec3(sx * 0.1f, -0.08f, -0.012f), top + Vec3(sx * 0.15f, -0.17f, -0.012f)};
                W.add(T.mesh, mat("lace"), sweepPart(sp, {Vec2(0.0032f), Vec2(0.0032f), Vec2(0.0038f)}, Vec3(1, 0, 0), 6, H.J("chest"), true));
            }
            // kangaroo pocket: raised panel on the belly
            std::vector<char> pm(H.v.size(), 0);
            for (size_t i = 0; i < H.v.size(); ++i) {
                const Vec3& p = H.v[i];
                if (used[i] && H.nrm[i].z < -0.25f && std::fabs(p.x) < 0.12f - (p.y - T.hemY) * 0.25f && p.y > T.hemY + 0.05f && p.y < T.hemY + 0.2f &&
                    domIs(H, int(i), {"pelvis", "spine", "chest"}))
                    pm[i] = 1;
            }
            Garment pk = garmentFromMask(H, pm);
            for (size_t i = 0; i < pk.pos.size(); ++i) {
                Vec3 p = bodyP(H, pk, i), n = bodyN(H, pk, i);
                pk.pos[i] = p + n * (T.offset + 0.004f);
                float fz = prof(front, p.x, p.z) - T.offset - 0.006f + (chestY - p.y) * 0.06f;
                pk.pos[i].z = std::min(pk.pos[i].z, fz);
            }
            smoothGarment(H, pk, 20, 0.5f, T.offset + 0.003f, true);
            smoothWeights(pk, 3);
            addHem(H, pk, 0.004f);
            W.add(T.mesh, T.material, garmentPart(pk));
        }
    }

    // ---- pants -----------------------------------------------------------------------------
    for (int variant = 0; variant < 2; ++variant) {
        float waistY = P.y + 0.035f;
        float cutY = variant == 0 ? ank[0].y + 0.05f : knee[0].y + 0.075f;
        std::vector<char> m(H.v.size(), 0);
        for (size_t i = 0; i < H.v.size(); ++i) {
            if (!used[i]) continue;
            const Vec3& p = H.v[i];
            if (p.y > waistY || p.y < cutY) continue;
            if (!domIs(H, int(i), {"pelvis", "thigh", "shin", "spine"})) continue;
            m[i] = 1;
        }
        Garment g = garmentFromMask(H, m);
        std::vector<float> legT(g.pos.size(), 0.0f);
        std::vector<Vec3> legDir(g.pos.size(), Vec3(0));
        for (size_t i = 0; i < g.pos.size(); ++i) {
            Vec3 p = bodyP(H, g, i), n = bodyN(H, g, i);
            int v = g.body[i];
            if (domIs(H, v, {"thigh", "shin"})) {
                int sd = side(p);
                Vec3 axis;
                float t = limbParam(p, hip[sd], knee[sd], ank[sd], &axis);
                Vec3 r = p - axis;
                r.y = 0.0f;
                float R = std::max(r.length(), 1e-4f);
                Vec3 dir = r / R;
                // baggy street pants: wide straight legs, relaxed at the knee, stacking at the shoe
                float target = t < 1.0f ? lerpf(0.09f, 0.083f, t) : lerpf(0.083f, 0.08f, t - 1.0f);
                if (variant == 1) target = lerpf(0.088f, 0.085f, t);
                float inner = dot(dir, Vec3(sd == 0 ? 1.0f : -1.0f, 0, 0));
                float crotch = smoothstep(0.2f, 0.7f, inner) * (1.0f - smoothstep(0.15f, 0.7f, t));
                float rr = std::max(R + 0.01f, lerpf(target, R + 0.013f, crotch));
                // the wide leg only starts below the hip joint (the waist hugs the hips)
                rr = lerpf(R + 0.011f, rr, smoothstep(hip[sd].y - 0.02f, hip[sd].y - 0.14f, p.y));
                g.pos[i] = Vec3(axis.x + dir.x * rr, p.y, axis.z + dir.z * rr);
                legT[i] = t;
                legDir[i] = dir;
            } else {
                g.pos[i] = p + n * 0.011f;
            }
        }
        smoothGarment(H, g, 20, 0.5f, 0.007f, true);
        // folds: soft horizontal ripples, stacking towards the hem, a few creases behind the knee
        for (size_t i = 0; i < g.pos.size(); ++i) {
            if (legT[i] <= 0.05f) continue;
            Vec3 p = bodyP(H, g, i);
            float t = legT[i];
            float stack = variant == 0 ? smoothstep(1.35f, 2.0f, t) : 0.0f;
            float ripple = std::sin(t * 23.0f + fbm3(p * 20.0f) * 6.0f + std::atan2(legDir[i].x, legDir[i].z) * 2.0f);
            float amp = 0.0022f + 0.005f * stack + 0.002f * (1.0f - smoothstep(0.1f, 0.35f, std::fabs(t - 1.0f)));
            g.pos[i] += legDir[i] * (ripple * amp);
        }
        smoothWeights(g, 6);
        // the waist must not swing with the thighs: fade thigh influence out above the hip joints
        for (size_t i = 0; i < g.w.size(); ++i) {
            float y = bodyP(H, g, i).y;
            float keep = smoothstep(hip[0].y + 0.03f, hip[0].y - 0.09f, y);
            float moved = 0.0f;
            for (auto& [j, w] : g.w[i]) {
                const std::string& n = H.joints[size_t(j)].name;
                if (n.rfind("thigh", 0) == 0 || n.rfind("shin", 0) == 0) {
                    moved += w * (1.0f - keep);
                    w *= keep;
                }
            }
            if (moved > 0.0f) g.w[i].push_back({H.J("pelvis"), moved});
        }
        {
            std::map<std::string, int> odd;
            for (size_t i = 0; i < g.w.size(); ++i)
                for (auto& [j, w] : g.w[i]) {
                    const std::string& n = H.joints[size_t(j)].name;
                    if (w > 0.01f && n != "pelvis" && n != "spine" && n.rfind("thigh", 0) != 0 && n.rfind("shin", 0) != 0) odd[n]++;
                }
            for (auto& [n, c] : odd) std::printf("  pants%d: %d verts weighted to %s\n", variant, c, n.c_str());
        }
        addHem(H, g, 0.011f);
        W.add(variant == 0 ? "pants_0" : "pants_1", mat("pants"), garmentPart(g));
    }

    // ---- shoes -----------------------------------------------------------------------------
    for (int variant = 0; variant < 2; ++variant) {
        float collarY = ank[0].y + (variant == 0 ? 0.03f : 0.1f);
        std::vector<char> m(H.v.size(), 0);
        for (size_t i = 0; i < H.v.size(); ++i) {
            if (!used[i]) continue;
            const Vec3& p = H.v[i];
            if (domIs(H, int(i), {"foot", "toes"}) || (domIs(H, int(i), {"shin"}) && p.y < collarY)) m[i] = 1;
        }
        Garment g = garmentFromMask(H, m);
        const float soleTop = 0.026f;
        for (size_t i = 0; i < g.pos.size(); ++i) g.pos[i] = bodyP(H, g, i) + bodyN(H, g, i) * 0.009f;
        smoothGarment(H, g, 30, 0.5f, 0.006f, true);
        for (auto& p : g.pos) p.y = std::max(p.y, soleTop);
        smoothWeights(g, 2);
        addHem(H, g, 0.012f);  // padded collar
        W.add(variant == 0 ? "shoes_0" : "shoes_1", mat("shoes"), garmentPart(g));
        // soles + laces per foot
        for (int sd = 0; sd < 2; ++sd) {
            std::vector<Vec2> pts;
            for (size_t i = 0; i < g.pos.size(); ++i)
                if (g.body[i] >= 0 && side(g.pos[i]) == sd && g.pos[i].y < 0.07f) pts.push_back(Vec2(g.pos[i].x, g.pos[i].z));
            if (pts.size() < 3) continue;
            // convex hull (monotone chain) of the footprint, pushed out a little
            std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            std::vector<Vec2> hull;
            auto crossZ = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
            for (int pass = 0; pass < 2; ++pass) {
                size_t start = hull.size();
                for (size_t k = 0; k < pts.size(); ++k) {
                    const Vec2& q = pass == 0 ? pts[k] : pts[pts.size() - 1 - k];
                    while (hull.size() >= start + 2 && crossZ(hull[hull.size() - 2], hull.back(), q) <= 0) hull.pop_back();
                    hull.push_back(q);
                }
                hull.pop_back();
            }
            Vec2 cen(0);
            for (auto& q : hull) cen += q;
            cen = cen / float(hull.size());
            // resample the hull evenly
            std::vector<Vec2> ring;
            for (size_t k = 0; k < hull.size(); ++k) {
                Vec2 a = hull[k], b = hull[(k + 1) % hull.size()];
                int steps = std::max(1, int((b - a).length() / 0.008f));
                for (int s2 = 0; s2 < steps; ++s2) ring.push_back(a + (b - a) * (float(s2) / float(steps)));
            }
            float zMin = 1e9f, zMax = -1e9f;
            for (auto& q : ring) {
                zMin = std::min(zMin, q.y);
                zMax = std::max(zMax, q.y);
            }
            Part S;
            int footJ = H.J(sd == 0 ? "foot_l" : "foot_r"), toeJ = H.J(sd == 0 ? "toes_l" : "toes_r");
            float toeZ = H.jp(sd == 0 ? "toes_l" : "toes_r").z;
            auto skAt = [&](float z) {
                Skinned k;
                float wt = smoothstep(toeZ + 0.02f, toeZ - 0.02f, z);
                k.j[0] = uint8_t(footJ);
                k.j[1] = uint8_t(toeJ);
                k.w[0] = 1.0f - wt;
                k.w[1] = wt;
                return k;
            };
            // rows: bottom (inset), bottom edge, top edge (slightly outside the upper)
            const float rowsY[4] = {0.0f, 0.004f, soleTop + 0.004f, soleTop + 0.006f};
            const float rowsOut[4] = {-0.004f, 0.006f, 0.007f, 0.0f};
            std::vector<std::vector<uint32_t>> id(4);
            for (int rI = 0; rI < 4; ++rI)
                for (size_t k = 0; k <= ring.size(); ++k) {
                    Vec2 q = ring[k % ring.size()];
                    Vec2 d = (q - cen);
                    d = d.lengthSq() > 1e-10f ? d.normalized() : Vec2(1, 0);
                    Vec2 pq = q + d * rowsOut[rI];
                    float toeLift = smoothstep(zMin + 0.05f, zMin, pq.y) * 0.008f;  // toe spring
                    float y = rowsY[rI] + (rI < 2 ? toeLift : toeLift * 0.3f);
                    Vec3 nn = rI == 0 ? Vec3(0, -1, 0) : rI == 3 ? Vec3(0, 1, 0) : Vec3(d.x, 0, d.y);
                    id[size_t(rI)].push_back(uint32_t(S.p.size()));
                    S.p.push_back(Vec3(pq.x, y, pq.y));
                    S.n.push_back(nn);
                    S.uv.push_back(Vec2(float(k) / float(ring.size()), float(rI) / 3.0f));
                    S.sk.push_back(skAt(pq.y));
                }
            for (int rI = 0; rI < 3; ++rI)
                for (size_t k = 0; k < ring.size(); ++k) {
                    uint32_t a = id[size_t(rI)][k], b = id[size_t(rI)][k + 1], c2 = id[size_t(rI) + 1][k + 1], d2 = id[size_t(rI) + 1][k];
                    Vec3 outward = Vec3(S.p[a].x - cen.x, 0, S.p[a].z - cen.y);
                    Vec3 fn = cross(S.p[b] - S.p[a], S.p[c2] - S.p[a]);
                    if (dot(fn, outward) >= 0.0f) S.idx.insert(S.idx.end(), {a, b, c2, a, c2, d2});
                    else S.idx.insert(S.idx.end(), {a, c2, b, a, d2, c2});
                }
            // bottom cap (tread)
            uint32_t cb = uint32_t(S.p.size());
            S.p.push_back(Vec3(cen.x, 0.0f, cen.y));
            S.n.push_back(Vec3(0, -1, 0));
            S.uv.push_back(Vec2(0.5f));
            S.sk.push_back(skAt(cen.y));
            for (size_t k = 0; k < ring.size(); ++k) {
                uint32_t a = id[0][k], b = id[0][k + 1];
                Vec3 fn = cross(S.p[a] - S.p[cb], S.p[b] - S.p[cb]);
                if (fn.y <= 0.0f) S.idx.insert(S.idx.end(), {cb, a, b});
                else S.idx.insert(S.idx.end(), {cb, b, a});
            }
            W.add(variant == 0 ? "shoes_0" : "shoes_1", mat("sole"), std::move(S));
            // laces: short flat bars across the instep, following the upper's top surface
            Vec3 ak = ank[sd];
            for (int k = 0; k < 6; ++k) {
                float z = ak.z - 0.035f - float(k) * 0.014f;
                float x0 = ak.x;
                float yTop = 0.0f;
                for (size_t i = 0; i < g.pos.size(); ++i)
                    if (side(g.pos[i]) == sd && std::fabs(g.pos[i].z - z) < 0.006f && std::fabs(g.pos[i].x - x0) < 0.012f) yTop = std::max(yTop, g.pos[i].y);
                if (yTop <= 0.0f) continue;
                float hw = 0.018f - float(k) * 0.0012f;
                std::vector<Vec3> lp = {Vec3(x0 - hw, yTop - 0.002f, z + 0.003f), Vec3(x0, yTop + 0.0035f, z), Vec3(x0 + hw, yTop - 0.002f, z - 0.003f)};
                W.add(variant == 0 ? "shoes_0" : "shoes_1", mat("lace"), sweepPart(lp, {Vec2(0.0038f, 0.0012f), Vec2(0.0038f, 0.0012f), Vec2(0.0038f, 0.0012f)}, Vec3(0, 1, 0), 6, footJ, true));
            }
        }
    }

    // ---- hair: short crop shell over the scalp (alpha faded hairline from the hair texture) -----
    {
        std::vector<char> m(H.v.size(), 0);
        for (size_t i = 0; i < H.v.size(); ++i) {
            if (!used[i]) continue;
            const Vec3& p = H.v[i];
            if ((p - L.headC).length() > 0.16f || p.y < L.eyeMid.y - 0.1f) continue;
            float az = std::atan2(p.x, -(p.z - L.headC.z));
            float hairline = L.eyeMid.y + lerpf(0.046f, -0.07f, smoothstep(0.9f, 2.6f, std::fabs(az)));
            hairline -= 0.03f * (1.0f - smoothstep(0.0f, 0.18f, std::fabs(std::fabs(az) - 1.35f)));
            bool ear = std::fabs(p.x) > L.headHalfW - 0.004f && std::fabs(p.y - (L.eyeMid.y - 0.015f)) < 0.04f;
            if (p.y > hairline - 0.012f && !ear) m[i] = 1;
        }
        Garment g = garmentFromMask(H, m);
        for (size_t i = 0; i < g.pos.size(); ++i) {
            Vec3 p = bodyP(H, g, i), n = bodyN(H, g, i);
            float top = smoothstep(L.eyeMid.y + 0.04f, L.eyeMid.y + 0.12f, p.y);
            g.pos[i] = p + n * (0.003f + 0.006f * top);
        }
        smoothGarment(H, g, 6, 0.4f, 0.0025f, true);
        W.add("hair", mat("hair"), garmentPart(g));
    }

    // ---- headwear ----------------------------------------------------------------------------
    {
        Vec3 mn(1e9f), mx(-1e9f);
        for (size_t i = 0; i < H.v.size(); ++i) {
            if (!used[i]) continue;
            const Vec3& p = H.v[i];
            if ((p - L.headC).length() < 0.16f && p.y > L.eyeMid.y + 0.005f && std::fabs(p.x) < L.headHalfW - 0.002f) {
                mn = vmin(mn, p);
                mx = vmax(mx, p);
            }
        }
        Vec3 ctr((mn.x + mx.x) * 0.5f, L.eyeMid.y + 0.005f, (mn.z + mx.z) * 0.5f);
        Vec3 half((mx.x - mn.x) * 0.5f, mx.y - ctr.y, (mx.z - mn.z) * 0.5f);
        float browY = L.eyeMid.y + 0.022f;
        int headJ = H.J("head");
        // skate helmet: thick round shell down to the brow, over the ears' tops, low at the back
        auto helmetEdge = [&](float az) {
            float a = std::fabs(std::remainder(az, kTwoPi));
            return lerpf(browY + 0.012f, L.eyeMid.y - 0.035f, smoothstep(0.6f, 2.6f, a));
        };
        Part shell = headShell(ctr, half + Vec3(0.03f, 0.028f, 0.032f), 0.022f, helmetEdge, headJ, 48, 16);
        W.add("helmet_1", mat("helmet"), std::move(shell));
        // straps: from below the shell at the ears to under the chin + buckle
        for (float sx : {-1.0f, 1.0f}) {
            Vec3 a(sx * (half.x + 0.012f), L.eyeMid.y - 0.022f, L.headC.z + 0.01f);
            Vec3 b(sx * (half.x - 0.004f), L.mouth.y - 0.005f, L.headC.z - 0.03f);
            Vec3 c2(sx * 0.018f, L.chin.y - 0.012f, L.chin.z + 0.035f);
            W.add("helmet_1", mat("strap"), sweepPart({a, b, c2}, {Vec2(0.0075f, 0.0015f), Vec2(0.0075f, 0.0015f), Vec2(0.0075f, 0.0015f)}, Vec3(0, 0, 1), 6, headJ, true));
        }
        // cap: snug crown + curved brim + top button
        auto capEdge = [&](float az) {
            float a = std::fabs(std::remainder(az, kTwoPi));
            return lerpf(browY + 0.03f, L.eyeMid.y + 0.004f, smoothstep(0.5f, 2.2f, a));
        };
        Vec3 capR = half + Vec3(0.012f, 0.012f, 0.014f);
        W.add("helmet_2", mat("cap"), headShell(ctr, capR, 0.004f, capEdge, headJ, 40, 12));
        {
            Part brim;
            float yEdge = capEdge(0.0f);
            // front arc of the crown edge at the cap edge height
            int n = 18;
            std::vector<uint32_t> top0, top1, bot0, bot1;
            for (int k = 0; k <= n; ++k) {
                float az = (-62.0f + float(k) / float(n) * 124.0f) * kDeg2Rad;
                // crown radius at this azimuth and height
                float el = std::acos(clampf((yEdge - ctr.y) / capR.y, -1.0f, 1.0f));
                Vec3 inner = ctr + Vec3(std::sin(el) * std::sin(az) * capR.x, yEdge - ctr.y, -std::sin(el) * std::cos(az) * capR.z);
                Vec3 outDir = Vec3(std::sin(az), 0, -std::cos(az)).normalized();
                float len = 0.072f * std::cos(az * 0.8f);
                float curve = 0.012f * sqr(std::sin(az * 1.4f));
                Vec3 outer = inner + outDir * len + Vec3(0, -0.006f - curve, 0);
                for (int layer = 0; layer < 2; ++layer) {
                    float dy = layer == 0 ? 0.002f : -0.002f;
                    Vec3 nn = layer == 0 ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
                    for (int e = 0; e < 2; ++e) {
                        Vec3 p = (e == 0 ? inner : outer) + Vec3(0, dy, 0);
                        uint32_t id = uint32_t(brim.p.size());
                        brim.p.push_back(p);
                        brim.n.push_back(nn);
                        brim.uv.push_back(Vec2(float(k) / float(n), float(e)));
                        Skinned sk;
                        sk.j[0] = uint8_t(headJ);
                        brim.sk.push_back(sk);
                        (layer == 0 ? (e == 0 ? top0 : top1) : (e == 0 ? bot0 : bot1)).push_back(id);
                    }
                }
            }
            auto strip = [&](std::vector<uint32_t>& a, std::vector<uint32_t>& b, Vec3 want) {
                for (int k = 0; k < n; ++k) {
                    uint32_t i0 = a[size_t(k)], i1 = a[size_t(k) + 1], i2 = b[size_t(k) + 1], i3 = b[size_t(k)];
                    Vec3 fn = cross(brim.p[i1] - brim.p[i0], brim.p[i2] - brim.p[i0]);
                    if (dot(fn, want) >= 0.0f) brim.idx.insert(brim.idx.end(), {i0, i1, i2, i0, i2, i3});
                    else brim.idx.insert(brim.idx.end(), {i0, i2, i1, i0, i3, i2});
                }
            };
            strip(top0, top1, Vec3(0, 1, 0));
            strip(bot0, bot1, Vec3(0, -1, 0));
            strip(top1, bot1, Vec3(0, 0, -1));
            W.add("helmet_2", mat("cap"), std::move(brim));
            Vec3 btn = ctr + Vec3(0, capR.y + 0.001f, 0);
            W.add("helmet_2", mat("cap"), sweepPart({btn - Vec3(0, 0.002f, 0), btn + Vec3(0, 0.003f, 0)}, {Vec2(0.007f), Vec2(0.006f)}, Vec3(1, 0, 0), 10, headJ, true));
        }
    }
}

}  // namespace

bool generateHumanRider(const std::string& root, const std::string& outPath) {
    const std::string mh = root + "/third_party/makehuman";
    HumanModel H;
    if (!buildHuman(mh, H, 1.77f)) return false;
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(root) / "assets/textures/rider");
    Landmarks L = landmarks(H);
    {
        // skin albedo (tint multiplier) + ORM (baked occlusion, roughness) in the MakeHuman UV layout
        std::vector<float> ao = vertexAO(H, 48, 0.2f);
        const int TS = 2048;
        Tex alb(TS, TS), orm(TS / 2, TS / 2);
        for (size_t t = 0; t < H.tris.size(); ++t) {
            const auto& tr = H.tris[t];
            const auto& tu = H.triUV[t];
            if (tu[0] < 0 || tu[1] < 0 || tu[2] < 0) continue;
            Vec2 uv[3];
            for (int k = 0; k < 3; ++k) {
                Vec2 u = H.obj.vt[size_t(tu[size_t(k)])];
                uv[k] = Vec2(u.x, 1.0f - u.y);
            }
            auto sampleAt = [&](float l0, float l1, float l2) {
                Vec3 p = H.v[size_t(tr[0])] * l0 + H.v[size_t(tr[1])] * l1 + H.v[size_t(tr[2])] * l2;
                Vec3 n = (H.nrm[size_t(tr[0])] * l0 + H.nrm[size_t(tr[1])] * l1 + H.nrm[size_t(tr[2])] * l2).normalized();
                float a = ao[size_t(tr[0])] * l0 + ao[size_t(tr[1])] * l1 + ao[size_t(tr[2])] * l2;
                return std::make_tuple(p, n, a);
            };
            rasterUV(TS, TS, uv[0], uv[1], uv[2], [&](int x, int y, float l0, float l1, float l2) {
                auto [p, n, a] = sampleAt(l0, l1, l2);
                SkinSample ss = skinAt(L, p, n);
                // cavity darkening in the albedo (subtle) - the rest of the occlusion goes to ORM
                Vec3 col = ss.col * lerpf(0.9f, 1.0f, a);
                alb.at(x, y) = Vec4(col * 0.86f, 1.0f);
                alb.set[size_t(y) * size_t(TS) + size_t(x)] = 1;
            });
            rasterUV(TS / 2, TS / 2, uv[0], uv[1], uv[2], [&](int x, int y, float l0, float l1, float l2) {
                auto [p, n, a] = sampleAt(l0, l1, l2);
                SkinSample ss = skinAt(L, p, n);
                orm.at(x, y) = Vec4(std::pow(clampf(a, 0.0f, 1.0f), 0.8f), ss.rough, 0.0f, 1.0f);
                orm.set[size_t(y) * size_t(TS / 2) + size_t(x)] = 1;
            });
        }
        alb.dilate(12);
        orm.dilate(8);
        alb.save((fs::path(root) / "assets/textures/rider/skin_albedo.png").string(), true, 3);
        orm.save((fs::path(root) / "assets/textures/rider/skin_orm.png").string(), false, 3);
        fs::copy_file(fs::path(mh) / "eyes/brown_eye.png", fs::path(root) / "assets/textures/rider/eye_brown.png", fs::copy_options::overwrite_existing);
        std::printf("human: baked skin textures\n");
        // hair: strand colour + alpha (dense on the scalp, noisy fade at the hairline)
        Tex hair(1024, 1024);
        for (size_t t = 0; t < H.tris.size(); ++t) {
            const auto& tr = H.tris[t];
            const auto& tu = H.triUV[t];
            if (tu[0] < 0 || tu[1] < 0 || tu[2] < 0) continue;
            if ((H.v[size_t(tr[0])] - L.headC).length() > 0.17f) continue;
            Vec2 uv[3];
            for (int k = 0; k < 3; ++k) {
                Vec2 u = H.obj.vt[size_t(tu[size_t(k)])];
                uv[k] = Vec2(u.x, 1.0f - u.y);
            }
            rasterUV(1024, 1024, uv[0], uv[1], uv[2], [&](int x, int y, float l0, float l1, float l2) {
                Vec3 p = H.v[size_t(tr[0])] * l0 + H.v[size_t(tr[1])] * l1 + H.v[size_t(tr[2])] * l2;
                float az = std::atan2(p.x, -(p.z - L.headC.z));
                float hairline = L.eyeMid.y + lerpf(0.046f, -0.07f, smoothstep(0.9f, 2.6f, std::fabs(az)));
                hairline -= 0.03f * (1.0f - smoothstep(0.0f, 0.18f, std::fabs(std::fabs(az) - 1.35f)));
                // strands flow from the crown outwards / down: stretched noise across the flow
                Vec3 crown = L.headC + Vec3(0, 0.1f, 0.02f);
                Vec3 flow = (p - crown).normalized();
                Vec3 across = cross(flow, Vec3(0, 1, 0));
                float along = dot(p, flow), ac = dot(p, across);
                float strand = vnoise3(Vec3(ac * 900.0f, along * 60.0f, p.y * 40.0f));
                float edgeNoise = fbm3(p * 260.0f) * 0.012f;
                float a = smoothstep(hairline - 0.008f, hairline + 0.004f, p.y + edgeNoise);
                a *= 0.75f + 0.25f * strand;
                Vec3 col = lerp(Vec3(0.018f, 0.013f, 0.009f), Vec3(0.055f, 0.038f, 0.025f), strand);
                hair.at(x, y) = Vec4(col, clampf(a, 0.0f, 1.0f));
                hair.set[size_t(y) * 1024 + size_t(x)] = 1;
            });
        }
        hair.dilate(6);
        hair.save((fs::path(root) / "assets/textures/rider/hair.png").string(), true, 4);
        // neutral fabric maps (the tint colours the garment)
        for (auto [src, dst, mean] : {std::make_tuple("fabric_jersey", "fabric_jersey_lum", 0.82f), std::make_tuple("fabric_fleece", "fabric_fleece_lum", 0.8f),
                                      std::make_tuple("fabric_denim", "fabric_denim_lum", 0.78f), std::make_tuple("fabric_leather", "fabric_leather_lum", 0.8f)})
            fabricLuminance((fs::path(root) / "assets/textures" / (std::string(src) + "_diff.jpg")).string(),
                            (fs::path(root) / "assets/textures/rider" / (std::string(dst) + ".png")).string(), mean);
    }

    GlbWriter w;
    enum { MSkin = 0, MEye };
    const char* matNames[] = {"rider_skin", "rider_eye", "rider_top", "rider_top_fleece", "rider_pants", "rider_shoes", "rider_sole", "rider_lace",
                              "rider_hair", "rider_helmet", "rider_helmet_cap", "rider_strap"};
    const char* matKeys[] = {"skin", "eye", "top", "hoodie", "pants", "shoes", "sole", "lace", "hair", "helmet", "cap", "strap"};
    std::map<std::string, int> matIndex;
    for (int i = 0; i < 12; ++i) {
        GMaterial gm;
        gm.name = matNames[i];
        gm.tintable = i == 0 || (i >= 2 && i <= 5) || i == 9 || i == 10;
        matIndex[matKeys[i]] = w.addMaterial(gm);
    }

    // skeleton nodes, identity rest rotations
    std::vector<int> jointNodes;
    std::vector<Mat4> invBind;
    for (auto& j : H.joints) {
        GNode n;
        n.name = j.name;
        n.parent = j.parentIdx;
        n.translation = j.parentIdx >= 0 ? j.pos - H.joints[size_t(j.parentIdx)].pos : j.pos;
        jointNodes.push_back(w.addNode(n));
        invBind.push_back(Mat4::translation(-j.pos));
    }
    w.setSkin(jointNodes, invBind);

    // body split by region so clothing can hide covered skin (see riderPartVisible)
    auto region = [&](int j) {
        const std::string& n = H.joints[size_t(j)].name;
        if (n.rfind("upperarm", 0) == 0 || n.rfind("lowerarm", 0) == 0) return std::string("arms_skin");
        if (n.rfind("thigh", 0) == 0 || n.rfind("shin", 0) == 0) return std::string("legs_skin");
        if (n.rfind("foot", 0) == 0 || n.rfind("toes", 0) == 0) return std::string("feet_skin");
        return std::string("body");
    };
    std::map<std::string, Part> parts;
    std::map<std::string, std::map<std::pair<int, int>, uint32_t>> remaps;
    for (size_t t = 0; t < H.tris.size(); ++t) {
        // region of the triangle = region of its most "peripheral" corner
        std::string reg = region(H.dominant[size_t(H.tris[t][0])]);
        for (int k = 1; k < 3; ++k) {
            std::string r2 = region(H.dominant[size_t(H.tris[t][size_t(k)])]);
            if (r2 == "feet_skin" || (r2 == "legs_skin" && reg == "body") || (r2 == "arms_skin" && reg == "body")) reg = r2;
        }
        Part& P = parts[reg];
        auto& rm = remaps[reg];
        for (int k = 0; k < 3; ++k) {
            auto key = std::make_pair(H.tris[t][size_t(k)], H.triUV[t][size_t(k)]);
            auto it = rm.find(key);
            if (it == rm.end()) {
                uint32_t id = uint32_t(P.p.size());
                P.p.push_back(H.v[size_t(key.first)]);
                P.n.push_back(H.nrm[size_t(key.first)]);
                Vec2 uv = key.second >= 0 ? H.obj.vt[size_t(key.second)] : Vec2(0);
                P.uv.push_back(Vec2(uv.x, 1.0f - uv.y));
                P.sk.push_back(topFour(H.weights[size_t(key.first)]));
                it = rm.emplace(key, id).first;
            }
            P.idx.push_back(it->second);
        }
    }
    size_t tris = 0;
    for (auto& [name, P] : parts) {
        if (name == "feet_skin") continue;  // always inside the shoes
        GMesh gm;
        gm.name = name;
        tris += P.idx.size() / 3;
        addPart(gm, MSkin, P);
        GNode n;
        n.name = name;
        n.mesh = w.addMesh(gm);
        n.skinned = true;
        w.addNode(n);
    }
    {
        Part eyes;
        if (fitEyes(mh, H, eyes, H.J("eye_l"), H.J("eye_r"))) {
            GMesh gm;
            gm.name = "eyes";
            tris += eyes.idx.size() / 3;
            addPart(gm, MEye, eyes);
            GNode n;
            n.name = "eyes";
            n.mesh = w.addMesh(gm);
            n.skinned = true;
            w.addNode(n);
        }
    }

    {
        Wardrobe wd;
        buildWardrobe(H, L, wd, matIndex);
        for (auto& [name, list] : wd.meshes) {
            GMesh gm;
            gm.name = name;
            for (auto& [mi, part] : list) {
                tris += part.idx.size() / 3;
                addPart(gm, mi, part);
            }
            GNode n;
            n.name = name;
            n.mesh = w.addMesh(gm);
            n.skinned = true;
            w.addNode(n);
        }
    }

    // animation clips (core joints; the extra joints stay at rest unless the runtime drives them)
    auto defs = clipDefs();
    const RiderJointDef* bpj = riderJoints();
    for (auto& cd : defs) {
        GAnimation a;
        a.name = cd.name;
        for (int j = 0; j < RJ_Count; ++j) {
            int gj = H.J(bpj[j].name);
            if (gj < 0) continue;
            GChannel ch;
            ch.node = jointNodes[size_t(gj)];
            ch.path = "rotation";
            for (auto& k : cd.keys) {
                Quat q = eulerDeg(k.second.e[j]).normalized();
                ch.times.push_back(k.first);
                ch.values.push_back(Vec4(q.x, q.y, q.z, q.w));
            }
            a.channels.push_back(ch);
        }
        GChannel tr;
        int pj = H.J("pelvis");
        tr.node = jointNodes[size_t(pj)];
        tr.path = "translation";
        Vec3 restLocal = H.joints[size_t(pj)].pos;
        for (auto& k : cd.keys) {
            Vec3 t = restLocal + k.second.pelvis;
            tr.times.push_back(k.first);
            tr.values.push_back(Vec4(t.x, t.y, t.z, 0));
        }
        a.channels.push_back(tr);
        w.addAnimation(a);
    }
    bool ok = w.write(outPath);
    std::printf("human rider: %zu triangles, %zu clips -> %s (%s)\n", tris, defs.size(), outPath.c_str(), ok ? "ok" : "FAILED");
    // blueprint rest positions for rider_blueprint.h (ragdoll / IK share them)
    for (int j = 0; j < RJ_Count; ++j) {
        Vec3 p = H.jp(bpj[j].name);
        std::printf("  {\"%s\", ..., {%.3ff, %.3ff, %.3ff}},\n", bpj[j].name, p.x, p.y, p.z);
    }
    return ok;
}

}  // namespace sw::tools
