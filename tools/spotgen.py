#!/usr/bin/env python3
"""Generate the Photoreal Street Spot (assets/scenes/street_spot.json) and its line challenge.

The Street Spot is the visual quality bar of the game and the main menu backdrop: one dense city
corner built around a single designed line

    bank up to the raised plaza -> flat rail -> manual pad -> 6 stair (handrails) -> ledge
    -> concrete quarter against the east wall -> landing back down

with a street, sidewalks, a parking lot, shopfront buildings, street furniture, parked cars, decals
(cracks, oil, tire marks, water stains, graffiti, stickers) and a far skyline layer.

Layout (X east, Z south, Y up). Street level (road, parking lot) is y = 0, pedestrian level
(sidewalks, lower plaza) is y = 0.15, the raised plaza is y = 1.17. The line runs along +X at z = 0.

Prefab conventions (see src/game/world/prefabs.cpp): origin at the footprint centre on the ground,
ramps rise towards local +X, widths run along local Z, stairs go down towards +X, shopfront facades
face local +Z. A yaw of t degrees turns local +X into (cos t, 0, -sin t) and local +Z into
(sin t, 0, cos t).

usage: python3 tools/spotgen.py
"""
import json
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = "assets/scenes/street_spot.json"
PED = 0.15           # sidewalk / lower plaza height
RISE = 0.17
STEPS = 6
TOP = PED + STEPS * RISE   # raised plaza height (1.17)

ents = []


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


def facing_yaw(dx, dz):
    """spawn yaw so the rider faces the world direction (dx, dz); yaw 0 faces -Z"""
    return math.degrees(math.atan2(-dx, -dz))


def front_yaw(dx, dz):
    """yaw that turns a prefab's local +Z (shopfront facade, wall decal) towards (dx, dz)"""
    return math.degrees(math.atan2(dx, dz))


def model(name, pos, yaw=0.0, collider="box", scale=1.0, exclude="_aged", include="", label=None):
    p = {"model": f"assets/models/props/{name}/{name}.gltf", "include": include, "exclude": exclude, "scale": scale, "collider": collider,
         "recenter": True}
    return add("model", pos, yaw, p, None, label or name)


def decal(mat, pos, size, yaw=0.0, wall=False, name=None):
    p = {"size": [round(size[0], 3), round(size[1], 3)], "orient": "wall" if wall else "ground"}
    if wall:
        p["offset"] = 0.012
    return add("decal", pos, yaw, p, mat, name or mat)


