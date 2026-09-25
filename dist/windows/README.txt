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
