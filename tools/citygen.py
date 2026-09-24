#!/usr/bin/env python3
"""Generate Scoot City (assets/scenes/city.json) and its challenges (assets/data/challenges.json).

The city is a 3 x 3 grid of 140 m blocks plus the Mega Park block to the east. Every block is one
district; the streets between them are shared asphalt with painted markings, so all districts are
connected and lines can run from one to the next.

Prefab conventions (see src/game/world/prefabs.cpp): Y up, origin at the centre of the footprint on
the ground, ramps rise towards local +X, widths run along local Z, stairs go down towards +X.
A yaw of t degrees turns local +X into (cos t, 0, -sin t).

usage: python3 tools/citygen.py
"""
import json
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOCK = 140.0
PITCH = 164.0

ents = []
challenges = []


def add(prefab, pos, yaw=0.0, params=None, material=None, name=None, area=None):
    e = {"id": len(ents) + 1, "name": name or prefab, "prefab": prefab, "params": params or {},
         "position": [round(pos[0], 3), round(pos[1], 3), round(pos[2], 3)]}
    if yaw:
        e["rotation"] = [0, round(yaw, 3), 0]
    if material:
        e["material"] = material
    if area:
        e["area"] = area
    ents.append(e)
    return e


def rise_yaw(dx, dz):
    """yaw that makes a ramp rise towards the world direction (dx, dz)"""
    return math.degrees(math.atan2(-dz, dx))


def along_yaw(dx, dz):
    """yaw that lays a length-along-X object along the world direction (dx, dz)"""
    return rise_yaw(dx, dz)


def facing_yaw(dx, dz):
    """spawn yaw so the rider faces the world direction (dx, dz); yaw 0 faces -Z"""
    return math.degrees(math.atan2(-dx, -dz))


def district(name, cx, cz, material, spawn_off, spawn_face, spawn_y=0.0, half=70.0):
    add("slab", (cx, 0, cz), 0, {"size": [BLOCK - 2, 0.03, BLOCK - 2], "grindEdges": False}, material, name + " ground", name)
    add("zone", (cx, 0, cz), 0, {"halfExtents": [half, 12, half], "area": name, "kind": "area"}, None, name + " area")
    add("spawn", (cx + spawn_off[0], spawn_y, cz + spawn_off[1]), facing_yaw(*spawn_face), {"label": name, "default": name == "Street Plaza"},
        None, name + " spawn")


def lamp(x, z, yaw=0.0):
    add("street_lamp", (x, 0, z), yaw, {"height": 6.5}, None, "lamp")


def tree(x, z, rng, h=None):
    add("tree", (x, 0, z), rng.uniform(0, 360), {"height": h or rng.uniform(6, 9), "crown": rng.uniform(2.2, 3.2), "seed": rng.randint(1, 999)}, None, "tree")


def building(x, z, sx, sy, sz, mat, yaw=0.0, shops=True, lit=True, parapet=True, roof="roof_grey", name="building"):
    add("building", (x, 0, z), yaw, {"size": [sx, sy, sz], "floorHeight": 3.3, "windowSpacing": 3.0, "shops": shops, "parapet": parapet,
                                      "litWindows": lit, "roofMaterial": roof}, mat, name)


def gate(pos, radius=3.0, yaw=0.0, require=""):
    g = {"pos": [round(pos[0], 2), round(pos[1], 2), round(pos[2], 2)], "radius": radius, "yaw": round(yaw, 1)}
    if require:
        g["require"] = require
    return g


# ------------------------------------------------------------------------------------------------
def ground_and_streets(rng):
    # asphalt city base + grass beyond
    add("ground", (82, 0, 0), 0, {"size": [760, 0.4, 600], "divisions": 4}, "asphalt", "city asphalt")
    add("ground", (82, -0.03, 0), 0, {"size": [2400, 0.4, 2400], "divisions": 1}, "grass", "countryside")
    xs = [-246, -82, 82, 246, 410]
    zs = [-246, -82, 82, 246]
    for x in xs:  # north-south avenues
        add("road", (x, 0.004, 0), 90, {"length": 492, "width": 12, "sidewalk": 0, "centerLine": True, "asphalt": False}, None, "avenue")
    for z in zs:  # east-west streets
        add("road", (82, 0.005, z), 0, {"length": 656, "width": 12, "sidewalk": 0, "centerLine": True, "asphalt": False}, None, "street")
    # lamps + trees along the streets
    for x in xs:
        for z in range(-240, 241, 40):
            if any(abs(z - zz) < 12 for zz in zs):
                continue
            lamp(x - 9, z, 0)
            lamp(x + 9, z + 20, 180)
    for z in zs:
        for x in range(-240, 405, 40):
            if any(abs(x - xx) < 12 for xx in xs):
                continue
            lamp(x, z - 9, 90)
    for z in zs:
        for x in range(-230, 400, 56):
            if any(abs(x - xx) < 14 for xx in xs):
                continue
            tree(x, z + 9.5, rng)
    # traffic cones and barriers here and there (dynamic props)
    for i in range(16):
        x = rng.choice(xs) + rng.uniform(-4, 4)
        z = rng.uniform(-230, 230)
        add("cone", (x, 0, z), rng.uniform(0, 360), {}, None, "cone")
    # skyline: buildings around the outside of the city
    for side in range(4):
        for i in range(14):
            t = -240 + i * 48 + rng.uniform(-6, 6)
            h = rng.choice([14, 18, 22, 28, 34, 42, 50])
            w = rng.uniform(22, 36)
            d = rng.uniform(18, 26)
            mat = rng.choice(["plaster_beige", "plaster_white", "brick_red", "concrete_wall"])
            if side == 0:
                building(t + 82, -290, w, h, d, mat, name="skyline")
            elif side == 1:
                building(t + 82, 290, w, h, d, mat, 180, name="skyline")
            elif side == 2 and abs(t) < 250:
                building(-290, t, w, h, d, mat, 90, name="skyline")
            elif side == 3 and abs(t) < 250:
                building(454, t, w, h, d, mat, -90, name="skyline")


