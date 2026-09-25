#!/usr/bin/env python3
"""Mix the trailer soundtrack: the rendered song plus the gameplay sounds logged while recording.

    python3 tools/trailer_audio.py music.wav video.sounds.json out.wav

music.wav   the song rendered with `scoot would --render-song assets/music/trailer_theme.json music.wav`
            (16-bit PCM, mono or stereo, 44.1 kHz)
sounds.json written by `scoot would --trailer ... --record video.mp4` next to the video:
            {"length": s, "fps": n, "sounds": [{"t", "sound", "volume"}], "beds": {"roll_concrete": [volume per frame]}}
            one shots (pops, landings, trick starts) play at their time; beds (rolling wheels, wind, grinds) loop
            with a volume per video frame
out.wav     16-bit stereo, as long as the video; the song fades out over the last half second

Sound effects come from assets/audio (the game's own generated sounds). Pure Python (wave + array), no packages.
"""
import array
import json
import math
import os
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUDIO = os.path.join(ROOT, "assets", "audio")
RATE = 44100
MUSIC_GAIN = 0.78
SFX_GAIN = 0.9
BED_GAIN = 0.8


def read_wav(path):
    """-> (samples as floats in [-1, 1], channels)"""
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit(f"{path}: only 16-bit PCM is supported")
        if w.getframerate() != RATE:
            raise SystemExit(f"{path}: expected {RATE} Hz, got {w.getframerate()}")
        ch = w.getnchannels()
        data = array.array("h")
        data.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        data.byteswap()
    return array.array("f", (x / 32768.0 for x in data)), ch


_cache = {}


def sfx(name):
    """mono float samples of assets/audio/<name>.wav"""
    if name not in _cache:
        path = os.path.join(AUDIO, name + ".wav")
        if not os.path.exists(path):
            print(f"trailer_audio: no sound '{name}', skipped")
            _cache[name] = None
        else:
            s, ch = read_wav(path)
            if ch == 2:
                s = array.array("f", ((s[i] + s[i + 1]) * 0.5 for i in range(0, len(s), 2)))
            _cache[name] = s
    return _cache[name]


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    music_path, sounds_path, out_path = sys.argv[1:]
    with open(sounds_path, encoding="utf-8") as f:
        log = json.load(f)
    length = float(log["length"])
    fps = int(log.get("fps", 30))
    n = int(round(length * RATE))
    mix = array.array("f", bytes(8 * n))  # interleaved stereo, zeros

    # song, trimmed to the video with a short fade at the cut
    music, mch = read_wav(music_path)
    frames = min(n, len(music) // mch)
    fade = int(0.5 * RATE)
    for i in range(frames):
        g = MUSIC_GAIN * (min(1.0, (n - i) / fade) if n - i < fade else 1.0)
        if mch == 2:
            mix[2 * i] = music[2 * i] * g
            mix[2 * i + 1] = music[2 * i + 1] * g
        else:
            mix[2 * i] = mix[2 * i + 1] = music[i] * g

    # one shots
    shots = 0
    for e in log.get("sounds", []):
        s = sfx(e["sound"])
        if s is None:
            continue
        start = int(round(float(e["t"]) * RATE))
        g = SFX_GAIN * float(e.get("volume", 1.0))
        end = min(len(s), n - start)
        for i in range(max(0, -start), end):
            x = s[i] * g
            mix[2 * (start + i)] += x
            mix[2 * (start + i) + 1] += x
        shots += 1

    # beds: the loop plays under a per-frame volume, smoothed so frame steps do not click
    spf = RATE / fps
    for name, env in log.get("beds", {}).items():
        s = sfx(name)
        if s is None or not env:
            continue
        k = 1.0 - math.exp(-1.0 / (0.03 * RATE))  # 30 ms smoothing
        g = 0.0
        pos = 0
        last = min(n, int(len(env) * spf + 0.1 * RATE))
        for i in range(last):
            f = int(i / spf)
            target = BED_GAIN * (env[f] if f < len(env) else 0.0)
            g += (target - g) * k
            if g > 1e-4:
                x = s[pos] * g
                mix[2 * i] += x
                mix[2 * i + 1] += x
            pos += 1
            if pos >= len(s):
                pos = 0

    # soft limiter and out
    out = array.array("h", bytes(2 * len(mix)))
    peak = 0.0
    for i, x in enumerate(mix):
        a = abs(x)
        peak = max(peak, a)
        if a > 0.8:
            x = math.copysign(0.8 + 0.2 * math.tanh((a - 0.8) / 0.2), x)
        out[i] = int(max(-32767, min(32767, x * 32767.0)))
    if sys.byteorder == "big":
        out.byteswap()
    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(out.tobytes())
    print(f"trailer_audio: {length:.1f} s, {shots} one shots, {len(log.get('beds', {}))} beds, peak {peak:.2f} -> {out_path}")


if __name__ == "__main__":
    main()
