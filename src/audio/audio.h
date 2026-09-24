// scoot would - audio engine (miniaudio): mixer buses, one shot / looping voices, 3D spatialisation
//
// Buses: Master > {Music, Sfx, Environment, Ui}. Sounds are loaded once through the asset manager
// (decoded into memory) and shared by every voice that plays them. Positional voices are attenuated
// and panned relative to the listener, with doppler from the voice / listener velocities.
// When no audio device is available (servers, CI, headless tests) the engine runs without output
// so gameplay code never has to care.
#pragma once

#include "core/math.h"

#include <memory>
#include <string>
#include <vector>

namespace sw {

enum class Bus { Master = 0, Music, Sfx, Environment, Ui, Count };

// decoded PCM shared by voices (float, interleaved)
struct SoundAsset {
    std::string path;
    std::vector<float> frames;
    int channels = 1;
    int sampleRate = 44100;
    float duration() const { return channels > 0 && sampleRate > 0 ? float(frames.size() / size_t(channels)) / float(sampleRate) : 0.0f; }
};
using SoundPtr = std::shared_ptr<SoundAsset>;

// decode a WAV / FLAC / MP3 file (relative to the data root)
SoundPtr loadSoundFile(const std::string& relPath);

struct PlayParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    bool spatial = false;
    Vec3 position;
    float minDistance = 2.0f;   // full volume inside
    float maxDistance = 80.0f;  // attenuation stops here
    float pan = 0.0f;           // 2D voices
};

class AudioSystem {
public:
    using Voice = uint32_t;
    static constexpr Voice kNoVoice = 0;

    bool init(bool useDevice = true);
    void shutdown();
    bool initialized() const { return be_ != nullptr; }
    bool hasDevice() const { return hasDevice_; }
    std::string deviceName() const { return deviceName_; }

    void setBusVolume(Bus b, float v);
    float busVolume(Bus b) const { return busVolume_[int(b)]; }
    void setMuted(bool m);  // e.g. window lost focus

    void setListener(const Vec3& pos, const Vec3& forward, const Vec3& up, const Vec3& velocity);

    // "pop" resolves to assets/audio/pop.(wav|flac|mp3); a path with an extension is used as is
    SoundPtr sound(const std::string& name);
    Voice play(const std::string& name, Bus bus, const PlayParams& p = {});
    Voice play(const SoundPtr& s, Bus bus, const PlayParams& p = {});
    // looping voice that stays alive until stopped (engine loops, ambience, music)
    Voice loop(const std::string& name, Bus bus, const PlayParams& p = {});
    Voice loop(const SoundPtr& s, Bus bus, const PlayParams& p = {});
    void setVolume(Voice v, float volume);
    void setPitch(Voice v, float pitch);
    void setPosition(Voice v, const Vec3& pos, const Vec3& velocity = Vec3(0));
    void stop(Voice v);
    bool playing(Voice v) const;
    float cursorSeconds(Voice v) const;
    void stopBus(Bus b);

    // call once per frame: reclaims finished one shot voices
    void update(float dt);
    int activeVoices() const;

private:
    struct VoiceSlot;
    VoiceSlot* find(Voice v) const;
    Voice start(const SoundPtr& s, Bus bus, const PlayParams& p, bool looping);

    struct Backend;  // miniaudio engine + bus groups
    Backend* be_ = nullptr;
    float busVolume_[int(Bus::Count)] = {1, 0.7f, 1, 0.8f, 0.8f};
    bool hasDevice_ = false;
    bool muted_ = false;
    std::string deviceName_;
    std::vector<std::unique_ptr<VoiceSlot>> voices_;
    uint32_t nextId_ = 1;
};

AudioSystem& audio();

}  // namespace sw
