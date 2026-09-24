// glTF 2.0 loader (cgltf): static + skinned meshes, PBR materials, skeletons and animation clips
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"

#include <cgltf.h>
#include <stb_image.h>

#include <algorithm>
#include <map>

namespace sw {

namespace {

Transform nodeLocal(const cgltf_node* n) {
    Transform t;
    if (n->has_matrix) {
        Mat4 m;
        for (int i = 0; i < 16; ++i) m.m[i] = n->matrix[i];
        t.position = m.translationPart();
        t.scale = m.scalePart();
        t.rotation = Quat::fromMat(m);
        return t;
    }
    if (n->has_translation) t.position = Vec3(n->translation[0], n->translation[1], n->translation[2]);
    if (n->has_rotation) t.rotation = Quat(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]).normalized();
    if (n->has_scale) t.scale = Vec3(n->scale[0], n->scale[1], n->scale[2]);
    return t;
}

struct Ctx {
    AssetManager* am = nullptr;
    const cgltf_data* data = nullptr;
    std::string dir;
    std::string path;
    std::map<std::pair<const cgltf_image*, bool>, TexturePtr> images;
};

TexturePtr loadImage(Ctx& ctx, const cgltf_texture_view& view, bool srgb) {
    if (!view.texture || !view.texture->image) return nullptr;
    const cgltf_image* img = view.texture->image;
    auto key = std::make_pair(img, srgb);
    auto it = ctx.images.find(key);
    if (it != ctx.images.end()) return it->second;
    TexturePtr tex;
    if (img->buffer_view) {
        const uint8_t* bytes = static_cast<const uint8_t*>(cgltf_buffer_view_data(img->buffer_view));
        int w, h, c;
        stbi_uc* px = stbi_load_from_memory(bytes, int(img->buffer_view->size), &w, &h, &c, 4);
        if (px) {
            tex = createTextureRGBA8(ctx.path + ":" + (img->name ? img->name : "image"), w, h, px, srgb, true);
            stbi_image_free(px);
        }
    } else if (img->uri) {
        std::string rel = ctx.dir.empty() ? img->uri : ctx.dir + "/" + img->uri;
        tex = ctx.am->texture(rel, srgb);
    }
    ctx.images[key] = tex;
    return tex;
}

MaterialPtr convertMaterial(Ctx& ctx, const cgltf_material* gm, size_t index) {
    std::string name = ctx.path + ":" + (gm->name ? gm->name : "material" + std::to_string(index));
    MaterialPtr m = ctx.am->createMaterial(name);
    if (gm->has_pbr_metallic_roughness) {
        const auto& pbr = gm->pbr_metallic_roughness;
        m->baseColorFactor = Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
        m->metallic = pbr.metallic_factor;
        m->roughness = pbr.roughness_factor;
        m->baseColor = loadImage(ctx, pbr.base_color_texture, true);
        m->orm = loadImage(ctx, pbr.metallic_roughness_texture, false);
    }
    m->normal = loadImage(ctx, gm->normal_texture, false);
    if (gm->normal_texture.texture) m->normalScale = gm->normal_texture.scale;
    m->emissive = loadImage(ctx, gm->emissive_texture, true);
    m->emissiveFactor = Vec3(gm->emissive_factor[0], gm->emissive_factor[1], gm->emissive_factor[2]);
    m->emissiveStrength = gm->has_emissive_strength ? gm->emissive_strength.emissive_strength : 1.0f;
    m->alphaMode = gm->alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                   : gm->alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                             : AlphaMode::Opaque;
    m->alphaCutoff = gm->alpha_cutoff;
    m->doubleSided = gm->double_sided;
    // extras: {"tintable": true, "surface": "metal"}
    if (gm->extras.data) {
        std::string extras = gm->extras.data;
        if (extras.find("\"tintable\"") != std::string::npos && extras.find("true") != std::string::npos) m->tintable = true;
    }
    return m;
}

}  // namespace

