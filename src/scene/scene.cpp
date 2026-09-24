#include "scene/scene.h"
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/timer.h"

#include <algorithm>
#include <map>

namespace sw {

bool Entity::hasTag(const std::string& t) const { return std::find(tags.begin(), tags.end(), t) != tags.end(); }

// ---------------------------------------------------------------------------
PrefabRegistry& prefabs() {
    static PrefabRegistry r;
    return r;
}

void PrefabRegistry::add(const std::string& name, const std::string& category, PrefabFn fn, Json defaults) {
    entries_[name] = Entry{category, std::move(fn), std::move(defaults)};
}

bool PrefabRegistry::build(const std::string& name, const Json& params, const std::string& material, PrefabBuild& out) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        LOG_WARN("scene: unknown prefab '%s'", name.c_str());
        return false;
    }
    Json merged = it->second.defaults;
    if (params.is_object())
        for (auto& [k, v] : params.items()) merged[k] = v;
    try {
        it->second.fn(merged, material, out);
    } catch (const std::exception& e) {
        LOG_ERROR("scene: prefab '%s' failed: %s", name.c_str(), e.what());
        return false;
    }
    return true;
}

const Json& PrefabRegistry::defaults(const std::string& name) const {
    static Json empty = Json::object();
    auto it = entries_.find(name);
    return it == entries_.end() ? empty : it->second.defaults;
}

std::vector<std::pair<std::string, std::string>> PrefabRegistry::list() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& [n, e] : entries_) out.push_back({n, e.category});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.second == b.second ? a.first < b.first : a.second < b.second; });
    return out;
}

// ---------------------------------------------------------------------------
Scene::~Scene() { clear(); }

Entity* Scene::create(const std::string& name, EntityId parent) {
    auto e = std::make_unique<Entity>();
    e->id = nextId_++;
    e->name = name;
    Entity* raw = e.get();
    entities_[raw->id] = std::move(e);
    if (parent && get(parent)) {
        raw->parent = parent;
        get(parent)->children.push_back(raw->id);
    } else {
        roots_.push_back(raw->id);
    }
    ++revision;
    return raw;
}

void Scene::destroy(EntityId id) {
    Entity* e = get(id);
    if (!e) return;
    std::vector<EntityId> kids = e->children;
    for (EntityId c : kids) destroy(c);
    releaseComponents(*e);
    if (e->parent) {
        if (Entity* p = get(e->parent)) {
            auto& ch = p->children;
            ch.erase(std::remove(ch.begin(), ch.end(), id), ch.end());
        }
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), id), roots_.end());
    }
    entities_.erase(id);
    ++revision;
}

Entity* Scene::get(EntityId id) {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : it->second.get();
}
const Entity* Scene::get(EntityId id) const {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : it->second.get();
}

Entity* Scene::find(const std::string& name) {
    for (auto& [id, e] : entities_)
        if (e->name == name) return e.get();
    return nullptr;
}

std::vector<Entity*> Scene::all() {
    std::vector<Entity*> v;
    v.reserve(entities_.size());
    for (auto& [id, e] : entities_) v.push_back(e.get());
    std::sort(v.begin(), v.end(), [](Entity* a, Entity* b) { return a->id < b->id; });
    return v;
}

void Scene::setParent(EntityId child, EntityId parent) {
    Entity* c = get(child);
    if (!c || child == parent) return;
    // prevent cycles
    for (EntityId p = parent; p; p = get(p) ? get(p)->parent : 0)
        if (p == child) return;
    if (c->parent) {
        if (Entity* op = get(c->parent)) op->children.erase(std::remove(op->children.begin(), op->children.end(), child), op->children.end());
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), child), roots_.end());
    }
    c->parent = parent && get(parent) ? parent : 0;
    if (c->parent)
        get(c->parent)->children.push_back(child);
    else
        roots_.push_back(child);
    markDirty(child);
}

void Scene::markDirty(EntityId id) {
    Entity* e = get(id);
    if (!e) return;
    e->worldDirty = true;
    for (EntityId c : e->children) markDirty(c);
}

void Scene::updateWorld(Entity& e, const Mat4& parentWorld) {
    if (e.worldDirty) {
        e.world = parentWorld * e.local.matrix();
        e.worldDirty = false;
    }
    for (EntityId c : e.children)
        if (Entity* ch = get(c)) {
            if (e.worldDirty) ch->worldDirty = true;
            updateWorld(*ch, e.world);
        }
}

