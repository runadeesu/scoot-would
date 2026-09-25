#!/usr/bin/env python3
"""Download the CC0 texture sets and HDRIs used by scoot would from Poly Haven, and the
UI fonts (Barlow family, SIL Open Font License 1.1) from the Google Fonts repository.

All Poly Haven assets are CC0 (public domain): https://polyhaven.com/license
The downloaded files are committed to assets/ so builds do not need network access;
run this script again only to refresh or add assets.

usage: python3 tools/fetch_assets.py [--res 1k]
"""
import argparse
import json
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX_DIR = os.path.join(ROOT, "assets", "textures")
HDRI_DIR = os.path.join(ROOT, "assets", "hdri")
FONT_DIR = os.path.join(ROOT, "assets", "fonts")
FONT_BASE = "https://raw.githubusercontent.com/google/fonts/main/ofl"
FONTS = [
    ("barlow", "Barlow-Regular.ttf"),
    ("barlow", "Barlow-SemiBold.ttf"),
    ("barlow", "Barlow-Bold.ttf"),
    ("barlowcondensed", "BarlowCondensed-ExtraBoldItalic.ttf"),
    ("barlow", "OFL.txt"),
]

# poly haven id -> local short name
TEXTURES = {
    "concrete_floor_02": "concrete_smooth",
    "brushed_concrete": "concrete_park",
    "concrete_floor_worn_001": "concrete_worn",
    "concrete_wall_008": "concrete_wall",
    "asphalt_02": "asphalt",
    "pavement_02": "pavement",
    "large_grey_tiles": "plaza_tiles",
    "plywood": "plywood",
    "wood_floor_deck": "wood_deck",
    "metal_plate": "metal_plate",
    "green_metal_rust": "metal_rust",
    "corrugated_iron": "corrugated",
    "painted_metal_shutter": "shutter",
    "red_brick_03": "brick_red",
    "beige_wall_001": "plaster_beige",
    "white_plaster_02": "plaster_white",
    "factory_wall": "factory_wall",
    "grey_roof_01": "roof_grey",
    "leafy_grass": "grass",
    "dirt_floor": "dirt",
    "bark_brown_02": "bark",
    "rubber_tiles": "rubber",
    "gravel_concrete": "gravel_concrete",
    # photoreal street pass
    "road_damaged": "road_damaged",
    "asphalt_03": "asphalt_dirty",
    "worn_asphalt": "asphalt_worn",
    "cracked_concrete": "concrete_cracked",
    "concrete_pavement": "sidewalk",
    "concrete_floor_damaged_01": "concrete_damaged",
    "rectangular_facade_tiles": "facade_tiles",
    "concrete_slab_wall": "facade_concrete",
    "painted_worn_brick": "brick_painted",
    "red_brick_plaster_patch_02": "brick_patch",
    "white_rough_plaster": "plaster_rough",
    "herringbone_pavement_03": "pavers",
    "cotton_jersey": "fabric_jersey",
    "denim_fabric": "fabric_denim",
    "knitted_fleece": "fabric_fleece",
    "fabric_leather_02": "fabric_leather",
    "rusty_painted_metal": "metal_painted_rusty",
    "painted_concrete": "concrete_painted",
    # scooter shop interior
    "laminate_floor_02": "wood_laminate",
}
MAPS = {"Diffuse": "diff", "nor_gl": "nrm", "arm": "arm"}

# CC0 glTF props (Poly Haven, mostly the "hidden alley" collection) -> assets/models/props/<name>/
MODELS = [
    "fire_hydrant", "metal_trash_can", "utility_box_01", "utility_box_02", "concrete_road_barrier", "street_lamp_01",
    "water_manhole_cover", "exterior_aircon_unit", "power_box_01", "trashbag", "old_tyre", "wooden_crate_01", "plastic_crate_01",
    "cardboard_box_01", "planter_box_01", "shrub_02", "shrub_04", "weed_plant_02", "painted_wooden_bench", "modular_street_seating",
    "rollershutter_door", "covered_car", "potted_plant_02", "nettle_plant",
    # scooter shop interior
    "boombox", "modern_arm_chair_01", "modern_ceiling_lamp_01", "pachira_aquatica_01", "steel_frame_shelves_01",
    "wooden_display_shelves_01", "wall_clock", "bar_chair_round_01",
]
MODEL_DIR = os.path.join(ROOT, "assets", "models", "props")

HDRIS = {
    "kloofendal_48d_partly_cloudy_puresky": ("day", "2k"),
    "industrial_sunset_puresky": ("sunset", "2k"),
    "kloofendal_43d_clear_puresky": ("clear", "1k"),
    "overcast_soil_puresky": ("overcast", "1k"),
    "moonless_golf": ("night", "1k"),
    "gear_store": ("shop", "2k"),
}