ModelPtr loadGltfModel(AssetManager& am, const std::string& relPath) {
    auto model = std::make_shared<Model>();
    model->path = relPath;
    std::string abs = fs::resolve(relPath);
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, abs.c_str(), &data) != cgltf_result_success) {
        LOG_ERROR("gltf: cannot parse %s", abs.c_str());
        return model;
    }
    if (cgltf_load_buffers(&options, data, abs.c_str()) != cgltf_result_success) {
        LOG_ERROR("gltf: cannot load buffers for %s", abs.c_str());
        cgltf_free(data);
        return model;
    }
    Ctx ctx;
    ctx.am = &am;
    ctx.data = data;
    ctx.path = relPath;
    ctx.dir = fs::parentPath(relPath);

    for (size_t i = 0; i < data->materials_count; ++i) model->materials.push_back(convertMaterial(ctx, &data->materials[i], i));

    // nodes
    std::map<const cgltf_node*, int> nodeIndex;
    for (size_t i = 0; i < data->nodes_count; ++i) nodeIndex[&data->nodes[i]] = int(i);
    model->nodes.resize(data->nodes_count);
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        ModelNode& mn = model->nodes[i];
        mn.name = n.name ? n.name : "node" + std::to_string(i);
        mn.parent = n.parent ? nodeIndex[n.parent] : -1;
        mn.local = nodeLocal(&n);
    }

    // skeleton from the first skin; joints ordered parents-first
    std::vector<int> jointOrder;  // new index -> skin joint index
    std::vector<int> skinToNew;
    if (data->skins_count > 0) {
        const cgltf_skin& skin = data->skins[0];
        auto skel = std::make_shared<Skeleton>();
        size_t jc = skin.joints_count;
        std::map<const cgltf_node*, int> jointOf;
        for (size_t j = 0; j < jc; ++j) jointOf[skin.joints[j]] = int(j);
        auto depth = [&](const cgltf_node* n) {
            int d = 0;
            while (n->parent && jointOf.count(n->parent)) {
                n = n->parent;
                ++d;
            }
            return d;
        };
        jointOrder.resize(jc);
        for (size_t j = 0; j < jc; ++j) jointOrder[j] = int(j);
        std::stable_sort(jointOrder.begin(), jointOrder.end(), [&](int a, int b) { return depth(skin.joints[a]) < depth(skin.joints[b]); });
        skinToNew.resize(jc);
        for (size_t k = 0; k < jc; ++k) skinToNew[size_t(jointOrder[k])] = int(k);
        skel->joints.resize(jc);
        for (size_t k = 0; k < jc; ++k) {
            int sj = jointOrder[k];
            const cgltf_node* n = skin.joints[sj];
            Joint& J = skel->joints[k];
            J.name = n->name ? n->name : "joint" + std::to_string(k);
            J.parent = (n->parent && jointOf.count(n->parent)) ? skinToNew[size_t(jointOf[n->parent])] : -1;
            J.bindLocal = nodeLocal(n);
            if (skin.inverse_bind_matrices) {
                float m[16];
                cgltf_accessor_read_float(skin.inverse_bind_matrices, size_t(sj), m, 16);
                for (int e = 0; e < 16; ++e) J.inverseBind.m[e] = m[e];
            }
            skel->byName[J.name] = int(k);
            model->nodes[size_t(nodeIndex[n])].joint = int(k);
        }
        model->skeleton = skel;
    }

    // meshes
    for (size_t ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh) continue;
        const cgltf_mesh& gm = *node.mesh;
        ModelMesh mm;
        mm.name = gm.name ? gm.name : (node.name ? node.name : "mesh");
        mm.node = int(ni);
        MeshData& md = mm.data;
        md.name = relPath + ":" + mm.name;
        bool skinned = node.skin != nullptr;
        for (size_t pi = 0; pi < gm.primitives_count; ++pi) {
            const cgltf_primitive& prim = gm.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *tan = nullptr, *uv = nullptr, *jnt = nullptr, *wgt = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_tangent) tan = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) jnt = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) wgt = at.data;
            }
            if (!pos) continue;
            uint32_t base = uint32_t(md.vertices.size());
            size_t vc = pos->count;
            for (size_t v = 0; v < vc; ++v) {
                Vertex vx;
                cgltf_accessor_read_float(pos, v, &vx.position.x, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, &vx.normal.x, 3);
                if (tan) cgltf_accessor_read_float(tan, v, &vx.tangent.x, 4);
                if (uv) cgltf_accessor_read_float(uv, v, &vx.uv.x, 2);
                md.vertices.push_back(vx);
                if (skinned) {
                    SkinVertex sv;
                    if (jnt && wgt) {
                        cgltf_uint j4[4] = {0, 0, 0, 0};
                        cgltf_accessor_read_uint(jnt, v, j4, 4);
                        float w4[4] = {1, 0, 0, 0};
                        cgltf_accessor_read_float(wgt, v, w4, 4);
                        float sum = w4[0] + w4[1] + w4[2] + w4[3];
                        for (int k = 0; k < 4; ++k) {
                            int nj = j4[k] < skinToNew.size() ? skinToNew[j4[k]] : 0;
                            sv.joints[k] = uint8_t(std::min(nj, 255));
                            sv.weights[k] = sum > 0 ? w4[k] / sum : (k == 0 ? 1.0f : 0.0f);
                        }
                    }
                    md.skin.push_back(sv);
                }
            }
            SubMesh sm;
            sm.firstIndex = uint32_t(md.indices.size());
            if (prim.indices) {
                for (size_t k = 0; k < prim.indices->count; ++k) md.indices.push_back(base + uint32_t(cgltf_accessor_read_index(prim.indices, k)));
                sm.indexCount = uint32_t(prim.indices->count);
            } else {
                for (size_t k = 0; k < vc; ++k) md.indices.push_back(base + uint32_t(k));
                sm.indexCount = uint32_t(vc);
            }
            sm.material = int(mm.materials.size());
            md.submeshes.push_back(sm);
            MaterialPtr mat = prim.material ? model->materials[size_t(prim.material - data->materials)] : am.defaultMaterial();
            mm.materials.push_back(mat);
            if (!nrm) md.computeNormals();
            if (!tan) md.computeTangents();
        }
        if (md.vertices.empty()) continue;
        md.computeBounds();
        mm.gpu = createGpuMesh(md, !skinned);
        model->bounds.expand(md.bounds);
        model->nodes[ni].mesh = int(model->meshes.size());
        model->meshes.push_back(std::move(mm));
    }

    // animation clips (joint channels only)
    for (size_t ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& ga = data->animations[ai];
        auto clip = std::make_shared<AnimationClip>();
        clip->name = ga.name ? ga.name : "clip" + std::to_string(ai);
        for (size_t ci = 0; ci < ga.channels_count; ++ci) {
            const cgltf_animation_channel& ch = ga.channels[ci];
            if (!ch.target_node || !ch.sampler) continue;
            int ni = nodeIndex[ch.target_node];
            int joint = model->nodes[size_t(ni)].joint;
            if (joint < 0) continue;
            AnimChannel ac;
            ac.joint = joint;
            if (ch.target_path == cgltf_animation_path_type_translation) ac.path = AnimPath::Translation;
            else if (ch.target_path == cgltf_animation_path_type_rotation) ac.path = AnimPath::Rotation;
            else if (ch.target_path == cgltf_animation_path_type_scale) ac.path = AnimPath::Scale;
            else continue;
            const cgltf_animation_sampler& s = *ch.sampler;
            ac.interp = s.interpolation == cgltf_interpolation_type_step ? AnimInterp::Step : AnimInterp::Linear;
            bool cubic = s.interpolation == cgltf_interpolation_type_cubic_spline;
            size_t keys = s.input->count;
            ac.times.resize(keys);
            for (size_t k = 0; k < keys; ++k) cgltf_accessor_read_float(s.input, k, &ac.times[k], 1);
            int comps = ac.path == AnimPath::Rotation ? 4 : 3;
            ac.values.resize(keys);
            for (size_t k = 0; k < keys; ++k) {
                float v[4] = {0, 0, 0, 1};
                cgltf_accessor_read_float(s.output, cubic ? k * 3 + 1 : k, v, size_t(comps));
                ac.values[k] = Vec4(v[0], v[1], v[2], comps == 4 ? v[3] : 0.0f);
            }
            if (!ac.times.empty()) clip->duration = std::max(clip->duration, ac.times.back());
            clip->channels.push_back(std::move(ac));
        }
        model->clips.push_back(clip);
    }
    cgltf_free(data);
    LOG_INFO("gltf: loaded %s (%zu meshes, %zu joints, %zu clips)", relPath.c_str(), model->meshes.size(),
             model->skeleton ? model->skeleton->size() : size_t(0), model->clips.size());
    return model;
}

}  // namespace sw
