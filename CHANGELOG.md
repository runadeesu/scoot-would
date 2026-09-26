# Changelog

## Unreleased

### Rider motion
- The rider moves looser and smoother:
  - Animation keys are joined by cubic curves and state crossfades ease in and out, so there are no sudden changes
    of speed at a key.
  - Blend weights (crouch, tricks, lean, head) follow critically damped springs instead of snapping at the start.
  - The hips ride on the legs like a mass on a soft spring. Landings and the bottom of transitions sink them, the
    legs push back with a little bounce, and speeding up or braking sways them.
  - Small slow drifts of the head, chest and hips mean the rider is never frozen.
  - In plain airs the torso stays upright while the scooter pitches and rolls under the feet (it used to be bolted
    to the deck angle).
  - Elbows sit softer and more to the side.
- Bunny hop, from reference photos:
  1. Out of the compression the legs straighten and the arms lift the bars.
  2. The rider leans forward over the bars with the knees up to the chest as the tail follows.
  3. The legs reach down and absorb the landing.
- Flair, from reference photos (the steps riders are taught):
  1. Pop out: stretched and arched back off the lip, head back.
  2. Look back: the knees start coming in.
  3. Curl in and dip the shoulder on the turning side.
  4. Spot the landing over that shoulder.
  5. Reach for the wall.
- Both are authored for the regular stance and mirrored for goofy. The flair's shoulder dip and head turn follow the
  direction of its half turn in the world.

### Trailer
- Trailer mode: `--trailer script.json --record out.mp4` plays scripted shots (autotest inputs, cinematic
  track / fixed / dolly / orbit cameras, slow motion, JA + EN title cards, letterbox, fades) and records every
  frame through ffmpeg at a fixed frame time. `tools/make_trailer.sh` renders the soundtrack (the game's own
  synthesiser, `assets/music/trailer_theme.json`), records `assets/trailer/trailer.json` (15 shots, 60 s, from
  `tools/trailergen.py`), mixes the gameplay sounds logged while recording under the song
  (`tools/trailer_audio.py`) and muxes the video.

### Fixes
- Mega Park: the mega ramp could not be cleared - an 11 m tower gave about 10 m/s off the kicker against a 14 m
  gap to a landing 2.5 m higher. The tower is now 16 m (26 m roll in), the gap 11 m and the landing deck 4 m: a
  plain drop in carries 14 to 18 m. The Mega Park spawn moved up onto the new tower deck. New autotest `mega_ramp`.
- Landings nose first down a transition (a pool air) could slew the scooter up to 60 degrees sideways: on the
  front wheel alone the side grip at the lone contact twisted the scooter, and the yaw kept building. The grip at
  a lone contact now keeps the line without twisting the scooter, and on the ground the yaw rate follows what the
  wheels allow (steer angle and speed). New autotest `pool_air_turn`.
- A bounce off the vert below the coping was treated as an air out of the ramp and pushed the rider away from the
  wall; it is a plain air back down the same wall now.

### Controls
- Spins and flips in the air are on the left stick (Scooter Flow layout too); the right stick is only for the pop,
  compressing and the RT / LT tricks. A stick held back or forward into the air (manual) flips only after it came
  back to the middle.
- Flair: out of a quarter or bowl wall, pull the left stick back and to a side on the way up - backflip with a 180,
  back down into the ramp going forwards.

