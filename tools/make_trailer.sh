#!/usr/bin/env bash
# Records the game trailer: renders the soundtrack, plays the scripted shots in the game while recording every frame,
# mixes the gameplay sounds under the song and muxes the final video.
#
# usage: tools/make_trailer.sh [path/to/scoot would executable] [output.mp4]
#   SCOOT_FFMPEG  ffmpeg to use (default: ffmpeg on PATH; needs libx264 and aac)
#   TRAILER       trailer script (default: assets/trailer/trailer.json, regenerate with tools/trailergen.py)
#
# The game runs with a fixed frame time (one video frame per frame), so the result does not depend on how fast the
# machine renders; a 60 s trailer at 1080p takes a few minutes on a desktop GPU. On Linux without a desktop run it
# under Xvfb (DISPLAY=:99).
set -euo pipefail
cd "$(dirname "$0")/.."
EXE="${1:-./build/linux/src/scoot would}"
OUT="${2:-out/trailer/scoot_would_trailer.mp4}"
TRAILER="${TRAILER:-assets/trailer/trailer.json}"
FF="${SCOOT_FFMPEG:-ffmpeg}"
export SCOOT_FFMPEG="$FF"
WORK="out/trailer/work"
mkdir -p "$WORK" "$(dirname "$OUT")"

song=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['music'])" "$TRAILER")
echo "== soundtrack ($song)"
"$EXE" --render-song "$song" "$WORK/music.wav"

echo "== recording ($TRAILER)"
"$EXE" --trailer "$TRAILER" --record "$WORK/video.mp4" > "$WORK/record.log" 2>&1 || { tail -20 "$WORK/record.log"; exit 1; }
grep -E "trailer:|BAIL" "$WORK/record.log" || true

echo "== mix"
python3 tools/trailer_audio.py "$WORK/music.wav" "$WORK/video.sounds.json" "$WORK/audio.wav"

echo "== mux"
"$FF" -y -loglevel error -i "$WORK/video.mp4" -i "$WORK/audio.wav" -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k -shortest \
    -movflags +faststart "$OUT"
echo "trailer: $OUT"
