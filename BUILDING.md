# Building scoot would

## Requirements

- CMake 3.24+, Ninja, a C++20 compiler (GCC 12+, Clang 16+ or MSVC 2022).
- Windows release from Linux: MinGW-w64 (`x86_64-w64-mingw32-g++-posix`), see
  `cmake/toolchains/mingw-w64-x86_64.cmake`.
- Python 3 (asset download / level generation scripts).
- Runtime: a GPU with Direct3D 12 (Windows) or Vulkan 1.1+.

Dependencies are fetched by CMake (`cmake/Dependencies.cmake`, every one pinned to a tag or commit): SDL3,
Jolt Physics, glslang, SPIRV-Cross, SDL_shadercross, Dear ImGui, ImGuizmo, miniaudio, cgltf, stb,
nlohmann/json, meshoptimizer. For offline builds clone them once and pass `-DSCOOT_DEPS_DIR=<folder>` (sub
folders named exactly like the FetchContent names).

## Linux (development build, tests, tools)

```
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build/linux
"./build/linux/src/scoot would"
```

Headless runs (screenshots, autotests) work under Xvfb with a software Vulkan driver (lavapipe):

```
Xvfb :99 -screen 0 1920x1080x24 &
DISPLAY=:99 ./tools/run_autotests.sh
DISPLAY=:99 "./build/linux/src/scoot would" --menu scooter --screenshot shop.png 40
```

Trailer video (needs ffmpeg with libx264 and aac, see DEVELOPMENT.md):

```
DISPLAY=:99 SCOOT_FFMPEG=/path/to/ffmpeg ./tools/make_trailer.sh
```

## Windows x64 release

From Linux with MinGW-w64:

```
tools/package_windows.sh                       # build/win64 -> dist/windows/scoot would.exe + licenses + README
tools/package_windows.sh --standalone out/sw   # plus a self contained folder (exe, assets, shaders, config)
```

The exe is linked fully static (no MinGW DLLs). It finds its data in its own folder or up to four parent
folders, so `dist/windows/scoot would.exe` runs straight from the repository.

Natively on Windows (Visual Studio 2022 or MinGW):

```
cmake -S . -B build/win64 -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build/win64
```

## Assets

Everything the game loads is in `assets/` and committed; rebuilding it is only needed after changing a
generator.

| tool | output |
|---|---|
| `python3 tools/fetch_assets.py` | CC0 Poly Haven textures, HDRIs and props, OFL fonts, `assets/SOURCES.md` |
| `build/linux/tools/scoot_assetgen --rider` | rider glTF (MakeHuman base mesh, clothing, baked skin / hair / fabric textures, clips) |
| `build/linux/tools/scoot_assetgen --textures` | generated textures (signs, decals, griptape, foliage, shop graphics) |
| `build/linux/tools/scoot_assetgen --audio` | synthesized sound effects (music tracks are note data in `assets/music/*.json`, synthesized at run time) |
| `python3 tools/citygen.py` | `assets/scenes/city.json` (Scoot City) |
| `python3 tools/spotgen.py` | `assets/scenes/street_spot.json` |
| `python3 tools/parkgen.py` | `assets/scenes/maple_grove.json` (Maple Grove Skatepark, dimensions in the script header) |

Shaders (`shaders/*.vert|frag`, GLSL 450) are compiled at run time to SPIR-V / DXIL through SDL_shadercross
and cached in the user folder.
