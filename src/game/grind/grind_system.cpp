#include "game/grind/grind_system.h"
#include "core/log.h"
#include "game/scooter/scooter_physics.h"
#include "render/debug_draw.h"

#include <algorithm>

namespace sw {

static const GrindTypeInfo kGrindTypes[] = {
    // name, contact, yaw, pitch, roll, difficulty, score/s, friction
    {"50-50", 0, 0.0f, 0.0f, 0.0f, 0.8f, 350, 1.0f},
    {"Nosegrind", 1, 0.0f, -11.0f, 0.0f, 1.25f, 520, 1.1f},
    {"5-0", 2, 0.0f, 13.0f, 0.0f, 1.1f, 480, 1.15f},
    {"Feeble", 2, 16.0f, 6.0f, -14.0f, 1.35f, 600, 1.2f},
    {"Smith", 2, -12.0f, 4.0f, 22.0f, 1.45f, 650, 1.25f},
    {"Crooked", 1, 18.0f, -8.0f, 10.0f, 1.4f, 620, 1.2f},
    {"Boardslide", 0, 90.0f, 0.0f, 0.0f, 1.0f, 450, 1.6f},
};

const GrindTypeInfo& grindInfo(GrindType t) { return kGrindTypes[int(t)]; }

// ---------------------------------------------------------------------------
Vec3 Rail::pointAt(float s) const {
    s = clampf(s, 0.0f, length);
    auto it = std::upper_bound(cumulative.begin(), cumulative.end(), s);
    size_t i = it == cumulative.begin() ? 0 : size_t(it - cumulative.begin()) - 1;
    i = std::min(i, points.size() - 2);
    float segLen = cumulative[i + 1] - cumulative[i];
    float t = segLen > 1e-6f ? (s - cumulative[i]) / segLen : 0.0f;
    return lerp(points[i], points[i + 1], t);
}

Vec3 Rail::tangentAt(float s) const {
    s = clampf(s, 0.0f, length);
    auto it = std::upper_bound(cumulative.begin(), cumulative.end(), s);
    size_t i = it == cumulative.begin() ? 0 : size_t(it - cumulative.begin()) - 1;
    i = std::min(i, points.size() - 2);
    Vec3 t = (points[i + 1] - points[i]).normalized();
    // smooth the tangent near kinks
    float segLen = cumulative[i + 1] - cumulative[i];
    float local = s - cumulative[i];
    const float blend = 0.25f;
    if (local > segLen - blend && i + 2 < points.size()) {
        Vec3 n = (points[i + 2] - points[i + 1]).normalized();
        t = lerp(t, n, 0.5f * saturate((local - (segLen - blend)) / blend)).normalized();
    } else if (local < blend && i > 0) {
        Vec3 p = (points[i] - points[i - 1]).normalized();
        t = lerp(p, t, 0.5f + 0.5f * saturate(local / blend)).normalized();
    }
    return t;
}

float Rail::closest(const Vec3& p, float& sOut) const {
    float best = 1e9f;
    sOut = 0.0f;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        Vec3 c;
        float t = closestPointOnSegment(points[i], points[i + 1], p, c);
        float d = (c - p).lengthSq();
        if (d < best) {
            best = d;
            sOut = cumulative[i] + t * (cumulative[i + 1] - cumulative[i]);
        }
    }
    return std::sqrt(best);
}

// ---------------------------------------------------------------------------
void GrindSystem::rebuild(Scene& scene) {
    rails_.clear();
    scene.updateTransforms();
    scene.forEach([&](Entity& e) {
        for (const RailComponent& rc : e.rails) {
            if (rc.points.size() < 2) continue;
            Rail r;
            r.type = rc.type;
            r.radius = rc.radius * std::max(0.5f, e.world.scalePart().y);
            r.entity = e.id;
            int matSurface = e.collider ? e.collider->surface : 0;
            // rails and copings are metal, ledges keep the surface of the object
            r.surface = (rc.type == RailType::Round || rc.type == RailType::Square || rc.type == RailType::Coping) ? surfaces().find("metal") : matSurface;
            for (const Vec3& p : rc.points) r.points.push_back(e.world.transformPoint(p));
            r.cumulative.push_back(0.0f);
            for (size_t i = 1; i < r.points.size(); ++i) r.cumulative.push_back(r.cumulative.back() + distance(r.points[i], r.points[i - 1]));
            r.length = r.cumulative.back();
            if (r.length < 0.3f) continue;
            for (auto& p : r.points) r.bounds.expand(p);
            rails_.push_back(std::move(r));
        }
    });
    LOG_INFO("grind: %zu rails / ledges", rails_.size());
}