# ------------------------------------------------------------------------------------------------
def street_plaza(rng):
    cx, cz = 0.0, 0.0
    name = "Street Plaza"
    district(name, cx, cz, "plaza_tiles", (0, 52), (0, -1))
    # raised centre terrace with stairs north + south and banks east + west
    th = 0.85
    add("slab", (cx, 0, cz - 8), 0, {"size": [26, th, 16], "grindEdges": True}, "concrete_smooth", "terrace", name)
    # south stairs (down towards +Z), wide, double handrail + hubbas
    add("stairs", (cx, 0, cz + 1.85), rise_yaw(0, 1), {"steps": 5, "rise": 0.17, "run": 0.34, "width": 8, "landingTop": 2.0,
                                                                   "handrail": True, "handrailBoth": True, "hubba": True, "railHeight": 0.9},
        "concrete_smooth", "plaza stairs south", name)
    # north stairs (down towards -Z) with a single handrail
    add("stairs", (cx, 0, cz - 17.85), rise_yaw(0, -1), {"steps": 5, "rise": 0.17, "run": 0.34, "width": 6, "landingTop": 2.0, "handrail": True},
        "concrete_smooth", "plaza stairs north", name)
    # east / west banks up to the terrace
    add("bank", (cx + 14.7, 0, cz - 8), rise_yaw(-1, 0), {"length": 3.4, "height": th, "width": 10, "deck": 0.0}, "concrete_smooth", "terrace bank east", name)
    add("bank", (cx - 14.7, 0, cz - 8), rise_yaw(1, 0), {"length": 3.4, "height": th, "width": 10, "deck": 0.0}, "concrete_smooth", "terrace bank west", name)
    # marble ledges in rows
    for i, (x, z, l) in enumerate([(-22, 14, 10), (22, 14, 10), (-30, -4, 8), (30, -4, 8), (-22, -30, 12), (22, -30, 12)]):
        add("ledge", (cx + x, 0, cz + z), 90 if i >= 2 and i < 4 else 0, {"length": l, "width": 0.7, "height": 0.46}, "concrete_smooth", "marble ledge", name)
    # manual pads
    add("manual_pad", (cx - 8, 0, cz + 30), 90, {"length": 7, "width": 2.2, "height": 0.24}, "concrete_smooth", "manual pad west", name)
    add("manual_pad", (cx + 8, 0, cz + 30), 90, {"length": 7, "width": 2.2, "height": 0.24}, "concrete_smooth", "manual pad east", name)
    # flat rails
    add("rail", (cx - 38, 0, cz + 22), 90, {"length": 9, "height": 0.42}, None, "flat rail", name)
    add("rail", (cx + 38, 0, cz + 22), 90, {"length": 9, "height": 0.42, "type": "square"}, "rail_red", "square rail", name)
    # dry fountain bowl
    add("bowl", (cx + 36, 0, cz - 36), 0, {"radius": 5.5, "depth": 1.4, "transition": 1.8, "deck": 1.2, "segments": 40}, "concrete_smooth", "fountain bowl", name)
    # planter gap + planters with trees
    add("planter", (cx - 36, 0, cz - 36), 0, {"size": [6, 0.6, 2.2]}, "concrete_smooth", "planter", name)
    add("planter", (cx - 36, 0, cz - 29), 0, {"size": [6, 0.6, 2.2]}, "concrete_smooth", "planter", name)
    for (x, z) in [(-50, 40), (50, 40), (-50, -50), (50, -50), (-12, 46), (12, 46)]:
        add("planter", (cx + x, 0, cz + z), 0, {"size": [3.2, 0.55, 3.2]}, "concrete_smooth", "tree planter", name)
        tree(cx + x, cz + z, rng, 6.5)
    # street furniture
    for (x, z, y) in [(-14, 40, 0), (14, 40, 0), (-44, 8, 90), (44, 8, 90), (0, -44, 0)]:
        add("bench", (cx + x, 0, cz + z), y, {"length": 2.2}, None, "bench", name)
    for (x, z) in [(-18, 42), (18, 42), (40, -12)]:
        add("trash_can", (cx + x, 0, cz + z), 0, {}, None, "trash can", name)
    for (x, z) in [(-60, 60), (60, 60), (-60, -60), (60, -60), (0, 60), (-60, 0), (60, 0)]:
        lamp(cx + x, cz + z)
    add("bike_rack", (cx + 26, 0, cz + 44), 0, {"length": 3}, None, "bike rack", name)
    return {
        "manual": (cx - 8, cz + 30), "stairs_top": (cx, cz - 2), "rail": (cx - 38, cz + 22), "hubba": (cx - 4.6, cz + 3),
        "spawn": (cx, cz + 52), "terrace": (cx, cz - 8), "bowl": (cx + 36, cz - 36),
    }


def skatepark(rng):
    cx, cz = PITCH, 0.0
    name = "Skatepark"
    district(name, cx, cz, "concrete_park", (0, 55), (0, -1))
    # fence around with entrances in the middle of each side
    for side in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
        for k in [-1, 1]:
            if side[0] != 0:
                add("fence", (cx + side[0] * 67, 0, cz + k * 40), 90, {"length": 50, "height": 1.3}, None, "park fence", name)
            else:
                add("fence", (cx + k * 40, 0, cz + side[1] * 67), 0, {"length": 50, "height": 1.3}, None, "park fence", name)
    # bowl
    add("bowl", (cx + 32, 0, cz - 32), 0, {"radius": 8.5, "depth": 2.6, "transition": 2.8, "deck": 2.0, "segments": 56}, "concrete_park", "bowl", name)
    # quarter pipes east + west
    for z in (-6, 10):
        add("quarter_pipe", (cx + 56, 0, cz + z), rise_yaw(1, 0), {"height": 2.0, "radius": 2.6, "width": 8, "deck": 1.6}, "concrete_park", "quarter east", name)
    add("quarter_pipe", (cx - 56, 0, cz + 2), rise_yaw(-1, 0), {"height": 2.4, "radius": 3.0, "width": 12, "deck": 1.6}, "concrete_park", "quarter west", name)
    # half pipe (mini ramp) north
    add("half_pipe", (cx - 20, 0, cz - 44), 0, {"height": 1.6, "radius": 2.2, "width": 10, "flat": 3.0, "deck": 1.5}, "plywood", "mini ramp", name)
    # spine in the middle
    add("spine", (cx, 0, cz + 28), 0, {"height": 1.5, "radius": 2.0, "width": 7}, "concrete_park", "spine", name)
    # funboxes with rails
    add("funbox", (cx - 10, 0, cz), 0, {"rampLength": 2.0, "topLength": 3.5, "height": 0.7, "width": 4, "rail": True}, "concrete_park", "funbox", name)
    add("funbox", (cx + 22, 0, cz + 6), 90, {"rampLength": 2.2, "topLength": 4.0, "height": 0.8, "width": 3.4, "rail": True}, "concrete_park", "funbox 2", name)
    # kicker to box (gap)
    add("kicker", (cx - 36, 0, cz + 24), rise_yaw(1, 0), {"length": 2.6, "height": 0.9, "width": 2.4}, "plywood", "kicker", name)
    add("funbox", (cx - 25, 0, cz + 24), 0, {"rampLength": 2.4, "topLength": 3.0, "height": 0.9, "width": 3.0, "rail": False}, "concrete_park", "landing box", name)
    # stairs with hubba
    add("slab", (cx - 40, 0, cz - 14), 0, {"size": [8, 0.51, 8], "grindEdges": True}, "concrete_park", "stair deck", name)
    add("stairs", (cx - 35.26, 0, cz - 14), rise_yaw(1, 0), {"steps": 3, "rise": 0.17, "run": 0.36, "width": 6, "landingTop": 0.4, "handrail": True,
                                                            "hubba": True}, "concrete_park", "3 stair", name)
    # rails + ledges + manual pad
    add("rail", (cx + 8, 0, cz - 16), 0, {"length": 10, "height": 0.4}, None, "flat bar", name)
    add("rail", (cx + 8, 0, cz - 22), 0, {"length": 8, "height": 0.5, "type": "square"}, "rail_yellow", "square bar", name)
    add("rail", (cx - 4, 0, cz + 44), 0, {"length": 10, "height": 0.35, "segments": 2, "drop": 0.0}, None, "long bar", name)
    add("ledge", (cx + 28, 0, cz + 30), 0, {"length": 8, "width": 0.6, "height": 0.45}, "concrete_smooth", "park ledge", name)
    add("manual_pad", (cx - 2, 0, cz - 30), 0, {"length": 6, "width": 2.0, "height": 0.25}, "concrete_park", "park manual pad", name)
    add("bank", (cx + 50, 0, cz + 45), rise_yaw(1, 1), {"length": 3.2, "height": 1.2, "width": 5, "deck": 1.0}, "concrete_park", "corner bank", name)
    add("bank", (cx - 50, 0, cz + 45), rise_yaw(-1, 1), {"length": 3.2, "height": 1.2, "width": 5, "deck": 1.0}, "concrete_park", "corner bank", name)
    for (x, z) in [(-62, -62), (62, -62), (-62, 62), (62, 62), (0, 62), (0, -62)]:
        lamp(cx + x, cz + z)
    for (x, z) in [(-20, 60), (20, 60)]:
        add("bench", (cx + x, 0, cz + z), 0, {"length": 2.2}, None, "bench", name)
    return {"kicker": (cx - 36, cz + 24), "funbox": (cx - 10, cz), "bowl": (cx + 32, cz - 32), "flat_bar": (cx + 8, cz - 16), "spine": (cx, cz + 28),
            "qp_west": (cx - 56, cz + 2), "manual": (cx - 2, cz - 30)}