### Ramps
- Airs out of a quarter or bowl wall go up past the coping and come back down onto the top of the transition, just
  inside the coping (a straight air lands fakie; an air turn lands forwards and is squared up to the wall before
  the wheels touch). Air turns turn round the wall (the rider's own length), not round the world vertical.
- No more automatic 50-50 on the coping during airs: a coping is only ground into when the scooter comes down along
  it, deck level.
- Popping on a steep wall pops up the wall instead of back into the ramp.
- Fast hits into a quarter no longer throw the rider: the scooter follows the curve of the transition (it used to
  lag behind and lean the rider into the wall ahead). Over a spine the nose follows the arc over the top.

### Riding (carving)
- The scooter and rider no longer lean over nearly flat in hard carves: scooter riders turn with the bars, the deck
  leans into a carve about half as much as a bike would, at most ~17 degrees.

- New default layout modelled on Scooter Flow's twin stick scheme: left stick = body weight (manuals),
  right stick down = compress / pump, flick up = pop, right stick in the air = spins and flips, RT + right
  stick = scooter tricks (right tailwhip, left heelwhip, RT right then RT left = whip rewind), LT + right stick
  = grabs. The previous layout is kept as "Classic".
- Every connected controller works (hot plug); Xbox / PlayStation / Nintendo button glyphs everywhere; trigger
  rebinding; move list in SETTINGS > CONTROLS; control hints during the first ride.
- Pumping: staying compressed through transitions adds speed.

### Tricks
- Tricks are rebuilt from how riders actually do them (how-to guides, trick dictionaries, photos of real
  tailwhips): each one is a timeline - pop, kick / throw, tuck, the scooter going round (fast after the kick,
  slowed by the catch), the front foot catching first, landing. Hands and feet let go and catch again at their
  moments. Tailwhip: the back foot kicks the tail out, the rider hunches over the bars pulled in to the hips,
  the deck goes round just under the feet. Barspin: front hand pushes, back hand pulls, the feet stay on.
  Bri flip / inward: bars turned 90 degrees, the scooter rolls round the grips over the head beside the body.
- The scooter hangs from the rider's hands: it can no longer float away from them. Legs that are off the deck
  are kept out of the deck, stem and wheels (they go over the deck and around the stem).
- The trick set of Scooter Flow on four input layers (RT, LT, RB + RT, RB + LT); holding the stick keeps a
  whip or barspin going; tricks link into each other; names the way riders say them (540 Flair, 360 Whip,
  Flair Quad Whip Bar, Buttercup, Truck Driver). SETTINGS > TRICKS lists every trick.
- The head leads spins and goes back into backflips; the body tucks into flips. Goofy riders whip the other way.
- Air and trick poses redone from photos of real riders (CC photos of airs, tailwhips, barspins, an inward bri
  and a scooter flip): the torso stays upright and the scooter is pulled up to the chest in every air (elbows out,
  knees bent under it, eyes down on the deck) and let back down just before the landing; whips turn the deck
  just under the tucked feet; hands that let go of a spinning bar wait just over the grips; bri flips split the
  legs in a stride; the scooter flip lets go with one hand and turns the scooter beside the body round the grip
  still held; the fingerwhip hand reaches down to the deck; tuck no hander clamps the stem between the knees;
  toboggan holds the back wheel with the nose pointed down.

### Scooter parts (from the reference photos)
- Truss Boxed 5.0" deck: skeletal A-frame neck (upper / lower struts and braces into the integrated headtube),
  boxed deck with the hollow section showing at the tail.
- Griptape cut to a point towards the neck, leaving the polished nose corners bare; hex mark near the front.
- Y-Bar 25": the downtube splits into two arms that sweep into the crossbar.
- Plated finishes for deck, bars, clamp and wheel cores: Chrome, Neo Chrome (oil-slick bands that shift with
  the viewing angle) and Blue Chrome.

### Parks and ramps (built to real dimensions)
- New map: Maple Grove Skatepark, a community concrete park poured in the ground like the real ones (the deck
  is the ground, no guardrails round the bowls): a 4.5 ft flow bowl with 6.5 ft transitions, corner pockets and
  a spine, an 8.5 ft deep end with about a foot of vert, concrete bullnose pool coping and a tile line, and a
  street section (platform with 27 degree banks, five stair with 6.5 in risers / 14 in treads, a 34 in handrail
  and hubbas, 16 and 18 in ledges, an 8 in manual pad, a 13 in flat bar, a 2 ft funbox with a bar, a 4 ft
  quarter with a 6 ft transition). Lawn, trees, park lights, rules sign, parking lot. Four spawn points and
  four challenges.
- New `pool` prefab: rounded rectangle bowls whose walls are one transition swept round the coping line, so
  the straight walls and the corner pockets are one surface (round pools too); steel or pool coping, tile.
- Copings are 2 3/8 in steel set 1/8 in proud of the deck; decks 38 in or higher get a 42 in guardrail with
  vertical bars that stops 24 in short of open sides (ASTM F2334).
- Air out of a steep transition (quarters, bowl walls) brings the rider back down the wall: riders steer the
  scooter back over the ramp with their body. Hold forward to air out onto the deck instead.

### Camera
- First person view (Y / Tab cycles: third person, close, far, first person; also SETTINGS > GAMEPLAY >
  Camera, remembered): a wide POV lens at the rider's eyes, forearms and hands on the grips at the bottom of
  the frame, the view turns with every spin and flip in the air and stays level through carves. The head and
  upper body are hidden from the lens only; the shadow keeps the whole rider.

### Riding
- New push: the pushing foot reaches off the tail, plants beside the deck and stays planted on the ground while
  the scooter rolls on (the leg extends far behind, heel lifting at the end), then swings back to the tail;
  hips drop and turn, the front knee bends deep and the chest leans over the bars. The push force is applied
  while the foot is on the ground.

### Language
- Japanese and English. Follows the system language, selectable in SETTINGS > GAMEPLAY. Japanese text uses
  M PLUS 1p (OFL).

### Scooter shop
- The scooter customisation happens in a furnished scooter shop: display counter, back lit sign, brand fascia,
  shelves, slatwall decks, hanging bars, wheels, pendant lamps and scanned props; lit by a photographed shop
  HDRI.
- Frosted glass panel with part categories, thumbnails rendered from the real part meshes in every colour,
  specs and brand marks; live preview, select to equip, save; orbit camera.

### Rendering
- Temporal anti-aliasing (default): jittered rendering, motion vectors for the camera, moving objects and the
  skinned rider, variance clipped history. Thin things (rails, cables, bars, foliage, the scooter) no longer
  crawl or shimmer. SETTINGS > VIDEO > Anti-Aliasing: Off / FXAA / TAA.
- Screen space sun contact shadows (shoes on the deck, wheels on the ground, cars on the street) on top of the
  shadow cascades; ambient occlusion noise now changes every frame and is accumulated by TAA.
- Tubes (bars, rails, hooks) were built inside out and rendered dark: fixed.
- Environment cache, interior HDRIs, frosted UI backdrop blur.

### Rider and scooter (earlier in this cycle)
- Realistic rider on the CC0 MakeHuman base mesh: skin / hair / fabric textures, layered clothing, face
  animation, finger grips, regular / goofy stance.
- Detailed pro scooter model (boxed deck, integrated headtube, threadless fork, double clamp facing the rider,
  chromoly T-bar, flanged grips, spoked cores).
- Street Spot map: one dense city corner built around one line; trees and shrubs; photoreal street materials.

## 1.0.0

- First release: custom runtime (SDL3 GPU, Jolt, miniaudio), scooter physics, tricks, grinds, manuals,
  combos, bails, Scoot City, challenge modes, customisation, settings, save system, Windows x64 build.
