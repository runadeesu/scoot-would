// scoot would - CPU particle system (dust, sparks, grass bits), rendered as camera facing quads
#pragma once

#include "core/math.h"

#include <vector>

namespace sw {

struct Particle {
    Vec3 pos, vel;
    Vec4 color;       // rgb (linear, may be > 1 for sparks), a = opacity
    float size = 0.1f, growth = 0.0f;
    float life = 1.0f, age = 0.0f;
    float drag = 1.0f, gravity = 1.0f;
    bool additive = false;
};

struct ParticleVertex {
    Vec3 pos;
    Vec2 uv;
    Vec4 color;
};

class ParticleSystem {
public:
    void emit(const Particle& p);
    // emit a burst of dust from a surface point
    void dust(const Vec3& pos, const Vec3& vel, const Vec3& color, int count, float spread);
    void sparks(const Vec3& pos, const Vec3& vel, int count);
    void update(float dt);
    void buildVertices(const Vec3& camRight, const Vec3& camUp, const Vec3& camPos, std::vector<ParticleVertex>& alpha,
                       std::vector<ParticleVertex>& additive) const;
    void clear() { particles_.clear(); }
    size_t count() const { return particles_.size(); }
    bool enabled = true;

private:
    std::vector<Particle> particles_;
    Rng rng_{1234};
};

ParticleSystem& particles();

}  // namespace sw