def mega_park(rng):
    cx, cz = 2 * PITCH, 0.0
    name = "Mega Park"
    district(name, cx, cz, "concrete_worn", (-63, 0), (1, 0), spawn_y=11.0)
    # mega ramp runs +X from the tower
    add("mega_ramp", (cx - 65, 0, cz), 0, {"towerHeight": 11.0, "rollInLength": 20.0, "width": 7.5, "kickerHeight": 3.0, "kickerLength": 7.0,
                                           "gap": 14.0, "landingHeight": 5.5, "landingLength": 20.0, "quarterHeight": 6.0}, "plywood", "mega ramp", name)
    # tower stairs for the walk back up (and the respawn point sits on the deck)
    # big air lines: kicker -> table top landing
    for (z, h, gapl) in [(34, 1.4, 6.0), (-34, 2.2, 9.0)]:
        kl = h * 2.4
        add("kicker", (cx - 30, 0, cz + z), rise_yaw(1, 0), {"length": kl, "height": h, "width": 4.0}, "plywood", "air kicker", name)
        land_len = h * 4.0
        deck = 3.0
        total = land_len + deck
        x_land = cx - 30 + kl * 0.5 + gapl + total * 0.5
        add("bank", (x_land, 0, cz + z), rise_yaw(-1, 0), {"length": land_len, "height": h, "width": 5.0, "deck": deck, "grindEdge": False},
            "plywood", "landing", name)
    # run-up banks to the lines
    add("quarter_pipe", (cx + 60, 0, cz + 34), rise_yaw(1, 0), {"height": 3.5, "radius": 3.8, "width": 10, "deck": 2.0}, "plywood", "big quarter", name)
    add("quarter_pipe", (cx + 60, 0, cz - 34), rise_yaw(1, 0), {"height": 3.5, "radius": 3.8, "width": 10, "deck": 2.0}, "plywood", "big quarter", name)
    for (x, z) in [(-60, 60), (0, 60), (60, 60), (-60, -60), (0, -60), (60, -60)]:
        lamp(cx + x, cz + z)
    for i in range(6):
        add("sign", (cx - 40 + i * 16, 0, cz + 62), 180, {"height": 3.2}, "sign_green", "banner", name)
    return {"tower": (cx - 63, cz), "mega_kicker": (cx - 65 + 4 + 30, cz), "line_n": (cx - 30, cz + 34), "line_s": (cx - 30, cz - 34)}


def downtown(rng):
    cx, cz = 0.0, -PITCH
    name = "Downtown"
    district(name, cx, cz, "pavement", (0, 55), (0, -1))
    mats = ["plaster_beige", "plaster_white", "concrete_wall", "brick_red", "factory_wall"]
    # towers on the north edge and the east / west sides, the square stays open to the south
    x = -58
    for w in [30, 26, 34, 28]:
        building(cx + x + w * 0.5, cz - 56, w, rng.choice([36, 42, 48, 54, 60]), 24, rng.choice(mats), name="downtown tower")
        x += w + 3
    for side in (-1, 1):
        z = -36
        for d in [24, 22, 26]:
            building(cx + side * 56, cz + z + d * 0.5, 24, rng.choice([24, 30, 36, 44]), d, rng.choice(mats), side * 90, name="downtown block")
            z += d + 4
    # terrace + big stair set with handrails and hubbas
    add("slab", (cx, 0, cz - 30), 0, {"size": [30, 1.36, 10], "grindEdges": True}, "concrete_smooth", "terrace", name)
    add("stairs", (cx, 0, cz - 23.14), rise_yaw(0, 1), {"steps": 8, "rise": 0.17, "run": 0.34, "width": 10, "landingTop": 1.0,
                                                                   "handrail": True, "handrailBoth": True, "hubba": True, "railHeight": 0.95},
        "concrete_smooth", "8 stair", name)
    add("bank", (cx - 17.5, 0, cz - 30), rise_yaw(1, 0), {"length": 5.0, "height": 1.36, "width": 10, "deck": 0.0}, "concrete_smooth", "terrace bank", name)
    add("bank", (cx + 17.5, 0, cz - 30), rise_yaw(-1, 0), {"length": 5.0, "height": 1.36, "width": 10, "deck": 0.0}, "concrete_smooth", "terrace bank", name)
    # square: long ledges, planter gaps, benches, bike racks
    for (x, z) in [(-24, 0), (24, 0), (-24, 16), (24, 16)]:
        add("ledge", (cx + x, 0, cz + z), 90, {"length": 12, "width": 0.8, "height": 0.5}, "concrete_smooth", "square ledge", name)
    for (x, z) in [(-8, 30), (8, 30)]:
        add("planter", (cx + x, 0, cz + z), 0, {"size": [8, 0.6, 2.4]}, "concrete_smooth", "planter", name)
    add("rail", (cx, 0, cz + 8), 0, {"length": 12, "height": 0.45}, None, "square rail", name)
    add("manual_pad", (cx, 0, cz + 42), 0, {"length": 10, "width": 2.4, "height": 0.26}, "concrete_smooth", "long manual", name)
    for (x, z) in [(-36, 30), (36, 30), (-36, 44), (36, 44)]:
        add("planter", (cx + x, 0, cz + z), 0, {"size": [3.2, 0.55, 3.2]}, "concrete_smooth", "tree planter", name)
        tree(cx + x, cz + z, rng, 7)
    for (x, z, y) in [(-12, 20, 0), (12, 20, 0)]:
        add("bench", (cx + x, 0, cz + z), y, {"length": 2.4}, None, "bench", name)
    for x in (-40, -20, 20, 40):
        add("bollard", (cx + x, 0, cz + 62), 0, {}, None, "bollard", name)
    add("bike_rack", (cx - 30, 0, cz + 56), 0, {"length": 3}, None, "bike rack", name)
    for (x, z) in [(-45, 60), (45, 60), (-45, -10), (45, -10)]:
        lamp(cx + x, cz + z)
    return {"stairs": (cx, cz - 23.14), "rail": (cx, cz + 8), "manual": (cx, cz + 42), "terrace": (cx, cz - 30), "spawn": (cx, cz + 55)}


