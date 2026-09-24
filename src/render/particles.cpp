#include "render/particles.h"

#include <algorithm>

namespace sw {

ParticleSystem& particles() {
    static ParticleSystem p;
    return p;
}

void ParticleSystem::emit(const Particle& p) {
    if (!enabled || particles_.size() > 4000) return;
    particles_.push_back(p);
}

void ParticleSystem::dust(const Vec3& pos, const Vec3& vel, const Vec3& color, int count, float spread) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = pos + Vec3(rng_.range(-spread, spread), rng_.range(0.0f, spread * 0.5f), rng_.range(-spread, spread));
        p.vel = vel + Vec3(rng_.range(-0.6f, 0.6f), rng_.range(0.2f, 1.0f), rng_.range(-0.6f, 0.6f));
        p.color = Vec4(color * rng_.range(0.8f, 1.1f), rng_.range(0.25f, 0.45f));
        p.size = rng_.range(0.08f, 0.18f);
        p.growth = rng_.range(0.3f, 0.7f);
        p.life = rng_.range(0.6f, 1.4f);
        p.drag = 2.5f;
        p.gravity = 0.05f;
        emit(p);
    }
}

void ParticleSystem::sparks(const Vec3& pos, const Vec3& vel, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = pos;
        p.vel = vel * rng_.range(0.3f, 0.8f) + Vec3(rng_.range(-1.5f, 1.5f), rng_.range(0.5f, 2.5f), rng_.range(-1.5f, 1.5f));
        float heat = rng_.range(0.6f, 1.0f);
        p.color = Vec4(Vec3(6.0f, 3.0f, 1.0f) * heat, 1.0f);
        p.size = rng_.range(0.012f, 0.025f);
        p.life = rng_.range(0.15f, 0.4f);
        p.drag = 0.5f;
        p.gravity = 1.0f;
        p.additive = true;
        emit(p);
    }
}

void ParticleSystem::update(float dt) {
    for (Particle& p : particles_) {
        p.age += dt;
        p.vel *= std::exp(-p.drag * dt);
        p.vel.y -= 9.81f * p.gravity * dt;
        p.pos += p.vel * dt;
        p.size += p.growth * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.age >= p.life; }),
                     particles_.end());
}

void ParticleSystem::buildVertices(const Vec3& right, const Vec3& up, const Vec3& camPos, std::vector<ParticleVertex>& alpha,
                                   std::vector<ParticleVertex>& additive) const {
    alpha.clear();
    additive.clear();
    // sort alpha particles back to front
    std::vector<const Particle*> sorted;
    sorted.reserve(particles_.size());
    for (const Particle& p : particles_) sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [&](const Particle* a, const Particle* b) {
        return (a->pos - camPos).lengthSq() > (b->pos - camPos).lengthSq();
    });
    for (const Particle* pp : sorted) {
        const Particle& p = *pp;
        float t = p.age / p.life;
        float fade = (1.0f - t) * std::min(1.0f, p.age * 10.0f);
        Vec4 c = p.color;
        c.w *= fade;
        Vec3 r = right * p.size, u = up * p.size;
        if (p.additive) {
            // stretch sparks along velocity
            Vec3 v = p.vel * 0.02f;
            u = u + v;
        }
        auto& out = p.additive ? additive : alpha;
        ParticleVertex v0{p.pos - r - u, {0, 0}, c}, v1{p.pos + r - u, {1, 0}, c}, v2{p.pos + r + u, {1, 1}, c}, v3{p.pos - r + u, {0, 1}, c};
        out.push_back(v0);
        out.push_back(v1);
        out.push_back(v2);
        out.push_back(v0);
        out.push_back(v2);
        out.push_back(v3);
    }
}

}  // namespace sw
