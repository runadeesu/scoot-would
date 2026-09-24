#include "audio/audio.h"

#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"

#include <miniaudio.h>

#include <algorithm>

namespace sw {

namespace {
AudioSystem g_audio;
constexpr size_t kMaxVoices = 96;
}  // namespace

AudioSystem& audio() { return g_audio; }

struct AudioSystem::Backend {
    ma_engine engine;
    ma_sound_group groups[int(Bus::Count)];
};

struct AudioSystem::VoiceSlot {
    uint32_t id = 0;
    SoundPtr sound;  // keeps the PCM alive while the voice plays
    ma_audio_buffer_ref ref{};
    ma_sound snd{};
    bool looping = false;
    bool spatial = false;
    Bus bus = Bus::Sfx;
    double started = 0.0;
};

SoundPtr loadSoundFile(const std::string& relPath) {
    std::string abs = fs::resolve(relPath);
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder dec;
    if (ma_decoder_init_file(abs.c_str(), &cfg, &dec) != MA_SUCCESS) {
        LOG_WARN("audio: cannot decode %s", relPath.c_str());
        return nullptr;
    }
    auto s = std::make_shared<SoundAsset>();
    s->path = relPath;
    s->channels = int(dec.outputChannels);
    s->sampleRate = int(dec.outputSampleRate);
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) != MA_SUCCESS || total == 0) total = 0;
    std::vector<float> chunk(4096 * size_t(s->channels));
    if (total > 0) s->frames.reserve(size_t(total) * size_t(s->channels));
    for (;;) {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &read);
        if (read == 0) break;
        s->frames.insert(s->frames.end(), chunk.begin(), chunk.begin() + long(read * ma_uint64(s->channels)));
        if (read < 4096) break;
    }
    ma_decoder_uninit(&dec);
    if (s->frames.empty()) return nullptr;
    return s;
}

// asset manager hook (declared in assets/asset_manager.h)
SoundPtr AssetManager::sound(const std::string& relPath) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sounds_.find(relPath);
        if (it != sounds_.end()) return it->second;
    }
    SoundPtr s = loadSoundFile(relPath);
    std::lock_guard<std::mutex> lock(mutex_);
    sounds_[relPath] = s;  // cache failures too (no retry every frame)
    return s;
}

bool AudioSystem::init(bool useDevice) {
    be_ = new Backend();
    ma_engine* engine_ = &be_->engine;
    ma_engine_config cfg = ma_engine_config_init();
    cfg.listenerCount = 1;
    ma_result r = MA_ERROR;
    if (useDevice) r = ma_engine_init(&cfg, engine_);
    if (r == MA_SUCCESS) {
        hasDevice_ = true;
        ma_device* dev = ma_engine_get_device(engine_);
        deviceName_ = dev ? dev->playback.name : "default";
    } else {
        // no output device: keep the mixer running so voices behave the same
        if (useDevice) LOG_WARN("audio: no output device (%s), running silent", ma_result_description(r));
        cfg = ma_engine_config_init();
        cfg.noDevice = MA_TRUE;
        cfg.channels = 2;
        cfg.sampleRate = 48000;
        if (ma_engine_init(&cfg, engine_) != MA_SUCCESS) {
            LOG_ERROR("audio: engine init failed");
            delete be_;
            be_ = nullptr;
            return false;
        }
        deviceName_ = "none";
    }
    for (int b = 0; b < int(Bus::Count); ++b) {
        ma_sound_group_init(engine_, 0, b == 0 ? nullptr : &be_->groups[0], &be_->groups[b]);
        ma_sound_group_set_volume(&be_->groups[b], busVolume_[b]);
    }
    LOG_INFO("audio: %s, %u Hz, %u channels", deviceName_.c_str(), ma_engine_get_sample_rate(engine_), ma_engine_get_channels(engine_));
    return true;
}

void AudioSystem::shutdown() {
    if (!be_) return;
    for (auto& v : voices_) ma_sound_uninit(&v->snd);
    voices_.clear();
    for (int b = int(Bus::Count) - 1; b >= 0; --b) ma_sound_group_uninit(&be_->groups[b]);
    ma_engine_uninit(&be_->engine);
    delete be_;
    be_ = nullptr;
}

void AudioSystem::setBusVolume(Bus b, float v) {
    busVolume_[int(b)] = clampf(v, 0.0f, 1.0f);
    if (!be_) return;
    float out = busVolume_[int(b)];
    if (b == Bus::Master && muted_) out = 0.0f;
    ma_sound_group_set_volume(&be_->groups[int(b)], out);
}

void AudioSystem::setMuted(bool m) {
    muted_ = m;
    setBusVolume(Bus::Master, busVolume_[0]);
}

