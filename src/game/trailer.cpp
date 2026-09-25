#include "game/trailer.h"

#include "core/log.h"
#include "ui/ui.h"

#include <cmath>
#include <cstdlib>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// recording pipes frames into an ffmpeg process: desktop only (iOS has no popen)
#if defined(_WIN32)
#define SW_POPEN _popen
#define SW_PCLOSE _pclose
#elif defined(__APPLE__) && TARGET_OS_IPHONE
#define SW_NO_RECORDER 1
#else
#define SW_POPEN popen
#define SW_PCLOSE pclose
#endif

namespace sw {

namespace {
float smooth01(float x) {
    x = saturate(x);
    return x * x * (3.0f - 2.0f * x);
}
}  // namespace

bool Trailer::load(const std::string& absPath) {
    auto j = loadJsonFile(absPath);
    if (!j) return false;
    fps_ = jget<int>(*j, "fps", 30);
    width_ = jget<int>(*j, "width", 1920);
    height_ = jget<int>(*j, "height", 1080);
    letterbox_ = jget<float>(*j, "letterbox", 0.0f);
    for (const Json& s : j->value("shots", Json::array())) {
        TrailerShot sh;
        sh.test = s;
        sh.map = jget<std::string>(s, "map", "assets/scenes/testpark.json");
        sh.env = jget<std::string>(s, "env", "");
        sh.view = jget<std::string>(s, "view", "third");
        sh.preroll = jget<float>(s, "preroll", 0.0f);
        sh.length = jget<float>(s, "length", 3.0f);
        sh.fadeIn = jget<float>(s, "fadeIn", 0.0f);
        sh.fadeOut = jget<float>(s, "fadeOut", 0.0f);
        sh.hideRider = jget<bool>(s, "hideRider", false);
        sh.camera = s.value("camera", Json::object());
        sh.custom = s.value("custom", Json::object());
        for (const Json& m : s.value("slowmo", Json::array())) sh.slowmo.push_back({m.value("from", 0.0f), m.value("to", 0.0f), m.value("scale", 0.4f)});
        for (const Json& t : s.value("texts", Json::array())) {
            TrailerText tx;
            tx.at = t.value("at", 0.0f);
            tx.dur = t.value("dur", 2.5f);
            tx.title = t.value("title", std::string());
            tx.sub = t.value("sub", std::string());
            tx.place = t.value("place", std::string("center"));
            tx.big = t.value("big", false);
            tx.fadeIn = t.value("fadeIn", tx.fadeIn);
            tx.fadeOut = t.value("fadeOut", tx.fadeOut);
            sh.texts.push_back(tx);
        }
        // the autotest runs for the pre roll plus what is recorded (slow motion stretches the recorded part less
        // than real time: the simulation needs at most the recorded length)
        sh.test["duration"] = sh.preroll + sh.length + 1.0f;
        shots_.push_back(sh);
    }
    LOG_INFO("trailer: %zu shots, %.1f s, %dx%d at %d fps", shots_.size(), double(totalLength()), width_, height_, fps_);
    return !shots_.empty();
}

float Trailer::totalLength() const {
    float t = 0.0f;
    for (auto& s : shots_) t += s.length;
    return t;
}

float Trailer::timeScale(const TrailerShot& s, float t) const {
    float scale = 1.0f;
    for (const TrailerSlowmo& m : s.slowmo) {
        if (t < m.from - 0.25f || t > m.to + 0.25f) continue;
        // ramps in and out over a quarter second (a speed ramp, like the edits)
        float w = std::min(smooth01((t - (m.from - 0.25f)) / 0.25f), smooth01(((m.to + 0.25f) - t) / 0.25f));
        scale = std::min(scale, lerpf(1.0f, m.scale, w));
    }
    return scale;
}

bool Trailer::camera(const TrailerShot& s, float t, float dt, const Vec3& rider, const Vec3& riderVel, Vec3& pos, Vec3& target, float& fov) {
    const Json& c = s.camera;
    std::string type = jget<std::string>(c, "type", "game");
    if (type == "game") return false;
    fov = jget<float>(c, "fov", 50.0f);
    float sharp = jget<float>(c, "smooth", 7.0f);
    // travel heading from the velocity (spins in the air never turn the camera)
    Vec3 vh(riderVel.x, 0.0f, riderVel.z);
    if (c.contains("heading")) {
        Vec2 h = jvec2(c, "heading");
        heading_ = Vec3(h.x, 0.0f, h.y).normalized();
    } else if (vh.length() > 0.8f) {
        heading_ = camInit_ ? lerp(heading_, vh.normalized(), damp(3.0f, dt)).normalized() : vh.normalized();
    }
    Vec3 right = cross(heading_, Vec3(0, 1, 0)).normalized();
    auto riderFrame = [&](const Vec3& o) { return right * o.x + Vec3(0, o.y, 0) + heading_ * o.z; };
    Vec3 look = rider + riderFrame(jvec3(c, "look", Vec3(0.0f, 1.0f, 0.0f)));
    Vec3 p;
    if (type == "track") {
        // moves with the rider: offset in the rider's travel frame (x right, y up, z ahead)
        p = rider + riderFrame(jvec3(c, "offset", Vec3(2.8f, 0.6f, 0.4f)));
    } else if (type == "fixed") {
        p = jvec3(c, "pos");
        if (c.contains("target")) look = jvec3(c, "target");
    } else if (type == "dolly") {
        float u = smooth01(t / std::max(0.01f, s.length));
        p = lerp(jvec3(c, "from"), jvec3(c, "to"), u);
        if (c.contains("lookFrom")) look = lerp(jvec3(c, "lookFrom"), jvec3(c, "lookTo", jvec3(c, "lookFrom")), u);
    } else if (type == "orbit") {
        Vec3 centre = c.contains("center") ? jvec3(c, "center") : rider + Vec3(0.0f, 0.0f, 0.0f);
        float a = (jget<float>(c, "start", 0.0f) + jget<float>(c, "speed", 20.0f) * t) * kDeg2Rad;
        float r = jget<float>(c, "radius", 3.5f), h = jget<float>(c, "height", 1.4f);
        p = centre + Vec3(std::cos(a) * r, h, std::sin(a) * r);
        if (c.contains("center")) look = centre + jvec3(c, "look", Vec3(0.0f, 1.0f, 0.0f));
    } else {
        return false;
    }
    if (!camInit_ || jget<bool>(c, "locked", false)) {
        posS_ = p;
        lookS_ = look;
        camInit_ = true;
    } else {
        posS_ = type == "track" ? lerp(posS_, p, damp(sharp, dt)) : p;
        lookS_ = lerp(lookS_, look, damp(sharp, dt));
    }
    pos = posS_;
    target = lookS_;
    return true;
}

void Trailer::drawOverlay(ui::Context& ui, const TrailerShot& s, float t) const {
    using ui::Align;
    using ui::FontStyle;
    using ui::Rect;
    float W = ui.width(), H = ui.height();
    // titles
    for (const TrailerText& tx : s.texts) {
        float u = t - tx.at;
        if (u < 0.0f || u > tx.dur) continue;
        float a = std::min(tx.fadeIn > 0.0f ? smooth01(u / tx.fadeIn) : 1.0f, tx.fadeOut > 0.0f ? smooth01((tx.dur - u) / tx.fadeOut) : 1.0f);
        float rise = tx.fadeIn > 0.0f ? (1.0f - smooth01(u / 0.5f)) * 18.0f : 0.0f;
        float y = tx.place == "lower" ? H * 0.72f : tx.place == "upper" ? H * 0.2f : H * 0.44f;
        float titleSize = tx.big ? 150.0f : 84.0f;
        if (!tx.title.empty())
            ui.textBox(tx.title, Rect(0, y - titleSize * 0.6f + rise, W, titleSize * 1.2f), titleSize, Vec4(1, 1, 1, a), FontStyle::Display, Align::Center, 0.55f * a);
        if (!tx.sub.empty())
            ui.textBox(tx.sub, Rect(0, y + titleSize * 0.62f + rise, W, 50), tx.big ? 40.0f : 34.0f, Vec4(1, 0.8f, 0.25f, a), FontStyle::Bold, Align::Center,
                       0.55f * a);
    }
    // letterbox bars
    if (letterbox_ > 0.0f) {
        float bh = H * letterbox_;
        ui.rect(Rect(0, 0, W, bh), Vec4(0, 0, 0, 1));
        ui.rect(Rect(0, H - bh, W, bh), Vec4(0, 0, 0, 1));
    }
    // dips to black
    float fade = 0.0f;
    if (s.fadeIn > 0.0f) fade = std::max(fade, 1.0f - smooth01(t / s.fadeIn));
    if (s.fadeOut > 0.0f) fade = std::max(fade, 1.0f - smooth01((s.length - t) / s.fadeOut));
    if (fade > 0.001f) ui.rect(Rect(0, 0, W, H), Vec4(0, 0, 0, fade));
}

bool Trailer::openRecorder(const std::string& outPath, int w, int h) {
#ifdef SW_NO_RECORDER
    (void)w;
    (void)h;
    LOG_ERROR("trailer: recording %s needs a desktop build (ffmpeg)", outPath.c_str());
    return false;
#else
    const char* env = std::getenv("SCOOT_FFMPEG");
    std::string ff = env && *env ? env : "ffmpeg";
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -c:v libx264 -preset medium -crf 17 -pix_fmt yuv420p "
             "-movflags +faststart \"%s\"",
             ff.c_str(), w, h, fps_, outPath.c_str());
#ifdef _WIN32
    pipe_ = SW_POPEN(cmd, "wb");
#else
    pipe_ = SW_POPEN(cmd, "w");
#endif
    if (!pipe_) {
        LOG_ERROR("trailer: cannot start ffmpeg (%s)", cmd);
        return false;
    }
    recW_ = w;
    recH_ = h;
    LOG_INFO("trailer: recording %dx%d to %s", w, h, outPath.c_str());
    return true;
#endif
}