# ------------------------------------------------------------------------------------------------
def ground_and_street(rng):
    # base: street level asphalt everywhere, grass/dirt verge far out
    add("ground", (0, 0, 0), 0, {"size": [420, 0.4, 420], "divisions": 6}, "asphalt_dirty", "street level")
    add("ground", (0, -0.03, 0), 0, {"size": [1600, 0.4, 1600], "divisions": 1}, "asphalt_worn", "outskirts")
    # main street along X (two lanes, worn markings) and a side street along Z at the west end
    add("road", (0, 0.003, -22), 0, {"length": 236, "width": 10, "sidewalk": 0, "centerLine": True, "asphalt": True}, "road_damaged", "main street")
    add("road", (-112, 0.002, 60), 90, {"length": 150, "width": 9, "sidewalk": 0, "centerLine": True, "asphalt": True}, "asphalt_worn", "side street")
    # sidewalks: north side (curb faces the road at +Z), south side split by the parking lot driveway
    add("sidewalk", (0, 0, -29), 0, {"length": 236, "width": 4, "height": PED}, "sidewalk", "north sidewalk")
    add("sidewalk", (-89, 0, -15), 180, {"length": 22, "width": 4, "height": PED}, "sidewalk", "south sidewalk west")
    add("sidewalk", (31, 0, -15), 180, {"length": 162, "width": 4, "height": PED}, "sidewalk", "south sidewalk east")
    add("crosswalk", (-60, 0.0, -22), 90, {"length": 9.4, "width": 3.2}, None, "crosswalk")
    add("crosswalk", (58, 0.0, -22), 90, {"length": 9.4, "width": 3.2}, None, "crosswalk east")
    # lane arrows, road patches, cracks, tire marks
    for x in (-40, 20, 80):
        decal("decal_arrow", (x, 0, -19.6), (1.2, 4.5), 90)
        decal("decal_arrow", (x - 12, 0, -24.4), (1.2, 4.5), -90)
    for i in range(14):
        x = rng.uniform(-110, 110)
        z = rng.uniform(-26.5, -17.5)
        k = rng.random()
        if k < 0.3:
            decal("decal_patch", (x, 0, z), (rng.uniform(1.5, 3.5), rng.uniform(1.0, 2.5)), rng.uniform(-15, 15))
        elif k < 0.6:
            decal(f"decal_crack_{rng.randint(1, 3)}", (x, 0, z), (rng.uniform(2, 4), rng.uniform(2, 4)), rng.uniform(0, 360))
        else:
            decal("decal_tire", (x, 0, z), (rng.uniform(4, 9), 0.9), rng.uniform(-8, 8))
    for x in (-95, -30, 35, 95):
        decal(f"decal_oil_{rng.randint(1, 2)}", (x + rng.uniform(-3, 3), 0, -22 + rng.uniform(-3, 3)), (1.6, 1.6), rng.uniform(0, 360))
    # storm drains at the curbs + manholes on the road
    for x in (-72, -18, 40, 96):
        decal("decal_drain", (x, 0, -17.45), (0.9, 0.5), 0)
        decal(f"decal_water_{rng.randint(1, 2)}", (x + 0.4, 0, -18.4), (2.2, 1.6), rng.uniform(0, 360))
    for x in (-48, 12, 74):
        model("water_manhole_cover", (x, 0.0, -22 + rng.uniform(-2.5, 2.5)), rng.uniform(0, 360), collider="none")


def parking_lot(rng):
    # lot at street level between the side street and the lower plaza, entered from the main street
    decal("decal_grime", (-81, 0, -2), (34, 26), 0, name="lot grime")
    for zc, face in ((-9.6, 1), (8.2, -1)):
        add("parking_lines", (-81.4, 0, zc), 0, {"bays": 11, "bayWidth": 2.6, "bayLength": 5.2}, None, "parking bays")
        for b in range(11):
            x = -81.4 - 5.5 * 2.6 + 1.3 + b * 2.6
            r = rng.random()
            if r < 0.62:
                paint = rng.choice(["white", "silver", "grey", "black", "blue", "red", "beige", "green", "white", "silver"])
                typ = rng.choice(["sedan", "sedan", "hatch", "hatch", "van"])
                add("car", (x + rng.uniform(-0.12, 0.12), 0, zc + face * rng.uniform(-0.1, 0.25)), 90 * face + rng.uniform(-3, 3),
                    {"type": typ, "paint": paint}, None, f"parked {typ}")
                decal(f"decal_oil_{rng.randint(1, 2)}", (x, 0, zc + face * 0.8), (1.3, 1.3), rng.uniform(0, 360))
            elif r < 0.68:
                model("covered_car", (x, 0, zc), 90 * face + 90, label="covered car")
            elif r < 0.8:
                decal(f"decal_oil_{rng.randint(1, 2)}", (x, 0, zc), (1.8, 1.8), rng.uniform(0, 360))
        # wheel stops / concrete barriers at the back of the bays
    for x in range(-96, -66, 3):
        add("slab", (x + 1.5, 0, -13.6), 0, {"size": [1.8, 0.12, 0.25]}, "concrete_worn", "wheel stop")
    # lot edges: barriers, fence, trees, a pay machine
    for z in (-12, -6, 0, 6, 12):
        model("concrete_road_barrier", (-99.3, 0, z), 90, label="lot barrier")
    add("fence", (-81, 0, 13.6), 0, {"length": 36, "height": 1.8}, None, "lot fence")
    add("parking_meter", (-66.5, PED, -12.2), 180)
    add("traffic_sign", (-67.2, PED, -16.2), 180, {"sign": "stop", "height": 2.4})
    add("traffic_sign", (-98.5, PED, -16.2), 180, {"sign": "noparking", "height": 2.5})
    model("utility_box_02", (-66.8, PED, 11.2), 90)
    model("trashbag", (-65.7, PED, 12.4), rng.uniform(0, 360), collider="none")
    model("trashbag", (-66.3, PED, 12.9), rng.uniform(0, 360), collider="none")
    model("cardboard_box_01", (-65.4, PED, 11.6), rng.uniform(0, 360))
    model("old_tyre", (-98.4, 0, 10.8), rng.uniform(0, 360))
    for z in (-10, 10):
        model("weed_plant_02", (-99.6, 0, z + rng.uniform(-1, 1)), rng.uniform(0, 360), collider="none")


