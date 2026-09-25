#!/usr/bin/env bash
# scoot would - Windows x64 release (MinGW-w64 cross compile from Linux) and dist/windows layout.
#
#   dist/windows/scoot would.exe   runs in place: the game looks for assets/, shaders/ and config/ next to the
#                                  exe and in its parent folders, i.e. the repository root
#   dist/windows/licenses/         third party licenses (code, fonts, MakeHuman, asset sources)
#   dist/windows/README.txt        how to run / controls in short (English + Japanese)
#
# usage: tools/package_windows.sh [--standalone <out dir>]
#   --standalone  also writes a self contained folder (exe + data) that can be zipped and shared
# SCOOT_DEPS_DIR (optional) points at pre-cloned dependency sources (see cmake/Dependencies.cmake).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
STANDALONE=""
if [[ "${1:-}" == "--standalone" ]]; then STANDALONE="${2:?output folder}"; fi

DEPS_ARG=()
if [[ -n "${SCOOT_DEPS_DIR:-}" ]]; then DEPS_ARG=(-DSCOOT_DEPS_DIR="$SCOOT_DEPS_DIR"); fi
cmake -S . -B build/win64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release \
    -DSCOOT_BUILD_TESTS=OFF "${DEPS_ARG[@]}" >/dev/null
ninja -C build/win64 scoot_would

OUT="dist/windows"
mkdir -p "$OUT/licenses"
cp "build/win64/src/scoot would.exe" "$OUT/"
x86_64-w64-mingw32-strip "$OUT/scoot would.exe" || true

# licenses: dependency sources (FetchContent or SCOOT_DEPS_DIR), fonts, rider base mesh, asset list
dep_dir() {
    local name="$1" lower
    lower="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
    if [[ -n "${SCOOT_DEPS_DIR:-}" && -d "$SCOOT_DEPS_DIR/$name" ]]; then echo "$SCOOT_DEPS_DIR/$name"; return; fi
    if [[ -d "build/win64/_deps/$lower-src" ]]; then echo "build/win64/_deps/$lower-src"; return; fi
    echo ""
}
for spec in "SDL3:LICENSE.txt" "JoltPhysics:LICENSE" "glslang:LICENSE.txt" "SPIRV-Cross:LICENSE" "SDL_shadercross:LICENSE.txt" \
            "imgui:LICENSE.txt" "ImGuizmo:LICENSE" "miniaudio:LICENSE" "cgltf:LICENSE" "stb:LICENSE" "json:LICENSE.MIT" \
            "meshoptimizer:LICENSE.md"; do
    name="${spec%%:*}"; file="${spec#*:}"
    dir="$(dep_dir "$name")"
    if [[ -n "$dir" && -f "$dir/$file" ]]; then cp "$dir/$file" "$OUT/licenses/$name-${file}"; else echo "warning: no license file for $name"; fi
done
cp assets/fonts/OFL.txt "$OUT/licenses/Barlow-OFL.txt"
cp assets/fonts/OFL-MPLUS1p.txt "$OUT/licenses/MPLUS1p-OFL.txt"
cp third_party/makehuman/LICENSE.ASSETS.md "$OUT/licenses/MakeHuman-LICENSE.ASSETS.md"
cp assets/SOURCES.md "$OUT/licenses/ASSET_SOURCES.md"

cat > "$OUT/README.txt" <<'TXT'
scoot would - freestyle scooter
===============================

Run "scoot would.exe". It uses the assets/, shaders/ and config/ folders of the repository (two folders up),
so keep it inside the repository, or use a standalone build made with tools/package_windows.sh --standalone.
Requires Windows 10/11 x64 with a Direct3D 12 or Vulkan GPU. A controller is recommended (Xbox, PlayStation,
Switch Pro and other SDL supported pads); keyboard + mouse work too.

Controls (default "Scooter Flow style" layout, change in SETTINGS > CONTROLS)
  Left stick        carve / lean, back = manual, forward = nose manual
  A                 push            B   brake
  Right stick down  compress (hold into transitions to pump)
  Right stick up    pop (flick)
  Right stick (air) spin / flip, LB / RB spin
  RT + right stick  scooter tricks: right tailwhip, left heelwhip, up barspin, down fingerwhip,
                    right then left = whip rewind (push the stick all the way)
  LT + right stick  grabs
  X revert, Y camera, View respawn, Menu pause
Keyboard: WASD = left stick, arrow keys = right stick, Space pop, Shift push, Ctrl brake, F trick, G grab.

Language: SETTINGS > GAMEPLAY > Language (English / 日本語, follows the system language by default).
Licenses of the code libraries, fonts and assets: licenses/.

----
「scoot would.exe」を起動してください。リポジトリ内の assets / shaders / config フォルダを使うので、
exe はリポジトリの中に置いたまま実行してください（単体配布用は tools/package_windows.sh --standalone で作成）。
操作は既定で「Scooter Flow 式」：左スティック＝体重移動（後ろでマニュアル）、A＝プッシュ、B＝ブレーキ、
右スティック下＝しゃがむ（トランジションでパンプ）、右スティック上に弾く＝ポップ、空中の右スティック＝回転、
RT＋右スティック＝トリック（右テールウィップ、左ヒールウィップ、上バースピン、右→左ウィップリワインド）、
LT＋右スティック＝グラブ。設定 > 操作 で変更・割り当て変更ができます。
言語は 設定 > ゲームプレイ > 言語 で切り替えられます（既定は OS の言語）。
TXT

if [[ -n "$STANDALONE" ]]; then
    mkdir -p "$STANDALONE"
    cp -r "$OUT/." "$STANDALONE/"
    cp -r assets shaders config "$STANDALONE/"
    echo "standalone build: $STANDALONE"
fi
ls -la "$OUT"
