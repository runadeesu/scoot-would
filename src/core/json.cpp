#include "core/json.h"
#include "core/filesystem.h"
#include "core/log.h"

namespace sw {

std::optional<Json> loadJsonFile(const std::string& absPath) {
    auto text = fs::readText(absPath);
    if (!text) {
        LOG_WARN("json: missing file %s", absPath.c_str());
        return std::nullopt;
    }
    try {
        return Json::parse(*text, nullptr, true, true /*ignore comments*/);
    } catch (const std::exception& e) {
        LOG_ERROR("json: parse error in %s: %s", absPath.c_str(), e.what());
        return std::nullopt;
    }
}

bool saveJsonFile(const std::string& absPath, const Json& j, bool safe) {
    std::string text = j.dump(2);
    return safe ? fs::writeTextSafe(absPath, text) : fs::writeText(absPath, text);
}

Vec2 jvec2(const Json& j, const char* key, const Vec2& def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 2) return def;
    return {(*it)[0].get<float>(), (*it)[1].get<float>()};
}
Vec3 jvec3(const Json& j, const char* key, const Vec3& def) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_number()) return Vec3(it->get<float>());
    if (!it->is_array() || it->size() < 3) return def;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}
Vec4 jvec4(const Json& j, const char* key, const Vec4& def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 4) return def;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
}
Quat jquat(const Json& j, const char* key, const Quat& def) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_number()) return Quat::angleAxis(it->get<float>() * kDeg2Rad, {0, 1, 0});  // yaw only
    if (!it->is_array()) return def;
    if (it->size() == 4) return Quat((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()).normalized();
    if (it->size() == 3)
        return Quat::euler((*it)[0].get<float>() * kDeg2Rad, (*it)[1].get<float>() * kDeg2Rad, (*it)[2].get<float>() * kDeg2Rad);
    return def;
}
Vec3 jcolor(const Json& j, const char* key, const Vec3& def) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_string()) {
        std::string s = it->get<std::string>();
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        uint32_t v = uint32_t(std::stoul(s, nullptr, 16));
        return hexColor(v);
    }
    return jvec3(j, key, def);
}

Json toJson(const Vec2& v) { return Json::array({v.x, v.y}); }
Json toJson(const Vec3& v) { return Json::array({v.x, v.y, v.z}); }
Json toJson(const Vec4& v) { return Json::array({v.x, v.y, v.z, v.w}); }
Json toJson(const Quat& q) { return Json::array({q.x, q.y, q.z, q.w}); }

Vec3 quatToEulerDeg(const Quat& q) {
    // inverse of Quat::euler (yaw * pitch * roll => Y X Z order)
    Mat4 m = Mat4::rotation(q);
    float pitch = std::asin(clampf(-m.at(1, 2), -1.0f, 1.0f));
    float yaw, roll;
    if (std::fabs(m.at(1, 2)) < 0.9999f) {
        yaw = std::atan2(m.at(0, 2), m.at(2, 2));
        roll = std::atan2(m.at(1, 0), m.at(1, 1));
    } else {
        yaw = std::atan2(-m.at(2, 0), m.at(0, 0));
        roll = 0;
    }
    return Vec3(pitch, yaw, roll) * kRad2Deg;
}

Json toJsonEuler(const Quat& q) {
    Vec3 e = quatToEulerDeg(q);
    auto r = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
    return Json::array({r(e.x), r(e.y), r(e.z)});
}

}  // namespace sw