def school(rng):
    cx, cz = -PITCH, 0.0
    name = "School"
    district(name, cx, cz, "pavement", (30, 50), (0, -1))
    building(cx, cz - 48, 70, 11, 20, "brick_red", shops=False, name="school building")
    # entrance terrace with stairs + central hubba
    add("slab", (cx, 0, cz - 33), 0, {"size": [36, 1.02, 10], "grindEdges": True}, "concrete_smooth", "school terrace", name)
    for x in (-10, 10):
        add("stairs", (cx + x, 0, cz - 26.68), rise_yaw(0, 1), {"steps": 6, "rise": 0.17, "run": 0.34, "width": 7, "landingTop": 0.6,
                                                                           "handrail": True, "handrailBoth": True, "railHeight": 0.9}, "concrete_smooth",
            "school stairs", name)
    add("bank", (cx + 20, 0, cz - 33), rise_yaw(-1, 0), {"length": 4.0, "height": 1.02, "width": 10, "deck": 0.0}, "concrete_smooth", "terrace bank", name)
    # basketball court
    add("slab", (cx - 30, 0, cz + 20), 0, {"size": [28, 0.05, 16], "grindEdges": False}, "rubber", "basketball court", name)
    for s in (-1, 1):
        add("fence", (cx - 30, 0, cz + 20 + s * 8.5), 0, {"length": 28, "height": 3.0}, None, "court fence", name)
    # benches, picnic tables, long flat bar
    for i in range(4):
        add("picnic_table", (cx + 14 + i * 6, 0, cz + 20), 90, {}, None, "picnic table", name)
    for i in range(3):
        add("bench", (cx + 14 + i * 8, 0, cz + 34), 0, {"length": 2.2}, None, "bench", name)
    add("rail", (cx + 30, 0, cz), 0, {"length": 14, "height": 0.45}, None, "courtyard bar", name)
    add("ledge", (cx - 2, 0, cz + 2), 0, {"length": 10, "width": 0.6, "height": 0.5}, "concrete_smooth", "courtyard ledge", name)
    add("manual_pad", (cx + 10, 0, cz + 48), 0, {"length": 6, "width": 2.2, "height": 0.25}, "concrete_smooth", "manual pad", name)
    for (x, z) in [(-60, 40), (-45, 55), (55, 55), (60, 5), (0, 55)]:
        tree(cx + x, cz + z, rng)
    add("bike_rack", (cx - 10, 0, cz - 20), 0, {"length": 4}, None, "bike rack", name)
    for (x, z) in [(-60, -20), (60, -20), (-60, 60), (60, 60)]:
        lamp(cx + x, cz + z)
    return {"stairs": (cx - 10, cz - 26.68), "bar": (cx + 30, cz), "manual": (cx + 10, cz + 48), "spawn": (cx + 30, cz + 50)}


def industrial(rng):
    cx, cz = PITCH, -PITCH
    name = "Industrial"
    district(name, cx, cz, "concrete_worn", (0, 55), (0, -1))
    # warehouses with loading docks
    for (x, z, w, d, mat) in [(-38, -45, 44, 22, "corrugated"), (30, -45, 40, 22, "factory_wall")]:
        building(cx + x, cz + z, w, 10, d, mat, shops=False, lit=False, name="warehouse")
        add("loading_dock", (cx + x, 0, cz + z + d * 0.5 + 2.0), 0, {"length": w - 6, "depth": 4.0, "height": 1.2}, "concrete_worn", "loading dock", name)
    building(cx + 48, cz + 30, 22, 9, 36, "corrugated", 90, shops=False, lit=False, name="warehouse")
    # container yard: rows with gaps
    colors = ["container_blue", "container_red"]
    for i in range(4):
        add("container", (cx - 40, 0, cz + 2 + i * 6.5), 0, {"long": True}, colors[i % 2], "container", name)
    add("container", (cx - 40, 2.59, cz + 8.5), 0, {"long": False}, "container_red", "stacked container", name)
    for i in range(3):
        add("container", (cx - 12 + i * 9, 0, cz + 40), 90, {"long": False}, colors[(i + 1) % 2], "container", name)
    # DIY banks and a pallet kicker
    add("bank", (cx - 6, 0, cz - 4), rise_yaw(1, 0), {"length": 4.0, "height": 1.4, "width": 6, "deck": 1.5}, "concrete_worn", "diy bank", name)
    add("bank", (cx + 10, 0, cz - 4), rise_yaw(-1, 0), {"length": 4.0, "height": 1.4, "width": 6, "deck": 1.5}, "concrete_worn", "diy bank", name)
    add("kicker", (cx + 2, 0, cz + 22), rise_yaw(1, 0), {"length": 2.2, "height": 0.7, "width": 2.0}, "plywood", "pallet kicker", name)
    add("pipe", (cx + 20, 0, cz + 18), 0, {"radius": 0.45, "length": 14, "height": 0.7}, "metal_rust", "pipe", name)
    for i in range(5):
        add("barrier", (cx - 20 + i * 3.2, 0, cz + 56), 0, {"length": 3.0}, None, "jersey barrier", name)
    add("slab", (cx + 20, 0, cz - 20), 0, {"size": [6, 0.045, 4], "grindEdges": False}, "metal_plate", "steel plate", name)
    for (x, z) in [(-60, 60), (60, 60), (-60, -20), (60, -20), (0, 60)]:
        lamp(cx + x, cz + z)
    return {"dock_w": (cx - 38, cz - 32), "dock_e": (cx + 30, cz - 32), "banks": (cx + 2, cz - 4), "pipe": (cx + 20, cz + 18),
            "containers": (cx - 40, cz + 12), "spawn": (cx, cz + 55)}


