#!/usr/bin/env python3
"""Generate Maple Grove Skatepark (assets/scenes/maple_grove.json), a community concrete park built from
real skatepark dimensions, and its challenges.

Built like a poured in-ground park: the deck is the ground (grade, y = 0) and the transitions are sunk into it,
so there are no guardrails around the bowls. The raised street platform and the free standing quarter follow
ASTM F2334 (above ground facilities).

Dimensions (sources: ASTM F2334 summary by Purdue, builder spec sheets, park plans):
  - coping: 2 3/8 in (60.3 mm) steel pipe set 1/8 in proud, or concrete bullnose pool coping over a tile line
  - flow bowl: 4.5 ft (1.37 m) deep, 6.5 ft (1.98 m) transitions, corner pockets, a spine splitting it
  - deep end pool: 8.5 ft (2.6 m) deep, 7.5 ft (2.29 m) transitions + about 1 ft of vert, pool coping + tile
  - street: 5 stair (6.5 in rise, 14 in run) with a 34 in handrail and hubbas, 16 and 18 in ledges, 8 in
    manual pad, 13 in flat bar, 2 ft funbox with a flat bar, 27 deg flat banks
  - 4 ft quarter with a 6 ft transition, deck guardrail 42 in (ASTM: decks 38 in or higher)

Layout (X east, Z south, Y up). Prefab conventions as in tools/spotgen.py: origin at the footprint centre on
the ground, ramps rise towards local +X, widths along local Z, a yaw of t degrees turns local +X into
(cos t, 0, -sin t).

usage: python3 tools/parkgen.py
"""
import json
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = "assets/scenes/maple_grove.json"
FT = 0.3048
IN = 0.0254

PARK = (-34.0, -24.0, 36.0, 24.0)   # concrete footprint (x0, z0, x1, z1)
LOT = (-30.0, -52.0, 30.0, -32.0)   # parking lot north of the park

# flow bowl: steel coping, spine
FLOW = dict(c=(-17.0, -9.0), length=22.0, width=13.0, cornerRadius=3.4, depth=4.5 * FT, transition=6.5 * FT, deck=1.0)
# deep end: pool coping + tile
POOL = dict(c=(-18.0, 12.0), length=12.0, width=9.0, cornerRadius=3.6, depth=2.6, transition=7.5 * FT, deck=1.2)

RISE, RUN, STEPS = 6.5 * IN, 14 * IN, 5
PLAT_H = STEPS * RISE          # 0.826 m
PLAT = (8.0, -23.0, 26.0, -16.0)

ents = []


def add(prefab, pos, yaw=0.0, params=None, material=None, name=None):
    e = {"id": len(ents) + 1, "name": name or prefab, "prefab": prefab, "params": params or {},
         "position": [round(pos[0], 3), round(pos[1], 3), round(pos[2], 3)]}
    if yaw:
        e["rotation"] = [0, round(yaw, 3), 0]
    if material:
        e["material"] = material
    ents.append(e)
    return e


def facing_yaw(dx, dz):
    """spawn yaw so the rider faces the world direction (dx, dz); yaw 0 faces -Z"""
    return math.degrees(math.atan2(-dx, -dz))


def rise_yaw(dx, dz):
    """yaw that turns a ramp's local +X (the way it rises) towards (dx, dz)"""
    return math.degrees(math.atan2(-dz, dx))


def model(name, pos, yaw=0.0, collider="box", scale=1.0, label=None):
    p = {"model": f"assets/models/props/{name}/{name}.gltf", "include": "", "exclude": "_aged", "scale": scale, "collider": collider,
         "recenter": True}
    return add("model", pos, yaw, p, None, label or name)


def decal(mat, pos, size, yaw=0.0, wall=False, name=None):
    p = {"size": [round(size[0], 3), round(size[1], 3)], "orient": "wall" if wall else "ground"}
    if wall:
        p["offset"] = 0.012
    return add("decal", pos, yaw, p, mat, name or mat)


def x_top(depth, radius):
    """plan length of a transition of this height and radius (vert above the radius adds none)"""
    h = min(depth, radius)
    return math.sqrt(radius * radius - (radius - h) ** 2)