def plaza(rng):
    """lower plaza (pedestrian level) + the raised plaza carrying the first half of the line"""
    add("slab", (-11, 0, 0), 0, {"size": [102, PED, 26], "grindEdges": False}, "sidewalk", "lower plaza")
    # raised plaza: concrete retaining walls + paver top with grindable edges
    add("slab", (-17, PED, 0), 0, {"size": [38, TOP - PED - 0.02, 18]}, "concrete_wall", "raised plaza base")
    add("slab", (-17, TOP - 0.02, 0), 0, {"size": [38, 0.02, 18], "grindEdges": True}, "pavers", "raised plaza top")
    # line: bank -> rail -> manual pad -> stairs -> ledge -> quarter
    add("bank", (-37.7, PED, 0), 0, {"length": 3.4, "height": TOP - PED, "width": 7, "deck": 0.0, "grindEdge": False}, "concrete_smooth", "entry bank")
    add("rail", (-26, TOP, 0), 0, {"length": 7, "height": 0.42, "type": "round"}, "rail_steel", "flat rail")
    add("manual_pad", (-11, TOP, 0), 0, {"length": 6, "width": 2.2, "height": 0.2}, "concrete_smooth", "manual pad")
    add("stairs", (1.99, PED, 0), 0, {"steps": STEPS, "rise": RISE, "run": 0.33, "width": 7, "landingTop": 2.0, "handrail": True,
                                      "handrailBoth": True, "railHeight": 0.86}, "concrete_worn", "six stair")
    add("ledge", (15.5, PED, 1.1), 0, {"length": 7.5, "width": 0.6, "height": 0.46, "bothEdges": True}, "concrete_smooth", "granite ledge")
    add("quarter_pipe", (36.9, PED, 0), 0, {"height": 1.7, "radius": 2.3, "width": 9, "deck": 1.0, "coping": True}, "concrete_park", "wall quarter")
    # secondary spots: long ledge along the south walkway, hubba beside the stairs, bench ledge
    add("ledge", (-17, PED, 11.2), 0, {"length": 10, "width": 0.5, "height": 0.42, "bothEdges": False}, "concrete_smooth", "walkway ledge")
    add("rail", (24, PED, -6.5), 0, {"length": 6, "height": 0.36, "type": "square"}, "rail_black", "square rail")
    add("manual_pad", (-52, PED, -6), 0, {"length": 4, "width": 1.6, "height": 0.18}, "concrete_smooth", "lot pad")
    # wear on the obstacles and plaza: wax, grime, cracks, water stains, tire marks
    decal("decal_grime", (-26, TOP, 0), (8, 1.4), 0, name="rail grime")
    decal("decal_tire", (-11, TOP + 0.2, 0), (5.5, 0.8), 0, name="pad marks")
    decal("decal_tire", (8.5, PED, 0.2), (6, 1.0), 2, name="stair landing marks")
    decal("decal_grime", (15.5, PED, 1.1), (8.2, 1.6), 0, name="ledge grime")
    decal("decal_grime", (33.5, PED, 0), (6, 9), 0, name="quarter grime")
    for i in range(22):
        if i < 11:
            x, z, y = rng.uniform(-35, 1), rng.uniform(-8.5, 8.5), TOP
        else:
            x, z, y = rng.uniform(-60, 38), rng.uniform(-12.5, 12.5), PED
            if -36 < x < 2 and -9 < z < 9:
                continue
        k = rng.random()
        if k < 0.45:
            decal(f"decal_crack_{rng.randint(1, 3)}", (x, y, z), (rng.uniform(1.5, 3.2), rng.uniform(1.5, 3.2)), rng.uniform(0, 360))
        elif k < 0.75:
            decal(f"decal_water_{rng.randint(1, 2)}", (x, y, z), (rng.uniform(1.5, 3.0), rng.uniform(1.2, 2.4)), rng.uniform(0, 360))
        else:
            decal("decal_grime", (x, y, z), (rng.uniform(2, 4), rng.uniform(2, 4)), rng.uniform(0, 360))
    # plaza furniture: planters with trees and shrubs at the corners of the raised plaza, benches, bins
    for x, z in ((-32, -6.8), (-32, 6.8), (-3, -6.8), (-3, 6.8)):
        add("planter", (x, TOP, z), 0, {"size": [3.2, 0.55, 2.2]}, "concrete_smooth", "plaza planter")
        add("tree", (x, TOP + 0.5, z), rng.uniform(0, 360), {"height": rng.uniform(6.5, 8.5), "crown": rng.uniform(2.4, 3.0), "seed": rng.randint(1, 999)})
    for x in (-18.5, -4.5):
        model("painted_wooden_bench", (x, TOP, -7.9), 0)
    model("metal_trash_can", (-20.5, TOP, -8.1), rng.uniform(0, 360))
    model("potted_plant_02", (-34.8, TOP, 8.2), 0)
    # lower plaza: trees in grates, seating, bike racks, lamps, bins
    for x in (-54, -44, 8, 20, 30):
        add("tree", (x, PED, -10.8), rng.uniform(0, 360), {"height": rng.uniform(7, 9), "crown": rng.uniform(2.4, 3.2), "seed": rng.randint(1, 999)})
        decal("decal_drain", (x, PED, -10.8), (1.3, 1.3), 0, name="tree grate")
    model("modular_street_seating", (26, PED, 8.5), 180)
    model("modular_street_seating", (-50, PED, 9.0), 180)
    add("bike_rack", (10, PED, 10.8), 0, {"length": 3.6, "stands": 5}, "metal_galvanized")
    add("bike_rack", (-56, PED, 5.5), 90, {"length": 2.7, "stands": 4}, "metal_galvanized")
    for x, z in ((-58, -8), (-34, -11.2), (4, -11.2), (34, -11.2), (-8, 11.4)):
        model("street_lamp_01", (x, PED, z), rng.choice([0, 90, 180, 270]), label="plaza lamp")
        add("light", (x, PED + 4.2, z), 0, {"type": "point", "intensity": 55.0, "radius": 16.0, "nightOnly": True})
    for x, z in ((-46, -11.6), (12, -11.6), (37, 11.8)):
        model("metal_trash_can", (x, PED, z), rng.uniform(0, 360))
    for z in (-12, -8, -4, 4, 8, 12):
        add("bollard", (-61.6, PED, z), 0)
    model("fire_hydrant", (-62.8, PED, -15.8), rng.uniform(0, 360))
    model("fire_hydrant", (46.5, PED, -28.5), rng.uniform(0, 360))
    # weeds in cracks at the walls of the raised plaza
    for i in range(8):
        x = rng.uniform(-35.5, 1.5)
        z = rng.choice([-9.15, 9.15])
        model(rng.choice(["weed_plant_02", "nettle_plant"]), (x, PED, z), rng.uniform(0, 360), collider="none", scale=rng.uniform(0.6, 1.0))
    # graffiti + stains on the raised plaza retaining walls
    decal("decal_graffiti_2", (-24, PED, -9.0), (3.4, 0.95), 180, wall=True)
    decal("decal_water_1", (-9, PED, -9.0), (2.2, 1.0), 180, wall=True)
    decal("decal_graffiti_4", (-14, PED, 9.0), (2.6, 0.95), 0, wall=True)