Vec3 GrindSystem::contactLocal(GrindType t) const {
    const GrindTypeInfo& gi = grindInfo(t);
    const float wheelR = 0.055f, halfBase = 0.28f;
    if (gi.contact == 1) return Vec3(0, -wheelR, -halfBase);
    if (gi.contact == 2) return Vec3(0, -wheelR, halfBase);
    return Vec3(0, 0.0f, 0);
}

Quat GrindSystem::grindRotation(const GrindState& st) const {
    const Rail& r = rails_[size_t(st.rail)];
    Vec3 T = r.tangentAt(st.s) * st.dir;
    Vec3 U = (Vec3(0, 1, 0) - T * T.y).normalized();
    const GrindTypeInfo& gi = grindInfo(st.type);
    Vec3 fwd = st.fakie ? -T : T;
    Quat base = Quat::lookRotation(fwd, U);
    Vec3 S = base * Vec3(1, 0, 0);
    float side = st.side;
    Quat q = Quat::angleAxis(gi.yaw * kDeg2Rad * side, U) * base;
    q = Quat::angleAxis(gi.pitch * kDeg2Rad, q * Vec3(1, 0, 0)) * q;
    q = Quat::angleAxis(gi.roll * kDeg2Rad * side, q * Vec3(0, 0, -1)) * q;
    // balance wobble: lean around the travel direction
    q = Quat::angleAxis(st.balance * 0.35f, T) * q;
    (void)S;
    return q.normalized();
}

