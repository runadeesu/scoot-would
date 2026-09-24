// scoot would - core math library
// Right handed, Y up, -Z forward (camera looks down -Z). Matrices are column major
// (same memory layout as GLSL mat4). Reverse-Z projections map near -> 1, far -> 0.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace sw {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
constexpr float kEpsilon = 1e-6f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float inverseLerp(float a, float b, float v) { return std::fabs(b - a) < kEpsilon ? 0.0f : (v - a) / (b - a); }
inline float remap(float v, float a0, float a1, float b0, float b1) { return lerpf(b0, b1, saturate(inverseLerp(a0, a1, v))); }
inline float smoothstep(float e0, float e1, float x) { float t = saturate((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }
inline float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }
inline float sqr(float v) { return v * v; }
// framerate independent exponential smoothing factor
inline float damp(float sharpness, float dt) { return 1.0f - std::exp(-sharpness * dt); }
inline float dampf(float current, float target, float sharpness, float dt) { return lerpf(current, target, damp(sharpness, dt)); }
inline float moveTowards(float current, float target, float maxDelta) {
    if (std::fabs(target - current) <= maxDelta) return target;
    return current + signf(target - current) * maxDelta;
}
// wrap angle to [-pi, pi]
inline float wrapAngle(float a) {
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f) a += kTwoPi;
    return a - kPi;
}
inline float angleDelta(float from, float to) { return wrapAngle(to - from); }

// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr explicit Vec2(float s) : x(s), y(s) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const { float l = length(); return l > kEpsilon ? *this / l : Vec2(0, 0); }
};
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(float s) const { float inv = 1.0f / s; return {x * inv, y * inv, z * inv}; }
    Vec3 operator/(const Vec3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    Vec3& operator/=(float s) { return *this *= (1.0f / s); }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }
    float operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSq() const { return x * x + y * y + z * z; }
    Vec3 normalized() const { float l = length(); return l > kEpsilon ? *this / l : Vec3(0, 0, 0); }
    Vec2 xz() const { return {x, z}; }
    static constexpr Vec3 zero() { return {0, 0, 0}; }
    static constexpr Vec3 one() { return {1, 1, 1}; }
    static constexpr Vec3 up() { return {0, 1, 0}; }
    static constexpr Vec3 right() { return {1, 0, 0}; }
    static constexpr Vec3 forward() { return {0, 0, -1}; }
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalize(const Vec3& v) { return v.normalized(); }
inline float length(const Vec3& v) { return v.length(); }
inline float distance(const Vec3& a, const Vec3& b) { return (a - b).length(); }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 vabs(const Vec3& a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
inline Vec3 projectOnPlane(const Vec3& v, const Vec3& n) { return v - n * dot(v, n); }
inline Vec3 projectOnVector(const Vec3& v, const Vec3& dir) { return dir * dot(v, dir); }
inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - n * (2.0f * dot(v, n)); }
inline Vec3 dampv(const Vec3& c, const Vec3& t, float sharp, float dt) { return lerp(c, t, damp(sharp, dt)); }
inline Vec3 clampLength(const Vec3& v, float maxLen) { float l = v.length(); return l > maxLen && l > kEpsilon ? v * (maxLen / l) : v; }
inline float angleBetween(const Vec3& a, const Vec3& b) {
    float d = dot(a.normalized(), b.normalized());
    return std::acos(clampf(d, -1.0f, 1.0f));
}
// signed angle from a to b around axis
inline float signedAngle(const Vec3& a, const Vec3& b, const Vec3& axis) {
    Vec3 pa = projectOnPlane(a, axis).normalized();
    Vec3 pb = projectOnPlane(b, axis).normalized();
    float ang = std::atan2(dot(cross(pa, pb), axis), dot(pa, pb));
    return ang;
}
inline Vec3 anyPerpendicular(const Vec3& v) {
    Vec3 a = std::fabs(v.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    return cross(v, a).normalized();
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}
    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vec4 operator*(const Vec4& o) const { return {x * o.x, y * o.y, z * o.z, w * o.w}; }
    float operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
    Vec3 xyz() const { return {x, y, z}; }
    bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};