def outer(b):
    """outer deck rectangle of a pool (x0, z0, x1, z1)"""
    cx, cz = b["c"]
    return (cx - b["length"] / 2 - b["deck"], cz - b["width"] / 2 - b["deck"], cx + b["length"] / 2 + b["deck"], cz + b["width"] / 2 + b["deck"])


def fill(bounds, holes, material, name, y=0.0, thickness=0.4):
    """ground slabs covering bounds minus the (axis aligned) holes, merged into strips"""
    x0, z0, x1, z1 = bounds
    xs = sorted({x0, x1} | {h[0] for h in holes} | {h[2] for h in holes})
    zs = sorted({z0, z1} | {h[1] for h in holes} | {h[3] for h in holes})
    xs = [x for x in xs if x0 <= x <= x1]
    zs = [z for z in zs if z0 <= z <= z1]

    def free(xa, za, xb, zb):
        mx, mz = (xa + xb) / 2, (za + zb) / 2
        return not any(h[0] <= mx <= h[2] and h[1] <= mz <= h[3] for h in holes)

    for j in range(len(zs) - 1):
        i = 0
        while i < len(xs) - 1:
            if not free(xs[i], zs[j], xs[i + 1], zs[j + 1]):
                i += 1
                continue
            k = i
            while k + 1 < len(xs) - 1 and free(xs[k + 1], zs[j], xs[k + 2], zs[j + 1]):
                k += 1
            xa, xb, za, zb = xs[i], xs[k + 1], zs[j], zs[j + 1]
            add("ground", ((xa + xb) / 2, y, (za + zb) / 2), 0,
                {"size": [round(xb - xa, 3), thickness, round(zb - za, 3)], "divisions": 4}, material, name)
            i = k + 1


# ------------------------------------------------------------------------------------------------
def ground():
    holes = [outer(FLOW), outer(POOL)]
    fill(PARK, holes, "concrete_park", "park deck")
    fill((-420.0, -420.0, 420.0, 420.0), [PARK, LOT, (-6.0, -32.0, 4.0, -24.0)], "grass", "lawn", y=-0.02)
    fill(LOT, [], "asphalt_worn", "parking lot", y=0.0)
    # footpath from the lot to the park
    add("ground", (-1.0, 0.0, -28.0), 0, {"size": [10.0, 0.4, 8.0], "divisions": 2}, "sidewalk", "entry path")
    # below everything: dirt, so nothing ever looks into the void
    add("ground", (5, -3.4, 0), 0, {"size": [600, 0.4, 600], "divisions": 1}, "dirt", "subgrade")


def transition_section(rng):
    fx, fz = FLOW["c"]
    add("pool", (fx, -FLOW["depth"], fz), 0, {k: FLOW[k] for k in ("length", "width", "cornerRadius", "depth", "transition", "deck")} |
        {"coping": "steel"}, "concrete_park", "flow bowl")
    # spine splitting the flow bowl into a big side and a pocket: its ridge sits at grade, coping to coping
    add("spine", (fx + 3.0, -FLOW["depth"], fz), 0, {"height": FLOW["depth"], "radius": FLOW["transition"], "width": FLOW["width"]},
        "concrete_park", "bowl spine")
    px, pz = POOL["c"]
    add("pool", (px, -POOL["depth"], pz), 0, {k: POOL[k] for k in ("length", "width", "cornerRadius", "depth", "transition", "deck")} |
        {"coping": "pool", "tile": True}, "concrete_park", "deep end")
    # wear on the flat bottoms only (ground decals are flat): cracks and a drain in each bowl, off the
    # line of the walls (ASTM: no drains or saw cuts in landing zones)
    for c, b, dx, span in ((FLOW["c"], FLOW, -4.2, 4.0), (POOL["c"], POOL, 0.0, 2.2)):
        floor_y = -b["depth"]
        fw = b["width"] - 2 * x_top(b["depth"], b["transition"])
        decal("decal_drain", (c[0] + dx - span * 0.6, floor_y, c[1] + fw * 0.3), (0.45, 0.45), 0, name="bowl drain")
        for i in range(3):
            decal(f"decal_crack_{rng.randint(1, 3)}", (c[0] + dx + rng.uniform(-span * 0.5, span * 0.5), floor_y, c[1] + rng.uniform(-fw * 0.2, fw * 0.2)),
                  (rng.uniform(0.9, 1.6), rng.uniform(0.9, 1.6)), rng.uniform(0, 360))
    # deck furniture next to the bowls (outside any line): benches with their backs to the lawn
    for x in (-28.5, -21.0):
        model("painted_wooden_bench", (x, 0.0, -21.8), 180, label="deck bench")
    model("metal_trash_can", (-31.5, 0.0, -21.5), rng.uniform(0, 360))