void Scene::updateTransforms() {
    for (EntityId r : roots_)
        if (Entity* e = get(r)) updateWorld(*e, Mat4::identity());
}

// ---------------------------------------------------------------------------
void Scene::releaseComponents(Entity& e) {
    if (e.meshRenderer && render_ && e.meshRenderer->handle != RenderScene::kInvalid) render_->remove(e.meshRenderer->handle);
    if (e.light && render_ && e.light->handle != RenderScene::kInvalid) render_->removeLight(e.light->handle);
    if (e.collider && e.collider->body != kNoBody) physics().removeBody(e.collider->body);
    if (e.rigidBody && e.rigidBody->body != kNoBody) physics().removeBody(e.rigidBody->body);
    e.meshRenderer.reset();
    e.light.reset();
    e.collider.reset();
    e.rigidBody.reset();
    e.rails.clear();
    e.spawn.reset();
    e.zone.reset();
    e.audioSource.reset();
}

void Scene::instantiate(Entity& e) {
    if (e.prefab.empty()) return;
    updateTransforms();
    PrefabBuild out;
    if (!prefabs().build(e.prefab, e.params, e.material, out)) return;
    e.editorOnly = out.editorHelper;

    std::string primaryMat = out.materials.empty() ? e.material : out.materials[0];
    int surface = out.surface.empty() ? surfaces().find(assets().material(primaryMat)->surface) : surfaces().find(out.surface);

    if (!out.mesh.vertices.empty()) {
        auto mr = std::make_unique<MeshRendererComponent>();
        for (auto& m : out.materials) mr->materials.push_back(assets().material(m));
        if (mr->materials.empty()) mr->materials.push_back(assets().material(e.material));
        mr->castShadows = out.castShadows && !out.editorHelper;
        mr->isStatic = out.isStatic;
        mr->cullDistance = out.cullDistance;
        if (!out.meshKey.empty()) {
            mr->meshKey = out.meshKey;
            MeshData md = out.mesh;
            mr->mesh = assets().getOrCreateMesh(out.meshKey, [&]() { return md; }, true);
        } else {
            out.mesh.name = e.name.empty() ? e.prefab : e.name;
            mr->mesh = createGpuMesh(out.mesh, false);
            mr->cpu = std::make_shared<MeshData>(out.mesh);
        }
        if (render_) {
            RenderObject ro;
            ro.mesh = mr->mesh;
            ro.materials = mr->materials;
            ro.world = e.world;
            ro.tint = mr->tint;
            ro.castShadows = mr->castShadows;
            ro.isStatic = mr->isStatic && !out.rigidBody;
            ro.cullDistance = mr->cullDistance;
            ro.userId = e.id;
            ro.layer = out.editorHelper ? LayerEditor : (mr->meshKey.empty() ? LayerWorld : LayerProps);
            ro.visible = e.visible && (!out.editorHelper || editorMode);
            mr->handle = render_->add(ro);
        }
        e.meshRenderer = std::move(mr);
    }

    if (enablePhysics && !out.editorHelper) {
        if (out.rigidBody) {
            BodyDesc bd;
            ShapeDesc sd;
            sd.kind = out.rigidBody->shape;
            sd.halfExtents = out.rigidBody->halfExtents;
            sd.radius = out.rigidBody->radius;
            sd.halfHeight = out.rigidBody->halfExtents.y;
            sd.position = out.boxCenter;
            bd.shapes.push_back(sd);
            bd.position = e.world.translationPart();
            bd.rotation = Quat::fromMat(e.world);
            bd.mass = out.rigidBody->mass;
            bd.layer = PhysLayer::Debris;
            bd.surface = surface;
            bd.entity = e.id;
            bd.friction = 0.7f;
            bd.restitution = 0.2f;
            out.rigidBody->body = physics().createBody(bd);
            e.rigidBody = std::move(out.rigidBody);
        } else if (out.collider != ColliderComponent::Kind::None) {
            auto col = std::make_unique<ColliderComponent>();
            col->kind = out.collider;
            col->surface = surface;
            const MeshData& cm = out.collisionMesh.indices.empty() ? out.mesh : out.collisionMesh;
            if (out.collider == ColliderComponent::Kind::Mesh && !cm.indices.empty()) {
                col->mesh = std::make_shared<MeshData>(cm);
                col->body = physics().createStaticMesh(*col->mesh, e.world, surface, e.id);
            } else if (out.collider == ColliderComponent::Kind::Box) {
                Vec3 s = e.world.scalePart();
                col->halfExtents = out.boxHalfExtents;
                col->center = out.boxCenter;
                col->body = physics().createStaticBox(e.world.transformPoint(out.boxCenter), out.boxHalfExtents * s, Quat::fromMat(e.world),
                                                      surface, e.id);
            }
            e.collider = std::move(col);
        }
    }

    for (auto& r : out.rails) {
        RailComponent rc = r;
        if (rc.surface == 0 && !out.surface.empty()) rc.surface = surface;
        e.rails.push_back(rc);
    }
    if (!out.lights.empty()) {
        auto lc = std::make_unique<LightComponent>(out.lights[0]);
        if (render_) {
            LightProxy lp = lc->light;
            lp.position = e.world.transformPoint(lc->light.position);
            lp.direction = e.world.transformDir(lc->light.direction).normalized();
            lc->handle = render_->addLight(lp);
        }
        e.light = std::move(lc);
    }
    e.spawn = std::move(out.spawn);
    e.zone = std::move(out.zone);
    e.audioSource = std::move(out.audio);
    ++revision;
}