def shop(x, zface, width, height, depth, mat, shopname, seed, face, awning=None, rng=None):
    """shopfront whose facade sits at zface and faces +Z (face=1) or -Z (face=-1)"""
    zc = zface - face * depth * 0.5
    yaw = 0 if face > 0 else 180
    add("shopfront", (x, PED, zc), yaw, {"size": [width, height, depth], "groundFloor": 4.2, "floorHeight": 3.3, "shop": shopname, "seed": seed},
        mat, f"shop {shopname}")
    if awning:
        add("awning", (x, PED + 3.75, zface), yaw, {"width": min(width - 3.0, 6.5), "depth": 1.3, "drop": 0.45, "valance": 0.25}, awning, "awning")
    return zc


def buildings(rng):
    mats = ["brick_painted", "plaster_rough", "brick_red", "facade_concrete", "plaster_beige", "facade_tiles", "brick_painted", "plaster_white"]
    shops = ["shop_coffee", "shop_laundry", "shop_pizza", "shop_supply", "shop_pharmacy", "shop_market", "shop_barber", "shop_records"]
    awn = ["awning_green", "awning_red", "awning_blue", "awning_cream", "awning_black", None, None]
    # north side of the main street (facades at z = -31 facing the road)
    x = -116.0
    i = 0
    while x < 112:
        w = rng.choice([12, 14, 16, 18, 20])
        h = rng.choice([11.5, 14.8, 14.8, 18.1, 21.4])
        shop(x + w * 0.5, -31, w - 0.1, h, 14, mats[i % len(mats)], shops[i % len(shops)], 100 + i, 1, rng.choice(awn))
        # AC units + stains on the upper facade
        for k in range(rng.randint(0, 2)):
            model("exterior_aircon_unit", (x + rng.uniform(2, w - 2), PED + 4.6 + 3.3 * rng.randint(0, 2), -30.75), 0, collider="none")
        decal(f"decal_water_{rng.randint(1, 2)}", (x + rng.uniform(2, w - 2), PED + h - 3.5, -30.97), (1.4, 3.2), 0, wall=True)
        x += w
        i += 1
    # south side behind the plaza (facades at z = 13 facing the plaza, -Z)
    x = -62.0
    for w, h, m, s, a in ((18, 14.8, "plaster_beige", "shop_records", "awning_black"), (22, 21.4, "brick_red", "shop_coffee", "awning_green"),
                          (20, 11.5, "facade_concrete", "shop_market", None), (20, 18.1, "brick_painted", "shop_barber", "awning_blue"),
                          (22, 14.8, "plaster_rough", "shop_pizza", "awning_red")):
        shop(x + w * 0.5, 13, w - 0.1, h, 14, m, s, 200 + int(x), -1, a)
        model("potted_plant_02", (x + 2.2, PED, 12.6), 0)
        x += w
    # east block: corner building whose plain west wall backs the quarter pipe
    add("shopfront", (48, PED, 0), 180, {"size": [16, 18.1, 26], "groundFloor": 4.2, "floorHeight": 3.3, "shop": "shop_supply", "seed": 301},
        "facade_concrete", "east corner")
    add("shopfront", (68, PED, -5), 180, {"size": [24, 14.8, 16], "groundFloor": 4.2, "floorHeight": 3.3, "shop": "shop_laundry", "seed": 302},
        "brick_painted", "east shop")
    add("shopfront", (96, PED, -5), 180, {"size": [32, 21.4, 16], "groundFloor": 4.2, "floorHeight": 3.3, "shop": "shop_pharmacy", "seed": 303},
        "plaster_white", "east shop 2")
    # the quarter wall: graffiti, stickers, grime, rollershutter door further along
    decal("decal_graffiti_1", (39.98, PED + 1.9, -1.5), (5.2, 1.6), -90, wall=True)
    decal("decal_graffiti_3", (39.98, PED + 2.1, 5.6), (3.4, 1.1), -90, wall=True)
    decal("decal_grime", (39.97, PED, 0), (14, 1.8), -90, wall=True)
    decal("decal_water_2", (39.97, PED + 7.5, 3.0), (2.0, 5.0), -90, wall=True)
    model("rollershutter_door", (39.7, PED, -9.2), -90, collider="none")
    model("exterior_aircon_unit", (39.6, PED + 5.4, 8.4), -90, collider="none")
    model("exterior_aircon_unit", (39.6, PED + 8.7, -4.0), -90, collider="none")
    model("power_box_01", (39.5, PED, 10.6), -90)
    decal("decal_sticker_1", (39.96, PED + 1.4, 10.6), (0.35, 0.35), -90, wall=True)
    # west of the parking lot, across the side street
    for zc, w, h, m, s in ((-5, 22, 14.8, "brick_red", "shop_pizza"), (20, 24, 18.1, "plaster_beige", "shop_laundry")):
        add("shopfront", (-126, PED, zc), -90, {"size": [w, h, 14], "groundFloor": 4.2, "floorHeight": 3.3, "shop": s, "seed": 400 + zc},
            m, "west shop")
    add("sidewalk", (-119, 0, 20), 90, {"length": 70, "width": 4, "height": PED}, "sidewalk", "west sidewalk")