def rooftops(rng):
    cx, cz = -PITCH, -PITCH
    name = "Rooftops"
    # roof A (arrival), B (higher, north), C (lower, west of B), D (east of A), drop ramp from D
    A = (cx, cz + 12, 34, 8.0, 26)
    B = (cx, cz - 17, 30, 9.0, 24)
    C = (cx - 33, cz - 17, 28, 7.0, 24)
    D = (cx + 36, cz + 12, 26, 8.0, 26)
    district(name, cx, cz, "pavement", (0, 16), (0, -1), spawn_y=A[3] + 0.05)
    for (x, z, sx, sy, sz), mat in zip([A, B, C, D], ["concrete_wall", "plaster_white", "brick_red", "plaster_beige"]):
        building(x, z, sx, sy, sz, mat, shops=False, parapet=False, roof="roof_grey", name="rooftop building")
    # access ramp from the street up to roof A (rises towards -Z)
    ramp_len, deck = 44.0, 3.0
    add("bank", (A[0], 0, A[1] + A[4] * 0.5 + (ramp_len + deck) * 0.5), rise_yaw(0, -1), {"length": ramp_len, "height": A[3], "width": 6.0, "deck": deck,
                                                                                          "grindEdge": False}, "concrete_smooth", "roof access ramp", name)
    # A -> B: kicker on A's north edge (B is 1 m higher)
    add("kicker", (A[0] + 6, A[3], A[1] - A[4] * 0.5 + 3.0), rise_yaw(0, -1), {"length": 2.6, "height": 1.0, "width": 2.6}, "plywood", "roof kicker", name)
    # B -> C: small kicker towards the west (drop to the lower roof)
    add("kicker", (B[0] - B[2] * 0.5 + 3.0, B[3], B[1]), rise_yaw(-1, 0), {"length": 2.2, "height": 0.6, "width": 2.4}, "plywood", "roof kicker west", name)
    # A -> D: plank bridge + a rail across the gap
    gap_x = (A[0] + A[2] * 0.5 + D[0] - D[2] * 0.5) * 0.5
    add("slab", (gap_x, A[3] - 0.3, A[1] + 6), 0, {"size": [6.4, 0.3, 3.0], "grindEdges": True}, "wood_deck", "plank bridge", name)
    add("rail", (gap_x, A[3], A[1] - 4), 0, {"length": 9, "height": 0.4}, None, "gap rail", name)
    # rooftop furniture: ledges, AC units, a small quarter on C
    add("ledge", (A[0] - 8, A[3], A[1] + 4), 0, {"length": 10, "width": 0.6, "height": 0.45}, "concrete_smooth", "roof ledge", name)
    add("manual_pad", (B[0] + 4, B[3], B[1] + 2), 0, {"length": 6, "width": 2.0, "height": 0.25}, "concrete_smooth", "roof manual", name)
    for (x, z) in [(-12, -26), (10, -14), (-44, -26)]:
        add("slab", (cx + x, 9.0 if x > -30 else 7.0, cz + z), 0, {"size": [2.2, 1.3, 1.6], "grindEdges": True}, "metal_plate", "ac unit", name)
    add("quarter_pipe", (C[0] - C[2] * 0.5 + 3, C[3], C[1]), rise_yaw(-1, 0), {"height": 1.4, "radius": 1.8, "width": 6, "deck": 1.0}, "plywood", "roof quarter", name)
    # drop back to the street from D (descends towards +X, then onto the avenue)
    drop_len = 32.0
    add("bank", (D[0] + D[2] * 0.5 + (drop_len + 2.0) * 0.5, 0, D[1]), rise_yaw(-1, 0), {"length": drop_len, "height": D[3], "width": 5.0, "deck": 2.0,
                                                                                          "grindEdge": False}, "concrete_smooth", "street drop", name)
    # C back down: stairs are too tall - a long bank to the west side
    add("bank", (C[0], 0, C[1] + C[4] * 0.5 + 15.0), rise_yaw(0, -1), {"length": 27.0, "height": C[3], "width": 5.0, "deck": 3.0, "grindEdge": False},
        "concrete_smooth", "roof c ramp", name)
    for (x, z) in [(-60, 60), (60, 60), (-60, -60), (60, -60)]:
        lamp(cx + x, cz + z)
    return {"A": A, "B": B, "C": C, "D": D, "ramp_bottom": (A[0], A[1] + A[4] * 0.5 + ramp_len + deck), "gap_x": gap_x}


def ditch(rng):
    cx, cz = PITCH, PITCH
    name = "Ditch"
    district(name, cx, cz, "dirt", (-62, 0), (1, 0))
    add("ditch", (cx, 0, cz), 0, {"length": 118, "bottom": 5.0, "bankWidth": 3.6, "depth": 2.6, "top": 2.2}, "gravel_concrete", "ditch", name)
    # hips / quarter at the east end and a bank start at the west end
    add("quarter_pipe", (cx + 56, 0, cz), rise_yaw(1, 0), {"height": 2.6, "radius": 3.0, "width": 5.0, "deck": 1.0}, "gravel_concrete", "ditch end", name)
    add("pipe", (cx + 10, 0, cz), 90, {"radius": 0.35, "length": 5.0, "height": 0.6}, "metal_rust", "ditch pipe", name)
    add("kicker", (cx - 20, 0, cz), rise_yaw(1, 0), {"length": 2.0, "height": 0.55, "width": 2.2}, "plywood", "ditch kicker", name)
    # access banks up to the ditch tops
    for s in (-1, 1):
        add("bank", (cx - 30, 0, cz + s * 12.5), rise_yaw(0, -s), {"length": 5.5, "height": 2.6, "width": 5, "deck": 0.0}, "gravel_concrete", "top access", name)
    for i in range(10):
        x = cx - 60 + i * 13
        tree(x, cz + rng.choice([-1, 1]) * rng.uniform(16, 30), rng)
    add("fence", (cx, 0, cz + 45), 0, {"length": 60, "height": 1.8}, None, "ditch fence", name)
    for (x, z) in [(-60, 60), (60, 60), (-60, -60), (60, -60)]:
        lamp(cx + x, cz + z)
    return {"west": (cx - 55, cz), "east": (cx + 50, cz), "kicker": (cx - 20, cz), "pipe": (cx + 10, cz)}


def residential(rng):
    cx, cz = -PITCH, PITCH
    name = "Residential"
    district(name, cx, cz, "grass", (0, 0), (0, -1))
    # inner street (asphalt strip + markings) running east-west
    add("slab", (cx, 0, cz), 0, {"size": [138, 0.045, 10], "grindEdges": False}, "asphalt", "residential street", name)
    add("road", (cx, 0.049, cz), 0, {"length": 138, "width": 8, "sidewalk": 0, "centerLine": True, "asphalt": False}, None, "residential markings", name)
    # sidewalk curbs = grindable ledges
    for s in (-1, 1):
        add("slab", (cx, 0, cz + s * 6.6), 0, {"size": [138, 0.14, 3.0], "grindEdges": True}, "pavement", "sidewalk", name)
    mats = ["plaster_beige", "plaster_white", "brick_red"]
    xs = [-52, -26, 0, 26, 52]
    for side in (-1, 1):
        for i, x in enumerate(xs):
            hx, hz = cx + x, cz + side * 30
            building(hx, hz, 13, rng.choice([6.5, 7.0, 7.5]), 10, rng.choice(mats), 0 if side < 0 else 180, shops=False, parapet=False, name="house")
            # driveway kicker (curb cut) + porch steps with a rail + low garden wall
            add("slab", (hx + 5, 0, cz + side * 16), 0, {"size": [4, 0.05, 16], "grindEdges": False}, "concrete_worn", "driveway", name)
            add("stairs", (hx - 2, 0, cz + side * 23.4), rise_yaw(0, -side) - 180 + 180 if side < 0 else rise_yaw(0, -side),
                {"steps": 3, "rise": 0.16, "run": 0.3, "width": 2.4, "landingTop": 1.2, "handrail": True, "railHeight": 0.85}, "concrete_smooth", "porch steps", name)
            add("wall", (hx - 3.5, 0, cz + side * 10.4), 0, {"size": [6, 0.55, 0.35], "grindTop": True}, "brick_red", "garden wall", name)
            add("fence", (hx, 0, cz + side * 40), 0, {"length": 22, "height": 1.2}, None, "backyard fence", name)
            tree(hx - 5, cz + side * 16, rng)
    # pocket park with a hidden kicker
    add("picnic_table", (cx + 60, 0, cz + 55), 0, {}, None, "picnic table", name)
    add("bench", (cx + 50, 0, cz + 58), 0, {"length": 2.2}, None, "bench", name)
    add("kicker", (cx - 60, 0, cz + 55), rise_yaw(1, 0), {"length": 2.4, "height": 0.8, "width": 2.0}, "plywood", "hidden kicker", name)
    for x in range(-60, 61, 30):
        lamp(cx + x, cz + 9, 0)
    return {"street_w": (cx - 60, cz), "street_e": (cx + 60, cz), "walls": [(cx + x - 3.5, cz - 10.4) for x in xs]}