void Scene::syncTransform(Entity& e) {
    markDirty(e.id);
    updateTransforms();
    std::vector<Entity*> stack{&e};
    while (!stack.empty()) {
        Entity* x = stack.back();
        stack.pop_back();
        if (x->meshRenderer && render_ && x->meshRenderer->handle != RenderScene::kInvalid) render_->setTransform(x->meshRenderer->handle, x->world);
        if (x->light && render_ && x->light->handle != RenderScene::kInvalid) {
            if (LightProxy* lp = render_->light(x->light->handle)) {
                lp->position = x->world.transformPoint(x->light->light.position);
                lp->direction = x->world.transformDir(x->light->light.direction).normalized();
            }
        }
        if (x->collider && x->collider->body != kNoBody) {
            // static colliders are rebuilt (scale may have changed)
            physics().removeBody(x->collider->body);
            x->collider->body = kNoBody;
            if (x->collider->kind == ColliderComponent::Kind::Mesh && x->collider->mesh) {
                x->collider->body = physics().createStaticMesh(*x->collider->mesh, x->world, x->collider->surface, x->id);
            } else if (x->collider->kind == ColliderComponent::Kind::Box) {
                Vec3 s = x->world.scalePart();
                x->collider->body = physics().createStaticBox(x->world.transformPoint(x->collider->center), x->collider->halfExtents * s,
                                                              Quat::fromMat(x->world), x->collider->surface, x->id);
            }
        }
        if (x->rigidBody && x->rigidBody->body != kNoBody)
            physics().setTransform(x->rigidBody->body, x->world.translationPart(), Quat::fromMat(x->world));
        for (EntityId c : x->children)
            if (Entity* ch = get(c)) stack.push_back(ch);
    }
    ++revision;
}

Entity* Scene::duplicate(EntityId id) {
    Entity* src = get(id);
    if (!src) return nullptr;
    Json j = entityToJson(*src);
    j.erase("id");
    Entity* e = entityFromJson(j, src->parent);
    if (!e) return nullptr;
    e->name = src->name + "_copy";
    e->local.position += Vec3(1.0f, 0, 1.0f);
    markDirty(e->id);
    updateTransforms();
    instantiate(*e);
    for (EntityId c : src->children) {
        Json cj = entityToJson(*get(c));
        cj.erase("id");
        Entity* ce = entityFromJson(cj, e->id);
        if (ce) {
            markDirty(ce->id);
            updateTransforms();
            instantiate(*ce);
        }
    }
    return e;
}