bool GrindSystem::tryAttach(const ScooterPhysics& sc, const Vec2& stick, bool airborne, GrindState& st) {
    if (cooldown > 0.0f || rails_.empty()) return false;
    Vec3 P = sc.position();
    Vec3 up = sc.up();
    Vec3 deck = P + up * 0.01f;
    Vec3 v = sc.velocity();
    Vec3 vh(v.x, 0, v.z);
    float best = 1e9f;
    int bestRail = -1;
    float bestS = 0.0f;
    for (size_t i = 0; i < rails_.size(); ++i) {
        const Rail& r = rails_[i];
        if (!r.bounds.inflated(0.7f).contains(deck)) continue;
        float s;
        r.closest(deck, s);
        Vec3 c = r.pointAt(s);
        Vec3 top = c + Vec3(0, r.radius, 0);
        float dv = deck.y - top.y;
        float dh = Vec2(deck.x - c.x, deck.z - c.z).length();
        if (dh > 0.4f) continue;
        if (dv < -0.18f || dv > 0.5f) continue;
        // coming up from below the rail is not a grind
        if (deck.y < top.y - 0.04f && v.y > 0.5f) continue;
        if (!airborne && dv > 0.12f) continue;
        Vec3 T = r.tangentAt(s);
        float along = dot(v, T);
        Vec3 th(T.x, 0, T.z);
        float angle = 0.0f;
        if (vh.length() > 0.5f && th.length() > 0.1f) angle = std::acos(clampf(std::fabs(dot(vh.normalized(), th.normalized())), 0.0f, 1.0f)) * kRad2Deg;
        bool board = angle >= 55.0f;
        if (angle > 38.0f && angle < 55.0f) {
            lastRejectReason = "approach angle";
            continue;
        }
        if (std::fabs(along) < (board ? 0.6f : 1.0f)) {
            lastRejectReason = "too slow along the rail";
            continue;
        }
        float score = dh + std::fabs(dv) * 0.6f;
        if (score < best) {
            best = score;
            bestRail = int(i);
            bestS = s;
        }
    }
    if (bestRail < 0) return false;

    const Rail& r = rails_[size_t(bestRail)];
    Vec3 T = r.tangentAt(bestS);
    float along = dot(v, T);
    Vec3 th(T.x, 0, T.z);
    float angle = vh.length() > 0.5f && th.length() > 0.1f ? std::acos(clampf(std::fabs(dot(vh.normalized(), th.normalized())), 0.0f, 1.0f)) * kRad2Deg : 0.0f;
    st = GrindState{};
    st.active = true;
    st.rail = bestRail;
    st.s = bestS;
    st.dir = along >= 0.0f ? 1.0f : -1.0f;
    st.speed = std::fabs(along) * (angle >= 55.0f ? 0.9f : 1.0f) + 0.2f;
    Vec3 Td = T * st.dir;
    Vec3 U = (Vec3(0, 1, 0) - Td * Td.y).normalized();
    Vec3 S = cross(Td, U).normalized();
    Vec3 c = r.pointAt(bestS);
    st.side = dot(P - c, S) >= 0.0f ? 1.0f : -1.0f;
    st.fakie = dot(sc.forward(), Td) < 0.0f;
    if (angle >= 55.0f) {
        st.type = GrindType::Boardslide;
        // pick the boardslide yaw closest to the current facing
        st.side = dot(sc.forward(), S) >= 0.0f ? -1.0f : 1.0f;
        st.fakie = false;
    } else if (stick.y > 0.5f) {
        st.type = std::fabs(stick.x) > 0.5f ? GrindType::Crooked : GrindType::Nosegrind;
    } else if (stick.y < -0.5f) {
        if (std::fabs(stick.x) > 0.5f)
            st.type = (stick.x * st.side > 0.0f) ? GrindType::Feeble : GrindType::Smith;
        else
            st.type = GrindType::FiveO;
    } else {
        st.type = GrindType::FiftyFifty;
    }
    st.surface = r.surface;
    st.balance = clampf((angle / 38.0f) * 0.25f * (std::fmod(bestS * 13.1f, 2.0f) > 1.0f ? 1.0f : -1.0f), -0.3f, 0.3f);
    // entry assist: remember where we were, blend to the rail instead of teleporting
    Quat rot = grindRotation(st);
    Vec3 target = c + U * r.radius - rot * contactLocal(st.type);
    st.entryOffset = P - target;
    st.entryRotOffset = (rot.conjugate() * sc.rotation()).normalized();
    st.segments.clear();
    return true;
}

