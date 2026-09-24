// scoot would - JSON helpers (nlohmann::json with math type conversions)
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "core/math.h"

namespace sw {

using Json = nlohmann::json;

// parse a JSON file from an absolute path; logs and returns nullopt on failure
std::optional<Json> loadJsonFile(const std::string& absPath);
bool saveJsonFile(const std::string& absPath, const Json& j, bool safe = true);

template <typename T>
T jget(const Json& j, const char* key, const T& def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (...) {
        return def;
    }
}

Vec2 jvec2(const Json& j, const char* key, const Vec2& def = {});
Vec3 jvec3(const Json& j, const char* key, const Vec3& def = {});
Vec4 jvec4(const Json& j, const char* key, const Vec4& def = {});
// rotation stored as euler degrees [pitch, yaw, roll] or quaternion [x,y,z,w]
Quat jquat(const Json& j, const char* key, const Quat& def = {});
Vec3 jcolor(const Json& j, const char* key, const Vec3& def = {1, 1, 1});  // "#rrggbb" or [r,g,b] linear

Json toJson(const Vec2& v);
Json toJson(const Vec3& v);
Json toJson(const Vec4& v);
Json toJson(const Quat& q);
// euler degrees (pitch, yaw, roll) for human friendly scene files
Json toJsonEuler(const Quat& q);
Vec3 quatToEulerDeg(const Quat& q);

}  // namespace sw
