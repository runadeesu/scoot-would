# scoot would runtime

A small C++20 engine written for this game. No commercial engine code; third party libraries are listed in
[BUILDING.md](BUILDING.md).

## Frame

`core/engine` runs a fixed 120 Hz simulation step (`fixedUpdate` -> Jolt step -> `postPhysics`) and one
`update` + `render` per displayed frame with an interpolation factor. The game client (`game/game`) implements
those callbacks.

## Modules

| folder | responsibility |
|---|---|
| `core/` | engine loop, file system (data root + per user folder), JSON, logging, jobs, math, localisation (`i18n`) |
| `platform/` | window, display modes |
| `input/` | actions and axes from every connected gamepad, keyboard and mouse; control layouts, rebinding, stick flicks |
| `render/` | GPU abstraction over SDL3 GPU, meshes, materials, textures, render scene, renderer, particles, debug draw |
| `physics/` | Jolt world, static colliders from the scene, ray / shape queries |
| `animation/` | skeletons, clips, pose blending, state machine |
| `audio/` | miniaudio mixer with buses, 3D sources, the music synthesiser |
| `assets/` | asset cache: textures, JSON materials, glTF models (cgltf), fonts |
| `scene/` | entities built from prefabs (JSON levels), static batching |
| `ui/` | immediate mode UI on a 1080p virtual canvas, SDF fonts with fallback faces |
| `game/` | player, scooter physics, tricks, grinds, manuals, combos, modes, camera, menus, HUD, world prefabs, shop |

## Renderer

Forward PBR (glTF metallic / roughness) with reverse Z and a depth prepass:

1. uploads (instances, bones, lights; textures in chunked uploads)
2. shadow cascades (up to 4, texel snapped)
3. depth + normal prepass, SSAO (half resolution, bilateral blur)
4. forward PBR: sun with PCF shadows, up to 64 local lights, image based ambient (SH9 irradiance + GGX
   prefiltered cube from the HDRI), specular occlusion, shading models: standard, skin (wrap + subsurface tint),
   cloth (sheen), interior mapped windows, foliage (translucency)
5. sky, transparent surfaces and decals, particles
6. motion blur (optional), bloom (13 tap down / tent up chain), ACES tone mapping and grading, FXAA +
   sharpening, UI, present

Environments come from photographed HDRIs (Poly Haven, CC0). Recently used environments stay resident so the
street and the scooter shop interior switch without reprocessing. Interior HDRIs skip the synthetic ground
used for pure sky maps. Frosted UI panels sample a 1/8 resolution blur of the final image.

Meshes get automatic LODs (meshoptimizer). Static geometry is batched and culled with a spatial grid;
dynamic objects (player, props, the shop) are culled individually.

## Player

- `scooter/`: scooter on a Jolt rigid body with ray cast wheel contacts: steering, carving lean, pushing,
  braking, rolling resistance, pop with timing bonus, pumping, air control.
- `tricks/`: data driven tricks (`assets/data/tricks.json`): input (flick / sequence / chain / grab per control
  layout), rotation axis and amount, air time needed, landing window, scores; body tricks (spins, flips,
  flairs, corks) are named from the measured rotation.
- `grind/`, `manual/`, `combo/`, `landing`: rails from the scene, 7 grind types, manual balance, combo
  multiplier, landing quality (clean / sketchy / bail), ragdoll bails.
- `player_visual`: scooter parts with separate transforms for tricks, the skinned rider (MakeHuman based glTF)
  driven by `rider_animator`: clips + procedural layers (crouch, lean, push cycle, look), two bone IK for
  hands and feet, finger grips, stance mirroring (goofy), face (blinks, saccades, breathing).

## UI and localisation

`ui::Context` draws with an SDF glyph atlas (4096^2) and anti-aliased shapes; widgets are focus driven for
gamepads and hover driven for the mouse. Every drawn string is looked up in the active language table
(`assets/data/lang/ja.json`, English source text as key); Japanese glyphs come from M PLUS 1p as a fallback
face. `game/ui/glyphs` draws the button / key bound to an action for the device in use.

## Data

Levels (`assets/scenes/*.json`) are lists of prefab instances with parameters; prefabs generate meshes,
colliders, rails, lights and spawns in C++ (`game/world/prefabs*.cpp`). Materials are JSON files in
`assets/materials/`. Challenges, maps, environments, surfaces and tricks are JSON in `assets/data/`.