def comp_park(rng):
    cx, cz = 0.0, PITCH
    name = "Competition Park"
    district(name, cx, cz, "concrete_smooth", (0, -52), (0, 1))
    # perimeter fence + banners + bleachers
    for s in (-1, 1):
        add("fence", (cx + s * 64, 0, cz), 90, {"length": 120, "height": 1.4}, None, "comp fence", name)
    for i in range(8):
        add("sign", (cx - 56 + i * 16, 0, cz - 62), 0, {"height": 3.4}, "sign_green", "banner", name)
    add("stairs", (cx, 0, cz + 60), rise_yaw(0, -1), {"steps": 6, "rise": 0.45, "run": 0.8, "width": 40, "landingTop": 1.0, "handrail": False},
        "concrete_smooth", "bleachers", name)
    # course
    add("quarter_pipe", (cx, 0, cz - 58), rise_yaw(0, -1), {"height": 3.4, "radius": 3.6, "width": 16, "deck": 2.0}, "concrete_smooth", "big quarter", name)
    add("kicker", (cx - 18, 0, cz - 20), rise_yaw(0, 1), {"length": 3.2, "height": 1.2, "width": 3.2}, "plywood", "box jump kicker", name)
    add("funbox", (cx - 18, 0, cz - 12.9), 90, {"rampLength": 3.0, "topLength": 5.0, "height": 1.2, "width": 5.0, "rail": True, "railHeight": 0.3},
        "concrete_smooth", "box jump", name)
    # 10 stair with rail from a platform
    add("slab", (cx + 24, 0, cz - 24), 0, {"size": [12, 1.7, 10], "grindEdges": True}, "concrete_smooth", "platform", name)
    add("stairs", (cx + 24, 0, cz - 17.1), rise_yaw(0, 1), {"steps": 10, "rise": 0.17, "run": 0.34, "width": 6, "landingTop": 0.4,
                                                                         "handrail": True, "hubba": True, "railHeight": 0.95}, "concrete_smooth", "10 stair", name)
    add("bank", (cx + 24, 0, cz - 32), rise_yaw(0, 1), {"length": 6.0, "height": 1.7, "width": 8, "deck": 0.0}, "concrete_smooth", "platform bank", name)
    # rails, spine, banks
    add("rail", (cx - 36, 0, cz + 14), 90, {"length": 14, "height": 0.45}, None, "long rail", name)
    add("rail", (cx - 44, 0, cz + 14), 90, {"length": 10, "height": 0.4, "drop": 1.0, "segments": 1}, "rail_red", "down rail", name)
    add("spine", (cx + 2, 0, cz + 22), 90, {"height": 1.8, "radius": 2.4, "width": 8}, "concrete_smooth", "spine", name)
    add("bank", (cx + 44, 0, cz + 20), rise_yaw(1, 0), {"length": 3.4, "height": 1.4, "width": 8, "deck": 1.2}, "concrete_smooth", "bank", name)
    add("bank", (cx - 50, 0, cz - 40), rise_yaw(-1, -1), {"length": 3.4, "height": 1.4, "width": 8, "deck": 1.2}, "concrete_smooth", "corner bank", name)
    add("wall", (cx + 52, 0, cz - 30), 90, {"size": [10, 3.0, 0.4], "grindTop": False}, "concrete_wall", "wallride wall", name)
    add("manual_pad", (cx - 20, 0, cz + 36), 90, {"length": 8, "width": 2.4, "height": 0.3}, "concrete_smooth", "manual pad", name)
    for (x, z) in [(-62, -62), (62, -62), (-62, 62), (62, 62)]:
        lamp(cx + x, cz + z)
    return {"box_kicker": (cx - 18, cz - 20), "box": (cx - 18, cz - 12.9), "stairs": (cx + 24, cz - 17.1), "rail": (cx - 36, cz + 14),
            "spine": (cx + 2, cz + 22), "spawn": (cx, cz - 52), "qp": (cx, cz - 58)}


