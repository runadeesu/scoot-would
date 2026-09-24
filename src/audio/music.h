// scoot would - music system
//
// A playlist (assets/music/playlist.json) of tracks. A track is either a streamed audio file
// (wav / flac / mp3 dropped into assets/music) or a song description rendered by the built in
// synthesiser (drums, bass, electric piano, pad, pluck lead, vinyl noise, reverb). Songs render on
// a background thread; the next track is prepared while the current one plays and tracks crossfade.
#pragma once

#include "audio/audio.h"
#include "core/json.h"

#include <future>
#include <string>
#include <vector>

namespace sw {

// render a song description into stereo PCM
SoundPtr renderSong(const Json& song, const std::string& name);

struct TrackInfo {
    std::string title;
    std::string artist;
    std::string source;  // song json or audio file
    bool menu = false;   // plays on the menus only
};

class MusicPlayer {
public:
    void init();
    void shutdown();
    void update(float dt);

    // context: menu music vs gameplay playlist
    void setContext(bool menu);
    void next();
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void setShuffle(bool s) { shuffle_ = s; }
    void setDuck(float d) { duckTarget_ = clampf(d, 0.0f, 1.0f); }  // 1 = full volume

    const TrackInfo* current() const { return current_ >= 0 ? &tracks_[size_t(current_)] : nullptr; }
    float currentAge() const { return age_; }  // seconds since the current track started
    const std::vector<TrackInfo>& tracks() const { return tracks_; }

private:
    int pickNext() const;
    void startLoad(int index);
    void startTrack(const SoundPtr& s, int index);

    std::vector<TrackInfo> tracks_;
    bool enabled_ = true, menu_ = true, shuffle_ = true;
    int current_ = -1, loading_ = -1;
    std::future<SoundPtr> pending_;
    AudioSystem::Voice voice_ = AudioSystem::kNoVoice, fading_ = AudioSystem::kNoVoice;
    float fadeIn_ = 1.0f, fadeOut_ = 0.0f, age_ = 0.0f, length_ = 0.0f;
    float duck_ = 1.0f, duckTarget_ = 1.0f;
    std::vector<int> history_;
    std::vector<SoundPtr> cache_;  // rendered songs by track index
};

MusicPlayer& music();

}  // namespace sw