void Trailer::writeFrame(const uint8_t* rgba, int w, int h) {
    if (!pipe_ || w != recW_ || h != recH_) return;
    fwrite(rgba, 1, size_t(w) * size_t(h) * 4, pipe_);
    ++frames_;
}

void Trailer::closeRecorder() {
    if (!pipe_) return;
#ifndef SW_NO_RECORDER
    SW_PCLOSE(pipe_);
#endif
    pipe_ = nullptr;
    LOG_INFO("trailer: %d frames written (%.1f s)", frames_, double(frames_) / double(fps_));
}

void Trailer::logSound(const std::string& sound, double t, float volume) {
    sounds_.push_back({{"t", t}, {"sound", sound}, {"volume", volume}});
}

void Trailer::logBed(const std::string& sound, int frame, float volume) {
    if (frame < 0) return;
    std::vector<float>& b = beds_[sound];
    if (int(b.size()) <= frame) b.resize(size_t(frame) + 1, 0.0f);
    b[size_t(frame)] = std::max(b[size_t(frame)], volume);
}

void Trailer::saveSounds(const std::string& absPath) const {
    Json j;
    j["length"] = totalLength();
    j["fps"] = fps_;
    j["sounds"] = sounds_;
    Json beds = Json::object();
    for (auto& [name, v] : beds_) {
        Json a = Json::array();
        for (float x : v) a.push_back(std::round(x * 1000.0f) / 1000.0f);
        beds[name] = a;
    }
    j["beds"] = beds;
    saveJsonFile(absPath, j, false);
}

}  // namespace sw