# ------------------------------------------------------------------------------------------------
def make_challenges(k):
    city = "assets/scenes/city.json"
    P = k["plaza"]
    S = k["park"]
    M = k["mega"]
    D = k["downtown"]
    I = k["industrial"]
    R = k["roof"]
    C = k["comp"]
    Dt = k["ditch"]
    Sc = k["school"]
    Re = k["res"]

    def ch(**kw):
        kw.setdefault("map", city)
        challenges.append(kw)

    # trick challenges (score attack + trick lists)
    ch(id="plaza_warmup", name="Plaza Warm-Up", mode="trick", spawn="Street Plaza", time=90, targets=[6000, 15000, 30000],
       desc="Score as many points as you can in 90 seconds. Link ledges, stairs and manual pads into long combos.")
    ch(id="park_session", name="Park Session", mode="trick", spawn="Skatepark", time=120, targets=[10000, 25000, 50000],
       desc="Two minutes in the skatepark. The bowl and the quarters keep your speed up.")
    ch(id="whip_class", name="Whip Class", mode="trick", spawn="Skatepark", time=180, targets=[2000, 8000, 16000],
       tricks=["Tailwhip", "Barspin", "180", "Manual", "50-50"],
       desc="Land every trick on the list. Your score decides the medal once the list is done.")
    ch(id="dock_grinds", name="Loading Dock Grinds", mode="trick", spawn="Industrial", time=90, targets=[5000, 12000, 25000],
       tricks=["50-50", "Feeble", "Smith"], desc="Grind the loading docks and containers. Land a 50-50, a Feeble and a Smith.")
    # line challenges (gates, some need a grind / manual / air)
    ch(id="plaza_line", name="Plaza Line", mode="line", spawn="Street Plaza", targets=[40, 30, 22],
       gates=[gate((P["manual"][0], 0, P["manual"][1]), 2.2, 0, "manual"),
              gate((P["rail"][0], 0, P["rail"][1]), 2.4, 0, "grind"),
              gate((P["stairs_top"][0], 0, P["stairs_top"][1] + 3.8), 4.0, 0, "air"),
              gate((P["bowl"][0], 0, P["bowl"][1] + 8), 3.0, 0, "")],
       desc="Manual the west pad, grind the flat rail, jump the stairs and finish at the fountain bowl.")
    ch(id="downtown_line", name="Downtown Line", mode="line", spawn="Downtown", targets=[45, 34, 26],
       gates=[gate((D["manual"][0], 0, D["manual"][1]), 2.4, 90, "manual"),
              gate((D["rail"][0], 0, D["rail"][1]), 2.6, 90, "grind"),
              gate((D["terrace"][0] + 17.5, 0, D["terrace"][1]), 3.5, 90, ""),
              gate((D["stairs"][0], 0, D["stairs"][1] + 0.5), 4.5, 0, "air")],
       desc="Manual, grind the square rail, roll up the terrace bank and send the eight stair.")
    ch(id="rooftop_line", name="Rooftop Gaps", mode="line", spawn="Rooftops", targets=[50, 38, 30],
       gates=[gate((R["A"][0] + 6, R["A"][3], R["A"][1] - R["A"][4] * 0.5 + 0.5), 3.0, 0, "air"),
              gate((R["B"][0], R["B"][3], R["B"][1]), 4.0, 0, ""),
              gate((R["B"][0] - R["B"][2] * 0.5 - 1.0, R["B"][3] - 1.0, R["B"][1]), 3.5, 90, "air"),
              gate((R["C"][0], R["C"][3], R["C"][1] + 6), 4.0, 0, "")],
       desc="Kicker gap from roof to roof, then drop down to the lower roof on the west side.")
    ch(id="school_line", name="Recess Line", mode="line", spawn="School", targets=[40, 30, 24],
       gates=[gate((Sc["bar"][0], 0, Sc["bar"][1]), 2.4, 90, "grind"),
              gate((Sc["manual"][0], 0, Sc["manual"][1]), 2.4, 90, "manual"),
              gate((Sc["stairs"][0], 0, Sc["stairs"][1] + 0.5), 3.5, 0, "air")],
       desc="Grind the courtyard bar, manual the pad and jump the school stairs.")
    # best trick
    ch(id="mega_best", name="Mega Ramp Best Trick", mode="besttrick", spawn="Mega Park", time=120, targets=[1500, 5000, 12000],
       desc="Drop in, clear the gap and throw your best combo. Only the best one counts.")
    ch(id="comp_best", name="Contest Best Trick", mode="besttrick", spawn="Competition Park", time=120, targets=[2500, 7000, 14000],
       desc="The judges want one big combo on the contest course.")
    # time attacks
    ch(id="city_dash", name="City Dash", mode="timeattack", spawn="Street Plaza", targets=[100, 80, 66],
       gates=[gate((0, 0, -82), 5.0, 0), gate((0, 0, -140), 5.0, 0), gate((82, 0, -120), 5.0, 90), gate((164, 0, -110), 5.0, 90),
              gate((164, 0, -40), 5.0, 0), gate((120, 0, 30), 5.0, 90), gate((82, 0, 0), 5.0, 90), gate((0, 0, 40), 5.0, 90)],
       desc="Plaza to Downtown, through Industrial and the Skatepark and back. Push hard, respawns cost time.")
    ch(id="ditch_run", name="Ditch Run", mode="timeattack", spawn="Ditch", targets=[35, 27, 21],
       gates=[gate((Dt["kicker"][0] + 6, 0, Dt["kicker"][1]), 3.0, 90), gate((Dt["pipe"][0] + 8, 0, Dt["pipe"][1]), 3.0, 90),
              gate((Dt["east"][0] - 6, 0, Dt["east"][1]), 3.0, 90), gate((Dt["west"][0] + 30, 0, Dt["west"][1]), 3.0, 90)],
       desc="Down the ditch, off the quarter and back again.")
    ch(id="residential_loop", name="Suburb Sprint", mode="timeattack", spawn="Residential", targets=[45, 35, 28],
       gates=[gate((Re["street_e"][0], 0, Re["street_e"][1]), 4.0, 90), gate((-82, 0, 164), 5.0, 0), gate((-82, 0, 100), 5.0, 0),
              gate((-164, 0, 82), 5.0, 90), gate((Re["street_w"][0], 0, Re["street_w"][1]), 4.0, 90)],
       desc="Out of the suburb, up the avenue and back through the school corner.")


