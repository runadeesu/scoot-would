// scoot would - trailer director: plays a script of shots (rider inputs, cinematic cameras, slow motion, title
// cards, fades) and records every frame into a video through ffmpeg.
//
//   scoot would --trailer assets/trailer/trailer.json --record out/video.mp4
//
// A shot is an autotest script (map, spawn, yaw, velocity, timed / conditional inputs) plus: "preroll" (seconds of
// simulation before the shot starts recording, not rendered), "length" (recorded seconds), "slowmo" ranges (in
// recorded seconds, with a time scale), a "camera", "texts" and fades. The gameplay events of the recorded part
// (pops, landings, trick starts) and the looped beds (rolling, wind, grinds) are written next to the video
// (<video>.sounds.json) so the sound effects can be mixed under the music (tools/trailer_audio.py,
// tools/make_trailer.sh).
#pragma once

#include "core/json.h"
#include "core/math.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace sw {

namespace ui {
class Context;
}

struct TrailerText {
    float at = 0.0f, dur = 2.5f;  // recorded seconds into the shot
    std::string title, sub;
    std::string place = "center";  // center | lower | upper
    bool big = false;              // the game's name
    float fadeIn = 0.35f, fadeOut = 0.4f;  // 0 = cut (a card carried across two shots)
};

struct TrailerSlowmo {
    float from = 0.0f, to = 0.0f, scale = 0.4f;
};

struct TrailerShot {
    Json test;  // autotest part
    std::string map, env, view;
    float preroll = 0.0f, length = 3.0f;
    float fadeIn = 0.0f, fadeOut = 0.0f;
    std::vector<TrailerSlowmo> slowmo;
    Json camera;
    Json custom;  // scooter parts / colours for this shot (same keys as the save's customisation)
    std::vector<TrailerText> texts;
    bool hideRider = false;
};

class Trailer {
public:
    bool load(const std::string& absPath);
    int fps() const { return fps_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<TrailerShot>& shots() const { return shots_; }
    float totalLength() const;
    float letterbox() const { return letterbox_; }

    // time scale for a moment of a shot (slow motion ranges ease in and out)
    float timeScale(const TrailerShot& s, float t) const;
    // cinematic camera: returns false for "game" (the gameplay camera is used)
    bool camera(const TrailerShot& s, float t, float dt, const Vec3& rider, const Vec3& riderVel, Vec3& pos, Vec3& target, float& fov);
    void resetCamera() { camInit_ = false; }
    // titles, fades, letterbox
    void drawOverlay(ui::Context& ui, const TrailerShot& s, float t) const;

    // video out
    bool openRecorder(const std::string& outPath, int w, int h);
    void writeFrame(const uint8_t* rgba, int w, int h);
    void closeRecorder();
    bool recording() const { return pipe_ != nullptr; }
    int framesWritten() const { return frames_; }

    // gameplay sounds for the mix: one shots at a time (seconds of video) and looped beds (rolling wheels, wind,
    // grinds) as a volume per recorded frame
    void logSound(const std::string& sound, double t, float volume);
    void logBed(const std::string& sound, int frame, float volume);
    void saveSounds(const std::string& absPath) const;

private:
    int fps_ = 30, width_ = 1920, height_ = 1080;
    float letterbox_ = 0.0f;
    std::vector<TrailerShot> shots_;
    FILE* pipe_ = nullptr;
    int frames_ = 0, recW_ = 0, recH_ = 0;
    Json sounds_ = Json::array();
    std::map<std::string, std::vector<float>> beds_;
    // smoothed camera state
    bool camInit_ = false;
    Vec3 heading_{0, 0, -1}, lookS_, posS_;
};

}  // namespace sw