GrindExit GrindSystem::update(float dt, const Vec2& stick, bool jumpPressed, GrindState& st, ScooterPhysics& sc, float balanceScale) {
    if (!st.active) return GrindExit::None;
    const Rail& r = rails_[size_t(st.rail)];
    const GrindTypeInfo& gi = grindInfo(st.type);
    st.time += dt;
    st.segmentTime += dt;
    Vec3 T = r.tangentAt(st.s) * st.dir;
    // speed: gravity along the rail minus grind friction
    float mu = surfaces().get(st.surface).grindFriction * gi.friction;
    st.speed += (-9.81f * T.y - mu * 9.81f) * dt;
    st.speed = std::min(st.speed, 20.0f);
    GrindExit exit = GrindExit::None;
    if (st.speed < 0.35f) exit = GrindExit::TooSlow;
    st.s += st.dir * st.speed * dt;
    if (st.s < 0.0f || st.s > r.length) exit = GrindExit::RailEnd;

    // balance: unstable equilibrium + drift, corrected with the left stick
    noiseT_ += dt;
    float drift = (std::sin(noiseT_ * 1.7f) * 0.6f + std::sin(noiseT_ * 3.1f + 1.0f) * 0.4f) * 0.9f;
    float inst = gi.difficulty * balanceScale;
    st.balanceVel += (st.balance * 1.8f * inst + drift * 0.9f * inst + stick.x * 3.4f) * dt;
    st.balanceVel *= std::exp(-2.4f * dt);
    st.balance += st.balanceVel * dt;
    if (std::fabs(st.balance) > 1.0f) {
        if (godMode)
            st.balance = clampf(st.balance, -0.98f, 0.98f);
        else if (exit == GrindExit::None)
            exit = GrindExit::Bail;
    }
    // switch grind type mid-grind with the stick (adds a segment to the combo)
    GrindType want = st.type;
    if (st.type != GrindType::Boardslide) {
        if (stick.y > 0.6f) want = std::fabs(stick.x) > 0.6f ? GrindType::Crooked : GrindType::Nosegrind;
        else if (stick.y < -0.6f) want = std::fabs(stick.x) > 0.6f ? (stick.x * st.side > 0 ? GrindType::Feeble : GrindType::Smith) : GrindType::FiveO;
        else if (std::fabs(stick.y) < 0.25f && std::fabs(stick.x) < 0.5f) want = GrindType::FiftyFifty;
        if (want != st.type && st.segmentTime > 0.35f) {
            st.segments.push_back({st.type, st.segmentTime});
            st.type = want;
            st.segmentTime = 0.0f;
        }
    }
    if (jumpPressed && exit == GrindExit::None) exit = GrindExit::Jump;
    // hop off the side: stick held hard sideways
    sideHold_ = std::fabs(stick.x) > 0.95f && std::fabs(stick.y) < 0.4f ? sideHold_ + dt : 0.0f;
    if (sideHold_ > 0.45f && exit == GrindExit::None && std::fabs(st.balance) < 0.8f) {
        exit = GrindExit::Side;
        sideHold_ = 0.0f;
    }

    st.s = clampf(st.s, 0.0f, r.length);
    Vec3 U = (Vec3(0, 1, 0) - T * T.y).normalized();
    Vec3 S = cross(T, U).normalized();
    Quat rot = grindRotation(st);
    st.entryOffset *= std::exp(-22.0f * dt);
    st.entryRotOffset = nlerp(st.entryRotOffset, Quat::identity(), damp(18.0f, dt));
    Vec3 target = r.pointAt(st.s) + U * r.radius - rot * contactLocal(st.type) + st.entryOffset;
    Quat finalRot = (rot * st.entryRotOffset).normalized();
    Vec3 vel = T * st.speed;

    if (exit != GrindExit::None) {
        st.segments.push_back({st.type, st.segmentTime});
        st.active = false;
        cooldown = 0.35f;
        sc.setKinematic(false);
        Vec3 outVel = vel;
        switch (exit) {
            case GrindExit::RailEnd: outVel += Vec3(0, 0.6f, 0); break;
            case GrindExit::Jump: outVel += U * 3.4f; break;
            case GrindExit::Side: outVel = vel * 0.85f + S * (stick.x > 0 ? 2.0f : -2.0f) + Vec3(0, 1.8f, 0); break;
            case GrindExit::Bail: outVel = vel * 0.6f + S * (st.balance > 0 ? 1.5f : -1.5f); break;
            case GrindExit::TooSlow: outVel = vel + S * (st.side * 1.2f) + Vec3(0, 0.8f, 0); break;
            default: break;
        }
        // lift the body slightly so the wheels do not start inside the rail
        sc.teleport(target + Vec3(0, 0.03f, 0), finalRot, outVel);
        return exit;
    }
    // follow the rail: set position + velocity every step
    sc.teleport(target, finalRot, vel);
    return GrindExit::None;
}

void GrindSystem::debugDraw(const Vec3& near, float radius) const {
    DebugDraw& dd = sw::debugDraw();
    for (const Rail& r : rails_) {
        if (r.bounds.distanceSq(near) > radius * radius) continue;
        Vec4 col = r.type == RailType::Ledge ? Vec4(1, 0.8f, 0.1f, 1) : r.type == RailType::Coping ? Vec4(0.2f, 0.8f, 1, 1) : Vec4(0.2f, 1, 0.3f, 1);
        for (size_t i = 0; i + 1 < r.points.size(); ++i) dd.line(r.points[i] + Vec3(0, r.radius, 0), r.points[i + 1] + Vec3(0, r.radius, 0), col);
    }
}

}  // namespace sw
