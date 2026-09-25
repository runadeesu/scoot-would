# scoot would

A realistic freestyle scooter game for Windows 10/11 x64, built on its own C++20 runtime (no commercial
engine): SDL3 GPU (Direct3D 12 / Vulkan) renderer, Jolt Physics, a physically based scooter and a skinned,
IK driven rider.

**[日本語はこちら](#日本語)**

## Play

- Windows: run `dist/windows/scoot would.exe` from inside the repository (it loads `assets/`, `shaders/` and
  `config/` from the repository root). `tools/package_windows.sh --standalone <folder>` writes a self
  contained copy for sharing.
- A controller is recommended (Xbox, PlayStation, Switch Pro and every other pad SDL supports, several at
  once, hot plug). Keyboard and mouse work too.
- Language: English or Japanese, following the system language by default (SETTINGS > GAMEPLAY > Language).

## What is in the game

- **Riding**: pushing, carving, pumping, pop timing on lips, manuals and nose manuals, grinds (50-50,
  nosegrind, 5-0, feeble, smith, crooked, boardslide), whips, barspins, flips, grabs, spins, flairs, combos
  with landing quality, bails into a ragdoll.
- **Controls**: the default layout follows Scooter Flow's twin stick scheme (left stick = body weight, right
  stick = pop, rotations and, with RT / LT, tricks and grabs); a classic button layout is available. Every
  action can be rebound. See [CONTROLS.md](CONTROLS.md).
- **Places**: Scoot City (plaza, downtown, skatepark, mega park, school, industrial, rooftops, ditch,
  residential, contest park), the Street Spot, one dense city corner built around one line, and Maple Grove,
  a concrete skatepark built to real dimensions (flow bowl, 8.5 ft pool with pool coping, street section).
- **Modes**: free ride, trick challenges, lines, best trick, time attack, with bronze / silver / gold.
- **Rider**: realistic human built on the CC0 MakeHuman base mesh, clothing that layers correctly, regular /
  goofy stance, face animation (blinks, saccades, breathing), finger grips on the bars.
- **Scooter shop**: customise deck, bars, clamp, wheels, urethane and grips in a furnished shop; thumbnails
  are rendered from the real part meshes.
- **Rendering**: PBR with image based lighting from photographed HDRIs, cascaded shadows, SSAO, bloom,
  ACES tone mapping, FXAA, frosted glass UI, interior lighting, procedural facades and interiors.

## Build

See [BUILDING.md](BUILDING.md). Short version (Linux host, Windows exe via MinGW-w64):

```
cmake -S . -B build/linux -G Ninja && ninja -C build/linux        # Linux build (development, tests)
tools/package_windows.sh                                           # Windows x64 release -> dist/windows
```

## Documentation

| file | content |
|---|---|
| [CONTROLS.md](CONTROLS.md) | every control, both layouts, keyboard, rebinding |
| [BUILDING.md](BUILDING.md) | toolchains, dependencies, Windows release, asset generation |
| [ENGINE.md](ENGINE.md) | runtime architecture: renderer, physics, animation, UI, localisation |
| [DEVELOPMENT.md](DEVELOPMENT.md) | code layout, data files, autotests, debugging, adding content |
| [CHANGELOG.md](CHANGELOG.md) | release notes |
| [assets/SOURCES.md](assets/SOURCES.md) | every third party asset and its license |

## Licenses

Code libraries are zlib / MIT / BSD / Apache-2.0 / public domain (see `dist/windows/licenses/`). Textures,
HDRIs and scanned props come from Poly Haven (CC0). The rider base mesh is MakeHuman's (CC0). Fonts: Barlow
and M PLUS 1p (SIL OFL 1.1). Brand names in the game (FLOWLAB, KDX PRO, AXLE CO.) are fictional.

---

## 日本語

Windows 10/11 x64 向けのリアル系フリースタイルスクーターゲームです。市販エンジンを使わず、独自の C++20
ランタイム（SDL3 GPU による Direct3D 12 / Vulkan 描画、Jolt Physics、物理ベースのスクーター、スキニング + IK の
ライダー）で作られています。

### 遊び方

- Windows: リポジトリの中にある `dist/windows/scoot would.exe` を起動します（リポジトリ直下の `assets/`、
  `shaders/`、`config/` を読み込みます）。単体で配布する場合は `tools/package_windows.sh --standalone <フォルダ>`。
- コントローラー推奨（Xbox、PlayStation、Switch Pro など SDL 対応パッドすべて。複数同時接続・抜き差し対応）。
  キーボードとマウスでも遊べます。
- 言語: 日本語 / 英語。既定では OS の言語に合わせます（設定 > ゲームプレイ > 言語）。

### 主な内容

- **ライディング**: プッシュ、カーブ、パンプ、リップでのポップタイミング、マニュアル / ノーズマニュアル、
  グラインド 7 種、ウィップ、バースピン、フリップ、グラブ、スピン、フレア、着地判定付きコンボ、ラグドール転倒。
- **操作**: 既定は Scooter Flow 式のツインスティック（左スティック＝体重移動、右スティック＝ポップ・回転、
  RT / LT と組み合わせてトリック・グラブ）。クラシック（ボタン式）も選べ、すべて割り当て変更できます。
  詳しくは [CONTROLS.md](CONTROLS.md)。
- **マップ**: スクートシティ（プラザ、ダウンタウン、スケートパーク、メガパーク、スクール、工業地帯、屋上、
  用水路、住宅街、コンテストパーク）と、1 本のラインのために作られた街角「ストリートスポット」。
- **モード**: フリーライド、トリック / ライン / ベストトリック / タイムアタックの各チャレンジ（ブロンズ・
  シルバー・ゴールド）。
- **ライダー**: CC0 の MakeHuman ベースメッシュによるリアルな人物、重ね着、レギュラー / グーフィー、
  まばたき・視線・呼吸、バーを握る指。
- **スクーターショップ**: 店内でデッキ、バー、クランプ、ウィール、ウレタン、グリップをカスタマイズ。
  サムネイルは実際のパーツのメッシュから描画しています。

### ビルド

[BUILDING.md](BUILDING.md) を参照してください（Linux 上でのビルドと、MinGW-w64 による Windows 版の作成）。