def street_section(rng):
    x0, z0, x1, z1 = PLAT
    xm = (x0 + x1) / 2
    # raised platform with grindable edges, banks up both ends, stairs + hubbas down the front
    add("slab", (xm, 0.0, (z0 + z1) / 2), 0, {"size": [x1 - x0, PLAT_H, z1 - z0], "grindEdges": True}, "concrete_smooth", "platform")
    bank_len = PLAT_H / math.tan(math.radians(27))
    add("bank", (x0 - bank_len / 2, 0.0, (z0 + z1) / 2), 0, {"length": round(bank_len, 3), "height": round(PLAT_H, 3), "width": z1 - z0,
                                                           "deck": 0.0, "grindEdge": False}, "concrete_park", "west bank")
    add("bank", (x1 + bank_len / 2, 0.0, (z0 + z1) / 2), 180, {"length": round(bank_len, 3), "height": round(PLAT_H, 3), "width": z1 - z0,
                                                             "deck": 0.0, "grindEdge": False}, "concrete_park", "east bank")
    land = 1.2
    slen = land + STEPS * RUN   # the prefab centres on landing + steps * run
    add("stairs", (xm, 0.0, z1 + slen / 2), rise_yaw(0, 1), {"steps": STEPS, "rise": round(RISE, 4), "run": round(RUN, 4), "width": 4.0,
                                                          "landingTop": land, "handrail": True, "railHeight": 34 * IN, "hubba": True},
        "concrete_smooth", "five stair")
    # a ledge along the back of the platform top (manny / ledge line on the deck)
    add("ledge", (xm - 4.0, PLAT_H, z0 + 1.2), 0, {"length": 6.0, "width": 0.5, "height": 16 * IN, "bothEdges": True}, "concrete_smooth", "deck ledge")
    # flat ground: manual pad, ledges, flat bar, funbox with a bar
    add("manual_pad", (6.0, 0.0, -4.0), 0, {"length": 6.0, "width": 2.0, "height": 8 * IN}, "concrete_smooth", "manual pad")
    add("ledge", (19.0, 0.0, -4.0), 0, {"length": 6.0, "width": 0.5, "height": 16 * IN, "bothEdges": True}, "concrete_smooth", "16 in ledge")
    add("ledge", (24.0, 0.0, 9.0), 20, {"length": 5.0, "width": 0.55, "height": 18 * IN, "bothEdges": True}, "concrete_smooth", "18 in ledge")
    add("rail", (11.0, 0.0, 7.0), 0, {"length": 6.0, "height": 13 * IN, "type": "round"}, "rail_steel", "flat bar")
    add("funbox", (4.0, 0.0, 15.0), 0, {"rampLength": round(2 * FT / math.tan(math.radians(25)), 3), "topLength": 3.0, "height": 2 * FT, "width": 4.0,
                                        "rail": True, "railHeight": 12 * IN}, "concrete_park", "funbox")
    add("ledge", (16.0, 0.0, 21.3), 0, {"length": 12.0, "width": 0.6, "height": 18 * IN, "bothEdges": False}, "concrete_smooth", "long ledge")
    # 4 ft quarter with a 6 ft transition at the east end (above ground: 42 in guardrail on the deck)
    h, r, deck = 4 * FT, 6 * FT, 1.5
    add("quarter_pipe", (PARK[2] - (x_top(h, r) + deck) / 2, 0.0, 0.0), 0, {"height": h, "radius": r, "width": 12.0, "deck": deck, "coping": True},
        "concrete_park", "east quarter")
    # wax + wheel marks on what gets skated, graffiti on the platform walls
    decal("decal_grime", (6.0, 8 * IN + 0.001, -4.0), (6.2, 2.1), 0, name="pad wax")
    decal("decal_grime", (19.0, 16 * IN + 0.001, -4.0), (6.2, 0.6), 0, name="ledge wax")
    decal("decal_tire", (xm, 0.0, z1 + slen + 1.8), (4.5, 0.9), 88, name="stair landing marks")
    decal("decal_tire", (x0 - 3.0, 0.0, (z0 + z1) / 2), (5.0, 0.8), 0, name="bank marks")
    decal("decal_graffiti_2", (xm - 5.5, 0.0, z1 + 0.001), (3.2, 0.7), 0, wall=True)
    decal("decal_graffiti_4", (xm + 6.0, 0.0, z1 + 0.001), (2.6, 0.7), 0, wall=True)
    for i in range(26):
        x, z = rng.uniform(-3, 34), rng.uniform(-14, 22)
        k = rng.random()
        if k < 0.45:
            decal(f"decal_crack_{rng.randint(1, 3)}", (x, 0.0, z), (rng.uniform(1.4, 3.0), rng.uniform(1.4, 3.0)), rng.uniform(0, 360))
        elif k < 0.7:
            decal(f"decal_water_{rng.randint(1, 2)}", (x, 0.0, z), (rng.uniform(1.4, 2.8), rng.uniform(1.2, 2.2)), rng.uniform(0, 360))
        else:
            decal("decal_tire", (x, 0.0, z), (rng.uniform(3, 6), 0.8), rng.uniform(0, 360))