# ------------------------------------------------------------------------------------------------
def extras(rng):
    """second pass: fill the open space of every district with more lines and street furniture"""
    # Street Plaza: seating walls, extra ledges, a gap and corner banks
    cx, cz, n = 0.0, 0.0, "Street Plaza"
    for x in (-40, -28, 28, 40):
        add("wall", (cx + x, 0, cz - 56), 0, {"size": [8, 0.5, 0.45], "grindTop": True}, "concrete_smooth", "seat wall", n)
    add("slab", (cx - 44, 0, cz + 50), 0, {"size": [10, 0.6, 5], "grindEdges": True}, "concrete_smooth", "gap block a", n)
    add("slab", (cx - 44, 0, cz + 41), 0, {"size": [10, 0.6, 5], "grindEdges": True}, "concrete_smooth", "gap block b", n)
    for (x, z, yy) in [(-62, -62, rise_yaw(-1, -1)), (62, -62, rise_yaw(1, -1)), (62, 62, rise_yaw(1, 1))]:
        add("bank", (cx + x, 0, cz + z), yy, {"length": 3.0, "height": 1.0, "width": 6, "deck": 1.0}, "concrete_smooth", "corner bank", n)
    add("ledge", (cx + 50, 0, cz - 20), 90, {"length": 14, "width": 0.6, "height": 0.4}, "concrete_smooth", "long ledge", n)
    add("rail", (cx - 50, 0, cz - 20), 90, {"length": 12, "height": 0.5, "segments": 2}, "rail_black", "long rail", n)
    for i in range(6):
        add("trash_can", (cx - 30 + i * 12, 0, cz + 64), 0, {}, None, "trash can", n)
    # Skatepark: pyramid, euro gap, more rails, planters outside the fence
    cx, cz, n = PITCH, 0.0, "Skatepark"
    for yy in (0, 90, 180, 270):
        add("bank", (cx + 30 + 3.4 * math.cos(math.radians(yy)), 0, cz + 10 - 3.4 * math.sin(math.radians(yy))), yy + 180,
            {"length": 2.8, "height": 0.9, "width": 3.8, "deck": 0.0}, "concrete_park", "pyramid face", n)
    add("slab", (cx + 30, 0, cz + 10), 0, {"size": [1.2, 0.9, 1.2], "grindEdges": False}, "concrete_park", "pyramid top", n)
    add("bank", (cx - 20, 0, cz + 50), rise_yaw(1, 0), {"length": 3.0, "height": 1.1, "width": 6, "deck": 0.0}, "concrete_park", "euro bank", n)
    add("wall", (cx - 12, 0, cz + 50), 90, {"size": [6, 0.9, 0.4], "grindTop": True}, "concrete_wall", "euro wall", n)
    add("rail", (cx + 40, 0, cz - 52), 0, {"length": 8, "height": 0.35}, None, "low bar", n)
    add("ledge", (cx - 30, 0, cz - 30), 0, {"length": 7, "width": 0.6, "height": 0.4}, "concrete_smooth", "ledge", n)
    add("kicker", (cx + 44, 0, cz + 26), rise_yaw(0, -1), {"length": 2.0, "height": 0.6, "width": 2.0}, "plywood", "small kicker", n)
    for x in range(-60, 61, 20):
        tree(cx + x, cz + 74, rng, 7)
    # Downtown: bus stop, planters along the sidewalks, hydrants, newsstands
    cx, cz, n = 0.0, -PITCH, "Downtown"
    add("wall", (cx + 30, 0, cz + 58), 0, {"size": [8, 2.6, 0.2], "grindTop": False}, "glass", "bus stop wall", n)
    add("slab", (cx + 30, 2.6, cz + 59.2), 0, {"size": [8.4, 0.12, 2.6], "grindEdges": False}, "metal_plate", "bus stop roof", n)
    add("bench", (cx + 30, 0, cz + 59), 180, {"length": 3.0}, None, "bus bench", n)
    for x in (-48, -34, 34, 48):
        add("planter", (cx + x, 0, cz + 14), 90, {"size": [5, 0.55, 1.6]}, "concrete_smooth", "sidewalk planter", n)
    for x in (-40, 40):
        add("slab", (cx + x, 0, cz + 50), 0, {"size": [1.4, 1.4, 1.0], "grindEdges": True}, "painted_blue", "newsstand", n)
    add("ledge", (cx - 10, 0, cz + 56), 0, {"length": 16, "width": 0.7, "height": 0.45}, "concrete_smooth", "curb ledge", n)
    # School: extra stair set on the east wing, picnic ledge, tree lines
    cx, cz, n = -PITCH, 0.0, "School"
    add("slab", (cx + 50, 0, cz + 12), 0, {"size": [8, 0.85, 8], "grindEdges": True}, "concrete_smooth", "east deck", n)
    add("stairs", (cx + 50, 0, cz + 17.85), rise_yaw(0, 1), {"steps": 5, "rise": 0.17, "run": 0.34, "width": 5, "landingTop": 0.0, "handrail": True},
        "concrete_smooth", "east stairs", n)
    add("bank", (cx + 50, 0, cz + 4 - 1.7), rise_yaw(0, 1), {"length": 3.4, "height": 0.85, "width": 6, "deck": 0.0}, "concrete_smooth", "east bank", n)
    for x in range(-60, 61, 24):
        tree(cx + x, cz + 66, rng)
    add("wall", (cx - 30, 0, cz + 40), 0, {"size": [14, 0.6, 0.4], "grindTop": True}, "brick_red", "court wall", n)
    # Industrial: pallet stacks, more containers, a fence line with a gap
    cx, cz, n = PITCH, -PITCH, "Industrial"
    for i in range(5):
        add("slab", (cx - 56 + i * 2.4, 0, cz + 30), 0, {"size": [1.2, 0.15 * (1 + i % 3), 1.0], "grindEdges": True}, "wood_deck", "pallet stack", n)
    add("container", (cx + 40, 0, cz - 10), 90, {"long": True}, "container_blue", "container", n)
    add("container", (cx + 40, 2.59, cz - 10), 90, {"long": False}, "container_red", "stacked container", n)
    add("fence", (cx - 30, 0, cz + 62), 0, {"length": 40, "height": 2.0}, None, "yard fence", n)
    add("rail", (cx - 10, 0, cz + 10), 90, {"length": 10, "height": 0.6, "type": "square"}, "rail_yellow", "yellow bar", n)
    # Ditch: bridge over the channel, tree rows
    cx, cz, n = PITCH, PITCH, "Ditch"
    add("slab", (cx + 30, 2.4, cz), 0, {"size": [4, 0.25, 17], "grindEdges": True}, "concrete_worn", "ditch bridge", n)
    add("rail", (cx + 31.8, 2.65, cz), 90, {"length": 16, "height": 0.9}, None, "bridge rail", n)
    for x in range(-60, 61, 15):
        tree(cx + x, cz + rng.choice([-50, 55]), rng)
    # Residential: hedges, mailboxes, a second hidden kicker
    cx, cz, n = -PITCH, PITCH, "Residential"
    for x in (-52, -26, 0, 26, 52):
        for side in (-1, 1):
            add("sign", (cx + x + 3.5, 0, cz + side * 9.5), 0, {"height": 1.2}, "painted_white", "mailbox", n)
    for x in (-40, 40):
        add("planter", (cx + x, 0, cz + 60), 0, {"size": [12, 0.8, 1.4]}, "concrete_smooth", "hedge", n)
    add("kicker", (cx + 60, 0, cz - 55), rise_yaw(-1, 0), {"length": 2.2, "height": 0.7, "width": 2.0}, "plywood", "hidden kicker 2", n)
    # Competition Park: pyramid + hubba row
    cx, cz, n = 0.0, PITCH, "Competition Park"
    add("funbox", (cx + 30, 0, cz + 45), 0, {"rampLength": 2.4, "topLength": 3.0, "height": 0.9, "width": 6, "rail": True}, "concrete_smooth", "funbox", n)
    add("ledge", (cx - 44, 0, cz - 20), 90, {"length": 12, "width": 0.8, "height": 0.6}, "concrete_smooth", "big ledge", n)
    add("rail", (cx + 44, 0, cz - 2), 90, {"length": 12, "height": 0.5, "type": "square"}, "rail_black", "flat bar", n)


def main():
    rng = random.Random(20260924)
    ground_and_streets(rng)
    keys = {
        "plaza": street_plaza(rng), "park": skatepark(rng), "mega": mega_park(rng), "downtown": downtown(rng), "school": school(rng),
        "industrial": industrial(rng), "roof": rooftops(rng), "ditch": ditch(rng), "res": residential(rng), "comp": comp_park(rng),
    }
    extras(rng)
    make_challenges(keys)
    scene = {"version": 1, "name": "Scoot City", "environment": {"preset": "day"}, "entities": ents}
    out = os.path.join(ROOT, "assets", "scenes", "city.json")
    with open(out, "w") as f:
        f.write('{\n  "version": 1,\n  "name": "Scoot City",\n  "environment": { "preset": "day", "adjust": { "urbanReflection": 0.6 } },\n  "entities": [\n')
        f.write(",\n".join("    " + json.dumps(e, separators=(", ", ": ")) for e in ents))
        f.write("\n  ]\n}\n")
    # keep challenges of other maps (tools/spotgen.py adds the Street Spot ones)
    cpath = os.path.join(ROOT, "assets", "data", "challenges.json")
    others = []
    if os.path.exists(cpath):
        others = [c for c in json.load(open(cpath)).get("challenges", []) if c.get("map", "assets/scenes/city.json") != "assets/scenes/city.json"]
    with open(cpath, "w") as f:
        json.dump({"_comment": "Generated by tools/citygen.py + tools/spotgen.py. Timed targets are seconds, score targets points (bronze, silver, gold).",
                   "challenges": challenges + others}, f, indent=1)
    print(f"city: {len(ents)} entities -> {out}; {len(challenges)} challenges")


if __name__ == "__main__":
    main()