// ---------------------------------------------------------------------------
Json Scene::entityToJson(const Entity& e) const {
    Json j;
    j["id"] = e.id;
    j["name"] = e.name;
    if (!e.prefab.empty()) j["prefab"] = e.prefab;
    if (!e.params.empty()) j["params"] = e.params;
    if (!e.material.empty()) j["material"] = e.material;
    if (!e.tags.empty()) j["tags"] = e.tags;
    if (!e.area.empty()) j["area"] = e.area;
    auto r3 = [](const Vec3& v) {
        auto r = [](float f) { return std::round(f * 1000.0f) / 1000.0f; };
        return Json::array({r(v.x), r(v.y), r(v.z)});
    };
    j["position"] = r3(e.local.position);
    Vec3 eul = quatToEulerDeg(e.local.rotation);
    if (eul.lengthSq() > 1e-6f) j["rotation"] = r3(eul);
    if (e.local.scale != Vec3(1, 1, 1)) j["scale"] = r3(e.local.scale);
    if (e.parent) j["parent"] = e.parent;
    if (!e.visible) j["visible"] = false;
    return j;
}

Entity* Scene::entityFromJson(const Json& j, EntityId parentOverride) {
    EntityId parent = parentOverride ? parentOverride : jget<EntityId>(j, "parent", 0);
    Entity* e = create(jget<std::string>(j, "name", "entity"), parent);
    if (j.contains("id")) {
        // keep file ids stable so parent references resolve
        EntityId want = j["id"].get<EntityId>();
        if (want != e->id && !get(want)) {
            auto node = std::move(entities_[e->id]);
            entities_.erase(e->id);
            if (parent && get(parent)) {
                auto& ch = get(parent)->children;
                std::replace(ch.begin(), ch.end(), e->id, want);
            } else {
                std::replace(roots_.begin(), roots_.end(), e->id, want);
            }
            node->id = want;
            entities_[want] = std::move(node);
            e = get(want);
            nextId_ = std::max(nextId_, want + 1);
        }
    }
    e->prefab = jget<std::string>(j, "prefab", "");
    if (j.contains("params")) e->params = j["params"];
    e->material = jget<std::string>(j, "material", "");
    if (j.contains("tags")) e->tags = j["tags"].get<std::vector<std::string>>();
    e->area = jget<std::string>(j, "area", "");
    e->local.position = jvec3(j, "position", Vec3(0));
    e->local.rotation = jquat(j, "rotation", Quat::identity());
    e->local.scale = jvec3(j, "scale", Vec3(1));
    e->visible = jget<bool>(j, "visible", true);
    return e;
}

bool Scene::load(const std::string& absPath) {
    Timer t;
    auto j = loadJsonFile(absPath);
    if (!j) return false;
    clear();
    name_ = jget<std::string>(*j, "name", fs::stem(absPath));
    if (j->contains("environment")) environment_ = (*j)["environment"];
    if (j->contains("metadata")) metadata_ = (*j)["metadata"];
    const Json& ents = (*j)["entities"];
    // two passes: create nodes (parents may come later in the file), then parent + instantiate
    std::vector<std::pair<Entity*, EntityId>> pending;
    for (const Json& ej : ents) {
        Json copy = ej;
        EntityId parent = jget<EntityId>(ej, "parent", 0);
        copy.erase("parent");
        Entity* e = entityFromJson(copy);
        if (e && parent) pending.push_back({e, parent});
    }
    for (auto& [e, parent] : pending) setParent(e->id, parent);
    updateTransforms();
    int n = 0;
    for (auto& [id, e] : entities_) {
        instantiate(*e);
        ++n;
    }
    physics().system();
    LOG_INFO("scene: loaded '%s' (%d entities) in %.0f ms", name_.c_str(), n, t.milliseconds());
    return true;
}

bool Scene::save(const std::string& absPath) const {
    Json j;
    j["version"] = 1;
    j["name"] = name_;
    j["environment"] = environment_;
    if (!metadata_.empty()) j["metadata"] = metadata_;
    Json ents = Json::array();
    std::vector<const Entity*> list;
    for (auto& [id, e] : entities_) list.push_back(e.get());
    std::sort(list.begin(), list.end(), [](const Entity* a, const Entity* b) { return a->id < b->id; });
    for (const Entity* e : list) ents.push_back(entityToJson(*e));
    j["entities"] = ents;
    bool ok = saveJsonFile(absPath, j, true);
    LOG_INFO("scene: saved %s (%zu entities)", absPath.c_str(), list.size());
    return ok;
}

void Scene::clear() {
    clearStaticBatches();
    for (auto& [id, e] : entities_) releaseComponents(*e);
    entities_.clear();
    roots_.clear();
    nextId_ = 1;
    environment_ = Json::object();
    metadata_ = Json::object();
    ++revision;
}

