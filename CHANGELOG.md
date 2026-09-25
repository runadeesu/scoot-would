# Changelog

## Unreleased

### Controls
- New default layout modelled on Scooter Flow's twin stick scheme: left stick = body weight (manuals),
  right stick down = compress / pump, flick up = pop, right stick in the air = spins and flips, RT + right
  stick = scooter tricks (right tailwhip, left heelwhip, RT right then RT left = whip rewind), LT + right stick
  = grabs. The previous layout is kept as "Classic".
- Every connected controller works (hot plug); Xbox / PlayStation / Nintendo button glyphs everywhere; trigger
  rebinding; move list in SETTINGS > CONTROLS; control hints during the first ride.
- Pumping: staying compressed through transitions adds speed.

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
