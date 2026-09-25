# Development

## Command line

```
scoot would [--map scene.json] [--spawn label] [--challenge id] [--play] [--editor]
            [--menu main|play|map|rider|scooter[:category]|settings[:tab]|challenges]
            [--windowed|--fullscreen] [--size WxH] [--novsync] [--gpu direct3d12|vulkan] [--gpu-debug]
            [--lang en|ja] [--stance regular|goofy] [--pad xbox|ps|switch] [--no-rider] [--aa off|fxaa|taa]
            [--view third|close|far|first]
            [--env preset] [--autotest test.json] [--screenshot file.png frame] [--camera x,y,z,tx,ty,tz]
            [--debugview n]
```

`--screenshot` saves the given frame and quits; `--camera` fixes the view; `--pad` shows controller glyphs
without a controller; `--aa` overrides the anti-aliasing setting (image comparisons); `--view` picks the camera; `--debugview` 1 albedo, 2 normals, 3 roughness, 4 occlusion, 5 shadow, 6 specular IBL,
7 reflection, 8 irradiance.

In game: F1 developer overlay (performance, physics, tuning), F2 level editor, F3 physics debug draw,
F5 shader reload, F11 screenshot.

## Autotests

`tests/autotest/*.json` script the player input (push, jump, flicks, stick positions, modifiers, conditions on
the player state) and check results (tricks landed, grinds, manuals, no bail, speed, air). Run all of them:

```
DISPLAY=:99 ./tools/run_autotests.sh
```

A test states the control layout it was written for with `"scheme": "flow"` (default classic).

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