// ---------------------------------------------------------------------------
void Scene::buildStaticBatches(float sectorSize) {
    if (!render_) return;
    clearStaticBatches();
    Timer t;
    struct Group {
        MeshData mesh;
        MaterialPtr material;
        bool castShadows = true;
    };
    std::map<std::tuple<int, int, uint32_t, bool>, Group> groups;
    // we need the CPU mesh of unique meshes: regenerate them from the prefab (cheap)
    int merged = 0;
    for (auto& [id, eptr] : entities_) {
        Entity& e = *eptr;
        auto* mr = e.meshRenderer.get();
        if (!mr || !mr->isStatic || !mr->meshKey.empty() || e.editorOnly || !e.visible || e.rigidBody || !mr->cpu) continue;
        MeshData& mesh = *mr->cpu;
        if (mesh.vertices.empty()) continue;
        if (!mesh.bounds.valid()) mesh.computeBounds();
        Vec3 c = e.world.transformPoint(mesh.bounds.center());
        int sx = int(std::floor(c.x / sectorSize)), sz = int(std::floor(c.z / sectorSize));
        Mat4 nm = e.world.affineInverse().transposed();
        for (const SubMesh& sm : mesh.submeshes) {
            MaterialPtr mat = size_t(sm.material) < mr->materials.size() ? mr->materials[size_t(sm.material)] : assets().defaultMaterial();
            if (mat->alphaMode == AlphaMode::Blend) continue;
            Group& g = groups[{sx, sz, mat->id, mr->castShadows}];
            g.material = mat;
            g.castShadows = mr->castShadows;
            uint32_t base = uint32_t(g.mesh.vertices.size());
            // copy only the vertices referenced by this submesh
            std::unordered_map<uint32_t, uint32_t> remap;
            if (g.mesh.submeshes.empty()) g.mesh.submeshes.push_back({0, 0, 0});
            for (uint32_t k = 0; k < sm.indexCount; ++k) {
                uint32_t src = mesh.indices[sm.firstIndex + k];
                auto it = remap.find(src);
                uint32_t dst;
                if (it == remap.end()) {
                    Vertex v = mesh.vertices[src];
                    v.position = e.world.transformPoint(v.position);
                    v.normal = nm.transformDir(v.normal).normalized();
                    v.tangent = Vec4(e.world.transformDir(v.tangent.xyz()).normalized(), v.tangent.w);
                    dst = base + uint32_t(remap.size());
                    remap[src] = dst;
                    g.mesh.vertices.push_back(v);
                } else {
                    dst = it->second;
                }
                g.mesh.indices.push_back(dst);
            }
            g.mesh.submeshes[0].indexCount = uint32_t(g.mesh.indices.size());
        }
        if (mr->handle != RenderScene::kInvalid) {
            render_->remove(mr->handle);
            mr->handle = RenderScene::kInvalid;
        }
        mr->batched = true;
        ++merged;
    }
    for (auto& [key, g] : groups) {
        if (g.mesh.indices.empty()) continue;
        g.mesh.name = "batch";
        g.mesh.computeBounds();
        optimizeMesh(g.mesh);
        RenderObject ro;
        ro.mesh = createGpuMesh(g.mesh, true);
        ro.materials = {g.material};
        ro.castShadows = g.castShadows;
        ro.isStatic = true;
        ro.layer = LayerWorld;
        batches_.push_back(render_->add(ro));
    }
    LOG_INFO("scene: static batching merged %d entities into %zu batches in %.0f ms", merged, batches_.size(), t.milliseconds());
}

void Scene::clearStaticBatches() {
    if (!render_) {
        batches_.clear();
        return;
    }
    for (auto h : batches_) render_->remove(h);
    bool any = !batches_.empty();
    batches_.clear();
    if (!any) return;
    // restore individual proxies
    for (auto& [id, eptr] : entities_) {
        Entity& e = *eptr;
        auto* mr = e.meshRenderer.get();
        if (!mr || !mr->batched) continue;
        RenderObject ro;
        ro.mesh = mr->mesh;
        ro.materials = mr->materials;
        ro.world = e.world;
        ro.tint = mr->tint;
        ro.castShadows = mr->castShadows;
        ro.isStatic = true;
        ro.userId = e.id;
        ro.visible = e.visible;
        mr->handle = render_->add(ro);
        mr->batched = false;
    }
}

}  // namespace sw
