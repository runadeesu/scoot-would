#include "audio/music.h"

#include "core/filesystem.h"
#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace sw {

namespace {
MusicPlayer g_music;
constexpr int SR = 44100;

// ---------------------------------------------------------------------------------------------
// synth building blocks

struct SynthRng {
    uint64_t s;
    explicit SynthRng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 32);
    }
    float uni() { return float(next() & 0xFFFFFF) / float(0x1000000); }
    float white() { return uni() * 2.0f - 1.0f; }
    int range(int n) { return int(next() % uint32_t(std::max(1, n))); }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(int type, float f, float q) {
        float w = kTwoPi * clampf(f, 10.0f, SR * 0.45f) / SR, cs = std::cos(w), sn = std::sin(w), al = sn / (2 * q), a0 = 1 + al;
        if (type == 0) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; }
        else if (type == 1) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; }
        else { b0 = al; b1 = 0; b2 = -al; }
        a1 = -2 * cs;
        a2 = 1 - al;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    float operator()(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

float midiHz(float m) { return 440.0f * std::pow(2.0f, (m - 69.0f) / 12.0f); }

int noteNumber(const std::string& s) {
    // "D3", "F#2", "Bb4"
    static const std::map<char, int> base = {{'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}};
    if (s.empty() || !base.count(char(std::toupper(s[0])))) return 50;
    int n = base.at(char(std::toupper(s[0])));
    size_t i = 1;
    if (i < s.size() && s[i] == '#') { ++n; ++i; }
    else if (i < s.size() && s[i] == 'b') { --n; ++i; }
    int oct = i < s.size() ? std::atoi(s.c_str() + i) : 3;
    return (oct + 1) * 12 + n;
}

std::vector<int> scaleSteps(const std::string& name) {
    if (name == "major") return {0, 2, 4, 5, 7, 9, 11};
    if (name == "dorian") return {0, 2, 3, 5, 7, 9, 10};
    if (name == "mixolydian") return {0, 2, 4, 5, 7, 9, 10};
    if (name == "phrygian") return {0, 1, 3, 5, 7, 8, 10};
    if (name == "pentatonic") return {0, 3, 5, 7, 10};
    return {0, 2, 3, 5, 7, 8, 10};  // natural minor
}

std::vector<int> chordTones(const std::string& q) {
    if (q == "maj7") return {0, 4, 7, 11};
    if (q == "m7") return {0, 3, 7, 10};
    if (q == "7") return {0, 4, 7, 10};
    if (q == "m9") return {0, 3, 7, 10, 14};
    if (q == "maj9") return {0, 4, 7, 11, 14};
    if (q == "sus2") return {0, 2, 7, 12};
    if (q == "5") return {0, 7, 12};
    if (q == "m") return {0, 3, 7, 12};
    return {0, 4, 7, 12};
}

struct Stereo {
    std::vector<float> l, r;
    explicit Stereo(size_t n) : l(n, 0.0f), r(n, 0.0f) {}
    void add(size_t i, float v, float pan) {
        if (i >= l.size()) return;
        float a = (pan + 1.0f) * 0.25f * kPi;
        l[i] += v * std::cos(a);
        r[i] += v * std::sin(a);
    }
};

// electric piano: two operator FM with a decaying index and gentle tremolo
void epiano(std::vector<float>& out, size_t s0, float hz, float dur, float vel) {
    size_t len = size_t((dur + 1.2f) * SR);
    float pc = 0, pm = 0;
    for (size_t i = 0; i < len && s0 + i < out.size(); ++i) {
        float t = float(i) / SR;
        float env = std::exp(-t * 1.6f) * std::min(1.0f, t / 0.004f);
        if (t > dur) env *= std::exp(-(t - dur) * 9.0f);
        float idx = 1.6f * std::exp(-t * 4.0f) + 0.25f;
        pm += kTwoPi * hz / SR;
        pc += kTwoPi * hz / SR;
        float v = std::sin(pc + idx * std::sin(pm)) + 0.12f * std::sin(2.0f * pc) * std::exp(-t * 3.0f);
        v *= 1.0f + 0.1f * std::sin(kTwoPi * 4.8f * t);
        out[s0 + i] += v * env * vel;
    }
}

// pad: detuned saws through a soft low pass, slow attack
void pad(std::vector<float>& out, size_t s0, float hz, float dur, float vel, float cutoff) {
    size_t len = size_t((dur + 0.8f) * SR);
    float ph[3] = {0.0f, 0.33f, 0.71f};
    const float det[3] = {0.996f, 1.0f, 1.0055f};
    Biquad lp;
    lp.set(0, cutoff, 0.8f);
    for (size_t i = 0; i < len && s0 + i < out.size(); ++i) {
        float t = float(i) / SR;
        float env = std::min(1.0f, t / 0.45f);
        if (t > dur) env *= std::exp(-(t - dur) * 5.0f);
        float v = 0;
        for (int k = 0; k < 3; ++k) {
            ph[k] += hz * det[k] / SR;
            ph[k] -= std::floor(ph[k]);
            v += 2.0f * ph[k] - 1.0f;
        }
        out[s0 + i] += lp(v / 3.0f) * env * vel;
    }
}

// Karplus-Strong pluck
void pluck(std::vector<float>& out, size_t s0, float hz, float dur, float vel, SynthRng& rng) {
    size_t period = size_t(std::max(2.0f, float(SR) / hz));
    std::vector<float> buf(period);
    for (auto& b : buf) b = rng.white();
    size_t len = size_t((dur + 0.6f) * SR);
    float last = 0;
    for (size_t i = 0; i < len && s0 + i < out.size(); ++i) {
        size_t k = i % period;
        float v = buf[k];
        float nv = 0.5f * (v + last) * 0.996f;
        last = v;
        buf[k] = nv;
        float t = float(i) / SR;
        float env = t > dur ? std::exp(-(t - dur) * 12.0f) : 1.0f;
        out[s0 + i] += v * env * vel;
    }
}

// bass: sine + a touch of drive, short attack, held until the next note
void bass(std::vector<float>& out, size_t s0, float hz, float dur, float vel) {
    size_t len = size_t((dur + 0.05f) * SR);
    float ph = 0;
    for (size_t i = 0; i < len && s0 + i < out.size(); ++i) {
        float t = float(i) / SR;
        float env = std::min(1.0f, t / 0.006f) * (0.65f + 0.35f * std::exp(-t * 6.0f));
        if (t > dur) env *= std::max(0.0f, 1.0f - (t - dur) / 0.05f);
        ph += kTwoPi * hz / SR;
        float v = std::sin(ph) + 0.25f * std::sin(2.0f * ph) * std::exp(-t * 5.0f);
        out[s0 + i] += std::tanh(v * 1.4f) * env * vel;
    }
}

void kick(std::vector<float>& out, size_t s0, float vel) {
    float ph = 0;
    for (size_t i = 0; i < size_t(0.45f * SR) && s0 + i < out.size(); ++i) {
        float t = float(i) / SR;
        float f = 45.0f + 110.0f * std::exp(-t * 28.0f);
        ph += kTwoPi * f / SR;
        float env = std::exp(-t * 7.5f);
        float click = i < 60 ? (1.0f - float(i) / 60.0f) * 0.4f : 0.0f;
        out[s0 + i] += (std::sin(ph) * env + click) * vel;
    }
}

void snare(std::vector<float>& out, size_t s0, float vel, SynthRng& rng) {
    Biquad bp;
    bp.set(2, 1900.0f, 0.7f);
    float ph = 0;
    for (size_t i = 0; i < size_t(0.3f * SR) && s0 + i < out.size(); ++i) {
        float t = float(i) / SR;
        ph += kTwoPi * 185.0f / SR;
        float v = bp(rng.white()) * 1.8f * std::exp(-t * 16.0f) + std::sin(ph) * 0.5f * std::exp(-t * 28.0f);
        out[s0 + i] += v * vel;
    }
}

void hat(std::vector<float>& out, size_t s0, float vel, float decay, SynthRng& rng) {
    Biquad hp;
    hp.set(1, 7200.0f, 0.7f);
    for (size_t i = 0; i < size_t(decay * 6.0f * SR) && s0 + i < out.size(); ++i) {
        float t = float(i) / SR;
        out[s0 + i] += hp(rng.white()) * std::exp(-t / decay) * vel;
    }
}

// small stereo reverb (Schroeder: 4 combs + 2 allpasses per side)
void reverb(const std::vector<float>& in, Stereo& out, float mix, float room) {
    const int combL[4] = {1116, 1188, 1277, 1356}, combR[4] = {1139, 1211, 1300, 1379};
    const int apL[2] = {556, 441}, apR[2] = {579, 464};
    auto side = [&](const int* cl, const int* ap, std::vector<float>& dst) {
        std::vector<std::vector<float>> combs(4), aps(2);
        size_t ci[4] = {0, 0, 0, 0}, ai[2] = {0, 0};
        float damp[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k) combs[size_t(k)].assign(size_t(cl[k]), 0.0f);
        for (int k = 0; k < 2; ++k) aps[size_t(k)].assign(size_t(ap[k]), 0.0f);
        for (size_t i = 0; i < in.size(); ++i) {
            float x = in[i] * 0.2f, y = 0;
            for (int k = 0; k < 4; ++k) {
                auto& c = combs[size_t(k)];
                float o = c[ci[k]];
                damp[k] = o * 0.6f + damp[k] * 0.4f;
                c[ci[k]] = x + damp[k] * room;
                ci[k] = (ci[k] + 1) % c.size();
                y += o;
            }
            for (int k = 0; k < 2; ++k) {
                auto& a = aps[size_t(k)];
                float b = a[ai[k]];
                a[ai[k]] = y + b * 0.5f;
                y = b - y;
                ai[k] = (ai[k] + 1) % a.size();
            }
            dst[i] += y * mix;
        }
    };
    side(combL, apL, out.l);
    side(combR, apR, out.r);
}

struct Section {
    int bars = 4;
    std::vector<std::string> parts;
    bool has(const char* p) const { return std::find(parts.begin(), parts.end(), p) != parts.end(); }
};

bool hit(const std::string& pattern, int step) { return !pattern.empty() && pattern[size_t(step) % pattern.size()] == 'x'; }
bool ghost(const std::string& pattern, int step) { return !pattern.empty() && pattern[size_t(step) % pattern.size()] == 'o'; }

}  // namespace

SoundPtr renderSong(const Json& j, const std::string& name) {
    auto t0 = std::chrono::steady_clock::now();
    float bpm = jget<float>(j, "bpm", 90.0f);
    float swing = jget<float>(j, "swing", 0.0f);
    int root = noteNumber(jget<std::string>(j, "root", "D3"));
    std::vector<int> scale = scaleSteps(jget<std::string>(j, "scale", "minor"));
    SynthRng rng(uint64_t(jget<int>(j, "seed", 1)));
    std::vector<std::pair<int, std::string>> chords;
    if (j.contains("chords"))
        for (auto& c : j["chords"]) chords.push_back({c[0].get<int>(), c[1].get<std::string>()});
    if (chords.empty()) chords = {{0, "m7"}};
    int barsPerChord = jget<int>(j, "barsPerChord", 1);
    std::vector<Section> sections;
    if (j.contains("structure"))
        for (auto& s : j["structure"]) {
            Section sec;
            sec.bars = jget<int>(s, "bars", 4);
            if (s.contains("parts"))
                for (auto& p : s["parts"]) sec.parts.push_back(p.get<std::string>());
            sections.push_back(sec);
        }
    if (sections.empty()) sections.push_back({8, {"keys", "drums", "bass"}});
    const Json& dr = j.contains("drums") ? j["drums"] : Json::object();
    std::string kickP = jget<std::string>(dr, "kick", "x.........x.....");
    std::string snareP = jget<std::string>(dr, "snare", "....x.......x...");
    std::string hatP = jget<std::string>(dr, "hat", "x.x.x.x.x.x.x.x.");
    std::string openP = jget<std::string>(dr, "openhat", "");
    std::string bassP = jget<std::string>(j, "bassRhythm", "x.....x...x.....");
    std::string keysP = jget<std::string>(j, "keysRhythm", "x...............");
    std::string keysSound = jget<std::string>(j, "keys", "epiano");
    float leadDensity = jget<float>(j, "leadDensity", 0.35f);
    int leadOct = jget<int>(j, "leadOctave", 5);
    float vinyl = jget<float>(j, "vinyl", 0.0f);
    float lowpass = jget<float>(j, "masterLowpass", 12000.0f);
    float padCut = jget<float>(j, "padCutoff", 1400.0f);

    int totalBars = 0;
    for (auto& s : sections) totalBars += s.bars;
    float stepSec = 60.0f / bpm / 4.0f;
    size_t total = size_t((float(totalBars) * 16.0f * stepSec + 2.5f) * SR);
    std::vector<float> keysB(total, 0.0f), padB(total, 0.0f), leadB(total, 0.0f), bassB(total, 0.0f), kickB(total, 0.0f), snareB(total, 0.0f), hatB(total, 0.0f);

    auto stepTime = [&](int globalStep) {
        float t = float(globalStep) * stepSec;
        if (globalStep % 2 == 1) t += swing * stepSec;
        return size_t(t * SR);
    };
    auto chordAt = [&](int bar) { return chords[size_t((bar / std::max(1, barsPerChord)) % int(chords.size()))]; };
    auto scaleNote = [&](int degree) {
        int n = int(scale.size());
        int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
        int d = degree - oct * n;
        return root + oct * 12 + scale[size_t(d)];
    };

    // lead motif: a 2 bar rhythm + contour, repeated with variations in every section that has a lead
    struct LeadNote {
        int step, len, degree;
    };
    std::vector<LeadNote> motif;
    {
        int deg = 7 + rng.range(3);
        for (int s = 0; s < 32;) {
            bool strong = s % 4 == 0;
            if (rng.uni() < (strong ? leadDensity + 0.25f : leadDensity)) {
                int len = 1 + rng.range(3);
                if (rng.uni() < 0.3f) len = 4;
                deg += rng.range(5) - 2;
                deg = std::clamp(deg, 4, 12);
                motif.push_back({s, len, deg});
                s += len;
            } else {
                ++s;
            }
        }
    }

    int bar0 = 0;
    for (const Section& sec : sections) {
        for (int b = 0; b < sec.bars; ++b) {
            int bar = bar0 + b;
            auto ch = chordAt(bar);
            std::vector<int> tones = chordTones(ch.second);
            int chordRoot = root + ch.first;
            bool fill = (b == sec.bars - 1);
            for (int s = 0; s < 16; ++s) {
                int g = bar * 16 + s;
                size_t at = stepTime(g);
                if (sec.has("drums")) {
                    if (hit(kickP, s) || (fill && s == 14 && rng.uni() < 0.5f)) kick(kickB, at, 0.95f);
                    if (hit(snareP, s)) snare(snareB, at, 0.6f, rng);
                    else if (ghost(snareP, s)) snare(snareB, at, 0.18f, rng);
                    if (fill && s >= 12 && rng.uni() < 0.45f) snare(snareB, at, 0.3f, rng);
                    if (hit(hatP, s)) hat(hatB, at, (s % 4 == 0 ? 0.32f : 0.2f) * (0.8f + 0.4f * rng.uni()), 0.035f, rng);
                    if (hit(openP, s)) hat(hatB, at, 0.22f, 0.16f, rng);
                } else if (sec.has("hats") && hit(hatP, s)) {
                    hat(hatB, at, 0.14f, 0.03f, rng);
                }
                if (sec.has("bass") && hit(bassP, s)) {
                    int len = 1;
                    while (len < 16 && !hit(bassP, s + len) && s + len < 16) ++len;
                    int note = chordRoot - 12;
                    if (s >= 8 && rng.uni() < 0.25f) note += 7;
                    if (note < 33) note += 12;
                    bass(bassB, at, midiHz(float(note)), float(len) * stepSec * 0.92f, 0.55f);
                }
                if (sec.has("keys") && hit(keysP, s)) {
                    int len = 1;
                    while (len < 16 && s + len < 16 && !hit(keysP, s + len)) ++len;
                    // voice the chord around middle C, closest inversion
                    for (size_t k = 0; k < tones.size() && k < 4; ++k) {
                        int n = chordRoot + tones[k];
                        while (n < 55) n += 12;
                        while (n > 70) n -= 12;
                        float d = float(len) * stepSec;
                        if (keysSound == "pluck") pluck(keysB, at + k * 180, midiHz(float(n)), d, 0.16f, rng);
                        else epiano(keysB, at + k * 90, midiHz(float(n)), d, 0.17f);
                    }
                }
                if (sec.has("pad") && s == 0) {
                    for (size_t k = 0; k < tones.size() && k < 4; ++k) {
                        int n = chordRoot + tones[k];
                        while (n < 52) n += 12;
                        while (n > 67) n -= 12;
                        pad(padB, at, midiHz(float(n)), 16.0f * stepSec * 0.98f, 0.12f, padCut);
                    }
                }
            }
            if (sec.has("lead")) {
                int half = (bar % 2) * 16;
                bool vary = (bar / 2) % 2 == 1;
                for (const LeadNote& ln : motif) {
                    if (ln.step < half || ln.step >= half + 16) continue;
                    int deg = ln.degree + (vary && ln.step >= 24 ? (bar % 4 == 3 ? -2 : 1) : 0);
                    int note = scaleNote(deg) + (leadOct - 4) * 12;
                    // strong beats land on chord tones
                    if (ln.step % 4 == 0) {
                        int best = note, bestD = 99;
                        for (int t : tones)
                            for (int o = -2; o <= 3; ++o) {
                                int c = chordRoot + t + o * 12;
                                if (std::abs(c - note) < bestD) {
                                    bestD = std::abs(c - note);
                                    best = c;
                                }
                            }
                        note = best;
                    }
                    size_t at = stepTime(bar * 16 + (ln.step - half));
                    pluck(leadB, at, midiHz(float(note)), float(ln.len) * stepSec, 0.3f, rng);
                }
            }
        }
        bar0 += sec.bars;
    }

    // mix
    Stereo mix(total);
    std::vector<float> send(total, 0.0f);
    for (size_t i = 0; i < total; ++i) {
        mix.add(i, kickB[i] * 0.9f, 0.0f);
        mix.add(i, snareB[i] * 0.7f, 0.05f);
        mix.add(i, hatB[i] * 0.5f, 0.2f);
        mix.add(i, bassB[i] * 0.75f, 0.0f);
        mix.add(i, keysB[i], -0.25f);
        mix.add(i, padB[i], 0.0f);
        mix.add(i, leadB[i] * 0.8f, 0.3f);
        send[i] = keysB[i] * 0.6f + padB[i] * 0.5f + leadB[i] * 0.9f + snareB[i] * 0.25f;
    }
    reverb(send, mix, 0.55f, 0.82f);
    if (vinyl > 0.0f) {
        Biquad lp;
        lp.set(0, 3500.0f, 0.7f);
        for (size_t i = 0; i < total; ++i) {
            float h = lp(rng.white()) * vinyl * 0.3f;
            if (rng.uni() < 6.0f / SR) h += rng.white() * vinyl * 6.0f;
            mix.l[i] += h;
            mix.r[i] += h;
        }
    }
    // master: low pass, soft clip, normalise, fade
    Biquad fl, fr;
    fl.set(0, lowpass, 0.7f);
    fr.set(0, lowpass, 0.7f);
    float peak = 1e-5f;
    for (size_t i = 0; i < total; ++i) {
        mix.l[i] = std::tanh(fl(mix.l[i]) * 0.9f);
        mix.r[i] = std::tanh(fr(mix.r[i]) * 0.9f);
        peak = std::max(peak, std::max(std::fabs(mix.l[i]), std::fabs(mix.r[i])));
    }
    auto s = std::make_shared<SoundAsset>();
    s->path = name;
    s->channels = 2;
    s->sampleRate = SR;
    s->frames.resize(total * 2);
    float g = 0.85f / peak;
    size_t fadeIn = size_t(0.02f * SR), fadeOut = size_t(2.0f * SR);
    for (size_t i = 0; i < total; ++i) {
        float f = 1.0f;
        if (i < fadeIn) f = float(i) / float(fadeIn);
        if (i + fadeOut > total) f *= float(total - i) / float(fadeOut);
        s->frames[i * 2] = mix.l[i] * g * f;
        s->frames[i * 2 + 1] = mix.r[i] * g * f;
    }
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    LOG_INFO("music: rendered '%s' (%.0f s) in %.0f ms", name.c_str(), s->duration(), ms);
    return s;
}

MusicPlayer& music() { return g_music; }

void MusicPlayer::init() {
    tracks_.clear();
    auto j = loadJsonFile(fs::resolve("assets/music/playlist.json"));
    if (j && j->contains("tracks"))
        for (auto& t : (*j)["tracks"]) {
            TrackInfo ti;
            ti.title = jget<std::string>(t, "title", "untitled");
            ti.artist = jget<std::string>(t, "artist", "");
            ti.source = jget<std::string>(t, "source", "");
            ti.menu = jget<bool>(t, "menu", false);
            if (!ti.source.empty()) tracks_.push_back(ti);
        }
    // any audio file dropped into assets/music joins the gameplay playlist
    for (const std::string& abs : fs::listFiles(fs::resolve("assets/music"))) {
        std::string ext = fs::extension(abs);
        if (ext != ".wav" && ext != ".mp3" && ext != ".flac") continue;
        std::string rel = "assets/music/" + fs::fileName(abs);
        bool known = false;
        for (auto& t : tracks_) known |= t.source == rel;
        if (!known) tracks_.push_back({fs::stem(abs), "", rel, false});
    }
    cache_.assign(tracks_.size(), nullptr);
    LOG_INFO("music: %zu tracks", tracks_.size());
}

void MusicPlayer::shutdown() {
    if (pending_.valid()) pending_.wait();
    audio().stop(voice_);
    audio().stop(fading_);
    voice_ = fading_ = AudioSystem::kNoVoice;
    cache_.clear();
}

void MusicPlayer::setEnabled(bool on) {
    enabled_ = on;
    if (!on) {
        audio().stop(voice_);
        voice_ = AudioSystem::kNoVoice;
        current_ = -1;
    }
}

void MusicPlayer::setContext(bool menu) {
    if (menu == menu_ && current_ >= 0) return;
    menu_ = menu;
    if (current_ >= 0 && tracks_[size_t(current_)].menu != menu) next();
}

int MusicPlayer::pickNext() const {
    std::vector<int> cand;
    for (size_t i = 0; i < tracks_.size(); ++i)
        if (tracks_[i].menu == menu_ && int(i) != current_) cand.push_back(int(i));
    if (cand.empty()) {
        for (size_t i = 0; i < tracks_.size(); ++i)
            if (tracks_[i].menu == menu_) cand.push_back(int(i));
    }
    if (cand.empty()) return -1;
    if (!shuffle_) {
        for (int c : cand)
            if (c > current_) return c;
        return cand.front();
    }
    // prefer tracks not heard recently
    int best = cand[size_t(uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) % cand.size())];
    for (int c : cand)
        if (std::find(history_.begin(), history_.end(), c) == history_.end()) {
            best = c;
            if ((std::chrono::steady_clock::now().time_since_epoch().count() & 1) == 0) break;
        }
    return best;
}

void MusicPlayer::startLoad(int index) {
    if (index < 0 || loading_ >= 0) return;
    loading_ = index;
    if (cache_[size_t(index)]) return;
    std::string src = tracks_[size_t(index)].source;
    std::string title = tracks_[size_t(index)].title;
    pending_ = std::async(std::launch::async, [src, title]() -> SoundPtr {
        if (src.size() > 5 && src.substr(src.size() - 5) == ".json") {
            auto j = loadJsonFile(fs::resolve(src));
            if (!j) return nullptr;
            return renderSong(*j, title);
        }
        return loadSoundFile(src);
    });
}

void MusicPlayer::startTrack(const SoundPtr& s, int index) {
    if (voice_ != AudioSystem::kNoVoice) {
        audio().stop(fading_);
        fading_ = voice_;
        fadeOut_ = 1.0f;
    }
    current_ = index;
    history_.push_back(index);
    if (history_.size() > 3) history_.erase(history_.begin());
    PlayParams p;
    p.volume = 0.0f;
    voice_ = audio().play(s, Bus::Music, p);
    fadeIn_ = 0.0f;
    age_ = 0.0f;
    length_ = s->duration();
    // keep only the playing song in memory (rendered songs are large)
    for (size_t i = 0; i < cache_.size(); ++i)
        if (int(i) != index) cache_[i].reset();
    cache_[size_t(index)] = s;
    LOG_INFO("music: now playing '%s'", tracks_[size_t(index)].title.c_str());
}

void MusicPlayer::next() {
    if (!enabled_) return;
    int n = pickNext();
    if (n < 0) return;
    if (pending_.valid() && loading_ >= 0) return;  // one load at a time; it will start when ready
    loading_ = -1;
    startLoad(n);
}

void MusicPlayer::update(float dt) {
    if (!audio().initialized()) return;
    duck_ = dampf(duck_, duckTarget_, 3.0f, dt);
    age_ += dt;
    if (enabled_ && !tracks_.empty()) {
        // finished loading?
        if (loading_ >= 0) {
            SoundPtr s = cache_[size_t(loading_)];
            if (!s && pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                s = pending_.get();
                if (!s) LOG_WARN("music: cannot load '%s'", tracks_[size_t(loading_)].source.c_str());
            }
            if (s || !pending_.valid()) {
                int idx = loading_;
                loading_ = -1;
                if (s) startTrack(s, idx);
            }
        } else if (current_ < 0 || (voice_ != AudioSystem::kNoVoice && !audio().playing(voice_)) || (length_ > 0 && age_ > length_ - 1.5f)) {
            next();
        } else if (current_ >= 0 && tracks_[size_t(current_)].menu != menu_) {
            next();
        }
    }
    fadeIn_ = std::min(1.0f, fadeIn_ + dt / 1.5f);
    audio().setVolume(voice_, fadeIn_ * duck_);
    if (fading_ != AudioSystem::kNoVoice) {
        fadeOut_ -= dt / 1.5f;
        if (fadeOut_ <= 0.0f) {
            audio().stop(fading_);
            fading_ = AudioSystem::kNoVoice;
        } else {
            audio().setVolume(fading_, fadeOut_ * duck_);
        }
    }
}

}  // namespace sw