def surroundings(rng):
    # park lights on tall poles round the concrete (on at night)
    for x, z in ((-34.8, -24.8), (0.0, -24.8), (36.8, -24.8), (-34.8, 24.8), (0.0, 24.8), (36.8, 24.8), (-34.8, 0.0), (36.8, -10.0)):
        add("street_lamp", (x, 0.0, z), rng.choice([0, 90, 180, 270]), {"height": 8.0}, None, "park light")
        add("light", (x, 8.0, z), 0, {"type": "point", "intensity": 90.0, "radius": 26.0, "nightOnly": True})
    # rules sign at the entrance (ASTM asks for posted warnings), bins, benches, bike rack, drinking spot
    add("sign", (4.8, 0.0, -25.2), 0, {"height": 2.2}, "sign_green", "park rules")
    model("metal_trash_can", (-5.4, 0.0, -25.0), rng.uniform(0, 360))
    model("metal_trash_can", (37.2, 0.0, 14.0), rng.uniform(0, 360))
    add("bike_rack", (-3.0, 0.0, -31.2), 0, {"length": 3.6, "stands": 5}, "metal_galvanized")
    for x, z, y in ((12.0, 25.6, 180), (22.0, 25.6, 180), (-12.0, 25.6, 180), (38.4, 0.0, 90)):
        add("bench", (x, -0.02, z), y, {"length": 2.2})
    add("picnic_table", (-40.0, -0.02, 6.0), 20)
    add("picnic_table", (-42.5, -0.02, 14.0), -10)
    # trees + shrubs on the lawn, clear of the concrete
    placed = 0
    while placed < 46:
        x, z = rng.uniform(-110, 120), rng.uniform(-100, 100)
        if PARK[0] - 6 < x < PARK[2] + 6 and PARK[1] - 6 < z < PARK[3] + 6:
            continue
        if LOT[0] - 4 < x < LOT[2] + 4 and LOT[1] - 4 < z < LOT[3] + 4:
            continue
        if z < -54.0:  # road and houses
            continue
        add("tree", (x, -0.02, z), rng.uniform(0, 360), {"height": rng.uniform(7.5, 13.0), "crown": rng.uniform(2.6, 4.2), "seed": rng.randint(1, 999)})
        placed += 1
    # belt of trees round the park grounds (no bare horizon)
    for i in range(110):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(95, 190)
        x, z = math.cos(a) * r, math.sin(a) * r * 0.85
        if z < -54.0 and abs(x) < 150:
            continue
        add("tree", (x, -0.02, z), rng.uniform(0, 360), {"height": rng.uniform(9.0, 16.0), "crown": rng.uniform(3.2, 5.0), "seed": rng.randint(1, 999)})
    for i in range(18):
        x = rng.choice([rng.uniform(-39, -36), rng.uniform(38, 41)])
        z = rng.uniform(-22, 22)
        add("shrub", (x, -0.02, z), rng.uniform(0, 360), {"size": [rng.uniform(1.0, 1.8), rng.uniform(0.7, 1.2), rng.uniform(1.0, 1.8)],
                                                          "seed": rng.randint(1, 999)})
    # parking lot: bays, cars, a fence on the road side
    lx = (LOT[0] + LOT[2]) / 2
    for zc, face in ((-45.6, 1), (-38.4, -1)):
        add("parking_lines", (lx, 0.0, zc), 0, {"bays": 18, "bayWidth": 2.6, "bayLength": 5.2}, None, "parking bays")
        for b in range(18):
            x = lx - 9 * 2.6 + 1.3 + b * 2.6
            if rng.random() < 0.45:
                paint = rng.choice(["white", "silver", "grey", "black", "blue", "red", "beige", "green"])
                typ = rng.choice(["sedan", "sedan", "hatch", "hatch", "van"])
                add("car", (x, 0.0, zc + face * rng.uniform(-0.1, 0.2)), 90 * face + rng.uniform(-3, 3), {"type": typ, "paint": paint}, None, f"parked {typ}")
    add("fence", (lx, 0.0, LOT[1] - 0.6), 0, {"length": 58.0, "height": 1.2}, None, "lot fence")
    decal("decal_grime", (lx, 0.0, -42.0), (50, 16), 0, name="lot grime")
    for x in (-20.0, -4.0, 14.0):
        decal(f"decal_oil_{rng.randint(1, 2)}", (x, 0.0, -45.2), (1.5, 1.5), rng.uniform(0, 360))
    # the road past the lot and houses / low blocks across it (closes the view)
    add("road", (0.0, 0.002, -60.0), 0, {"length": 300, "width": 9, "sidewalk": 0, "centerLine": True, "asphalt": True}, "asphalt_worn", "park road")
    add("sidewalk", (0.0, 0.0, -66.5), 0, {"length": 300, "width": 4, "height": 0.15}, "sidewalk", "far sidewalk")
    x = -140.0
    while x < 140:
        w = rng.uniform(12, 22)
        add("building", (x + w / 2, 0.15, -78.0 - rng.uniform(0, 4)), 0,
            {"size": [round(w - 1.5, 1), rng.choice([7.0, 7.0, 10.3, 13.6]), 14], "floorHeight": 3.3, "windowSpacing": 3.2, "shops": False,
             "parapet": True, "litWindows": True}, rng.choice(["brick_red", "plaster_beige", "plaster_white", "brick_painted"]), "house row")
        x += w + rng.uniform(2, 6)
    for i in range(9):
        a = rng.uniform(0.2, math.pi - 0.2)
        r = rng.uniform(170, 240)
        add("building", (math.cos(a) * r, 0.0, math.sin(a) * r * 0.9 + 20), rng.uniform(0, 90),
            {"size": [rng.uniform(20, 34), rng.uniform(14, 36), rng.uniform(18, 30)], "floorHeight": 3.3, "windowSpacing": 3.0, "shops": False,
             "parapet": True, "litWindows": True}, rng.choice(["facade_concrete", "brick_red", "plaster_beige"]), "far block")


