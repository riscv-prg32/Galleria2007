#!/usr/bin/env python3
"""Compile the Galleria 2007 map source (JSON) into packed C data.

The map source is a deliberately simple intermediate representation:
convex sectors given as counter-clockwise polygons in centimetres, a list of
door edges and a list of entities. Portals are *not* written by hand: two
sectors are connected wherever one polygon contains the reversed edge of the
other. When official survey data becomes available it should be converted to
this same JSON shape; the engine does not need to change.

Checks performed (any failure aborts the build):
  * polygon winding is counter-clockwise and every polygon is convex;
  * no polygon self-intersects; no zero-length or over-long edges;
  * every coordinate fits the renderer's fixed-point range;
  * floor < ceiling for every sector;
  * portal edges pair up exactly once; door edges exist and are portals;
  * every entity lies inside exactly one sector;
  * counts fit the packed C field widths.

Outputs: src/gen/map_data.h, assets/map/map_report.md, assets/map/map_debug.png
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g2007_ids as ids  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
COORD_LIMIT = 8000       # |x|,|y| after re-centring (see docs/renderer.md)
MAX_EDGE = 4096          # keeps cross products inside int32
MIN_EDGE = 8


def fail(msg: str) -> None:
    raise SystemExit(f"build_map: {msg}")


def cross(ax, ay, bx, by):
    return ax * by - ay * bx


def segments_intersect(p1, p2, p3, p4) -> bool:
    """Proper intersection test (shared endpoints are not intersections)."""
    def orient(a, b, c):
        v = cross(b[0] - a[0], b[1] - a[1], c[0] - a[0], c[1] - a[1])
        return (v > 0) - (v < 0)
    if len({tuple(p1), tuple(p2), tuple(p3), tuple(p4)}) < 4:
        return False
    o1, o2 = orient(p1, p2, p3), orient(p1, p2, p4)
    o3, o4 = orient(p3, p4, p1), orient(p3, p4, p2)
    return o1 * o2 < 0 and o3 * o4 < 0


def texture_id(name: str) -> int:
    name = ids.TEXTURE_ALIASES.get(name, name)
    return ids.index(ids.TEXTURES, name, "texture")


def point_in_poly(poly, x, y) -> bool:
    n = len(poly)
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % n]
        if cross(bx - ax, by - ay, x - ax, y - ay) < 0:
            return False
    return True


def compile_map(src: dict):
    sectors = src["sectors"]
    if not sectors:
        fail("no sectors")
    xs = [p[0] for s in sectors for p in s["poly"]]
    ys = [p[1] for s in sectors for p in s["poly"]]
    # Re-centre to keep fixed-point products small; rounded for readability.
    ox = int(round((min(xs) + max(xs)) / 2 / 100.0)) * 100
    oy = int(round((min(ys) + max(ys)) / 2 / 100.0)) * 100

    vertices: list[tuple[int, int]] = []
    vindex: dict[tuple[int, int], int] = {}

    def vid(p):
        key = (p[0] - ox, p[1] - oy)
        if abs(key[0]) > COORD_LIMIT or abs(key[1]) > COORD_LIMIT:
            fail(f"vertex {p} outside +/-{COORD_LIMIT} after centring")
        if key not in vindex:
            vindex[key] = len(vertices)
            vertices.append(key)
        return vindex[key]

    walls = []
    sector_rows = []
    edge_owner: dict[tuple[int, int], int] = {}
    for si, s in enumerate(sectors):
        poly = s["poly"]
        name = s["id"]
        if len(poly) < 3:
            fail(f"{name}: needs at least 3 vertices")
        if not s["floor"] < s["ceil"]:
            fail(f"{name}: floor must be below ceiling")
        n = len(poly)
        area2 = 0
        for i in range(n):
            ax, ay = poly[i]
            bx, by = poly[(i + 1) % n]
            cx, cy = poly[(i + 2) % n]
            area2 += cross(ax, ay, bx, by)
            if cross(bx - ax, by - ay, cx - bx, cy - by) < 0:
                fail(f"{name}: polygon is not convex at {poly[(i + 1) % n]}")
            length = math.hypot(bx - ax, by - ay)
            if length < MIN_EDGE or length > MAX_EDGE:
                fail(f"{name}: edge {poly[i]}->{poly[(i + 1) % n]} length {length:.0f} "
                     f"outside [{MIN_EDGE},{MAX_EDGE}]")
        if area2 <= 0:
            fail(f"{name}: polygon must be counter-clockwise (y grows north)")
        for i in range(n):
            for j in range(i + 1, n):
                if segments_intersect(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n]):
                    fail(f"{name}: self-intersection")
        first = len(walls)
        wall_tex = texture_id(s["wall"])
        for i in range(n):
            a, b = vid(poly[i]), vid(poly[(i + 1) % n])
            if (a, b) in edge_owner:
                fail(f"{name}: edge duplicated in sectors {edge_owner[(a, b)]} and {si}")
            edge_owner[(a, b)] = len(walls)
            walls.append(dict(v0=a, v1=b, sector=si, tex=wall_tex, door=0,
                              neighbor=-1, mate=255))
        flags = 0
        for f in s.get("flags", []):
            flags |= 1 << ids.SECTOR_FLAGS.index(f)
        sector_rows.append(dict(
            name=name, first=first, count=n, light=int(s["light"]),
            floor=int(s["floor"]), ceil=int(s["ceil"]),
            ftex=texture_id(s["floor_tex"]), ctex=texture_id(s["ceil_tex"]),
            flags=flags, zone=ids.index(ids.ZONES, s["zone"], "zone"),
            prov=ids.index(ids.PROVENANCE, s.get("provenance", "RECON"), "provenance"),
            poly=[(p[0] - ox, p[1] - oy) for p in poly]))
        if not 0 <= s["light"] <= 16:
            fail(f"{name}: light must be 0..16")

    # Portal pairing: the reversed edge in another sector.
    for wi, w in enumerate(walls):
        other = edge_owner.get((w["v1"], w["v0"]))
        if other is not None:
            if walls[other]["sector"] == w["sector"]:
                fail("sector shares an edge with itself")
            w["neighbor"] = walls[other]["sector"]
            w["mate"] = other

    for d in src.get("doors", []):
        a, b = (vid(d["edge"][0]), vid(d["edge"][1]))
        did = ids.index(ids.DOORS, d["door"], "door")
        hits = [i for i, w in enumerate(walls) if {w["v0"], w["v1"]} == {a, b}]
        if len(hits) != 2:
            fail(f"door {d['door']} edge {d['edge']} must be a portal between two sectors")
        for i in hits:
            walls[i]["door"] = did
            walls[i]["door_tex"] = texture_id(d["tex"])

    if len(vertices) > 255 or len(walls) > 255 or len(sector_rows) > 127:
        fail("map exceeds packed field widths (255 vertices/walls, 127 sectors)")

    for w in walls:
        ax, ay = vertices[w["v0"]]
        bx, by = vertices[w["v1"]]
        ex, ey = bx - ax, by - ay
        length = math.hypot(ex, ey)
        # Inward normal of a CCW polygon is the left perpendicular (-ey, ex).
        w["nx"] = int(round(-ey / length * 4096))
        w["ny"] = int(round(ex / length * 4096))
        dom = max(abs(ex), abs(ey))
        w["ulen"] = int(round(length / dom * 256))
        if "door_tex" in w:
            w["tex"] = w["door_tex"]

    entities = []
    item_count = 0
    for e in src.get("entities", []):
        x, y = e["pos"][0] - ox, e["pos"][1] - oy
        owners = [i for i, s in enumerate(sector_rows) if point_in_poly(s["poly"], x, y)]
        if not owners:
            fail(f"entity {e} is not inside any sector")
        kind = ids.index(ids.ENTITY_KINDS, e["kind"], "entity kind")
        arg = arg2 = 0
        if e["kind"] == "SPAWN":
            arg, arg2 = int(e["checkpoint"]), int(round(e["angle"] / 360.0 * 256)) & 255
        elif e["kind"] == "ITEM":
            arg = ids.index(ids.ITEMS, e["item"], "item")
            item_count += 1
        elif e["kind"] == "DOOR":
            arg = ids.index(ids.DOORS, e["door"], "door")
        elif e["kind"] == "CLUE":
            arg = int(e["clue"])
        elif e["kind"] == "WAYPOINT":
            arg = int(e["order"])
        sprite = 255
        if "sprite" in e:
            sprite = ids.index(ids.SPRITES, e["sprite"], "sprite")
        elif e["kind"] == "ITEM":
            name = e["item"] if not e["item"].startswith("KEY") else "KEY"
            sprite = ids.index(ids.SPRITES, "ITEM_" + name, "sprite")
        archive = ids.index(ids.ARCHIVE, e["archive"], "archive") if "archive" in e else 255
        prov = ids.index(ids.PROVENANCE, e.get("provenance", "GAME"), "provenance")
        flags = (1 if e.get("hidden") else 0) | (prov << 4)
        entities.append(dict(x=x, y=y, sector=owners[0], kind=kind, arg=arg, arg2=arg2,
                             sprite=sprite, archive=archive,
                             name=("S_" + e["name"]) if "name" in e else "S_NONE",
                             desc=("S_" + e["desc"]) if "desc" in e else "S_NONE",
                             flags=flags, src=e))
    if item_count > 16:
        fail("more than MAX_WORLD_DROPS (16) item instances")
    return dict(origin=(ox, oy), vertices=vertices, walls=walls, sectors=sector_rows,
                entities=entities, item_count=item_count)


def emit_c(m) -> str:
    out = ["/* Generated by tools/build_map.py from assets/map/galleria2007.json.",
           " * PROVISIONAL TOPOLOGY - not a survey. Do not edit by hand. */",
           "#ifndef G2007_GEN_MAP_DATA_H", "#define G2007_GEN_MAP_DATA_H", "",
           f"#define MAP_ORIGIN_X {m['origin'][0]}",
           f"#define MAP_ORIGIN_Y {m['origin'][1]}",
           f"#define MAP_VERTEX_COUNT {len(m['vertices'])}",
           f"#define MAP_WALL_COUNT {len(m['walls'])}",
           f"#define MAP_SECTOR_COUNT {len(m['sectors'])}",
           f"#define MAP_ENTITY_COUNT {len(m['entities'])}",
           f"#define MAP_ITEM_COUNT {m['item_count']}", "",
           "static const MapVertex g_map_vertices[MAP_VERTEX_COUNT] = {"]
    out += [f"    {{{x}, {y}}}," for x, y in m["vertices"]]
    out += ["};", "", "static const MapWall g_map_walls[MAP_WALL_COUNT] = {",
            "    /* v0, v1, neighbor, mate, nx, ny, ulen_q8, tex, door */"]
    for i, w in enumerate(m["walls"]):
        out.append(f"    {{{w['v0']}, {w['v1']}, {w['neighbor']}, {w['mate']}, {w['nx']}, "
                   f"{w['ny']}, {w['ulen']}, {w['tex']}, {w['door']}}}, /* {i} s{w['sector']} */")
    out += ["};", "", "static const MapSector g_map_sectors[MAP_SECTOR_COUNT] = {",
            "    /* first_wall, wall_count, light, flags, floor_z, ceil_z, floor_tex, ceil_tex, zone, provenance */"]
    for i, s in enumerate(m["sectors"]):
        out.append(f"    {{{s['first']}, {s['count']}, {s['light']}, {s['flags']}, {s['floor']}, "
                   f"{s['ceil']}, {s['ftex']}, {s['ctex']}, {s['zone']}, {s['prov']}}}, /* {i} {s['name']} */")
    out += ["};", "", "static const MapEntity g_map_entities[MAP_ENTITY_COUNT] = {",
            "    /* x, y, sector, kind, arg, arg2, sprite, archive, flags, name, desc */"]
    for i, e in enumerate(m["entities"]):
        out.append(f"    {{{e['x']}, {e['y']}, {e['sector']}, {e['kind']}, {e['arg']}, {e['arg2']}, "
                   f"{e['sprite']}, {e['archive']}, {e['flags']}, {e['name']}, {e['desc']}}}, /* {i} {e['src']['kind']} */")
    out += ["};", "", "#endif", ""]
    return "\n".join(out)


def report(m, src) -> str:
    vb = len(m["vertices"]) * 4
    wb = len(m["walls"]) * 12
    sb = len(m["sectors"]) * 12
    eb = len(m["entities"]) * 12
    lines = ["# Galleria 2007 map report", "",
             "Generated by `tools/build_map.py`. " + src.get("notice", ""), "",
             f"Origin offset (cm): {m['origin']}", "",
             "| Table | Count | Bytes |", "|---|---:|---:|",
             f"| vertices | {len(m['vertices'])} | {vb} |",
             f"| walls | {len(m['walls'])} | {wb} |",
             f"| sectors | {len(m['sectors'])} | {sb} |",
             f"| entities | {len(m['entities'])} | {eb} |",
             f"| **total** | | **{vb + wb + sb + eb}** |", "",
             "| # | Sector | Zone | Floor | Ceil | Light | Walls | Portals | Provenance |",
             "|---:|---|---|---:|---:|---:|---:|---:|---|"]
    for i, s in enumerate(m["sectors"]):
        portals = sum(1 for w in m["walls"][s["first"]:s["first"] + s["count"]] if w["neighbor"] >= 0)
        lines.append(f"| {i} | {s['name']} | {ids.ZONES[s['zone']]} | {s['floor']} | {s['ceil']} | "
                     f"{s['light']} | {s['count']} | {portals} | {ids.PROVENANCE[s['prov']]} |")
    lines += ["", "| # | Entity | Sector | Provenance |", "|---:|---|---|---|"]
    for i, e in enumerate(m["entities"]):
        label = e["src"].get("name", e["src"].get("item", ""))
        lines.append(f"| {i} | {e['src']['kind']} {label} | {m['sectors'][e['sector']]['name']} | "
                     f"{e['src'].get('provenance', 'GAME')} |")
    return "\n".join(lines) + "\n"


def debug_png(m, path: Path) -> None:
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("Pillow not installed: skipping debug PNG")
        return
    xs = [v[0] for v in m["vertices"]]
    ys = [v[1] for v in m["vertices"]]
    scale, pad = 0.1, 20
    w = int((max(xs) - min(xs)) * scale) + 2 * pad
    h = int((max(ys) - min(ys)) * scale) + 2 * pad
    img = Image.new("RGB", (w, h), (16, 14, 12))
    d = ImageDraw.Draw(img)

    def p(v):
        return (pad + (v[0] - min(xs)) * scale, h - pad - (v[1] - min(ys)) * scale)
    for s in m["sectors"]:
        shade = 40 + max(0, min(200, s["floor"] // 4 + 60))
        d.polygon([p(v) for v in s["poly"]], fill=(shade, shade * 4 // 5, shade // 2))
    for wl in m["walls"]:
        a, b = p(m["vertices"][wl["v0"]]), p(m["vertices"][wl["v1"]])
        col = (230, 220, 200) if wl["neighbor"] < 0 else (90, 160, 220)
        if wl["door"]:
            col = (230, 40, 40)
        d.line([a, b], fill=col, width=2 if wl["neighbor"] < 0 or wl["door"] else 1)
    for e in m["entities"]:
        x, y = p((e["x"], e["y"]))
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 255, 0))
    img.save(path)


def main() -> int:
    src_path = ROOT / "assets/map/galleria2007.json"
    src = json.loads(src_path.read_text(encoding="utf-8"))
    m = compile_map(src)
    (ROOT / "src/gen").mkdir(parents=True, exist_ok=True)
    (ROOT / "src/gen/map_data.h").write_text(emit_c(m), encoding="utf-8")
    rep = report(m, src)
    (ROOT / "assets/map/map_report.md").write_text(rep, encoding="utf-8")
    debug_png(m, ROOT / "assets/map/map_debug.png")
    print(f"map: {len(m['sectors'])} sectors, {len(m['walls'])} walls, "
          f"{len(m['vertices'])} vertices, {len(m['entities'])} entities, "
          f"{m['item_count']} items")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