UA = {"User-Agent": "scoot-would-asset-fetch/1.0 (+https://polyhaven.com)"}


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def download(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return False
    tmp = path + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
    os.replace(tmp, path)
    return True


def prune_gltf(path):
    """drop KHR_materials_variants (alternate 'aged' looks) and every image nothing references any more"""
    with open(path) as f:
        g = json.load(f)
    ext = g.get("extensions", {})
    ext.pop("KHR_materials_variants", None)
    if not ext:
        g.pop("extensions", None)
    for key in ("extensionsUsed", "extensionsRequired"):
        if key in g:
            g[key] = [e for e in g[key] if e != "KHR_materials_variants"]
            if not g[key]:
                g.pop(key)
    for m in g.get("meshes", []):
        for p in m.get("primitives", []):
            p.get("extensions", {}).pop("KHR_materials_variants", None)
            if "extensions" in p and not p["extensions"]:
                p.pop("extensions")
    used_mats = {p["material"] for m in g.get("meshes", []) for p in m.get("primitives", []) if "material" in p}
    used_tex = set()
    for i, m in enumerate(g.get("materials", [])):
        if i not in used_mats:
            continue
        def walk(o):
            if isinstance(o, dict):
                if "index" in o and isinstance(o["index"], int) and ("texCoord" in o or "scale" in o or "strength" in o or len(o) <= 3):
                    used_tex.add(o["index"])
                for v in o.values():
                    walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)
        walk(m)
    used_img = {g["textures"][t]["source"] for t in used_tex if t < len(g.get("textures", []))}
    removed = []
    for i, img in enumerate(g.get("images", [])):
        if i not in used_img and "uri" in img:
            removed.append(img["uri"])
    with open(path, "w") as f:
        json.dump(g, f)
    return removed


def fetch_models():
    for name in MODELS:
        files = get_json(f"https://api.polyhaven.com/files/{name}")
        entry = files["gltf"]["1k"]["gltf"]
        out_dir = os.path.join(MODEL_DIR, name)
        os.makedirs(out_dir, exist_ok=True)
        gltf_path = os.path.join(out_dir, f"{name}.gltf")
        fresh = download(entry["url"], gltf_path)
        for rel, inc in entry["include"].items():
            dst = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst) and fresh:
                download(inc["url"], dst)
        if fresh:
            for rel in prune_gltf(gltf_path):
                p = os.path.join(out_dir, rel)
                if os.path.exists(p):
                    os.remove(p)
            print(f"  + {name}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default="1k")
    args = ap.parse_args()
    os.makedirs(TEX_DIR, exist_ok=True)
    os.makedirs(HDRI_DIR, exist_ok=True)
    manifest = []
    for pid, name in TEXTURES.items():
        files = get_json(f"https://api.polyhaven.com/files/{pid}")
        for key, suffix in MAPS.items():
            entry = files.get(key, {}).get(args.res, {}).get("jpg")
            if not entry:
                print(f"  ! {pid}: no {key} {args.res} jpg", file=sys.stderr)
                continue
            out = os.path.join(TEX_DIR, f"{name}_{suffix}.jpg")
            if download(entry["url"], out):
                print(f"  + {out}")
        manifest.append(f"| textures/{name}_* | https://polyhaven.com/a/{pid} | CC0 |")
    for pid, (name, res) in HDRIS.items():
        files = get_json(f"https://api.polyhaven.com/files/{pid}")
        entry = files["hdri"][res]["hdr"]
        out = os.path.join(HDRI_DIR, f"{name}.hdr")
        if download(entry["url"], out):
            print(f"  + {out}")
        manifest.append(f"| hdri/{name}.hdr | https://polyhaven.com/a/{pid} | CC0 |")
    fetch_models()
    for name in MODELS:
        manifest.append(f"| models/props/{name}/ | https://polyhaven.com/a/{name} | CC0 |")
    os.makedirs(FONT_DIR, exist_ok=True)
    for family, fname in FONTS:
        out = os.path.join(FONT_DIR, fname)
        if download(f"{FONT_BASE}/{family}/{fname}", out):
            print(f"  + {out}")
    manifest.append("| fonts/Barlow*.ttf | https://github.com/google/fonts/tree/main/ofl/barlow (Jeremy Tribby) | SIL OFL 1.1 (fonts/OFL.txt) |")
    manifest += [
        "",
        "## Rider (human)",
        "",
        "- Body, shape targets, reference rig weights and eyes: MakeHuman system assets, CC0 1.0",
        "  (https://github.com/makehumancommunity/makehuman, see `third_party/makehuman/README.md`).",
        "- Skin, hair and neutral fabric textures in `assets/textures/rider/` are baked by",
        "  `tools/assetgen/human_gen.cpp` (fabric weave derived from the CC0 Poly Haven fabric sets above;",
        "  eye texture copied from MakeHuman, CC0).",
    ]
    with open(os.path.join(ROOT, "assets", "SOURCES.md"), "w") as f:
        f.write("# Asset sources\n\n")
        f.write("Photographic textures and HDRIs come from [Poly Haven](https://polyhaven.com) and are CC0 (public domain).\n")
        f.write("The UI typeface is Barlow by Jeremy Tribby, licensed under the SIL Open Font License 1.1.\n")
        f.write("Models, animations, sounds and level data are generated by the tools in `tools/` and are part of this project.\n\n")
        f.write("| file | source | license |\n|---|---|---|\n")
        f.write("\n".join(manifest) + "\n")
    print("done")


if __name__ == "__main__":
    main()