def gameplay():
    add("zone", (0, 0, 0), 0, {"halfExtents": [80, 20, 70], "area": "Maple Grove", "kind": "area"}, None, "park area")
    add("spawn", (-2.0, 0.0, -4.0), facing_yaw(1, 0), {"label": "Maple Grove", "default": True}, None, "park start")
    fx, fz = FLOW["c"]
    add("spawn", (fx - 4.0, 0.0, fz - FLOW["width"] / 2 - 0.7), facing_yaw(0, 1), {"label": "Flow Bowl", "default": False}, None, "flow bowl deck")
    px, pz = POOL["c"]
    add("spawn", (px + POOL["length"] / 2 + 0.8, 0.0, pz), facing_yaw(-1, 0), {"label": "Deep End", "default": False}, None, "deep end deck")
    add("spawn", (PLAT[0] + 3.0, PLAT_H, (PLAT[1] + PLAT[3]) / 2 + 1.5), facing_yaw(1, 0), {"label": "Street Section", "default": False},
        None, "platform top")
    add("spawn", (-1.0, 0.0, -27.0), facing_yaw(0, 1), {"label": "Menu", "default": False}, None, "menu idle")


def challenges():
    def gate(pos, radius=3.0, yaw=0.0, require=""):
        g = {"pos": [round(pos[0], 2), round(pos[1], 2), round(pos[2], 2)], "radius": radius, "yaw": round(yaw, 1)}
        if require:
            g["require"] = require
        return g

    xm = (PLAT[0] + PLAT[2]) / 2
    return [
        {"id": "mg_session", "name": "Maple Grove Session", "mode": "trick", "map": SCENE, "spawn": "Maple Grove", "time": 120,
         "targets": [10000, 25000, 50000], "desc": "Two minutes in a real concrete park. Carve the bowls for speed, link the street section."},
        {"id": "mg_deep_end", "name": "Deep End", "mode": "trick", "map": SCENE, "spawn": "Deep End", "time": 90,
         "targets": [4000, 12000, 25000], "desc": "Eight and a half feet, pool coping and tile. Pump the walls and boost over the coping."},
        {"id": "mg_street_line", "name": "Street Section Line", "mode": "line", "map": SCENE, "spawn": "Maple Grove", "targets": [40, 28, 20],
         "gates": [gate((6.0, 8 * IN, -4.0), 2.4, 90, "manual"), gate((19.0, 16 * IN, -4.0), 2.4, 90, "grind"),
                   gate((PARK[2] - 2.2, 1.0, 0.0), 3.5, 90, "air"), gate((24.0, 18 * IN, 9.0), 2.6, 90, "grind"),
                   gate((11.0, 13 * IN, 7.0), 2.4, 90, "grind")],
         "desc": "Manual the pad, grind the 16 in ledge, air the quarter, then back through the 18 in ledge and the flat bar."},
        {"id": "mg_five_stair", "name": "Five Stair", "mode": "line", "map": SCENE, "spawn": "Street Section", "targets": [25, 16, 11],
         "gates": [gate((xm, PLAT_H, (PLAT[1] + PLAT[3]) / 2), 3.0, 90), gate((xm, 0.0, PLAT[3] + 4.5), 3.5, 0, "air")],
         "desc": "Roll the platform and send the five stair (or the hubbas, or the rail)."},
    ]


def main():
    rng = random.Random(1972)
    ground()
    transition_section(rng)
    street_section(rng)
    surroundings(rng)
    gameplay()
    out = os.path.join(ROOT, SCENE)
    with open(out, "w") as f:
        f.write('{\n  "version": 1,\n  "name": "Maple Grove Skatepark",\n  "environment": { "preset": "day", "adjust": { "rotationOffset": 4.2, "sunIntensity": 2.2 } },\n'
                '  "entities": [\n')
        f.write(",\n".join("    " + json.dumps(e, separators=(", ", ": ")) for e in ents))
        f.write("\n  ]\n}\n")
    cpath = os.path.join(ROOT, "assets", "data", "challenges.json")
    data = json.load(open(cpath)) if os.path.exists(cpath) else {"challenges": []}
    data["challenges"] = [c for c in data["challenges"] if c.get("map") != SCENE] + challenges()
    with open(cpath, "w") as f:
        json.dump(data, f, indent=1)
    print(f"maple grove: {len(ents)} entities -> {out}")


if __name__ == "__main__":
    main()
