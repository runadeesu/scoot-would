#include "render/render_scene.h"

#include <algorithm>

namespace sw {

// ---------------------------------------------------------------------------
void SpatialGrid::build(const AABB& worldBounds, float cellSize) {
    cellSize_ = cellSize;
    AABB b = worldBounds.valid() ? worldBounds : AABB(Vec3(-100), Vec3(100));
    origin_ = b.min;
    nx_ = std::max(1, int(std::ceil((b.max.x - b.min.x) / cellSize)) + 1);
    nz_ = std::max(1, int(std::ceil((b.max.z - b.min.z) / cellSize)) + 1);
    nx_ = std::min(nx_, 512);
    nz_ = std::min(nz_, 512);
    cells_.assign(size_t(nx_ * nz_), {});
    cellAabb_.assign(size_t(nx_ * nz_), AABB());
    large_.clear();
}

void SpatialGrid::cellRange(const AABB& b, int& x0, int& z0, int& x1, int& z1) const {
    x0 = std::clamp(int(std::floor((b.min.x - origin_.x) / cellSize_)), 0, nx_ - 1);
    z0 = std::clamp(int(std::floor((b.min.z - origin_.z) / cellSize_)), 0, nz_ - 1);
    x1 = std::clamp(int(std::floor((b.max.x - origin_.x) / cellSize_)), 0, nx_ - 1);
    z1 = std::clamp(int(std::floor((b.max.z - origin_.z) / cellSize_)), 0, nz_ - 1);
}

void SpatialGrid::insert(uint32_t id, const AABB& b) {
    if (!valid()) return;
    int x0, z0, x1, z1;
    cellRange(b, x0, z0, x1, z1);
    if ((x1 - x0 + 1) * (z1 - z0 + 1) > 16) {
        large_.push_back(id);
        return;
    }
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            size_t c = size_t(z * nx_ + x);
            cells_[c].push_back(id);
            cellAabb_[c].expand(b);
        }
}

void SpatialGrid::remove(uint32_t id, const AABB& b) {
    if (!valid()) return;
    auto erase = [id](std::vector<uint32_t>& v) {
        auto it = std::find(v.begin(), v.end(), id);
        if (it != v.end()) {
            *it = v.back();
            v.pop_back();
        }
    };
    erase(large_);
    int x0, z0, x1, z1;
    cellRange(b, x0, z0, x1, z1);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) erase(cells_[size_t(z * nx_ + x)]);
}

// ---------------------------------------------------------------------------
RenderScene::Handle RenderScene::add(const RenderObject& obj) {
    Handle h;
    if (!free_.empty()) {
        h = free_.back();
        free_.pop_back();
        objects_[h] = obj;
        alive_[h] = 1;
    } else {
        h = Handle(objects_.size());
        objects_.push_back(obj);
        alive_.push_back(1);
        instances_.push_back(InstanceGpu{});
        visit_.push_back(0);
        movedFlag_.push_back(0);
    }
    ++liveCount_;
    RenderObject& o = objects_[h];
    if (o.mesh) o.worldBounds = o.mesh->bounds.transformed(o.world);
    writeInstance(h);
    instances_[h].prevModel = o.world;
    if (!o.isStatic)
        dynamic_.push_back(h);
    else if (grid_.valid())
        grid_.insert(h, o.worldBounds);
    return h;
}

void RenderScene::remove(Handle h) {
    if (!valid(h)) return;
    RenderObject& o = objects_[h];
    if (!o.isStatic) {
        auto it = std::find(dynamic_.begin(), dynamic_.end(), h);
        if (it != dynamic_.end()) dynamic_.erase(it);
    } else if (grid_.valid()) {
        grid_.remove(h, o.worldBounds);
    }
    o = RenderObject{};
    alive_[h] = 0;
    free_.push_back(h);
    --liveCount_;
}

void RenderScene::setTransform(Handle h, const Mat4& world) {
    if (!valid(h)) return;
    RenderObject& o = objects_[h];
    AABB old = o.worldBounds;
    o.world = world;
    if (o.mesh) o.worldBounds = o.mesh->bounds.transformed(world);
    if (o.isStatic && grid_.valid()) {
        grid_.remove(h, old);
        grid_.insert(h, o.worldBounds);
    }
    writeInstance(h);
    if (!movedFlag_[h]) {
        movedFlag_[h] = 1;
        moved_.push_back(h);
    }
}

void RenderScene::setVisible(Handle h, bool visible) {
    if (valid(h)) objects_[h].visible = visible;
}