inline float dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Vec4 lerp(const Vec4& a, const Vec4& b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------
struct Mat4;

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    static Quat identity() { return {0, 0, 0, 1}; }
    static Quat angleAxis(float angle, const Vec3& axis) {
        Vec3 a = axis.normalized();
        float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }
    // yaw (Y), pitch (X), roll (Z) applied as yaw * pitch * roll
    static Quat euler(float pitch, float yaw, float roll) {
        return angleAxis(yaw, {0, 1, 0}) * angleAxis(pitch, {1, 0, 0}) * angleAxis(roll, {0, 0, 1});
    }
    // rotation that takes vector a onto vector b
    static Quat fromTo(const Vec3& a, const Vec3& b) {
        Vec3 na = a.normalized(), nb = b.normalized();
        float d = dot(na, nb);
        if (d > 0.99999f) return identity();
        if (d < -0.99999f) return angleAxis(kPi, anyPerpendicular(na));
        Vec3 c = cross(na, nb);
        Quat q{c.x, c.y, c.z, 1.0f + d};
        return q.normalized();
    }
    // rotation whose -Z axis points along forward and +Y is close to up
    static Quat lookRotation(const Vec3& forward, const Vec3& up = {0, 1, 0});
    static Quat fromMat(const Mat4& m);

    Quat operator*(const Quat& q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w,
                w * q.w - x * q.x - y * q.y - z * q.z};
    }
    Vec3 operator*(const Vec3& v) const {
        Vec3 u{x, y, z};
        Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
    Quat operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Quat operator+(const Quat& q) const { return {x + q.x, y + q.y, z + q.z, w + q.w}; }
    Quat operator-() const { return {-x, -y, -z, -w}; }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Quat inverse() const { float l = lengthSq(); return l > kEpsilon ? conjugate() * (1.0f / l) : identity(); }
    float lengthSq() const { return x * x + y * y + z * z + w * w; }
    Quat normalized() const { float l = std::sqrt(lengthSq()); return l > kEpsilon ? *this * (1.0f / l) : identity(); }
    Vec3 right() const { return *this * Vec3(1, 0, 0); }
    Vec3 up() const { return *this * Vec3(0, 1, 0); }
    Vec3 forward() const { return *this * Vec3(0, 0, -1); }
    // angle of rotation (0..pi)
    float angle() const { return 2.0f * std::acos(clampf(std::fabs(w), 0.0f, 1.0f)); }
    Vec3 axis() const { float s = std::sqrt(std::max(0.0f, 1.0f - w * w)); return s < 1e-4f ? Vec3(1, 0, 0) : Vec3(x, y, z) / s; }
    // scaled axis (rotation vector), shortest path
    Vec3 toScaledAxis() const {
        Quat q = w < 0 ? -*this : *this;
        float s = std::sqrt(std::max(0.0f, 1.0f - q.w * q.w));
        float a = 2.0f * std::acos(clampf(q.w, -1.0f, 1.0f));
        if (s < 1e-5f) return {q.x * 2.0f, q.y * 2.0f, q.z * 2.0f};
        return Vec3(q.x, q.y, q.z) * (a / s);
    }
    static Quat fromScaledAxis(const Vec3& v) {
        float a = v.length();
        if (a < 1e-6f) return identity();
        return angleAxis(a, v / a);
    }
};
inline float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat nlerp(const Quat& a, const Quat& b, float t) {
    Quat bb = dot(a, b) < 0 ? -b : b;
    return (a * (1.0f - t) + bb * t).normalized();
}
inline Quat slerp(const Quat& a, const Quat& b, float t) {
    Quat bb = b;
    float d = dot(a, b);
    if (d < 0) { bb = -b; d = -d; }
    if (d > 0.9995f) return nlerp(a, bb, t);
    float th = std::acos(d);
    float s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return (a * wa + bb * wb).normalized();
}
inline Quat dampq(const Quat& c, const Quat& t, float sharp, float dt) { return slerp(c, t, damp(sharp, dt)); }