def street_furniture(rng):
    # street lamps both sides every 24 m (lights at night)
    for x in range(-108, 112, 24):
        for z, yaw in ((-30.4, 0), (-13.6, 180)):
            if z > -20 and -70 < x < -62:
                continue
            add("street_lamp", (x, PED, z), yaw + 90, {"height": 7.0})
    # utility poles + cables along the north sidewalk
    poles = list(range(-112, 116, 32))
    for x in poles:
        add("utility_pole", (x, PED, -30.6), 90, {"height": 9.0})
        decal(f"decal_sticker_{rng.randint(1, 2)}", (x, PED + 1.5, -30.44), (0.22, 0.28), 0, wall=True, name="pole sticker")
    for a, b in zip(poles, poles[1:]):
        for dz in (-1.1, -0.4, 0.4, 1.1):
            add("cable", ((a + b) * 0.5, PED + 8.6, -30.6 - dz), 0, {"length": b - a, "sag": rng.uniform(0.45, 0.8), "radius": 0.011}, None, "power line")
    # parking meters along the north curb, signs, hydrants, bins, utility boxes
    for x in range(-100, 104, 9):
        if rng.random() < 0.7:
            add("parking_meter", (x + 0.3, PED, -27.5), 0)
    add("traffic_sign", (-61.6, PED, -27.4), 0, {"sign": "street_1", "height": 3.0})
    add("traffic_sign", (-55.5, PED, -16.6), 180, {"sign": "speed", "height": 2.4})
    add("traffic_sign", (5.5, PED, -27.4), 0, {"sign": "noparking", "height": 2.4})
    add("traffic_sign", (47.4, PED, -16.6), 180, {"sign": "oneway", "height": 2.8})
    add("traffic_sign", (57.4, PED, -27.4), 0, {"sign": "street_2", "height": 3.0})
    for x in (-86, -20, 30, 84):
        model("fire_hydrant", (x, PED, -27.8), rng.uniform(0, 360))
    for x in (-74, -8, 44, 92):
        model("metal_trash_can", (x, PED, -30.2), rng.uniform(0, 360))
    for x in (-38, 64):
        model(rng.choice(["utility_box_01", "utility_box_02"]), (x, PED, -30.4), 0)
    for x in (-66, 18):
        add("bike_rack", (x, PED, -28.0), 0, {"length": 2.7, "stands": 4}, "metal_galvanized")
    # parked cars along the north curb
    x = -104.0
    while x < 104:
        x += rng.uniform(5.2, 9.0)
        if rng.random() < 0.72 and not (-64 < x < -56) and not (54 < x < 62):
            paint = rng.choice(["white", "silver", "grey", "black", "blue", "red", "beige", "green", "yellow"])
            typ = rng.choice(["sedan", "sedan", "hatch", "hatch", "van"])
            add("car", (x, 0, -25.9 + rng.uniform(-0.1, 0.1)), 180 + rng.uniform(-1.5, 1.5), {"type": typ, "paint": paint}, None, f"parked {typ}")
    # sidewalk wear
    for i in range(18):
        x = rng.uniform(-110, 110)
        z = rng.choice([rng.uniform(-30.8, -27.4), rng.uniform(-16.8, -13.4)])
        if z > -20 and -80 < x < -62:
            continue
        k = rng.random()
        mat = f"decal_crack_{rng.randint(1, 3)}" if k < 0.5 else (f"decal_water_{rng.randint(1, 2)}" if k < 0.8 else "decal_grime")
        decal(mat, (x, PED, z), (rng.uniform(1.2, 2.6), rng.uniform(1.2, 2.4)), rng.uniform(0, 360))
    # trash + crates at the east alley
    for p in ((41.2, 12.2), (41.8, 12.6), (42.6, 12.0)):
        model("trashbag", (p[0], PED, p[1]), rng.uniform(0, 360), collider="none")
    model("wooden_crate_01", (43.6, PED, 12.2), rng.uniform(0, 360))
    model("plastic_crate_01", (44.4, PED, 12.4), rng.uniform(0, 360))