void RenderScene::setTint(Handle h, const Vec4& tint) {
    if (!valid(h)) return;
    objects_[h].tint = tint;
    writeInstance(h);
}

void RenderScene::setMaterials(Handle h, const std::vector<MaterialPtr>& mats) {
    if (valid(h)) objects_[h].materials = mats;
}

void RenderScene::setMesh(Handle h, std::shared_ptr<GpuMesh> mesh) {
    if (!valid(h)) return;
    RenderObject& o = objects_[h];
    AABB old = o.worldBounds;
    o.mesh = std::move(mesh);
    if (o.mesh) o.worldBounds = o.mesh->bounds.transformed(o.world);
    if (o.isStatic && grid_.valid()) {
        grid_.remove(h, old);
        grid_.insert(h, o.worldBounds);
    }
}

RenderObject* RenderScene::get(Handle h) { return valid(h) ? &objects_[h] : nullptr; }
const RenderObject* RenderScene::get(Handle h) const { return valid(h) ? &objects_[h] : nullptr; }

void RenderScene::writeInstance(Handle h) {
    const RenderObject& o = objects_[h];
    InstanceGpu& g = instances_[h];
    g.model = o.world;
    Mat4 n = o.world.affineInverse().transposed();
    g.nrm0 = Vec4(n.col(0).xyz(), 0);
    g.nrm1 = Vec4(n.col(1).xyz(), 0);
    g.nrm2 = Vec4(n.col(2).xyz(), 0);
    g.tint = o.tint;
    g.misc = Vec4(float(std::max(o.boneOffset, 0)), 1.0f, 0.0f, float(o.userId));
    markDirty(h);
}

void RenderScene::markDirty(Handle h) {
    dirtyMin_ = std::min(dirtyMin_, h);
    dirtyMax_ = std::max(dirtyMax_, h);
}

bool RenderScene::takeDirtyRange(uint32_t& first, uint32_t& count) {
    if (dirtyMin_ > dirtyMax_ || instances_.empty()) return false;
    first = dirtyMin_;
    count = std::min<uint32_t>(dirtyMax_, uint32_t(instances_.size() - 1)) - dirtyMin_ + 1;
    dirtyMin_ = 0xffffffffu;
    dirtyMax_ = 0;
    return true;
}

void RenderScene::commitMotion() {
    for (Handle h : moved_) {
        if (h >= instances_.size()) continue;
        movedFlag_[h] = 0;
        instances_[h].prevModel = instances_[h].model;
        markDirty(h);
    }
    moved_.clear();
}

void RenderScene::markAllDirty() {
    if (instances_.empty()) return;
    dirtyMin_ = 0;
    dirtyMax_ = uint32_t(instances_.size() - 1);
}

RenderScene::Handle RenderScene::addLight(const LightProxy& l) {
    for (size_t i = 0; i < lightAlive_.size(); ++i)
        if (!lightAlive_[i]) {
            lights_[i] = l;
            lightAlive_[i] = 1;
            return Handle(i);
        }
    lights_.push_back(l);
    lightAlive_.push_back(1);
    return Handle(lights_.size() - 1);
}

void RenderScene::removeLight(Handle h) {
    if (h < lightAlive_.size()) lightAlive_[h] = 0;
}

LightProxy* RenderScene::light(Handle h) { return h < lights_.size() && lightAlive_[h] ? &lights_[h] : nullptr; }

int RenderScene::allocateBones(size_t count) {
    int off = int(bones_.size());
    bones_.resize(bones_.size() + count, Mat4::identity());
    return off;
}

void RenderScene::buildGrid(float cellSize) {
    grid_.build(bounds(), cellSize);
    for (Handle h = 0; h < objects_.size(); ++h)
        if (alive_[h] && objects_[h].isStatic) grid_.insert(h, objects_[h].worldBounds);
}

AABB RenderScene::bounds() const {
    AABB b;
    for (Handle h = 0; h < objects_.size(); ++h)
        if (alive_[h] && objects_[h].isStatic) b.expand(objects_[h].worldBounds);
    return b.inflated(16.0f);
}

void RenderScene::clear() {
    objects_.clear();
    alive_.clear();
    free_.clear();
    instances_.clear();
    visit_.clear();
    dynamic_.clear();
    moved_.clear();
    movedFlag_.clear();
    liveCount_ = 0;
    lights_.clear();
    lightAlive_.clear();
    bones_.clear();
    grid_ = SpatialGrid();
    dirtyMin_ = 0xffffffffu;
    dirtyMax_ = 0;
}

}  // namespace sw
