# Development

## Command line

```
scoot would [--map scene.json] [--spawn label] [--challenge id] [--play] [--editor]
            [--menu main|play|map|rider|scooter[:category]|settings[:tab]|challenges]
            [--windowed|--fullscreen] [--size WxH] [--novsync] [--gpu direct3d12|vulkan] [--gpu-debug]
            [--lang en|ja] [--stance regular|goofy] [--pad xbox|ps|switch] [--no-rider] [--aa off|fxaa|taa]
            [--view third|close|far|first]
            [--env preset] [--autotest test.json] [--screenshot file.png frame] [--camera x,y,z,tx,ty,tz]
            [--debugview n] [--trailer script.json [--record out.mp4]] [--render-song song.json out.wav]
```

`--screenshot` saves the given frame and quits; `--camera` fixes the view; `--pad` shows controller glyphs
without a controller; `--aa` overrides the anti-aliasing setting (image comparisons); `--view` picks the camera; `--debugview` 1 albedo, 2 normals, 3 roughness, 4 occlusion, 5 shadow, 6 specular IBL,
7 reflection, 8 irradiance, 9 diffuse ambient only, 10 SSAO, 11 material AO.

In game: F1 developer overlay (performance, physics, tuning), F2 level editor, F3 physics debug draw,
F5 shader reload, F11 screenshot.

## Autotests

`tests/autotest/*.json` script the player input (push, jump, flicks, stick positions, modifiers, conditions on
the player state) and check results (tricks landed, grinds, manuals, no bail, speed, air). Run all of them:

```
DISPLAY=:99 ./tools/run_autotests.sh
```

A test states the control layout it was written for with `"scheme": "flow"` (default classic).

## Trailer

`tools/make_trailer.sh` makes the game trailer (about 60 s, 1920x1080, 30 fps) from inside the game:

```
DISPLAY=:99 SCOOT_FFMPEG=/path/to/ffmpeg ./tools/make_trailer.sh     # -> out/trailer/scoot_would_trailer.mp4
```

- `python3 tools/trailergen.py` writes the script, `assets/trailer/trailer.json`. Each shot is an autotest (map,
  spawn, speed, timed / conditional inputs) plus a pre-roll (simulated, not recorded), a length, a camera (`game`,
  `track` in the rider's travel frame, `fixed`, `dolly`, `orbit`), slow-motion ranges, title cards (JA + EN,
  fades, cards can run across a cut), a scooter setup, an environment preset and fades to black. Shot lengths
  are whole bars of the song (120 bpm, 2 s a bar), so cuts land on the beat.
- `--trailer script.json --record out.mp4` plays the shots with a fixed frame time (one video frame per frame,
  vsync off, the window at the script's size) and pipes every frame (scene + titles + letterbox) to ffmpeg
  (`libx264`). The gameplay sounds are logged next to the video (`out.sounds.json`): one-shots (push, pop,
  landings, trick starts, catches, bail) and looped beds per frame (rolling on the ground surface, wind, grinds),
  hushed in slow motion.
- `--render-song song.json out.wav` renders the soundtrack (`assets/music/trailer_theme.json`) with the game's own
  synthesiser; `tools/trailer_audio.py` mixes the song with the logged sounds (`assets/audio`), and ffmpeg muxes
  it all.

To check one shot quickly, copy the script with only that shot and a small `width` / `height` and record it.
The current cut is committed as `dist/trailer/scoot_would_trailer.mp4` (re-encoded at CRF 22 for size).

## Adding content

- **Trick**: add an entry to `assets/data/tricks.json` (`input.dirs` for the classic layout, `input.flow_dirs`
  for the Scooter Flow layout, rotation, air time, score) and its Japanese name to `assets/data/lang/ja.json`.
- **Challenge**: `assets/data/challenges.json` (mode, map, spawn, time, medal targets, gates / trick list).
- **Level piece**: a prefab function in `src/game/world/prefabs*.cpp`, registered in `registerBuiltinPrefabs`,
  then place it in a scene JSON or with the editor.
- **Material**: a JSON file in `assets/materials/` (textures, factors, shading model, surface type).
- **UI text**: write English in the code, add the translation to `assets/data/lang/ja.json`. Strings built at
  run time go through `T()` / `i18n::trPhrase()`.
- **Input action**: `Action` in `src/input/input.h`, its name in `kActionNames`, defaults in both
  `config/input*.json`.

## Conventions

- C++20, 4 spaces, 140 columns, `snake_case` files, `CamelCase` types, `camelCase` functions and members with a
  trailing underscore for private ones.
- Units: metres, seconds, radians (degrees only in data files), +Y up, -Z forward.
- Assets must be CC0 / OFL / generated here; record every source in `tools/fetch_assets.py` so
  `assets/SOURCES.md` stays complete. Brands and logos in the game are fictional.