void AudioSystem::setListener(const Vec3& pos, const Vec3& forward, const Vec3& up, const Vec3& velocity) {
    if (!be_) return;
    ma_engine* engine_ = &be_->engine;
    ma_engine_listener_set_position(engine_, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(engine_, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(engine_, 0, up.x, up.y, up.z);
    ma_engine_listener_set_velocity(engine_, 0, velocity.x, velocity.y, velocity.z);
}

SoundPtr AudioSystem::sound(const std::string& name) {
    if (name.find('.') != std::string::npos) return assets().sound(name);
    static const char* exts[] = {".wav", ".flac", ".mp3"};
    for (const char* e : exts) {
        std::string p = "assets/audio/" + name + e;
        if (fs::exists(fs::resolve(p))) return assets().sound(p);
    }
    static std::string lastMissing;
    if (lastMissing != name) {
        LOG_WARN("audio: sound '%s' not found", name.c_str());
        lastMissing = name;
    }
    return nullptr;
}

AudioSystem::VoiceSlot* AudioSystem::find(Voice v) const {
    if (v == kNoVoice) return nullptr;
    for (auto& s : voices_)
        if (s->id == v) return s.get();
    return nullptr;
}

AudioSystem::Voice AudioSystem::start(const SoundPtr& s, Bus bus, const PlayParams& p, bool looping) {
    if (!be_ || !s || s->frames.empty()) return kNoVoice;
    ma_engine* engine_ = &be_->engine;
    if (voices_.size() >= kMaxVoices) {
        // steal the oldest one shot
        auto it = std::min_element(voices_.begin(), voices_.end(), [](const auto& a, const auto& b) {
            if (a->looping != b->looping) return !a->looping;
            return a->started < b->started;
        });
        if (it == voices_.end() || (*it)->looping) return kNoVoice;
        ma_sound_uninit(&(*it)->snd);
        voices_.erase(it);
    }
    auto slot = std::make_unique<VoiceSlot>();
    slot->id = nextId_++;
    if (nextId_ == 0) nextId_ = 1;
    slot->sound = s;
    slot->looping = looping;
    slot->spatial = p.spatial;
    slot->bus = bus;
    slot->started = double(ma_engine_get_time_in_milliseconds(engine_));
    ma_audio_buffer_ref_init(ma_format_f32, ma_uint32(s->channels), s->frames.data(), ma_uint64(s->frames.size() / size_t(s->channels)), &slot->ref);
    slot->ref.sampleRate = ma_uint32(s->sampleRate);
    ma_uint32 flags = p.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(engine_, &slot->ref, flags, &be_->groups[int(bus)], &slot->snd) != MA_SUCCESS) {
        LOG_WARN("audio: cannot start voice for %s", s->path.c_str());
        return kNoVoice;
    }
    ma_sound_set_looping(&slot->snd, looping ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&slot->snd, p.volume);
    ma_sound_set_pitch(&slot->snd, std::max(0.05f, p.pitch));
    if (p.spatial) {
        ma_sound_set_position(&slot->snd, p.position.x, p.position.y, p.position.z);
        ma_sound_set_attenuation_model(&slot->snd, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&slot->snd, p.minDistance);
        ma_sound_set_max_distance(&slot->snd, p.maxDistance);
        ma_sound_set_rolloff(&slot->snd, 1.0f);
        ma_sound_set_doppler_factor(&slot->snd, 0.6f);
    } else {
        ma_sound_set_pan(&slot->snd, p.pan);
    }
    ma_sound_start(&slot->snd);
    Voice id = slot->id;
    voices_.push_back(std::move(slot));
    return id;
}

AudioSystem::Voice AudioSystem::play(const std::string& name, Bus bus, const PlayParams& p) { return start(sound(name), bus, p, false); }
AudioSystem::Voice AudioSystem::play(const SoundPtr& s, Bus bus, const PlayParams& p) { return start(s, bus, p, false); }
AudioSystem::Voice AudioSystem::loop(const std::string& name, Bus bus, const PlayParams& p) { return start(sound(name), bus, p, true); }
AudioSystem::Voice AudioSystem::loop(const SoundPtr& s, Bus bus, const PlayParams& p) { return start(s, bus, p, true); }

void AudioSystem::setVolume(Voice v, float volume) {
    if (VoiceSlot* s = find(v)) ma_sound_set_volume(&s->snd, std::max(0.0f, volume));
}
void AudioSystem::setPitch(Voice v, float pitch) {
    if (VoiceSlot* s = find(v)) ma_sound_set_pitch(&s->snd, clampf(pitch, 0.05f, 8.0f));
}
void AudioSystem::setPosition(Voice v, const Vec3& pos, const Vec3& vel) {
    VoiceSlot* s = find(v);
    if (!s || !s->spatial) return;
    ma_sound_set_position(&s->snd, pos.x, pos.y, pos.z);
    ma_sound_set_velocity(&s->snd, vel.x, vel.y, vel.z);
}
void AudioSystem::stop(Voice v) {
    for (auto it = voices_.begin(); it != voices_.end(); ++it)
        if ((*it)->id == v) {
            ma_sound_uninit(&(*it)->snd);
            voices_.erase(it);
            return;
        }
}
bool AudioSystem::playing(Voice v) const {
    VoiceSlot* s = find(v);
    return s && !ma_sound_at_end(&s->snd);
}
float AudioSystem::cursorSeconds(Voice v) const {
    VoiceSlot* s = find(v);
    float c = 0.0f;
    if (s) ma_sound_get_cursor_in_seconds(&s->snd, &c);
    return c;
}
void AudioSystem::stopBus(Bus b) {
    for (auto it = voices_.begin(); it != voices_.end();) {
        if ((*it)->bus == b) {
            ma_sound_uninit(&(*it)->snd);
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::update(float dt) {
    if (!be_) return;
    ma_engine* engine_ = &be_->engine;
    if (!hasDevice_) {
        // advance the silent mixer by real time so one shots finish and get reclaimed
        static std::vector<float> scratch;
        ma_uint64 frames = ma_uint64(clampf(dt, 0.0f, 0.1f) * float(ma_engine_get_sample_rate(engine_)));
        scratch.resize(size_t(frames) * ma_engine_get_channels(engine_));
        if (frames > 0) ma_engine_read_pcm_frames(engine_, scratch.data(), frames, nullptr);
    }
    for (auto it = voices_.begin(); it != voices_.end();) {
        if (!(*it)->looping && ma_sound_at_end(&(*it)->snd)) {
            ma_sound_uninit(&(*it)->snd);
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
}

int AudioSystem::activeVoices() const { return int(voices_.size()); }

}  // namespace sw
