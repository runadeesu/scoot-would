#!/usr/bin/env python3
"""Generate the trailer script (assets/trailer/trailer.json) for `scoot would --trailer`.

Each shot is an autotest (map, spawn, yaw, velocity, timed / conditional inputs, classic layout: jump = pop,
bare right stick flick in the air = scooter trick, left stick = spin / flip) plus a camera, slow motion, titles and
fades. Shot lengths line up with the soundtrack (assets/music/trailer_theme.json: 120 bpm, 2 s per bar).

usage: python3 tools/trailergen.py
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = "assets/trailer/trailer.json"
MG = "assets/scenes/maple_grove.json"
SS = "assets/scenes/street_spot.json"
CITY = "assets/scenes/city.json"
MG_PLAT = 5 * 6.5 * 0.0254   # Maple Grove platform height (five 6.5 in risers)
SS_TOP = 0.15 + 6 * 0.17     # Street Spot raised plaza


def yaw_to(dx, dz):
    """autotest yaw so the rider faces (dx, dz): yaw 0 faces -Z, the rider turns left with positive yaw"""
    import math
    return round(math.degrees(math.atan2(-dx, -dz)), 2)


def shot(name, map_, spawn, heading, speed, length, steps=(), camera=None, preroll=0.2, **kw):
    d = {"name": name, "map": map_, "spawn": list(spawn), "yaw": yaw_to(*heading), "preroll": preroll, "length": length,
         "velocity": [heading[0] * speed, 0.0, heading[1] * speed], "steps": list(steps), "camera": camera or {"type": "game"}}
    d.update(kw)
    return d


def pushes(t0, n, every=0.55):
    return [{"t": round(t0 + i * every, 3), "push": True} for i in range(n)]


def text(at, dur, title, sub="", place="lower", big=False, fade_in=None, fade_out=None):
    t = {"at": at, "dur": dur, "title": title, "sub": sub, "place": place}
    if big:
        t["big"] = True
    if fade_in is not None:
        t["fadeIn"] = fade_in
    if fade_out is not None:
        t["fadeOut"] = fade_out
    return t


NEO = [-2, -2, -2]    # plated finishes (see PartFinish): neo chrome
BLUE = [-3, -3, -3]   # blue chrome
CHROME = [-1, -1, -1]

shots = [
    # 0-6 s: the park from above, the name
    shot("title", MG, (-9, 0, 3), (1, 0), 5.0, 6.0, pushes(0.4, 4, 1.2), preroll=0.3, fadeIn=1.2,
         camera={"type": "dolly", "from": [-46, 17, 30], "to": [-32, 8.5, 21], "lookFrom": [2, 0, -2], "lookTo": [0, 0.5, -1], "fov": 48},
         texts=[text(1.3, 4.3, "scoot would", "REAL FREESTYLE SCOOTER", "center", big=True)]),
    # 6-10: pushing, close
    shot("push", MG, (-6, 0, 1.5), (1, 0), 3.2, 4.0, pushes(0.25, 7), preroll=0.3,
         camera={"type": "track", "offset": [-2.9, 0.85, 1.3], "look": [0, 0.8, 0.3], "fov": 42, "smooth": 6},
         texts=[text(0.5, 3.1, "リアルな乗り心地", "REAL RIDING PHYSICS")]),
    # 10-14: five stair tailwhip
    shot("stairs", MG, (17, MG_PLAT, -22.2), (0, 1), 5.6, 4.0,
         [{"t": 0.05, "jump": "press"}, {"when": {"zAbove": -15.25}, "jump": "release"},
          {"when": {"state": "Air", "airAbove": 0.04}, "flick": "right"}],
         {"type": "fixed", "pos": [21.8, 0.55, -9.2], "look": [0, 0.7, 0], "fov": 46, "smooth": 9},
         preroll=0.2, slowmo=[{"from": 1.25, "to": 1.85, "scale": 0.35}]),
    # 14-18: flat rail on the raised plaza (Street Spot)
    shot("rail", SS, (-35.5, SS_TOP, 0.35), (1, 0), 5.8, 4.0,
         [{"t": 0.05, "jump": "press"}, {"when": {"xAbove": -31.0}, "jump": "release"}],
         {"type": "fixed", "pos": [-25.0, SS_TOP + 0.6, 3.6], "look": [0, 0.9, 0], "fov": 44, "smooth": 7},
         preroll=0.2, texts=[text(0.4, 3.0, "グラインド、マニュアル、ステア", "GRINDS, MANUALS, STAIRS", "upper")]),
    # 18-22: the deep end
    shot("pool", MG, (-15.5, -2.6, 12.3), (-1, 0), 9.0, 4.0,
         [{"when": {"state": "Air", "yAbove": -0.5}, "move": [1, 0]}, {"when": {"state": "Air", "spinAbove": 140}, "move": [0, 0]}],
         {"type": "fixed", "pos": [-20.5, 1.0, 17.3], "look": [0, 0.8, 0], "fov": 50, "smooth": 6},
         preroll=0.1, slowmo=[{"from": 1.1, "to": 2.4, "scale": 0.45}], texts=[text(0.3, 3.3, "実寸で作ったスケートパーク", "PARKS BUILT TO REAL DIMENSIONS")]),
    # 22-26: big kicker backflip (Mega Park)
    shot("kicker", CITY, (284, 0, 34), (1, 0), 10.8, 4.0,
         [{"t": 0.05, "jump": "press"}, {"when": {"xAbove": 298.9}, "jump": "release"},
          {"when": {"state": "Air", "airAbove": 0.05}, "move": [0, -1]}, {"when": {"state": "Air", "flipAbove": 250}, "move": [0, 0]}],
         {"type": "fixed", "pos": [303.5, 1.4, 42.5], "look": [0, 0.8, 0], "fov": 48, "smooth": 7},
         preroll=0.4, slowmo=[{"from": 1.5, "to": 2.3, "scale": 0.4}],
         texts=[text(0.3, 3.0, "40種類以上のトリック", "40+ TRICKS")]),
    # 26-28: flow bowl, 180 air turn
    shot("flow", MG, (-21, -1.37, -9), (0, 1), 8.4, 2.0,
         [{"when": {"state": "Air", "yAbove": -0.3}, "move": [1, 0]}, {"when": {"state": "Air", "spinAbove": 140}, "move": [0, 0]}],
         {"type": "fixed", "pos": [-27.0, 1.0, -0.8], "look": [0, 0.8, 0], "fov": 40, "smooth": 8}, preroll=0.3),
    # 28-32: flair on the quarter, slow motion over the breakdown
    shot("flair", CITY, (379, 0, 34.5), (1, 0), 10.0, 4.0,
         [{"when": {"state": "Air", "airAbove": 0.03}, "move": [0.8, -0.8]}, {"when": {"state": "Air", "airAbove": 0.4}, "move": [0, 0]}],
         {"type": "fixed", "pos": [383.5, 0.9, 41.5], "look": [0, 0.7, 0], "fov": 50, "smooth": 7},
         preroll=0.2, slowmo=[{"from": 1.0, "to": 2.4, "scale": 0.45}],
         texts=[text(1.1, 2.0, "FLAIR", "", "upper")]),
    # 32-36: first person down the five stair
    shot("pov", MG, (17, MG_PLAT, -22.5), (0, 1), 5.4, 4.0,
         [{"t": 0.05, "jump": "press"}, {"when": {"zAbove": -15.25}, "jump": "release"}],
         preroll=0.5, view="first", texts=[text(0.3, 3.2, "一人称視点", "FIRST PERSON")]),
    # 36-40: the setups
    shot("setup1", MG, (4.0, 0, 20.0), (1, 0), 0.0, 2.0, [], preroll=0.6,
         camera={"type": "orbit", "center": [4.0, 0.0, 20.0], "radius": 1.35, "height": 0.32, "start": 48, "speed": 20, "look": [0, 0.28, 0], "fov": 40},
         custom={"deck": 3, "deckColor": NEO, "bars": 3, "barsColor": BLUE, "clampColor": NEO, "coreColor": NEO, "wheels": 2},
         texts=[text(0.2, 1.8, "自分だけのセットアップ", "BUILD YOUR SCOOTER", "upper", fade_out=0)]),
    shot("setup2", MG, (4.0, 0, 20.0), (1, 0), 0.0, 2.0, [], preroll=0.6,
         camera={"type": "orbit", "center": [4.0, 0.0, 20.0], "radius": 3.1, "height": 1.0, "start": 300, "speed": -26, "look": [0, 0.85, 0], "fov": 40},
         custom={"deck": 1, "deckColor": BLUE, "bars": 1, "barsColor": CHROME, "clampColor": [0.9, 0.55, 0.1], "coreColor": CHROME, "wheels": 1},
         texts=[text(0.0, 1.8, "自分だけのセットアップ", "BUILD YOUR SCOOTER", "upper", fade_in=0)]),  # the card carries over the cut
    # 40-44: street spot wall quarter, barspin air
    shot("wall", SS, (20, 0.15, 0.4), (1, 0), 8.6, 4.0,
         [{"when": {"state": "Air", "airAbove": 0.05}, "flick": "up"}],
         {"type": "fixed", "pos": [30.5, 1.1, 6.8], "look": [0, 0.8, 0], "fov": 46, "smooth": 7},
         preroll=0.3, slowmo=[{"from": 1.3, "to": 2.1, "scale": 0.45}]),
    # 44-50: mega ramp
    shot("mega", CITY, (265, 16, 0), (1, 0), 1.5, 6.0,
         [{"t": 0.2, "push": True}, {"when": {"state": "Air", "xAbove": 305.0}, "move": [0, -1]}, {"when": {"state": "Air", "flipAbove": 250}, "move": [0, 0]}],
         {"type": "track", "offset": [7.5, 1.2, -1.5], "look": [0, 0.8, 1.0], "fov": 42, "smooth": 4},
         preroll=3.0, slowmo=[{"from": 2.3, "to": 3.9, "scale": 0.45}],
         texts=[text(0.3, 2.6, "メガランプ", "MEGA RAMP", "upper")]),
    # 50-54: carve through the flow bowl
    shot("carve", MG, (-19, -1.37, -12.8), (-1, 0), 7.0, 4.0, [{"t": 0.1, "move": [-0.5, 0]}],
         {"type": "fixed", "pos": [-21.5, 1.3, -1.8], "look": [0, 0.7, 0], "fov": 44, "smooth": 6}, preroll=0.2),
    # 54-60: end card at sunset
    shot("end", MG, (4.0, 0, 20.0), (1, 0), 0.0, 6.0, [], preroll=0.6, env="sunset", fadeOut=1.6,
         camera={"type": "orbit", "center": [4.0, 0.0, 20.0], "radius": 3.6, "height": 1.3, "start": 120, "speed": 10, "look": [0, 0.8, 0], "fov": 44},
         texts=[text(0.6, 5.2, "scoot would", "PC  |  WINDOWS 10 / 11", "center", big=True)]),
]


def main():
    doc = {"_comment": "generated by tools/trailergen.py", "fps": 30, "width": 1920, "height": 1080, "letterbox": 0.06, "music": "assets/music/trailer_theme.json",
           "shots": shots}
    out = os.path.join(ROOT, OUT)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
    total = sum(s["length"] for s in shots)
    print(f"trailer: {len(shots)} shots, {total:.1f} s -> {out}")


if __name__ == "__main__":
    main()
