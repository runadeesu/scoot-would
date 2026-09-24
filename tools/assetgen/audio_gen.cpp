// scoot would asset tools - procedural sound effects (16 bit mono WAV, 44.1 kHz)
//
// Every sound is synthesised from noise, filters, resonators and envelopes, so the game ships with
// original audio. Loops crossfade their tail into the head so they repeat without clicks. Any file
// can be replaced by a recording with the same name (see assets/audio/README.md).
#include "assetgen.h"

#include "core/math.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace sw::tools {
namespace {

constexpr int SR = 44100;
using Buf = std::vector<float>;

struct Noise {
    uint64_t s;
    explicit Noise(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    float white() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return float(int64_t(s >> 11) & 0xFFFFF) / float(0x7FFFF) - 1.0f;
    }
    float uni() { return white() * 0.5f + 0.5f; }
};

// RBJ biquad
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad make(int type, float freq, float q, float gainDb = 0.0f) {
        Biquad f;
        float w = kTwoPi * clampf(freq, 10.0f, SR * 0.45f) / SR;
        float cs = std::cos(w), sn = std::sin(w), alpha = sn / (2.0f * q);
        float a0 = 1;
        float A = std::pow(10.0f, gainDb / 40.0f);
        switch (type) {
            case 0:  // low pass
                f.b0 = (1 - cs) / 2; f.b1 = 1 - cs; f.b2 = (1 - cs) / 2; a0 = 1 + alpha; f.a1 = -2 * cs; f.a2 = 1 - alpha;
                break;
            case 1:  // high pass
                f.b0 = (1 + cs) / 2; f.b1 = -(1 + cs); f.b2 = (1 + cs) / 2; a0 = 1 + alpha; f.a1 = -2 * cs; f.a2 = 1 - alpha;
                break;
            case 2:  // band pass (0 dB peak)
                f.b0 = alpha; f.b1 = 0; f.b2 = -alpha; a0 = 1 + alpha; f.a1 = -2 * cs; f.a2 = 1 - alpha;
                break;
            default:  // peaking
                f.b0 = 1 + alpha * A; f.b1 = -2 * cs; f.b2 = 1 - alpha * A; a0 = 1 + alpha / A; f.a1 = -2 * cs; f.a2 = 1 - alpha / A;
                break;
        }
        f.b0 /= a0; f.b1 /= a0; f.b2 /= a0; f.a1 /= a0; f.a2 /= a0;
        return f;
    }
    float operator()(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
Biquad LP(float f, float q = 0.707f) { return Biquad::make(0, f, q); }
Biquad HP(float f, float q = 0.707f) { return Biquad::make(1, f, q); }
Biquad BP(float f, float q) { return Biquad::make(2, f, q); }

struct OnePole {
    float a, z = 0;
    explicit OnePole(float cutoff) : a(1.0f - std::exp(-kTwoPi * cutoff / SR)) {}
    float operator()(float x) { return z += a * (x - z); }
};

// damped sine bank (modal synthesis for metal / wood hits)
struct Mode {
    float freq, decay, amp;
};
void addStrike(Buf& b, float t0, const std::vector<Mode>& modes, float gain) {
    size_t s0 = size_t(std::max(0.0f, t0) * SR);
    for (const Mode& m : modes) {
        size_t len = size_t(std::min(float(b.size() - std::min(b.size(), s0)), m.decay * 7.0f * SR));
        for (size_t i = 0; i < len; ++i) {
            float t = float(i) / SR;
            b[s0 + i] += gain * m.amp * std::exp(-t / m.decay) * std::sin(kTwoPi * m.freq * t);
        }
    }
}
// short filtered noise burst
void addBurst(Buf& b, float t0, float dur, float gain, float hp, float lp, uint64_t seed) {
    Noise n(seed);
    Biquad h = HP(hp), l = LP(lp);
    size_t s0 = size_t(t0 * SR), len = size_t(dur * SR);
    for (size_t i = 0; i < len && s0 + i < b.size(); ++i) {
        float t = float(i) / float(len);
        float env = std::exp(-t * 6.0f) * std::min(1.0f, float(i) / (0.0015f * SR));
        b[s0 + i] += gain * env * l(h(n.white()));
    }
}
// low thump: sine sweep with exponential decay
void addThump(Buf& b, float t0, float f0, float f1, float decay, float gain) {
    size_t s0 = size_t(t0 * SR);
    float ph = 0;
    for (size_t i = 0; s0 + i < b.size() && i < size_t(decay * 8 * SR); ++i) {
        float t = float(i) / SR;
        float f = f1 + (f0 - f1) * std::exp(-t / (decay * 0.35f));
        ph += kTwoPi * f / SR;
        b[s0 + i] += gain * std::exp(-t / decay) * std::sin(ph) * std::min(1.0f, float(i) / (0.002f * SR));
    }
}
// sparse grain ticks (pebbles, surface texture)
void addGrains(Buf& b, float rate, float gain, float hp, float lp, uint64_t seed) {
    Noise n(seed);
    Buf g(b.size(), 0.0f);
    for (size_t i = 0; i < b.size(); ++i)
        if (n.uni() < rate / SR) {
            float a = gain * (0.3f + 0.7f * n.uni());
            size_t len = size_t((0.001f + 0.003f * n.uni()) * SR);
            for (size_t k = 0; k < len && i + k < b.size(); ++k) g[i + k] += a * std::exp(-float(k) / float(len) * 4.0f) * n.white();
        }
    Biquad h = HP(hp), l = LP(lp);
    for (size_t i = 0; i < b.size(); ++i) b[i] += l(h(g[i]));
}

Buf make(float seconds) { return Buf(size_t(seconds * SR), 0.0f); }

// crossfade the extra tail into the head: result length = total - fade
Buf loopify(const Buf& in, float fadeSeconds) {
    size_t fade = size_t(fadeSeconds * SR);
    size_t n = in.size() - fade;
    Buf out(in.begin(), in.begin() + long(n));
    for (size_t i = 0; i < fade; ++i) {
        float t = float(i) / float(fade);
        float a = std::sqrt(1.0f - t), c = std::sqrt(t);
        out[i] = in[n + i] * a + in[i] * c;
    }
    return out;
}

void normalize(Buf& b, float peak) {
    float m = 1e-6f;
    for (float v : b) m = std::max(m, std::fabs(v));
    for (float& v : b) v *= peak / m;
}
void fadeEdges(Buf& b, float in, float out) {
    size_t ni = size_t(in * SR), no = size_t(out * SR);
    for (size_t i = 0; i < ni && i < b.size(); ++i) b[i] *= float(i) / float(ni);
    for (size_t i = 0; i < no && i < b.size(); ++i) b[b.size() - 1 - i] *= float(i) / float(no);
}

bool writeWav(const std::string& path, const Buf& b) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t bytes = uint32_t(b.size() * 2);
    f.write("RIFF", 4);
    w32(36 + bytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    w32(16);
    w16(1);
    w16(1);
    w32(SR);
    w32(SR * 2);
    w16(2);
    w16(16);
    f.write("data", 4);
    w32(bytes);
    for (float v : b) {
        int s = int(std::lround(clampf(v, -1.0f, 1.0f) * 32767.0f));
        w16(uint16_t(int16_t(s)));
    }
    return bool(f);
}

// ---------------------------------------------------------------------------------------------
// rolling loops: speed is applied at runtime by pitch + volume

Buf rolling(uint64_t seed, float rumbleHz, float rumbleGain, float bandHz, float bandQ, float bandGain, float grainRate, float grainGain,
            float grainLp, float amRate, float amDepth) {
    Buf b = make(2.25f);
    Noise n(seed);
    Biquad rl = LP(rumbleHz), rl2 = LP(rumbleHz), bp = BP(bandHz, bandQ);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = float(i) / SR;
        float am = 1.0f - amDepth * (0.5f + 0.5f * std::sin(kTwoPi * amRate * t + std::sin(kTwoPi * 0.37f * t) * 2.0f));
        b[i] = (rl2(rl(n.white())) * rumbleGain * 3.0f + bp(n.white()) * bandGain) * am;
    }
    addGrains(b, grainRate, grainGain, 400.0f, grainLp, seed + 7);
    return loopify(b, 0.25f);
}

Buf grindMetal() {
    Buf b = make(2.3f);
    Noise n(11);
    // bright screech: narrow resonances with slow wobble, driven by scrape noise
    const float fr[] = {1830.0f, 2620.0f, 3910.0f, 5240.0f, 7020.0f};
    const float ga[] = {1.0f, 0.8f, 0.55f, 0.35f, 0.2f};
    Biquad hp = HP(900.0f);
    for (int k = 0; k < 5; ++k) {
        float ph = 0;
        Noise nn(uint64_t(20 + k));
        OnePole jit(6.0f);
        for (size_t i = 0; i < b.size(); ++i) {
            float t = float(i) / SR;
            float wob = 1.0f + 0.012f * std::sin(kTwoPi * (0.9f + 0.4f * float(k)) * t) + 0.01f * jit(nn.white() * 3.0f);
            ph += kTwoPi * fr[k] * wob / SR;
            float am = 0.6f + 0.4f * std::fabs(jit.z * 4.0f);
            b[i] += ga[k] * 0.25f * std::sin(ph) * am;
        }
    }
    Biquad bp = BP(3200.0f, 0.8f);
    OnePole rough(40.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        float am = 0.5f + std::fabs(rough(n.white() * 4.0f));
        b[i] += hp(bp(n.white())) * 0.9f * am;
    }
    addGrains(b, 300.0f, 0.5f, 1500.0f, 9000.0f, 31);
    return loopify(b, 0.3f);
}

Buf grindConcrete() {
    Buf b = make(2.3f);
    Noise n(41);
    Biquad bp = BP(1400.0f, 0.5f), lp = LP(160.0f);
    OnePole rough(35.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        float am = 0.35f + std::fabs(rough(n.white() * 5.0f));
        b[i] = bp(n.white()) * am + lp(n.white()) * 2.5f;
    }
    addGrains(b, 600.0f, 0.6f, 800.0f, 6000.0f, 43);
    return loopify(b, 0.3f);
}

Buf grindWood() {
    Buf b = make(2.3f);
    Noise n(51);
    Biquad r1 = BP(310.0f, 6.0f), r2 = BP(720.0f, 5.0f), lp = LP(1600.0f);
    OnePole rough(25.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        float x = n.white();
        float am = 0.4f + std::fabs(rough(n.white() * 4.0f));
        b[i] = (lp(x) * 0.6f + r1(x) * 1.6f + r2(x) * 1.2f) * am;
    }
    addGrains(b, 180.0f, 0.4f, 300.0f, 4000.0f, 53);
    return loopify(b, 0.3f);
}

const std::vector<Mode> kDeckModes = {{1180.0f, 0.05f, 1.0f}, {2350.0f, 0.035f, 0.6f}, {3710.0f, 0.02f, 0.4f}, {620.0f, 0.07f, 0.5f}};
const std::vector<Mode> kBarModes = {{880.0f, 0.12f, 0.8f}, {2410.0f, 0.08f, 0.6f}, {4720.0f, 0.05f, 0.4f}, {6630.0f, 0.03f, 0.25f}};

Buf landing(float strength, uint64_t seed) {
    Buf b = make(0.25f + strength * 0.5f);
    addThump(b, 0.0f, 150.0f, 48.0f, 0.06f + strength * 0.1f, 0.9f);
    addBurst(b, 0.0f, 0.03f + strength * 0.03f, 0.5f + strength * 0.3f, 250.0f, 5000.0f, seed);
    addStrike(b, 0.002f, kDeckModes, 0.25f + strength * 0.3f);
    if (strength > 0.5f) {
        addStrike(b, 0.03f, kBarModes, 0.2f * strength);
        addBurst(b, 0.05f, 0.08f, 0.2f * strength, 1500.0f, 9000.0f, seed + 3);
    }
    addGrains(b, 800.0f * strength, 0.4f, 500.0f, 6000.0f, seed + 9);
    fadeEdges(b, 0.0005f, 0.05f);
    return b;
}

Buf whoosh(float dur, float f0, float f1, float f2, uint64_t seed) {
    Buf b = make(dur);
    Noise n(seed);
    for (float& v : b) v = n.white();
    // time varying band pass: recompute coefficients every 32 samples, keep the filter state
    Buf out(b.size(), 0.0f);
    Biquad bp = BP(f0, 1.4f);
    for (size_t i = 0; i < b.size(); ++i) {
        if (i % 32 == 0) {
            float t = float(i) / float(b.size());
            float f = t < 0.5f ? f0 + (f1 - f0) * (t * 2.0f) : f1 + (f2 - f1) * ((t - 0.5f) * 2.0f);
            Biquad nb = BP(f, 1.4f);
            nb.z1 = bp.z1;
            nb.z2 = bp.z2;
            bp = nb;
        }
        float t = float(i) / float(b.size());
        float env = std::sin(kPi * std::pow(t, 0.8f));
        out[i] = bp(b[i]) * env * env;
    }
    return out;
}

Buf tone(const std::vector<std::pair<float, float>>& notes, float noteDur, float decay, float bright, float gap = 0.0f) {
    Buf b = make(float(notes.size()) * (noteDur + gap) + decay * 4.0f);
    for (size_t k = 0; k < notes.size(); ++k) {
        float t0 = float(k) * (noteDur + gap);
        size_t s0 = size_t(t0 * SR);
        float f = notes[k].first, a = notes[k].second;
        for (size_t i = 0; s0 + i < b.size(); ++i) {
            float t = float(i) / SR;
            float env = std::exp(-t / decay) * std::min(1.0f, t / 0.003f);
            float v = std::sin(kTwoPi * f * t) + bright * 0.5f * std::sin(kTwoPi * f * 2.0f * t) * std::exp(-t / (decay * 0.5f)) +
                      bright * 0.25f * std::sin(kTwoPi * f * 3.01f * t) * std::exp(-t / (decay * 0.3f));
            b[s0 + i] += a * env * v;
        }
    }
    fadeEdges(b, 0.0005f, 0.02f);
    return b;
}

struct Entry {
    const char* name;
    std::function<Buf()> gen;
    float peak;
};

}  // namespace