def skyline(rng):
    """far layer: taller blocks behind the street rows, closing every view"""
    for z0, face in ((-66, 1), (52, -1)):
        x = -170.0
        while x < 170:
            w = rng.uniform(22, 36)
            h = rng.choice([24, 28, 32, 38, 45, 52])
            add("building", (x + w * 0.5, 0, z0 + rng.uniform(-6, 6)), 0 if face > 0 else 180,
                {"size": [round(w - 1, 1), h, 22], "floorHeight": 3.3, "windowSpacing": 3.0, "shops": False, "parapet": True, "litWindows": True},
                rng.choice(["facade_concrete", "brick_red", "plaster_beige", "facade_tiles", "concrete_wall"]), "far block")
            x += w + rng.uniform(2, 8)
    # close both ends of the main street and the side street with blocks (no empty horizon)
    for xs in (-1, 1):
        z = -100.0
        while z < 100:
            w = rng.uniform(20, 32)
            if abs(z + w * 0.5 + 22) < 8:  # keep the street axis open a little further, then close it
                add("building", (xs * 232, 0, z + w * 0.5), 90 * xs,
                    {"size": [round(w - 1, 1), rng.choice([30, 36, 42]), 24], "floorHeight": 3.3, "windowSpacing": 3.0, "shops": True,
                     "parapet": True, "litWindows": True}, rng.choice(["brick_red", "plaster_beige", "facade_concrete"]), "street end")
            else:
                add("building", (xs * rng.uniform(196, 206), 0, z + w * 0.5), 90 * xs,
                    {"size": [round(w - 1, 1), rng.choice([22, 26, 32, 40]), 24], "floorHeight": 3.3, "windowSpacing": 3.0, "shops": True,
                     "parapet": True, "litWindows": True}, rng.choice(["brick_red", "plaster_beige", "facade_concrete", "facade_tiles"]), "far end block")
            z += w + rng.uniform(1, 4)
    for i in range(7):
        a = rng.uniform(-2.6, -0.5) if i < 4 else rng.uniform(0.6, 2.6)
        r = rng.uniform(180, 260)
        add("building", (math.cos(a) * r, 0, math.sin(a) * r), rng.uniform(0, 90),
            {"size": [rng.uniform(26, 40), rng.uniform(60, 110), rng.uniform(26, 40)], "floorHeight": 3.6, "windowSpacing": 2.6, "shops": False,
             "parapet": True, "litWindows": True}, rng.choice(["facade_concrete", "facade_tiles", "concrete_wall"]), "tower")