// ---------------------------------------------------------------------------
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column major

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    Vec4 col(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]}; }
    void setCol(int c, const Vec4& v) { m[c * 4] = v.x; m[c * 4 + 1] = v.y; m[c * 4 + 2] = v.z; m[c * 4 + 3] = v.w; }
    Vec4 row(int r) const { return {m[r], m[4 + r], m[8 + r], m[12 + r]}; }

    static Mat4 identity() { return Mat4(); }
    static Mat4 translation(const Vec3& t) { Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static Mat4 scale(const Vec3& s) { Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
    static Mat4 rotation(const Quat& q) {
        Mat4 r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz);     r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz);     r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy);     r.m[9] = 2 * (yz - wx);     r.m[10] = 1 - 2 * (xx + yy);
        return r;
    }
    static Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s) {
        Mat4 r = rotation(q);
        r.m[0] *= s.x; r.m[1] *= s.x; r.m[2] *= s.x;
        r.m[4] *= s.y; r.m[5] *= s.y; r.m[6] *= s.y;
        r.m[8] *= s.z; r.m[9] *= s.z; r.m[10] *= s.z;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    // right handed view matrix
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).normalized();
        Vec3 s = cross(f, up).normalized();
        if (s.lengthSq() < 1e-8f) s = anyPerpendicular(f);
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
    // reverse-Z infinite far perspective, depth range [0,1] (near = 1)
    static Mat4 perspectiveReverseZ(float fovY, float aspect, float zNear) {
        float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 r;
        for (float& v : r.m) v = 0;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[11] = -1.0f;
        r.m[14] = zNear;
        return r;
    }
    // standard perspective, depth range [0,1]
    static Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
        float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 r;
        for (float& v : r.m) v = 0;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = zFar / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = zNear * zFar / (zNear - zFar);
        return r;
    }
    // orthographic, depth range [0,1]
    static Mat4 ortho(float l, float r_, float b, float t, float n, float f) {
        Mat4 r;
        r.m[0] = 2.0f / (r_ - l);
        r.m[5] = 2.0f / (t - b);
        r.m[10] = -1.0f / (f - n);
        r.m[12] = -(r_ + l) / (r_ - l);
        r.m[13] = -(t + b) / (t - b);
        r.m[14] = -n / (f - n);
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    Vec4 operator*(const Vec4& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
    }
    Vec3 transformPoint(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 transformPointH(const Vec3& p) const {
        Vec4 r = *this * Vec4(p, 1.0f);
        return r.xyz() / r.w;
    }
    Vec3 transformDir(const Vec3& d) const {
        return {m[0] * d.x + m[4] * d.y + m[8] * d.z,
                m[1] * d.x + m[5] * d.y + m[9] * d.z,
                m[2] * d.x + m[6] * d.y + m[10] * d.z};
    }
    Vec3 translationPart() const { return {m[12], m[13], m[14]}; }
    Vec3 scalePart() const { return {col(0).xyz().length(), col(1).xyz().length(), col(2).xyz().length()}; }
    Mat4 transposed() const { Mat4 r; for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.at(i, j) = at(j, i); return r; }
    Mat4 inverse() const;
    // inverse for rigid/affine transforms (faster, more precise)
    Mat4 affineInverse() const;
};

inline Mat4 Mat4::inverse() const {
    const float* a = m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    Mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * det;
    return r;
}

inline Mat4 Mat4::affineInverse() const {
    // invert the upper 3x3 (may contain scale) then the translation
    float a00 = m[0], a01 = m[4], a02 = m[8];
    float a10 = m[1], a11 = m[5], a12 = m[9];
    float a20 = m[2], a21 = m[6], a22 = m[10];
    float c00 = a11 * a22 - a12 * a21, c01 = a02 * a21 - a01 * a22, c02 = a01 * a12 - a02 * a11;
    float c10 = a12 * a20 - a10 * a22, c11 = a00 * a22 - a02 * a20, c12 = a02 * a10 - a00 * a12;
    float c20 = a10 * a21 - a11 * a20, c21 = a01 * a20 - a00 * a21, c22 = a00 * a11 - a01 * a10;
    float det = a00 * c00 + a01 * c10 + a02 * c20;
    Mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    float id = 1.0f / det;
    r.at(0, 0) = c00 * id; r.at(0, 1) = c01 * id; r.at(0, 2) = c02 * id;
    r.at(1, 0) = c10 * id; r.at(1, 1) = c11 * id; r.at(1, 2) = c12 * id;
    r.at(2, 0) = c20 * id; r.at(2, 1) = c21 * id; r.at(2, 2) = c22 * id;
    Vec3 t = translationPart();
    Vec3 it = r.transformDir(t);
    r.m[12] = -it.x; r.m[13] = -it.y; r.m[14] = -it.z;
    return r;
}

inline Quat Quat::fromMat(const Mat4& mm) {
    // expects orthonormal upper 3x3 (scale removed)
    Vec3 c0 = mm.col(0).xyz().normalized(), c1 = mm.col(1).xyz().normalized(), c2 = mm.col(2).xyz().normalized();
    float m00 = c0.x, m11 = c1.y, m22 = c2.z;
    float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (c1.z - c2.y) / s;
        q.y = (c2.x - c0.z) / s;
        q.z = (c0.y - c1.x) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (c1.z - c2.y) / s;
        q.x = 0.25f * s;
        q.y = (c1.x + c0.y) / s;
        q.z = (c2.x + c0.z) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (c2.x - c0.z) / s;
        q.x = (c1.x + c0.y) / s;
        q.y = 0.25f * s;
        q.z = (c2.y + c1.z) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (c0.y - c1.x) / s;
        q.x = (c2.x + c0.z) / s;
        q.y = (c2.y + c1.z) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

inline Quat Quat::lookRotation(const Vec3& forward, const Vec3& up) {
    Vec3 f = forward.normalized();
    Vec3 r = cross(f, up).normalized();
    if (r.lengthSq() < 1e-8f) r = anyPerpendicular(f);
    Vec3 u = cross(r, f);
    Mat4 m;
    m.setCol(0, Vec4(r, 0));
    m.setCol(1, Vec4(u, 0));
    m.setCol(2, Vec4(-f, 0));
    return fromMat(m);
}

// ---------------------------------------------------------------------------
struct Transform {
    Vec3 position{0, 0, 0};
    Quat rotation{};
    Vec3 scale{1, 1, 1};

    Mat4 matrix() const { return Mat4::trs(position, rotation, scale); }
    Vec3 transformPoint(const Vec3& p) const { return position + rotation * (p * scale); }
    Vec3 transformDir(const Vec3& d) const { return rotation * d; }
    Vec3 inverseTransformPoint(const Vec3& p) const { return rotation.conjugate() * (p - position) / scale; }
    Transform operator*(const Transform& child) const {
        Transform r;
        r.position = transformPoint(child.position);
        r.rotation = (rotation * child.rotation).normalized();
        r.scale = scale * child.scale;
        return r;
    }
    Transform inverse() const {
        Transform r;
        r.rotation = rotation.conjugate();
        r.scale = Vec3(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        r.position = r.rotation * (-position) * r.scale;
        return r;
    }
    static Transform lerp(const Transform& a, const Transform& b, float t) {
        return {sw::lerp(a.position, b.position, t), nlerp(a.rotation, b.rotation, t), sw::lerp(a.scale, b.scale, t)};
    }
};

// ---------------------------------------------------------------------------
struct AABB {
    Vec3 min{std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max()};

    AABB() = default;
    AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}
    static AABB fromCenterExtents(const Vec3& c, const Vec3& e) { return {c - e, c + e}; }
    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    Vec3 size() const { return max - min; }
    void expand(const Vec3& p) { min = vmin(min, p); max = vmax(max, p); }
    void expand(const AABB& b) { if (b.valid()) { min = vmin(min, b.min); max = vmax(max, b.max); } }
    AABB inflated(float r) const { return {min - Vec3(r), max + Vec3(r)}; }
    bool contains(const Vec3& p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z; }
    bool intersects(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z && max.z >= o.min.z;
    }
    AABB transformed(const Mat4& m) const {
        // Arvo's method
        Vec3 c = center(), e = extents();
        Vec3 nc = m.transformPoint(c);
        Vec3 ne{std::fabs(m.at(0, 0)) * e.x + std::fabs(m.at(0, 1)) * e.y + std::fabs(m.at(0, 2)) * e.z,
                std::fabs(m.at(1, 0)) * e.x + std::fabs(m.at(1, 1)) * e.y + std::fabs(m.at(1, 2)) * e.z,
                std::fabs(m.at(2, 0)) * e.x + std::fabs(m.at(2, 1)) * e.y + std::fabs(m.at(2, 2)) * e.z};
        return {nc - ne, nc + ne};
    }
    float distanceSq(const Vec3& p) const {
        Vec3 c = vmax(min, vmin(p, max));
        return (c - p).lengthSq();
    }
};

struct Plane {
    Vec3 normal{0, 1, 0};
    float d = 0;  // dot(n, p) + d = 0
    Plane() = default;
    Plane(const Vec3& n, float d_) : normal(n), d(d_) {}
    static Plane fromPointNormal(const Vec3& p, const Vec3& n) { Vec3 nn = n.normalized(); return {nn, -dot(nn, p)}; }
    float distance(const Vec3& p) const { return dot(normal, p) + d; }
    Plane normalized() const { float l = normal.length(); return {normal / l, d / l}; }
};

struct Ray {
    Vec3 origin;
    Vec3 dir{0, 0, -1};
    Vec3 at(float t) const { return origin + dir * t; }
    // returns t or negative on miss
    float intersect(const Plane& p) const {
        float den = dot(p.normal, dir);
        if (std::fabs(den) < 1e-8f) return -1.0f;
        return -(dot(p.normal, origin) + p.d) / den;
    }
    bool intersect(const AABB& b, float& tmin, float& tmax) const {
        tmin = 0.0f; tmax = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; ++i) {
            float o = origin[i], d = dir[i];
            if (std::fabs(d) < 1e-9f) {
                if (o < b.min[i] || o > b.max[i]) return false;
            } else {
                float inv = 1.0f / d;
                float t0 = (b.min[i] - o) * inv, t1 = (b.max[i] - o) * inv;
                if (t0 > t1) std::swap(t0, t1);
                tmin = std::max(tmin, t0);
                tmax = std::min(tmax, t1);
                if (tmin > tmax) return false;
            }
        }
        return true;
    }
};

struct Frustum {
    Plane planes[6];  // left right bottom top near far(optional)
    int count = 6;
    // from a view-projection matrix with depth range [0,1] (works for reverse Z too, far plane is dropped for infinite)
    // Degenerate planes (e.g. the far plane of an infinite reverse-Z projection) are dropped.
    static Frustum fromMatrix(const Mat4& vp) {
        Frustum f;
        f.count = 0;
        Vec4 r0 = vp.row(0), r1 = vp.row(1), r2 = vp.row(2), r3 = vp.row(3);
        Vec4 cand[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
        for (const Vec4& v : cand) {
            float l = v.xyz().length();
            if (l < 1e-7f) continue;
            f.planes[f.count++] = Plane(v.xyz() / l, v.w / l);
        }
        return f;
    }
    bool testAABB(const AABB& b) const {
        Vec3 c = b.center(), e = b.extents();
        for (int i = 0; i < count; ++i) {
            const Plane& p = planes[i];
            float r = e.x * std::fabs(p.normal.x) + e.y * std::fabs(p.normal.y) + e.z * std::fabs(p.normal.z);
            if (p.distance(c) < -r) return false;
        }
        return true;
    }
    bool testSphere(const Vec3& c, float radius) const {
        for (int i = 0; i < count; ++i)
            if (planes[i].distance(c) < -radius) return false;
        return true;
    }
};

// simple deterministic RNG (PCG32)
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL;
    uint64_t inc = 0xda3e39cb94b95bdbULL;
    explicit Rng(uint64_t seed = 1) { state = 0; next(); state += seed; next(); }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + (inc | 1);
        uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int rangeInt(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
};

// Hermite / Catmull-Rom helpers for splines
inline Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

// closest point on segment ab to p, returns parameter t in [0,1]
inline float closestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p, Vec3& out) {
    Vec3 ab = b - a;
    float l2 = ab.lengthSq();
    float t = l2 > 1e-9f ? saturate(dot(p - a, ab) / l2) : 0.0f;
    out = a + ab * t;
    return t;
}

// color helpers
inline Vec3 srgbToLinear(const Vec3& c) {
    auto f = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return {f(c.x), f(c.y), f(c.z)};
}
inline Vec3 hexColor(uint32_t rgb) {
    return srgbToLinear(Vec3(float((rgb >> 16) & 0xff) / 255.0f, float((rgb >> 8) & 0xff) / 255.0f, float(rgb & 0xff) / 255.0f));
}

}  // namespace sw