bool generateAudio(const std::string& dir) {
    std::vector<Entry> list = {
        // rolling surfaces (loops)
        {"roll_concrete", [] { return rolling(1, 140.0f, 0.5f, 900.0f, 0.7f, 0.35f, 90.0f, 0.5f, 5000.0f, 3.1f, 0.15f); }, 0.7f},
        {"roll_asphalt", [] { return rolling(2, 170.0f, 0.7f, 620.0f, 0.6f, 0.45f, 420.0f, 0.55f, 4200.0f, 5.3f, 0.2f); }, 0.7f},
        {"roll_wood",
         [] {
             Buf b = rolling(3, 200.0f, 0.5f, 420.0f, 3.0f, 0.8f, 40.0f, 0.3f, 3000.0f, 2.0f, 0.1f);
             for (int k = 0; k < 4; ++k) addStrike(b, 0.1f + float(k) * 0.5f, {{240.0f, 0.05f, 1.0f}, {530.0f, 0.03f, 0.5f}}, 0.35f);
             return b;
         },
         0.7f},
        {"roll_metal",
         [] {
             Buf b = rolling(4, 120.0f, 0.4f, 1250.0f, 4.0f, 0.5f, 60.0f, 0.3f, 8000.0f, 4.0f, 0.2f);
             Noise n(5);
             Biquad r1 = BP(2900.0f, 12.0f), r2 = BP(4400.0f, 14.0f);
             for (float& v : b) {
                 float x = n.white();
                 v += r1(x) * 0.6f + r2(x) * 0.4f;
             }
             return b;
         },
         0.6f},
        {"roll_grass",
         [] {
             Buf b = rolling(6, 300.0f, 0.4f, 2500.0f, 0.5f, 0.12f, 700.0f, 0.25f, 7000.0f, 7.0f, 0.5f);
             return b;
         },
         0.55f},
        {"roll_dirt", [] { return rolling(7, 220.0f, 0.6f, 1500.0f, 0.5f, 0.25f, 900.0f, 0.6f, 6000.0f, 6.0f, 0.3f); }, 0.65f},
        // grinds (loops)
        {"grind_metal", grindMetal, 0.7f},
        {"grind_concrete", grindConcrete, 0.7f},
        {"grind_wood", grindWood, 0.7f},
        // impacts
        {"land_soft", [] { return landing(0.2f, 61); }, 0.6f},
        {"land_normal", [] { return landing(0.55f, 62); }, 0.8f},
        {"land_hard", [] { return landing(1.0f, 63); }, 0.95f},
        {"pop",
         [] {
             Buf b = make(0.3f);
             addBurst(b, 0.0f, 0.02f, 0.9f, 900.0f, 9000.0f, 71);
             addThump(b, 0.0f, 120.0f, 70.0f, 0.04f, 0.8f);
             addStrike(b, 0.001f, kDeckModes, 0.35f);
             fadeEdges(b, 0.0003f, 0.05f);
             return b;
         },
         0.8f},
        {"push",
         [] {
             Buf b = make(0.32f);
             Noise n(81);
             Biquad bp = BP(1400.0f, 0.6f), lp = LP(4000.0f);
             OnePole grit(80.0f);
             for (size_t i = 0; i < b.size(); ++i) {
                 float t = float(i) / SR;
                 float env = std::min(1.0f, t / 0.03f) * std::exp(-std::max(0.0f, t - 0.03f) / 0.07f);
                 b[i] = lp(bp(n.white())) * env * (0.5f + std::fabs(grit(n.white() * 5.0f)));
             }
             addThump(b, 0.0f, 90.0f, 60.0f, 0.03f, 0.3f);
             return b;
         },
         0.55f},
        {"bail",
         [] {
             Buf b = make(1.4f);
             addThump(b, 0.0f, 110.0f, 40.0f, 0.18f, 1.0f);
             addBurst(b, 0.0f, 0.25f, 0.8f, 60.0f, 700.0f, 91);
             addBurst(b, 0.0f, 0.06f, 0.4f, 900.0f, 6000.0f, 92);
             const float ts[] = {0.09f, 0.21f, 0.37f, 0.52f, 0.7f};
             for (int k = 0; k < 5; ++k) {
                 float a = 0.6f / float(k + 1);
                 addStrike(b, ts[k], kBarModes, a);
                 addStrike(b, ts[k] + 0.006f, kDeckModes, a * 0.8f);
                 addBurst(b, ts[k], 0.05f, a * 0.8f, 1200.0f, 8000.0f, uint64_t(93 + k));
             }
             fadeEdges(b, 0.0003f, 0.2f);
             return b;
         },
         0.95f},
        {"scooter_hit",
         [] {
             Buf b = make(0.6f);
             addStrike(b, 0.0f, kBarModes, 0.8f);
             addStrike(b, 0.004f, kDeckModes, 0.6f);
             addBurst(b, 0.0f, 0.03f, 0.5f, 1500.0f, 9000.0f, 101);
             fadeEdges(b, 0.0003f, 0.1f);
             return b;
         },
         0.8f},
        {"catch",
         [] {
             Buf b = make(0.25f);
             addStrike(b, 0.0f, kDeckModes, 0.7f);
             addBurst(b, 0.0f, 0.015f, 0.5f, 2000.0f, 10000.0f, 111);
             fadeEdges(b, 0.0003f, 0.05f);
             return b;
         },
         0.55f},
        {"whip", [] { return whoosh(0.34f, 350.0f, 1700.0f, 500.0f, 121); }, 0.6f},
        {"barspin",
         [] {
             Buf b = whoosh(0.26f, 600.0f, 2600.0f, 900.0f, 131);
             Buf c = make(0.34f);
             for (size_t i = 0; i < b.size(); ++i) c[i] = b[i];
             addStrike(c, 0.25f, kBarModes, 0.12f);
             return c;
         },
         0.55f},
        {"wind",
         [] {
             Buf b = make(4.5f);
             Noise n(141);
             Biquad lp = LP(700.0f), lp2 = LP(1800.0f);
             OnePole slow(0.6f), slow2(1.3f);
             for (size_t i = 0; i < b.size(); ++i) {
                 float g = 0.6f + std::fabs(slow(n.white() * 30.0f));
                 float h = 0.3f + std::fabs(slow2(n.white() * 25.0f));
                 float x = n.white();
                 b[i] = lp(x) * g * 1.5f + (lp2(x) - lp(x)) * h * 0.5f;
             }
             return loopify(b, 0.5f);
         },
         0.6f},
        {"ambience_day",
         [] {
             Buf b = make(9.0f);
             Noise n(151);
             Biquad lp = LP(160.0f), lp2 = LP(120.0f), hp = HP(1800.0f), lp3 = LP(5000.0f);
             OnePole swell(0.15f);
             for (size_t i = 0; i < b.size(); ++i) {
                 float s = 0.7f + std::fabs(swell(n.white() * 40.0f));
                 b[i] = lp2(lp(n.white())) * 4.0f * s + lp3(hp(n.white())) * 0.03f;
             }
             // birds: short FM chirps
             Noise r(152);
             for (int k = 0; k < 14; ++k) {
                 float t0 = 0.3f + r.uni() * 8.0f;
                 float base = 2600.0f + r.uni() * 1800.0f;
                 int notes = 2 + int(r.uni() * 4.0f);
                 for (int m = 0; m < notes; ++m) {
                     size_t s0 = size_t((t0 + float(m) * 0.11f) * SR);
                     float ph = 0;
                     for (size_t i = 0; i < size_t(0.07f * SR) && s0 + i < b.size(); ++i) {
                         float t = float(i) / (0.07f * SR);
                         float f = base * (1.0f + 0.35f * std::sin(t * kPi)) * (m % 2 ? 1.12f : 1.0f);
                         ph += kTwoPi * f / SR;
                         b[s0 + i] += 0.05f * std::sin(t * kPi) * std::sin(ph);
                     }
                 }
             }
             return loopify(b, 0.8f);
         },
         0.35f},
        {"ambience_night",
         [] {
             Buf b = make(9.0f);
             Noise n(161);
             Biquad lp = LP(110.0f), lp2 = LP(90.0f);
             for (size_t i = 0; i < b.size(); ++i) b[i] = lp2(lp(n.white())) * 3.0f;
             // crickets: pulsed high tones in chirp groups
             for (int c = 0; c < 3; ++c) {
                 float f = 4300.0f + float(c) * 420.0f;
                 float period = 0.9f + float(c) * 0.23f;
                 for (float t0 = float(c) * 0.3f; t0 < 8.8f; t0 += period) {
                     for (int p = 0; p < 3; ++p) {
                         size_t s0 = size_t((t0 + float(p) * 0.045f) * SR);
                         for (size_t i = 0; i < size_t(0.03f * SR) && s0 + i < b.size(); ++i) {
                             float t = float(i) / (0.03f * SR);
                             b[s0 + i] += 0.03f * std::sin(t * kPi) * std::sin(kTwoPi * f * float(i) / SR);
                         }
                     }
                 }
             }
             return loopify(b, 0.8f);
         },
         0.3f},
        // interface and scoring
        {"ui_move", [] { return tone({{1600.0f, 0.6f}}, 0.02f, 0.012f, 0.2f); }, 0.35f},
        {"ui_select", [] { return tone({{880.0f, 0.6f}, {1320.0f, 0.7f}}, 0.05f, 0.05f, 0.4f); }, 0.45f},
        {"ui_back", [] { return tone({{1320.0f, 0.6f}, {880.0f, 0.6f}}, 0.05f, 0.05f, 0.4f); }, 0.4f},
        {"trick_land", [] { return tone({{1046.5f, 0.5f}, {1318.5f, 0.5f}, {1568.0f, 0.6f}}, 0.045f, 0.12f, 0.8f); }, 0.4f},
        {"combo_bank", [] { return tone({{784.0f, 0.5f}, {1046.5f, 0.5f}, {1318.5f, 0.6f}, {2093.0f, 0.7f}}, 0.06f, 0.25f, 0.9f); }, 0.5f},
        {"combo_fail", [] { return tone({{220.0f, 0.7f}, {164.8f, 0.8f}}, 0.12f, 0.12f, 1.0f); }, 0.45f},
        {"checkpoint", [] { return tone({{1760.0f, 0.5f}, {2349.3f, 0.5f}}, 0.04f, 0.06f, 0.3f); }, 0.35f},
        {"respawn", [] { return whoosh(0.4f, 300.0f, 1200.0f, 2400.0f, 171); }, 0.45f},
        {"challenge_complete", [] { return tone({{523.25f, 0.5f}, {659.25f, 0.5f}, {783.99f, 0.5f}, {1046.5f, 0.8f}}, 0.11f, 0.35f, 0.9f); }, 0.55f},
    };
    size_t ok = 0;
    for (auto& e : list) {
        Buf b = e.gen();
        normalize(b, e.peak);
        std::string path = dir + "/" + e.name + ".wav";
        if (writeWav(path, b)) ++ok;
        else std::printf("audio: failed to write %s\n", path.c_str());
    }
    std::printf("audio: %zu/%zu sounds -> %s\n", ok, list.size(), dir.c_str());
    return ok == list.size();
}

}  // namespace sw::tools