def gameplay():
    add("zone", (-11, 0, -8), 0, {"halfExtents": [120, 15, 60], "area": "Street Spot", "kind": "area"}, None, "street spot area")
    add("spawn", (-58, PED, 0.0), facing_yaw(1, 0), {"label": "Street Spot", "default": True}, None, "line start")
    add("spawn", (-8.5, TOP, 3.6), facing_yaw(1, -0.35), {"label": "Menu", "default": False}, None, "menu idle")
    add("spawn", (20, PED, -4), facing_yaw(-1, 0), {"label": "Lower Plaza", "default": False}, None, "lower plaza")
    add("audio", (-10, 2, -20), 0, {"sound": "ambience_city", "volume": 0.7, "minDistance": 8.0, "maxDistance": 90.0})


def challenge():
    def gate(pos, radius=3.0, yaw=0.0, require=""):
        g = {"pos": [round(pos[0], 2), round(pos[1], 2), round(pos[2], 2)], "radius": radius, "yaw": round(yaw, 1)}
        if require:
            g["require"] = require
        return g

    return [
        {"id": "spot_line", "name": "The Street Spot Line", "mode": "line", "map": SCENE, "spawn": "Street Spot", "targets": [45, 32, 24],
         "gates": [gate((-33.5, TOP, 0), 3.2, 90), gate((-26, TOP, 0), 2.4, 90, "grind"), gate((-11, TOP, 0), 2.4, 90, "manual"),
                   gate((3.2, PED, 0), 4.0, 90, "air"), gate((15.5, PED, 1.1), 2.4, 90, "grind"), gate((35.6, PED + 0.8, 0), 3.8, 90, "air"),
                   gate((24, PED, 0), 4.0, 90)],
         "desc": "Bank up to the plaza, grind the rail, manual the pad, send the six stair, grind the ledge and air the wall quarter."},
        {"id": "spot_session", "name": "Street Spot Session", "mode": "trick", "map": SCENE, "spawn": "Street Spot", "time": 120,
         "targets": [8000, 20000, 40000], "desc": "Two minutes at the spot. Every obstacle counts, link them into long combos."},
    ]


def main():
    rng = random.Random(21)
    ground_and_street(rng)
    parking_lot(rng)
    plaza(rng)
    buildings(rng)
    street_furniture(rng)
    skyline(rng)
    gameplay()
    out = os.path.join(ROOT, SCENE)
    with open(out, "w") as f:
        f.write('{\n  "version": 1,\n  "name": "Street Spot",\n  "environment": { "preset": "day", "adjust": { "rotationOffset": 3.14, "urbanReflection": 0.9 } },\n  "entities": [\n')
        f.write(",\n".join("    " + json.dumps(e, separators=(", ", ": ")) for e in ents))
        f.write("\n  ]\n}\n")
    # merge the spot challenges into the shared challenge list
    cpath = os.path.join(ROOT, "assets", "data", "challenges.json")
    data = json.load(open(cpath)) if os.path.exists(cpath) else {"challenges": []}
    data["challenges"] = [c for c in data["challenges"] if c.get("map") != SCENE] + challenge()
    with open(cpath, "w") as f:
        json.dump(data, f, indent=1)
    print(f"street spot: {len(ents)} entities -> {out}")


if __name__ == "__main__":
    main()
