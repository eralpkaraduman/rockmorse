#!/usr/bin/env python3
"""Extract the Rockmorse Unity project into readable JSON, a C header and a trimmed asset folder.

Usage (from the repo root):
    python -I tools/extract.py /path/to/LD48-Unity /path/to/RockMorse.app/Contents/Resources/Data

The first argument is the Unity project, the second the Data folder of the 2016 Mac build. Both are read-only.
The build is the faithful reference for the audio the port plays (the build re-encoded 8 MP3 files),
and the source of the shader, material and font facts. Build reading is in tools/build_extract.py.
The script reads them with UnityPy and writes:
    extract/scenes/<Scene>.json        every GameObject of the 7 scenes
    extract/prefabs/<Prefab>.json      every GameObject of the prefabs
    extract/animation/*.json           animation clips and the animator controller
    extract/materials/*.json           materials used by scenes and prefabs
    extract/settings/*.json            ProjectSettings assets (plus derived files)
    extract/assets/*.json              import settings from every used .meta file
    extract/derived/*.json             polygon pieces, layer matrix, used-asset list
    extract/build/*.json               shaders, materials and the Font object of the 2016 build, audio clip facts
    port/src/values.h                  C constants from ONE table (build_values below)
    port/src/tables.h                  levels.json and morsecodes.json as C tables
    port/src/code_constants.h          strings and numbers that live only in the C# code (CODE_CONSTANTS below)
    port/assets/...                    the asset files the game really uses (sounds: see audio_port_file)

The output is deterministic: sorted keys, stable ordering, no timestamps.
The asserts check the facts the port relies on, so never run it with -O. A changed input fails loudly.

"""
import ast
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import array
from bisect import bisect_right

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I does not add the script folder
import build_extract  # noqa: E402  (reads the 2016 Mac build)
import UnityPy  # noqa: E402
from PIL import Image  # comes with UnityPy

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENES = ["Intro", "Title", "Story1", "Story2", "Game", "Death", "Win"]

# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------


def f32_bits(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def f32(x):
    """Shortest decimal that round-trips as float32. A double that is not a float32 stays as it is."""
    if f32_bits(x) != x:
        return x
    for n in range(1, 10):
        s = float("%.*g" % (n, x))
        if f32_bits(s) == x:
            return s
    raise AssertionError("nine digits always round-trip a float32: %r" % x)


def clean(v):
    """Make a typetree value JSON friendly: short floats, bytes to hex."""
    if isinstance(v, float):
        return f32(v)
    if isinstance(v, dict):
        out = {k: clean(x) for k, x in v.items()}
        if set(v) == {"rgba"}:  # Color32 packed as r | g << 8 | b << 16 | a << 24
            n = v["rgba"]
            out.update({"r": n & 255, "g": (n >> 8) & 255, "b": (n >> 16) & 255, "a": (n >> 24) & 255})
        return out
    if isinstance(v, (list, tuple)):
        return [clean(x) for x in v]
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).hex()
    return v


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=1, sort_keys=True, ensure_ascii=False, allow_nan=False)
        fh.write("\n")


def read_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


# --------------------------------------------------------------------------
# tiny YAML reader, enough for Unity .meta files
# --------------------------------------------------------------------------

_INT = re.compile(r"^[-+]?\d+$")
_FLT = re.compile(r"^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$")
_STRING_KEYS = ("guid", "userData", "name", "path")  # never turned into numbers (a guid can look like 12e4)


def _scalar(s, key=None):
    s = s.strip()
    if key in _STRING_KEYS:
        return s.strip("'\"")
    if _INT.match(s):
        return int(s)
    if _FLT.match(s):
        return float(s)
    return s


def _flow(s):
    """Flow collections of the .meta files: {x: 0, y: 1} and []. They never nest."""
    s = s.strip()
    if s.startswith("{"):
        d = {}
        for part in s[1:-1].split(","):
            if part.strip():
                k, _, v = part.partition(":")
                d[k.strip()] = _scalar(v)
        return d
    assert s == "[]", "unsupported flow list in a .meta file: " + s
    return []


def parse_yaml(text):
    """Block maps, lists of maps and flow values: all that Unity writes into .meta files."""
    lines = [(len(raw) - len(raw.lstrip(" ")), raw.strip()) for raw in text.splitlines()]
    assert lines and all(t for _, t in lines), "empty or blank line in a .meta file"
    val, end = _block(lines, 0)
    assert end == len(lines), "unparsed lines in a .meta file: %r" % (lines[end],)
    return val


def _block(lines, i):
    indent, text = lines[i]
    return _list(lines, i, indent) if text.startswith("- ") else _map(lines, i, indent)


def _map(lines, i, indent):
    d = {}
    while i < len(lines) and lines[i][0] == indent and not lines[i][1].startswith("- "):
        key, colon, rest = lines[i][1].partition(":")
        assert colon, "line without a colon in a .meta file: " + lines[i][1]
        key, rest = key.strip(), rest.strip()
        i += 1
        if rest.startswith(("{", "[")):
            d[key] = _flow(rest)
        elif rest:
            d[key] = _scalar(rest, key)
        elif i < len(lines) and (lines[i][0] > indent or (lines[i][0] == indent and lines[i][1].startswith("- "))):
            d[key], i = _block(lines, i)
        else:
            d[key] = None
    return d, i


def _list(lines, i, indent):
    out = []
    while i < len(lines) and lines[i][0] == indent and lines[i][1].startswith("- "):
        content = lines[i][1][1:].strip()
        assert re.match(r"^[^{\[\"':]+:(\s|$)", content), "list item is not a map: " + content
        lines[i] = (indent + 2, content)  # "- key: value" starts a map whose keys sit at indent + 2
        v, i = _map(lines, i, indent + 2)
        out.append(v)
    return out, i


# --------------------------------------------------------------------------
# project index: GUID -> asset path, with .meta data
# --------------------------------------------------------------------------

# Unity's built-in resource files and the only objects in them that scenes, prefabs and materials point to.
BUILTIN = {"library/unity default resources": {10209, 10753, 10754}, "resources/unity_builtin_extra": {30}}
BUILTIN_NAMES = {10754: "Sprites-Default (Material)"}
TEXTURE2D_ID = 2800000  # the texture itself, as opposed to one of its sprites



class Project:
    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.assets = os.path.join(self.root, "Assets")
        self.guid_path = {}     # guid -> path relative to Assets
        self.meta = {}          # path relative to Assets -> parsed .meta
        self.prefab_index = {}  # prefab path -> {path id: (class, GameObject path)}, filled by index_prefabs
        for dirpath, dirs, files in os.walk(self.assets):
            dirs.sort()
            for fn in sorted(files):
                if fn.endswith(".meta"):
                    full = os.path.join(dirpath, fn)
                    rel = os.path.relpath(full[:-5], self.assets).replace(os.sep, "/")
                    with open(full, encoding="utf-8") as fh:
                        m = parse_yaml(fh.read())
                    assert m["guid"] not in self.guid_path, "duplicate GUID in " + rel
                    self.guid_path[m["guid"]] = rel
                    self.meta[rel] = m

    def guid_of(self, raw):
        """Binary external GUID to the hex form of the .meta files (Unity swaps the nibbles of each byte).
        None for the built-in resources file."""
        hexed = raw.hex()
        guid = "".join(hexed[i + 1] + hexed[i] for i in range(0, 32, 2))
        if guid in self.guid_path:
            return guid
        assert guid.startswith("0" * 16), "external GUID %s matches no .meta file" % guid
        return None

    def recycle_name(self, rel, file_id):
        names = self.meta[rel].get("TextureImporter", {}).get("fileIDToRecycleName") or {}
        return names.get(str(file_id))  # the reader keeps every key as text

# --------------------------------------------------------------------------
# reading one Unity serialized file
# --------------------------------------------------------------------------

def comp_pid(c):
    """Path id of an entry of GameObject.m_Component (UnityPy gives a (class id, pointer) tuple)."""
    return c[1]["m_PathID"]


class UFile:
    """One scene, prefab or asset file, read through UnityPy."""

    def __init__(self, proj, path):
        self.proj = proj
        self.rel = os.path.relpath(path, proj.assets).replace(os.sep, "/")
        env = UnityPy.load(path)
        assert len(env.files) == 1, self.rel
        sf = list(env.files.values())[0]
        self.version = str(sf.unity_version)
        self.externals = [(proj.guid_of(e.guid), e.path) for e in sf.externals]  # (guid or None, file name)
        self.cls = {o.path_id: o.type.name for o in env.objects}  # in file order
        self.tree = {o.path_id: o.read_typetree() for o in env.objects}
        self._index()

    def components(self, g):
        pids = [comp_pid(c) for c in self.tree[g]["m_Component"]]
        assert all(p in self.cls for p in pids), "%s: GameObject %d has a component outside the file" % (self.rel, g)
        return pids

    def script_name(self, pid):
        ref = self.tree[pid]["m_Script"]
        assert ref["m_FileID"] > 0 and ref["m_PathID"] != 0, "%s: script reference of %d" % (self.rel, pid)
        guid, _ = self.externals[ref["m_FileID"] - 1]
        rel = self.proj.guid_path[guid]
        assert rel.endswith((".cs", ".js")), rel
        return os.path.splitext(os.path.basename(rel))[0]

    def _index(self):
        cls, tree = self.cls, self.tree
        gos = [p for p in cls if cls[p] == "GameObject"]
        # Transform of each GameObject, parent and children
        self.transform_of = {}
        for g in gos:
            trs = [p for p in self.components(g) if cls[p] == "Transform"]
            assert len(trs) == 1, "%s: GameObject %d needs exactly one Transform" % (self.rel, g)
            self.transform_of[g] = trs[0]
        go_of_transform = {t: g for g, t in self.transform_of.items()}
        self.parent, self.children = {}, {}
        for g, t in self.transform_of.items():
            father = tree[t]["m_Father"]["m_PathID"]
            assert father == 0 or father in go_of_transform, "%s: parent of %d is outside the file" % (self.rel, g)
            self.parent[g] = go_of_transform.get(father)
            self.children[g] = [go_of_transform[c["m_PathID"]] for c in tree[t]["m_Children"]]
        # roots in file order, then every GameObject parents first. The walk reaches each exactly once,
        # so a cycle in the parent chain (which has no root) would show up as a missing GameObject.
        pos = {p: i for i, p in enumerate(cls)}
        self.roots = sorted((g for g in gos if self.parent[g] is None), key=lambda g: pos[self.transform_of[g]])
        self.order = []

        def walk(g):
            self.order.append(g)
            for c in self.children[g]:
                assert self.parent[c] == g, "%s: children and parent disagree at %d" % (self.rel, c)
                walk(c)
        for r in self.roots:
            walk(r)
        assert sorted(self.order) == sorted(gos), "%s: parent chain has a cycle or a lost GameObject" % self.rel
        # unique paths, e.g. "Rover/RoverBody"
        self.go_path = {}
        for g in self.order:
            name = tree[g]["m_Name"]
            assert "/" not in name, "%s: GameObject name with a slash: %s" % (self.rel, name)
            p = self.parent[g]
            self.go_path[g] = name if p is None else self.go_path[p] + "/" + name
        assert len(set(self.go_path.values())) == len(gos), "%s: GameObject paths are not unique" % self.rel
        # component keys: the class name, or the script name for a MonoBehaviour. Unique per GameObject.
        self.comp_key, self.script_of = {}, {}
        for g in gos:
            keys = []
            for pid in self.components(g):
                key = cls[pid]
                if key == "MonoBehaviour":
                    key = self.script_of[pid] = self.script_name(pid)
                self.comp_key[pid] = key
                keys.append(key)
            assert len(set(keys)) == len(keys), "%s: %s has two components of one kind" % (self.rel, self.go_path[g])

    # ---- references ----------------------------------------------------
    def ref_to_str(self, ref):
        """A Unity reference as text: None for null, @GameObject:Component inside the file,
        asset path (plus #sprite or #object) outside it."""
        fid, pid = ref["m_FileID"], ref["m_PathID"]
        if pid == 0:
            return None
        if fid == 0:
            return self.local_ref(pid)
        assert fid <= len(self.externals), "%s: external file %d does not exist" % (self.rel, fid)
        guid, epath = self.externals[fid - 1]
        if guid is None:
            assert pid in BUILTIN.get(epath, ()), "%s: unknown built-in %s#%d" % (self.rel, epath, pid)
            return "builtin:%s#%s" % (epath, BUILTIN_NAMES.get(pid, pid))
        rel = self.proj.guid_path[guid]
        if rel.endswith((".cs", ".js")):
            return rel
        if rel.endswith(".prefab"):
            idx = self.proj.prefab_index.get(rel)  # only the project's own prefabs are indexed
            if idx is None:
                return "%s#%d" % (rel, pid)
            assert pid in idx, "%s: %s has no object %d" % (self.rel, rel, pid)
            return "%s#%s:%s" % ((rel,) + idx[pid])
        if rel.lower().endswith(IMAGE_EXT):
            rn = self.proj.recycle_name(rel, pid)
            assert rn is not None or pid == TEXTURE2D_ID, "%s: %s has no sprite %d" % (self.rel, rel, pid)
            return rel if rn is None else "%s#%s" % (rel, rn)
        return rel

    def local_ref(self, pid):
        assert pid in self.cls, "%s: reference to %d, which is not in the file" % (self.rel, pid)
        if pid in self.comp_key:
            owner = self.tree[pid]["m_GameObject"]["m_PathID"]
            return "@%s:%s" % (self.go_path[owner], self.comp_key[pid])
        assert self.cls[pid] != "GameObject", "%s: no field points at a GameObject" % self.rel
        return "@%s#%d" % (self.cls[pid], pid)

    def convert(self, v):
        if isinstance(v, dict):
            if set(v) == {"m_FileID", "m_PathID"}:
                return self.ref_to_str(v)
            if set(v) == {"rgba"}:
                return clean(v)
            return {k: self.convert(x) for k, x in v.items()}
        if isinstance(v, (list, tuple)):
            return [self.convert(x) for x in v]
        return clean(v)


# --------------------------------------------------------------------------
# transforms
# --------------------------------------------------------------------------

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    x, y, z, w = q
    # v' = q * v * q^-1
    t = (2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0]))
    return (v[0] + w * t[0] + (y * t[2] - z * t[1]),
            v[1] + w * t[1] + (z * t[0] - x * t[2]),
            v[2] + w * t[2] + (x * t[1] - y * t[0]))


def quat_euler(q):
    """Unity euler angles in degrees (rotation order Z, X, Y)."""
    n = math.sqrt(sum(c * c for c in q))  # the file stores float32 quaternions, a bit off unit length
    x, y, z, w = (c / n for c in q)
    r12 = 2 * (y * z - w * x)
    r02 = 2 * (x * z + w * y)
    r22 = 1 - 2 * (x * x + y * y)
    cx = math.hypot(r02, r22)  # cos of the X angle
    ex = math.degrees(math.atan2(-r12, cx))
    if cx < 1e-6:  # looking straight up or down: Y and Z turn the same axis, so all of it goes into Z
        ey = 0.0
        ez = math.degrees(math.atan2(-2 * (x * y - w * z), 1 - 2 * (y * y + z * z)))
    else:
        ey = math.degrees(math.atan2(r02, r22))
        ez = math.degrees(math.atan2(2 * (x * y + w * z), 1 - 2 * (x * x + z * z)))

    def norm(a):
        a = a % 360.0
        return round(a if a < 359.9999 else 0.0, 4)
    return [norm(ex), norm(ey), norm(ez)]


def r32(x):
    """A computed double as the nearest float32, written as its shortest decimal. + 0.0 drops -0.0.
    A value that already is a float32 passes through unchanged."""
    return f32(f32_bits(x)) + 0.0


def world_transforms(uf):
    """GameObject id -> (world position, rotation, scale, local position, rotation, scale)."""
    infos = {}
    for g in uf.order:  # parents first
        tr = uf.tree[uf.transform_of[g]]
        lp = tuple(tr["m_LocalPosition"][k] for k in "xyz")
        lr = tuple(tr["m_LocalRotation"][k] for k in "xyzw")
        ls = tuple(tr["m_LocalScale"][k] for k in "xyz")
        par = uf.parent[g]
        if par is None:
            wp, wr, ws = lp, lr, ls
        else:
            pp, pr, ps = infos[par][:3]
            rp = qrot(pr, tuple(s * p for s, p in zip(ps, lp)))
            wp = tuple(a + b for a, b in zip(pp, rp))
            wr = qmul(pr, lr)
            ws = tuple(a * b for a, b in zip(ps, ls))
        infos[g] = (wp, wr, ws, lp, lr, ls)
    return infos


def fmt3(t):
    return {"x": r32(t[0]), "y": r32(t[1]), "z": r32(t[2])}


def fmtq(q):
    return {"x": r32(q[0]), "y": r32(q[1]), "z": r32(q[2]), "w": r32(q[3])}


# --------------------------------------------------------------------------
# convex splitting of PolygonCollider2D paths (plan section 6.1)
# --------------------------------------------------------------------------

MAX_PIECE_POINTS = 8  # Box2D's b2_maxPolygonVertices


def area2(poly):
    """Twice the signed area, positive for counter-clockwise."""
    return sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
               for i in range(len(poly)))


def cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def in_tri(p, a, b, c):
    return cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0


def triangulate(poly):
    """Ear clipping on a CCW polygon. Always cuts the widest ear first.

    Widest ear = the ear triangle with the largest area. Ties go to the lowest vertex index.
    Returns triangles as lists of vertex indices.
    """
    idx = list(range(len(poly)))
    tris = []
    while len(idx) > 3:
        best, best_area = None, -1.0
        n = len(idx)
        for k in range(n):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % n]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if cross(a, b, c) <= 0:
                continue
            if any(j not in (i0, i1, i2) and in_tri(poly[j], a, b, c) for j in idx):
                continue
            ar = cross(a, b, c) / 2
            if ar > best_area:
                best, best_area = k, ar
        assert best is not None, "no ear found, polygon not simple"
        tris.append([idx[best - 1], idx[best], idx[(best + 1) % n]])
        del idx[best]
    tris.append(list(idx))
    return tris


def corner_dist(a, b, c):
    """Distance of b from the line a-c, positive when a->b->c turns left (convex for CCW)."""
    d = math.hypot(c[0] - a[0], c[1] - a[1])
    return cross(a, b, c) / d if d > 0 else 0.0


def corners_ok(poly, slop):
    """Every corner bends by more than 2 x slop (the Box2D hull rule): the middle point sits
    more than 2*slop away from the line through its two neighbours, on the convex side."""
    n = len(poly)
    return all(corner_dist(poly[i - 1], poly[i], poly[(i + 1) % n]) > 2 * slop for i in range(n))


def merged(p, q):
    """Join two CCW index lists along their shared edge (p has a->b, q has b->a), or None."""
    n, m = len(p), len(q)
    for i in range(n):
        a, b = p[i], p[(i + 1) % n]
        for j in range(m):
            if q[j] == b and q[(j + 1) % m] == a:
                pr = [p[(i + 1 + k) % n] for k in range(n)]      # b ... a
                qr = [q[(j + 2 + k) % m] for k in range(m - 2)]  # after a, ... before b
                return pr + qr
    return None


def merge_pieces(poly, tris, slop):
    """Merge neighbouring triangles while the result stays convex, has at most 8 points
    and every corner passes corners_ok. Picks the merge that gives the largest area first."""
    pieces = [list(t) for t in tris]  # lists of indices into poly, CCW
    while True:
        best = None
        for i in range(len(pieces)):
            for j in range(i + 1, len(pieces)):
                m = merged(pieces[i], pieces[j])
                if m is None or len(m) > MAX_PIECE_POINTS:
                    continue
                pts = [poly[k] for k in m]
                if not corners_ok(pts, slop):
                    continue
                key = (abs(area2(pts)), -i, -j)
                if best is None or key > best[0]:
                    best = (key, i, j, m)
        if best is None:
            return pieces
        _, i, j, m = best
        pieces = [p for k, p in enumerate(pieces) if k not in (i, j)] + [m]


def box2d_hull_ok(pts, slop):
    """Checks b2ComputeHull would keep every point of a convex CCW piece:
    no two points closer than 4 x slop (welding), no corner within 2 x slop of collinear."""
    tol = 16 * slop * slop
    for i, p in enumerate(pts):
        for q in pts[:i]:
            if (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 < tol:
                return False
    return corners_ok(pts, slop)


def is_simple(poly):
    """True when no two edges that do not share a corner touch or cross."""
    def side(o, a, b):
        c = cross(o, a, b)
        return (c > 0) - (c < 0)
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        for j in range(i + 2, n if i else n - 1):
            c, d = poly[j], poly[(j + 1) % n]
            if side(a, b, c) == side(a, b, d) == 0:  # collinear: they touch only when their ranges overlap
                if all(max(min(a[k], b[k]), min(c[k], d[k])) <= min(max(a[k], b[k]), max(c[k], d[k])) for k in (0, 1)):
                    return False
            elif side(a, b, c) * side(a, b, d) <= 0 and side(c, d, a) * side(c, d, b) <= 0:
                return False
    return True


# a T shape has two separate edges on the line y = 1 and is simple; a bowtie is not
assert is_simple([(0, 0), (3, 0), (3, 1), (2, 1), (2, 3), (1, 3), (1, 1), (0, 1)])
assert not is_simple([(0, 0), (1, 1), (1, 0), (0, 1)])


def split_path(points, slop):
    """points: CCW list of (x, y), too complex for one piece (no path in the data is a single
    convex piece of <= 8 points). Returns convex pieces as lists of (x, y), CCW."""
    assert area2(points) > 0, "collider path is not counter-clockwise"
    assert is_simple(points), "collider path crosses itself"

    pieces = merge_pieces(points, triangulate(points), slop)
    return [[points[k] for k in p] for p in pieces]


# --------------------------------------------------------------------------
# dump scenes and prefabs
# --------------------------------------------------------------------------


def dump_file(uf, layers):
    out = {"file": uf.rel, "unity_serialized_version": uf.version, "gameobjects": [], "scene_settings": {}}
    infos = world_transforms(uf)
    for g in uf.order:
        t = uf.tree[g]
        wp, wr, ws, lp, lr, ls = infos[g]
        comps = []
        for cid in uf.components(g):
            tree = uf.tree[cid]
            comp = {"key": uf.comp_key[cid], "type": uf.cls[cid], "id": cid,
                    "fields": {k: uf.convert(v) for k, v in tree.items() if k != "m_GameObject"}}
            if uf.cls[cid] == "MonoBehaviour":
                comp["script"] = uf.script_of[cid]
            if "m_Enabled" in tree:
                comp["enabled"] = bool(tree["m_Enabled"])
            comps.append(comp)
        out["gameobjects"].append({
            "id": g, "name": t["m_Name"], "path": uf.go_path[g], "tag": t["m_TagString"],
            "layer": t["m_Layer"], "layer_name": layers[t["m_Layer"]],
            "active": bool(t["m_IsActive"]),
            "parent": uf.go_path[uf.parent[g]] if uf.parent[g] is not None else None,
            "children": [uf.go_path[c] for c in uf.children[g]],
            "prefab_source": uf.convert(t["m_PrefabParentObject"]),
            "transform": {
                "local_position": fmt3(lp), "local_rotation": fmtq(lr), "local_euler": quat_euler(lr),
                "local_scale": fmt3(ls),
                "world_position": fmt3(wp), "world_rotation": fmtq(wr), "world_euler": quat_euler(wr),
                "world_scale": fmt3(ws),
            },
            "components": comps,
        })
    for pid, c in uf.cls.items():
        if c in SCENE_SETTINGS:
            out["scene_settings"][c] = uf.convert(uf.tree[pid])
    return out


SCENE_SETTINGS = ("OcclusionCullingSettings", "RenderSettings", "HaloManager", "LightmapSettings", "NavMeshSettings")


def read_layers(proj):
    objs = UnityPy.load(os.path.join(proj.root, "ProjectSettings", "TagManager.asset")).objects
    assert len(objs) == 1
    t = objs[0].read_typetree()
    # layers 8 and 9 have no entry in the file
    layers = [t.get(("Builtin Layer %d" if i < 8 else "User Layer %d") % i) or "" for i in range(32)]
    return layers


def index_prefabs(proj, files):
    """Prefab object ids -> (class, GameObject path), used when scenes point into prefabs."""
    for rel, uf in files.items():
        proj.prefab_index[rel] = {pid: (uf.cls[pid], uf.go_path[uf.tree[pid]["m_GameObject"]["m_PathID"]])
                                  for pid in uf.comp_key}
        proj.prefab_index[rel].update({g: ("GameObject", p) for g, p in uf.go_path.items()})


# --------------------------------------------------------------------------
# colliders: world-scaled derived values and convex pieces
# --------------------------------------------------------------------------



def collider_derived(node, comp):
    """Adds comp["derived"]: world-scaled size and offset, and for polygons the world-scaled path."""
    ws = node["transform"]["world_scale"]
    sx, sy = abs(ws["x"]), abs(ws["y"])
    f = comp["fields"]
    t = comp["type"]
    d = {"world_scale_xy": [ws["x"], ws["y"]]}
    off = f.get("m_Center", {"x": 0, "y": 0})  # the 5.4 PolygonCollider2D has no offset
    d["offset_world"] = {"x": r32(off["x"] * ws["x"]), "y": r32(off["y"] * ws["y"])}
    if t == "CircleCollider2D":
        d["radius_world"] = r32(f["m_Radius"] * max(sx, sy))
    elif t == "BoxCollider2D":
        sz = f["m_Size"]
        d["size_world"] = {"x": r32(sz["x"] * sx), "y": r32(sz["y"] * sy)}
    else:
        assert t == "PolygonCollider2D", t
        raw = f["m_Poly"]["m_Paths"]
        assert len(raw) == 1, "the value table reads path 0 only"
        pts = [((p["x"] + off["x"]) * ws["x"], (p["y"] + off["y"]) * ws["y"]) for p in raw[0]]
        ys = [pt[1] for pt in pts]
        d["paths"] = [{"source_points": len(pts), "scaled_points": [[r32(x), r32(y)] for x, y in pts]}]
        d["height_world"] = r32(max(ys) - min(ys))
        d["_points"] = pts  # full precision for add_pieces, removed there
    comp["derived"] = d


def add_pieces(comp, slop):
    """Splits a polygon collider path into convex pieces (needs the slop, which comes from the player's height)."""
    d = comp["derived"]
    pts = d.pop("_points")
    pieces = split_path(pts, slop)
    d["paths"][0].update({
        "pieces": [[[r32(x), r32(y)] for x, y in pc] for pc in pieces],
        "piece_point_counts": [len(pc) for pc in pieces],
        "convex": corners_ok(pts, slop),
    })
    d["piece_total"] = len(pieces)


# --------------------------------------------------------------------------
# assets: import settings, summaries, reading the dumps back
# --------------------------------------------------------------------------

PIVOTS = {0: (0.5, 0.5), 1: (0.0, 1.0), 2: (0.5, 1.0), 3: (1.0, 1.0), 4: (0.0, 0.5), 5: (1.0, 0.5),
          6: (0.0, 0.0), 7: (0.5, 0.0), 8: (1.0, 0.0)}
IMAGE_EXT = (".png", ".tif", ".tga", ".psd")
AUDIO_EXT = (".wav", ".mp3")
FONT_EXT = (".ttf",)
SCRIPT_EXT = (".cs", ".js")
DUMP_EXT = (".mat", ".anim", ".controller")

# Objects the scenes contain but that no script ever switches on. Their references are ignored
# when deciding which asset files are used.
IGNORED_OBJECTS = {("scenes/Game.json", "Level/rockDestructionEffect"):
                   "inactive Fireworks instance, no script activates it"}


# References that exist in the data but that nothing samples. Key: dump that holds the reference.
IGNORED_REFS = {"materials/Fire Smoke.mat.json": {
    "Standard Assets/Particles/Sources/Textures/smoketest2.psd":
        "_AlphaTex slot of Fire Smoke.mat. The 2016 build's material has only _MainTex, because its shader "
        "(Alpha-Diffuse) has no alpha texture"}}


UNUSED_ASSETS = {a for d in IGNORED_REFS.values() for a in d}  # named in a dump, not copied


def sprite_pivot(alignment, stored):
    """Effective pivot: alignment 9 means the stored pivot, the others are fixed points."""
    return list((stored["x"], stored["y"]) if alignment == 9 else PIVOTS[alignment])


def texture_info(proj, rel):
    m = proj.meta[rel]
    ti = m["TextureImporter"]
    with Image.open(os.path.join(proj.assets, rel)) as im:
        w, h = im.size
    ts = ti["textureSettings"]
    mode = ti["spriteMode"]
    ppu = ti["spritePixelsToUnits"]
    info = {"file": rel, "guid": m["guid"], "width": w, "height": h,
            "textureType": ti.get("textureType"), "spriteMode": mode, "pixelsPerUnit": ppu,
            "filterMode": ts.get("filterMode"), "filterModeName": {0: "point", 1: "bilinear", 2: "trilinear"}.get(ts.get("filterMode")),
            "wrapMode": ts.get("wrapMode"), "aniso": ts.get("aniso"),
            "textureFormat": ti.get("textureFormat"), "maxTextureSize": ti.get("maxTextureSize"),
            "npotScale": ti.get("nPOTScale"), "mipmaps": (ti.get("mipmaps") or {}).get("enableMipMap"),
            "isReadable": ti.get("isReadable"), "alphaIsTransparency": ti.get("alphaIsTransparency"),
            "spriteExtrude": ti.get("spriteExtrude"), "spriteMeshType": ti.get("spriteMeshType"),
            "spritePackingTag": ti.get("spritePackingTag"), "spriteBorder": ti.get("spriteBorder"),
            "alignment": ti.get("alignment"), "spritePivot": ti.get("spritePivot"),
            "meta": ti, "sprites": []}
    assert mode in (0, 1, 2), "%s: sprite mode %s" % (rel, mode)
    if mode == 1:  # single sprite: the whole image
        al = ti["alignment"]
        info["sprites"].append({"name": proj.recycle_name(rel, 21300000) or os.path.splitext(os.path.basename(rel))[0],
                                "x": 0, "y": 0, "w": w, "h": h, "alignment": al,
                                "pivot": sprite_pivot(al, ti["spritePivot"]), "size_world": [f32(w / ppu), f32(h / ppu)]})
    elif mode == 2:  # sprite sheet
        for sp in ti["spriteSheet"]["sprites"]:
            r = sp["rect"]
            al = sp["alignment"]
            info["sprites"].append({"name": sp["name"], "x": r["x"], "y": r["y"], "w": r["width"], "h": r["height"],
                                    "alignment": al, "pivot": sprite_pivot(al, sp["pivot"]),
                                    "stored_pivot": [sp["pivot"]["x"], sp["pivot"]["y"]],
                                    "size_world": [f32(r["width"] / ppu), f32(r["height"] / ppu)]})
    return info


_MP3_BITRATES = {1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],  # MPEG 1 layer 3, kbit/s
                 2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]}  # MPEG 2 and 2.5 layer 3
_MP3_RATES = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}  # by version bits


def mp3_source(raw):
    """Length of an MP3 file as its header says. Layer 3 only (all eight game files are).
    The Xing or Info frame count when there is one, else the size of the audio divided by the bitrate
    (a constant bitrate estimate). Returns a dict without sample data."""
    p = 0
    if raw[:3] == b"ID3":  # skip the ID3v2 tag: 10 byte header, synchsafe size
        p = 10 + ((raw[6] << 21) | (raw[7] << 14) | (raw[8] << 7) | raw[9])
    while not (raw[p] == 0xFF and raw[p + 1] & 0xE0 == 0xE0 and (raw[p + 1] >> 1) & 3 == 1 and raw[p + 2] >> 4 not in (0, 15)):
        p += 1
        assert p < len(raw) - 4, "no MP3 frame found"
    ver, prot = (raw[p + 1] >> 3) & 3, raw[p + 1] & 1
    assert ver in _MP3_RATES, "reserved MPEG version"
    mpeg1 = ver == 3
    kbps = _MP3_BITRATES[1 if mpeg1 else 2][raw[p + 2] >> 4]
    rate = _MP3_RATES[ver][(raw[p + 2] >> 2) & 3]
    mono = (raw[p + 3] >> 6) & 3 == 3
    spf = 1152 if mpeg1 else 576
    side = (17 if mono else 32) if mpeg1 else (9 if mono else 17)
    tag = p + 4 + (0 if prot else 2) + side
    out = {"container": "mp3", "channels": 1 if mono else 2, "sample_rate": rate, "bitrate_kbps": kbps,
           "samples_per_frame": spf, "audio_starts_at": p, "file_bytes": len(raw)}
    if raw[tag:tag + 4] in (b"Xing", b"Info") and struct.unpack_from(">I", raw, tag + 4)[0] & 1:
        frames = struct.unpack_from(">I", raw, tag + 8)[0]
        out.update({"seconds": round(frames * spf / rate, 4), "seconds_from": "Xing/Info frame count",
                    "frames_in_header": frames})
    else:
        out.update({"seconds": round((len(raw) - p) * 8 / (kbps * 1000), 4),
                    "seconds_from": "file size / bitrate (constant bitrate estimate)"})
    return out


def wav_source(raw):
    """Facts about a WAVE file: format, channels, rate, bits, frames and the PCM bytes (key "pcm")."""
    fmt, data = build_extract.wav_parts(raw)
    tag, ch, rate, _, align, bits = fmt
    assert len(data) % align == 0
    return {"container": "wav", "format_tag": tag, "channels": ch, "sample_rate": rate, "bits_per_sample": bits,
            "frames": len(data) // align, "seconds": round(len(data) // align / rate, 4),
            "seconds_from": "data chunk size / frame size", "file_bytes": len(raw), "pcm": data}


def audio_info(proj, rel, clips):
    """Import settings plus the source file's own length and the build's decoded clip.
    clips: the AudioClips of the build by name (build_extract.audio_clips)."""
    m = proj.meta[rel]
    ai = m["AudioImporter"]
    with open(os.path.join(proj.assets, rel), "rb") as fh:
        raw = fh.read()
    src = wav_source(raw) if rel.lower().endswith(".wav") else mp3_source(raw)
    clip = clips[stem(rel)]
    # The port plays the build's audio. The source WAV is kept only when its PCM is the same as the build's.
    same = (src["container"] == "wav" and src["format_tag"] == 1 and src["bits_per_sample"] == 16
            and src["channels"] == clip["channels"] and src["sample_rate"] == clip["sample_rate"]
            and src["pcm"] == clip["pcm"])
    port_rel = rel if same else os.path.splitext(rel)[0] + ".wav"
    port_bytes = raw if same else clip["wav"]
    info = {"file": rel, "guid": m["guid"], "bytes": len(raw), "import": ai,
            "is_3d": bool(ai.get("3D")), "stream": ai.get("stream"), "format": ai.get("format"),
            "quality": ai.get("quality"), "forceToMono": ai.get("forceToMono"), "loopable": ai.get("loopable"),
            "source": {k: v for k, v in src.items() if k != "pcm"},
            "build": {k: v for k, v in clip.items() if k not in ("pcm", "wav")},
            "port_file": port_rel, "port_file_origin": "source" if same else "build",
            "port_file_bytes": len(port_bytes), "port_data_chunk_bytes": len(clip["pcm"])}
    info["seconds_source"] = src["seconds"]
    info["seconds_build"] = round(clip["seconds"], 4)
    info["seconds_difference"] = round(clip["seconds"] - src["seconds"], 4)
    info["sample_rate_differs"] = src["sample_rate"] != clip["sample_rate"]
    if src["container"] == "wav" and not same and src["format_tag"] == 3:  # float source, 16 bit build
        assert src["bits_per_sample"] == 32 and src["frames"] == clip["frames"]
        f, i = array.array("f"), array.array("h")
        f.frombytes(src["pcm"])
        i.frombytes(clip["pcm"])
        diffs = [abs(int(round(x * 32768)) - y) for x, y in zip(f, i)]
        info["float_to_16_bit"] = {"max_abs_difference_lsb": max(diffs), "samples_that_differ": sum(1 for d in diffs if d),
                                  "samples": len(diffs)}
        assert max(diffs) <= 1, "the build's Menu4 is not the 16 bit form of the source"
    return info, port_bytes


def font_info(proj, rel):
    m = proj.meta[rel]
    path = os.path.join(proj.assets, rel)
    return {"file": rel, "guid": m["guid"], "bytes": os.path.getsize(path),
            "import": m["TrueTypeFontImporter"], "ttf": ttf_metrics(path)}


def ttf_metrics(path):
    """Vertical metrics and advance widths from the TrueType tables head, hhea, hmtx, OS/2 (standard library only).
    Values are in font units. unitsPerEm says how many units one em is."""
    with open(path, "rb") as fh:
        b = fh.read()
    n = struct.unpack_from(">H", b, 4)[0]
    tab = {}
    for i in range(n):
        tag, _, off, ln = struct.unpack_from(">4sIII", b, 12 + 16 * i)
        tab[tag.decode("latin1")] = (off, ln)
    head, hhea, hmtx = tab["head"][0], tab["hhea"][0], tab["hmtx"][0]
    upem = struct.unpack_from(">H", b, head + 18)[0]
    asc, desc, gap, adv_max = struct.unpack_from(">hhhH", b, hhea + 4)
    nh = struct.unpack_from(">H", b, hhea + 34)[0]
    adv = [struct.unpack_from(">H", b, hmtx + 4 * k)[0] for k in range(nh)]
    nglyphs = struct.unpack_from(">H", b, tab["maxp"][0] + 4)[0]
    widths = sorted(set(a for a in adv if a))
    assert len(widths) == 1, "not a monospace font: the non-zero advance widths are %s" % widths
    out = {"unitsPerEm": upem, "hhea_ascent": asc, "hhea_descent": desc, "hhea_lineGap": gap,
           "hhea_advanceWidthMax": adv_max, "numberOfHMetrics": nh, "glyphs": nglyphs,
           "advance_width": widths[0],
           "glyphs_with_advance_zero": sum(1 for a in adv if a == 0)}
    assert nh == nglyphs, "hmtx does not cover every glyph"
    if "OS/2" in tab:
        o = tab["OS/2"][0]
        out["os2_version"] = struct.unpack_from(">H", b, o)[0]
        out["os2_xAvgCharWidth"] = struct.unpack_from(">h", b, o + 2)[0]
        out["os2_typoAscender"], out["os2_typoDescender"], out["os2_typoLineGap"] = struct.unpack_from(">hhh", b, o + 68)
        out["os2_winAscent"], out["os2_winDescent"] = struct.unpack_from(">HH", b, o + 74)
    out["line_height_units"] = asc - desc + gap
    return out


def scan_refs(obj, known, hits):
    """Collect the known asset paths mentioned in a dump (strings like 'Sprites/a.png#name')."""
    if isinstance(obj, dict):
        obj = list(obj.values())
    if isinstance(obj, list):
        for v in obj:
            scan_refs(v, known, hits)
    elif isinstance(obj, str):
        base = obj.split("#", 1)[0]
        if base in known:
            hits.add(base)


def only(objects, type_name):
    found = [o for o in objects if o["type"] == type_name]
    assert len(found) == 1, "expected one %s, found %d" % (type_name, len(found))
    return found[0]["fields"]


def material_summary(d):
    f = only(d["objects"], "Material")
    sp = f["m_SavedProperties"]
    return {"colors": {k["name"]: v for k, v in sp["m_Colors"]},
            "floats": {k["name"]: v for k, v in sp["m_Floats"]},
            "textures": {k["name"]: v["m_Texture"] for k, v in sp["m_TexEnvs"]},
            "shader": f["m_Shader"], "keywords": f["m_ShaderKeywords"], "name": f["m_Name"]}


def clip_summary(d):
    f = only(d["objects"], "AnimationClip")
    settings = f.get("m_AnimationClipSettings", {})  # the legacy emitter clip has none
    keys = [{"time": k["time"], "sprite": k["value"]} for c in f.get("m_PPtrCurves", []) for k in c["curve"]]
    return {"name": f["m_Name"], "sample_rate": f["m_SampleRate"], "wrap_mode": f["m_WrapMode"],
            "loop_time": settings.get("m_LoopTime"), "start_time": settings.get("m_StartTime"),
            "stop_time": settings.get("m_StopTime"), "key_count": len(keys), "keys": keys,
            "float_curves": f["m_FloatCurves"], "events": f["m_Events"]}


def controller_summary(d):
    states = {o["id"]: {"name": o["fields"]["m_Name"], "speed": o["fields"]["m_Speed"],
                        "motion": o["fields"]["m_Motions"][0]}
              for o in d["objects"] if o["type"] == "AnimatorState"}
    machines = [o["fields"] for o in d["objects"] if o["type"] == "AnimatorStateMachine"]
    assert len(machines) == 1, "one state machine expected"
    m = machines[0]
    return {"states": [states[k] for k in sorted(states)], "default_state": m["m_DefaultState"],
            "state_machines": [{"name": m["m_Name"], "transitions": m["m_OrderedTransitions"], "states": m["m_States"]}],
            "note": "no transitions, no parameters; the code calls Animator.Play"}


def get_path(base, field):
    """Walk a dotted path. A token matches a key exactly first, then as name[index]."""
    cur = base
    for tok in field.split("."):
        if isinstance(cur, dict) and tok in cur:
            cur = cur[tok]
            continue
        m = re.match(r"^(.*)\[(\d+)\]$", tok)
        assert m, "no key %r (in %s)" % (tok, field)
        if m.group(1):
            cur = cur[m.group(1)]
        cur = cur[int(m.group(2))]
    return cur


class Dumps:
    """Reads back the JSON dumps so values.h only ever takes values from them."""

    def __init__(self, ext):
        self.ext = ext
        self.cache = {}

    def load(self, rel):
        if rel not in self.cache:
            self.cache[rel] = read_json(os.path.join(self.ext, rel))
        return self.cache[rel]

    def get(self, src):
        if src.startswith("derived:"):
            kind, _, loc = src[len("derived:"):].partition("|")
            f, _, go = loc.partition("#")
            clip = self.get("%s#%s:AudioSource:m_audioClip" % (f, go))
            pan = self.get("%s#%s:AudioSource:panLevelCustomCurve.m_Curve[0].value" % (f, go))
            is_3d = self.load("assets/%s.json" % clip)["is_3d"]
            if kind == "spatial_blend":
                return pan if is_3d else 0.0
            assert kind == "reverb_zone_mix", kind
            return 1.0 if is_3d else 0.0
        rel, _, rest = src.partition("#")
        d = self.load(rel)
        if rel.startswith(("scenes/", "prefabs/")):
            go, comp, field = rest.split(":", 2)
            [node] = [n for n in d["gameobjects"] if n["path"] == go]
            if comp == "@node":
                return get_path(node, field)
            if comp == "@transform":
                return get_path(node["transform"], field)
            [c] = [c for c in node["components"] if c["key"] == comp]
            if field.startswith("derived."):
                return get_path(c["derived"], field[len("derived."):])
            return get_path(c["fields"], field)
        if rel.startswith("settings/"):
            return get_path(d["objects"][0]["fields"], rest)
        return get_path(d, rest)


def dump_asset(proj, path):
    """Dump a whole non-scene asset (animation clip, controller, material) as typetrees."""
    uf = UFile(proj, path)
    return {"file": uf.rel, "objects": [{"id": pid, "type": uf.cls[pid], "fields": uf.convert(uf.tree[pid])}
                                        for pid in sorted(uf.cls)]}


def dump_settings(folder, outdir):
    for fn in sorted(os.listdir(folder)):
        if fn.endswith(".asset"):
            objs = sorted(UnityPy.load(os.path.join(folder, fn)).objects, key=lambda x: x.path_id)
            write_json(os.path.join(outdir, fn[:-6] + ".json"), {
                "file": fn, "objects": [{"type": o.type.name, "fields": clean(o.read_typetree())} for o in objs]})
    with open(os.path.join(folder, "ProjectVersion.txt")) as fh:
        write_json(os.path.join(outdir, "ProjectVersion.json"), {"text": fh.read().strip().splitlines()})


# --------------------------------------------------------------------------
# THE value table: (C name, source in the dumps). Everything in values.h comes from here.
#
# Source format:
#   scenes/<Scene>.json#<GameObject path>:<Component key>:<field path>
#   prefabs/<Prefab>.json#<GameObject path>:<Component key>:<field path>
#     Component key "@transform" reads the computed local/world transform,
#     "@node" reads the GameObject (tag, layer, active). A field path starting with
#     "derived." reads the world-scaled values added by this script.
#   settings/<Name>.json#<field path>, materials/<Name>.json#<path>, animation/...
# --------------------------------------------------------------------------

G = "scenes/Game.json"


def tr(p, f, go, euler=False):
    rows = [(p + "_POS", "%s#%s:@transform:world_position" % (f, go)),
            (p + "_SCALE", "%s#%s:@transform:world_scale" % (f, go))]
    if euler:
        rows.append((p + "_EULER", "%s#%s:@transform:world_euler" % (f, go)))
    return rows


def local_tr(p, f, go):
    return [(p + "_LOCAL_POS", "%s#%s:@transform:local_position" % (f, go)),
            (p + "_LOCAL_SCALE", "%s#%s:@transform:local_scale" % (f, go))]


def audio(p, f, go, curve=False):
    """AudioSource rows. The _SPATIAL_BLEND and _REVERB_ZONE_MIX rows follow the 2016 Unity 5.4.3 build."""
    c = "%s#%s:AudioSource:" % (f, go)
    rows = [(p + "_CLIP", c + "m_audioClip"), (p + "_VOLUME", c + "m_Volume"), (p + "_PITCH", c + "m_Pitch"),
            (p + "_LOOP", c + "Loop"), (p + "_PLAY_ON_AWAKE", c + "m_PlayOnAwake"), (p + "_ENABLED", c + "m_Enabled"),
            (p + "_MIN_DISTANCE", c + "MinDistance"), (p + "_MAX_DISTANCE", c + "MaxDistance"),
            (p + "_ROLLOFF_MODE", c + "rolloffMode"), (p + "_PAN2D", c + "Pan2D"),
            (p + "_PRIORITY", c + "Priority"), (p + "_DOPPLER", c + "DopplerLevel"),
            (p + "_MUTE", c + "Mute"), (p + "_SPREAD", c + "spreadCustomCurve.m_Curve[0].value"),
            (p + "_PAN_LEVEL_IN_SOURCE_SCENE", c + "panLevelCustomCurve.m_Curve[0].value"),
            (p + "_SPATIAL_BLEND", "derived:spatial_blend|%s#%s" % (f, go)),
            (p + "_REVERB_ZONE_MIX", "derived:reverb_zone_mix|%s#%s" % (f, go))]
    if curve:
        rows.append((p + "_ROLLOFF_CURVE", c + "rolloffCustomCurve.m_Curve"))
    return rows



def rigid(p, f, go):
    c = "%s#%s:Rigidbody2D:" % (f, go)
    return [(p + "_MASS", c + "m_Mass"), (p + "_LINEAR_DRAG", c + "m_LinearDrag"),
            (p + "_ANGULAR_DRAG", c + "m_AngularDrag"), (p + "_GRAVITY_SCALE", c + "m_GravityScale"),
            (p + "_FIXED_ANGLE", c + "m_FixedAngle"), (p + "_KINEMATIC", c + "m_IsKinematic"),
            (p + "_SLEEPING_MODE", c + "m_SleepingMode"), (p + "_INTERPOLATE", c + "m_Interpolate"),
            (p + "_COLLISION_DETECTION", c + "m_CollisionDetection")]


def circle(p, f, go):
    c = "%s#%s:CircleCollider2D:" % (f, go)
    return [(p + "_RADIUS", c + "m_Radius"), (p + "_CENTER", c + "m_Center"), (p + "_IS_TRIGGER", c + "m_IsTrigger"),
            (p + "_RADIUS_WORLD", c + "derived.radius_world"), (p + "_OFFSET_WORLD", c + "derived.offset_world")]


def box(p, f, go):
    c = "%s#%s:BoxCollider2D:" % (f, go)
    return [(p + "_SIZE", c + "m_Size"), (p + "_CENTER", c + "m_Center"), (p + "_IS_TRIGGER", c + "m_IsTrigger"),
            (p + "_SIZE_WORLD", c + "derived.size_world"), (p + "_OFFSET_WORLD", c + "derived.offset_world")]


def poly(p, f, go):
    c = "%s#%s:PolygonCollider2D:" % (f, go)
    return [(p + "_IS_TRIGGER", c + "m_IsTrigger"), (p + "_SOURCE_POINT_COUNT", c + "derived.paths[0].source_points"),
            (p + "_PIECES", c + "derived.paths[0].pieces")]


def sprite(p, f, go):
    c = "%s#%s:SpriteRenderer:" % (f, go)
    return [(p + "_SPRITE", c + "m_Sprite"), (p + "_COLOR", c + "m_Color"), (p + "_SORTING_ORDER", c + "m_SortingOrder"),
            (p + "_ENABLED", c + "m_Enabled")]


def guitext(p, f, go):
    c = "%s#%s:GUIText:" % (f, go)
    return [(p + "_TEXT", c + "m_Text"), (p + "_FONT", c + "m_Font"), (p + "_FONT_SIZE", c + "m_FontSize"),
            (p + "_FONT_STYLE", c + "m_FontStyle"), (p + "_ANCHOR", c + "m_Anchor"), (p + "_ALIGNMENT", c + "m_Alignment"),
            (p + "_COLOR", c + "m_Color"), (p + "_LINE_SPACING", c + "m_LineSpacing"),
            (p + "_PIXEL_OFFSET", c + "m_PixelOffset"), (p + "_ENABLED", c + "m_Enabled"),
            (p + "_TAB_SIZE", c + "m_TabSize"), (p + "_PIXEL_CORRECT", c + "m_PixelCorrect"),
            (p + "_VIEWPORT_POS", "%s#%s:@transform:world_position" % (f, go))]


def guitex(p, f, go):
    c = "%s#%s:GUITexture:" % (f, go)
    return [(p + "_TEXTURE", c + "m_Texture"), (p + "_COLOR", c + "m_Color"), (p + "_PIXEL_INSET", c + "m_PixelInset"),
            (p + "_ENABLED", c + "m_Enabled"), (p + "_VIEWPORT_POS", "%s#%s:@transform:world_position" % (f, go))]


def camera(p, f, go="Main Camera"):
    c = "%s#%s:Camera:" % (f, go)
    return [(p + "_ORTHO_SIZE", c + "orthographic size"), (p + "_ORTHOGRAPHIC", c + "orthographic"),
            (p + "_BACKGROUND", c + "m_BackGroundColor"), (p + "_CLEAR_FLAGS", c + "m_ClearFlags"),
            (p + "_NEAR", c + "near clip plane"), (p + "_FAR", c + "far clip plane"),
            (p + "_VIEWPORT_RECT", c + "m_NormalizedViewPortRect"), (p + "_DEPTH", c + "m_Depth")] + \
        tr(p, f, go)


def emitter(p, f, go):
    e = "%s#%s:EllipsoidParticleEmitter:" % (f, go)
    a = "%s#%s:ParticleAnimator:" % (f, go)
    r = "%s#%s:ParticleRenderer:" % (f, go)
    rows = [(p + "_" + n, e + k) for n, k in [
        ("EMIT", "m_Emit"), ("ONE_SHOT", "m_OneShot"), ("MIN_EMISSION", "minEmission"), ("MAX_EMISSION", "maxEmission"),
        ("MIN_ENERGY", "minEnergy"), ("MAX_ENERGY", "maxEnergy"), ("MIN_SIZE", "minSize"), ("MAX_SIZE", "maxSize"),
        ("WORLD_SPACE", "Simulate in Worldspace?"), ("EMITTER_VELOCITY_SCALE", "emitterVelocityScale"),
        ("LOCAL_VELOCITY", "localVelocity"), ("WORLD_VELOCITY", "worldVelocity"), ("RND_VELOCITY", "rndVelocity"),
        ("TANGENT_VELOCITY", "tangentVelocity"), ("ELLIPSOID", "m_Ellipsoid"), ("MIN_EMITTER_RANGE", "m_MinEmitterRange"),
        ("RND_ROTATION", "rndRotation"), ("ANGULAR_VELOCITY", "angularVelocity"),
        ("RND_ANGULAR_VELOCITY", "rndAngularVelocity")]]
    rows += [(p + "_ANIM_" + n, a + k) for n, k in [
        ("ANIMATE_COLOR", "Does Animate Color?"), ("AUTODESTRUCT", "autodestruct"), ("DAMPING", "damping"),
        ("FORCE", "force"), ("RND_FORCE", "rndForce"), ("SIZE_GROW", "sizeGrow"),
        ("COLOR_0", "colorAnimation[0]"), ("COLOR_1", "colorAnimation[1]"), ("COLOR_2", "colorAnimation[2]"),
        ("COLOR_3", "colorAnimation[3]"), ("COLOR_4", "colorAnimation[4]"),
        ("LOCAL_ROTATION_AXIS", "localRotationAxis"), ("WORLD_ROTATION_AXIS", "worldRotationAxis")]]
    rows += [(p + "_RENDER_" + n, r + k) for n, k in [
        ("LENGTH_SCALE", "m_LengthScale"), ("MAX_PARTICLE_SIZE", "m_MaxParticleSize"),
        ("STRETCH", "m_StretchParticles"), ("VELOCITY_SCALE", "m_VelocityScale"),
        ("CAMERA_VELOCITY_SCALE", "m_CameraVelocityScale"), ("MATERIAL", "m_Materials[0]"),
        ("UV_ANIMATION", "UV Animation")]]
    return rows


def reverb(p, f, go):
    c = "%s#%s:AudioReverbZone:" % (f, go)
    return [(p + "_" + n, c + k) for n, k in [
        ("PRESET", "m_ReverbPreset"), ("MIN_DISTANCE", "m_MinDistance"), ("MAX_DISTANCE", "m_MaxDistance"),
        ("ROOM", "m_Room"), ("ROOM_HF", "m_RoomHF"), ("ROOM_LF", "m_RoomLF"), ("DECAY_TIME", "m_DecayTime"),
        ("DECAY_HF_RATIO", "m_DecayHFRatio"), ("REFLECTIONS", "m_Reflections"),
        ("REFLECTIONS_DELAY", "m_ReflectionsDelay"), ("REVERB", "m_Reverb"), ("REVERB_DELAY", "m_ReverbDelay"),
        ("HF_REFERENCE", "m_HFReference"), ("LF_REFERENCE", "m_LFReference"), ("DIFFUSION", "m_Diffusion"),
        ("DENSITY", "m_Density"), ("ROLLOFF_FACTOR", "m_RoomRolloffFactor"), ("ENABLED", "m_Enabled")]]


def input_axis(p, idx):
    c = "settings/InputManager.json#m_Axes[%d]." % idx
    return [(p + "_" + n, c + k) for n, k in [
        ("SENSITIVITY", "sensitivity"), ("GRAVITY", "gravity"), ("DEAD", "dead"), ("SNAP", "snap"),
        ("INVERT", "invert"), ("TYPE", "type"), ("AXIS", "axis"), ("JOY_NUM", "joyNum"),
        ("POSITIVE_BUTTON", "positiveButton"), ("NEGATIVE_BUTTON", "negativeButton"),
        ("ALT_POSITIVE_BUTTON", "altPositiveButton"), ("ALT_NEGATIVE_BUTTON", "altNegativeButton")]]


def build_values():
    R = []
    # ---- project, time, physics, audio, quality, input
    R += [("WINDOW_WIDTH", "settings/ProjectSettings.json#defaultScreenWidth"),
          ("WINDOW_HEIGHT", "settings/ProjectSettings.json#defaultScreenHeight"),
          ("WINDOW_WIDTH_WEB", "settings/ProjectSettings.json#defaultScreenWidthWeb"),
          ("WINDOW_HEIGHT_WEB", "settings/ProjectSettings.json#defaultScreenHeightWeb"),
          ("WINDOW_RESIZABLE", "settings/ProjectSettings.json#resizableWindow"),
          ("WINDOW_DEFAULT_FULLSCREEN", "settings/ProjectSettings.json#defaultIsFullScreen"),
          ("RUN_IN_BACKGROUND", "settings/ProjectSettings.json#runInBackground"),
          ("PRODUCT_NAME", "settings/ProjectSettings.json#productName"),
          ("COMPANY_NAME", "settings/ProjectSettings.json#companyName"),
          ("SHOW_UNITY_SPLASH", "settings/ProjectSettings.json#m_ShowUnitySplashScreen"),
          ("FIXED_TIMESTEP", "settings/TimeManager.json#Fixed Timestep"),
          ("MAX_ALLOWED_TIMESTEP", "settings/TimeManager.json#Maximum Allowed Timestep"),
          ("TIME_SCALE_START", "settings/TimeManager.json#m_TimeScale"),
          ("PHYSICS_GRAVITY", "settings/Physics2DSettings.json#m_Gravity"),
          ("PHYSICS_VELOCITY_ITERATIONS", "settings/Physics2DSettings.json#m_VelocityIterations"),
          ("PHYSICS_POSITION_ITERATIONS", "settings/Physics2DSettings.json#m_PositionIterations"),
          ("PHYSICS_VELOCITY_THRESHOLD", "settings/Physics2DSettings.json#m_VelocityThreshold"),
          ("PHYSICS_MAX_LINEAR_CORRECTION", "settings/Physics2DSettings.json#m_MaxLinearCorrection"),
          ("PHYSICS_MAX_ANGULAR_CORRECTION", "settings/Physics2DSettings.json#m_MaxAngularCorrection"),
          ("PHYSICS_MAX_TRANSLATION_SPEED", "settings/Physics2DSettings.json#m_MaxTranslationSpeed"),
          ("PHYSICS_MAX_ROTATION_SPEED", "settings/Physics2DSettings.json#m_MaxRotationSpeed"),
          ("PHYSICS_BAUMGARTE_SCALE", "settings/Physics2DSettings.json#m_BaumgarteScale"),
          ("PHYSICS_BAUMGARTE_TOI_SCALE", "settings/Physics2DSettings.json#m_BaumgarteTimeOfImpactScale"),
          ("PHYSICS_TIME_TO_SLEEP", "settings/Physics2DSettings.json#m_TimeToSleep"),
          ("PHYSICS_LINEAR_SLEEP_TOLERANCE", "settings/Physics2DSettings.json#m_LinearSleepTolerance"),
          ("PHYSICS_ANGULAR_SLEEP_TOLERANCE", "settings/Physics2DSettings.json#m_AngularSleepTolerance"),
          ("PHYSICS_RAYCASTS_HIT_TRIGGERS", "settings/Physics2DSettings.json#m_RaycastsHitTriggers"),
          ("AUDIO_MASTER_VOLUME", "settings/AudioManager.json#m_Volume"),
          ("AUDIO_SPEED_OF_SOUND", "settings/AudioManager.json#m_SpeedOfSound"),
          ("AUDIO_DOPPLER_FACTOR", "settings/AudioManager.json#Doppler Factor"),
          ("AUDIO_ROLLOFF_SCALE", "settings/AudioManager.json#Rolloff Scale"),
          ("QUALITY_CURRENT_INDEX", "settings/QualitySettings.json#m_CurrentQuality"),
          ("QUALITY_NAME", "settings/QualitySettings.json#m_QualitySettings[0].name"),
          ("QUALITY_VSYNC_COUNT", "settings/QualitySettings.json#m_QualitySettings[0].vSyncCount"),
          ("QUALITY_ANTI_ALIASING", "settings/QualitySettings.json#m_QualitySettings[0].antiAliasing")]
    R += input_axis("INPUT_KEY_HORIZONTAL", 0) + input_axis("INPUT_KEY_VERTICAL", 1)
    R += input_axis("INPUT_STICK_HORIZONTAL", 10) + input_axis("INPUT_STICK_VERTICAL", 11)

    # ---- Intro
    I = "scenes/Intro.json"
    R += camera("INTRO_CAMERA", I) + tr("INTRO_LOGO", I, "superdamage") + sprite("INTRO_LOGO", I, "superdamage")
    R += audio("INTRO_SOUND", I, "Explosion_04")

    # ---- Title
    T = "scenes/Title.json"
    R += camera("TITLE_CAMERA", T)
    for p, go in [("TITLE_PLAY", "PLAY"), ("TITLE_SD", "SD"), ("TITLE_WR", "WR"), ("TITLE_NAME", "Title"),
                  ("TITLE_LD", "LD")]:
        R += guitext(p, T, go)
    for p, go in [("TITLE_PLAY_BUTTON", "PLAY"), ("TITLE_SD_BUTTON", "SD")]:
        c = "%s#%s:GUITextButton:" % (T, go)
        R += [(p + "_HIGHLIGHT_COLOR", c + "highlightedColor"), (p + "_NORMAL_COLOR", c + "normalColor"),
              (p + "_IDENTIFIER", c + "identifier"), (p + "_HIGHLIGHT_SFX", c + "higlightSFX"),
              (p + "_CLICK_SFX", c + "clickSFX")]
    R += audio("TITLE_CLICK_SOUND", T, "clickSound") + audio("TITLE_HIGHLIGHT_SOUND", T, "highlightSound")

    # ---- Story1, Story2, Death, Win
    for s, S in [("STORY1", "scenes/Story1.json"), ("STORY2", "scenes/Story2.json")]:
        R += camera(s + "_CAMERA", S) + guitex(s + "_IMAGE", S, "gfx") + audio(s + "_SOUND", S, "Menu4")
        c = "%s#gfx:NextSceneOnSpace:" % S
        R += [(s + "_DISMISS_DURATION", c + "dismissDuration"), (s + "_NEXT_SCENE", c + "nextScene")]
    D = "scenes/Death.json"
    R += camera("DEATH_CAMERA", D) + guitex("DEATH_IMAGE", D, "gfx") + \
        audio("DEATH_SOUND", D, "Victory_-arewhyae-8093_hifi")
    W = "scenes/Win.json"
    R += camera("WIN_CAMERA", W) + guitex("WIN_IMAGE", W, "gfx") + \
        audio("WIN_MUSIC", W, "bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm") + \
        audio("WIN_HORN", W, "FOLEY HORN PARTY TOY BLOW OUT 01")

    # ---- Game: camera, night overlay, cutscene images
    R += camera("GAME_CAMERA", G)
    R += [("NIGHT_OVERLAY_EULER", G + "#Main Camera/nightosphere:@transform:world_euler"),
          ("NIGHT_OVERLAY_LOCAL_POS", G + "#Main Camera/nightosphere:@transform:local_position"),
          ("NIGHT_OVERLAY_COLOR", "materials/nightMat.mat.json#summary.colors._Color"),
          ("NIGHT_OVERLAY_MESH", G + "#Main Camera/nightosphere:MeshFilter:m_Mesh"),
          ("NIGHT_OVERLAY_ENABLED", G + "#Main Camera/nightosphere:MeshRenderer:m_Enabled")]
    R += tr("NIGHT_OVERLAY", G, "Main Camera/nightosphere")
    R += tr("CUTSCENES", G, "Main Camera/cutscenes") + local_tr("CUTSCENES", G, "Main Camera/cutscenes")
    for p, go in [("CUT_COULD_NOT_DECODE", "spr_couldNotDecode"), ("CUT_NEW_MESSAGE", "spr_newMessage"),
                  ("CUT_COULD_DECODE", "spr_couldDecode"), ("CUT_TUTORIAL", "spr_tutorial")]:
        R += sprite(p, G, "Main Camera/cutscenes/" + go) + \
            tr(p, G, "Main Camera/cutscenes/" + go) + local_tr(p, G, "Main Camera/cutscenes/" + go)
    R += local_tr("CAMERA_FOCUS", G, "Main Camera/CameraFocus")

    # ---- Game: LevelLoader, Mars, Earth, RoverScreen, RockGenerator
    c = G + "#Level:LevelLoader:"
    R += [("LEVEL_LOADER_CURRENT_LEVEL", c + "currentLevel"), ("LEVEL_LOADER_GAP", c + "gap"),
          ("LEVEL_LOADER_INITIAL_QUESTION", c + "currentQuestion"), ("LEVEL_LOADER_SENTENCE_SIZE", c + "sentenceSize")]
    R += tr("LEVEL", G, "Level") + tr("LETTERS", G, "Level/letters")
    c = G + "#Mars:Mars:"
    R += [("MARS_TILE_SIZE", c + "tileSize"), ("MARS_NUM_TILES_X", c + "numTilesX"), ("MARS_NUM_TILES_Y", c + "numTilesY"),
          ("MARS_TIME_SCALE", c + "timeScale"), ("MARS_SECONDS_SPENT", c + "secondsSpent"),
          ("MARS_TILE_SPRITES", c + "sprites"), ("MARS_TILE_TEMPLATE", c + "tileTemplate")]
    R += tr("MARS", G, "Mars") + guitext("SOL_COUNTER", G, "Mars/solCounter")
    R += tr("MARS_TILE", "prefabs/MarsTile.json", "MarsTile") + sprite("MARS_TILE", "prefabs/MarsTile.json", "MarsTile")
    c = G + "#RoverScreen:RoverScreen:"
    R += [("ROVER_SCREEN_STARTING_LIFE_SUPPORT", c + "startingLifeSupport")]
    R += guitex("ROVER_SCREEN_BACKGROUND", G, "RoverScreen") + guitext("ROVER_SCREEN_TEXT", G, "RoverScreen/screenGUIText")
    R += tr("ROVER_SCREEN", G, "RoverScreen")
    c = G + "#Earth:Earth:"
    R += [("EARTH_MAX_MESSAGE_SOUND_VOLUME", c + "maxMessageSoundVolume"), ("EARTH_TELEMETRY_DELAY", c + "telemetryDelay"),
          ("EARTH_DEBUG_FORCE_NEXT_LEVEL", c + "debugForceNextLevel"),
          ("EARTH_TEST_IGNORE_ROVER_ENTRANCE", c + "testIgnoreRoverEnterance"),
          ("EARTH_ROCKET_PREFAB", c + "rocketPrefab")]
    R += guitext("VISIBILITY_INDICATOR", G, "Earth/visibiltyIndicator")
    c = G + "#Rocks:RockGenerator:"
    R += [("ROCK_GENERATOR_NUM_ROCKS", c + "numRocks"), ("ROCK_GENERATOR_ROCK_PREFAB", c + "rockPrefab"),
          ("ROCK_GENERATOR_SHATTER_PREFAB", c + "rockShatterAnimationPrefab")]
    R += tr("ROCKS", G, "Rocks")
    R += audio("GAME_START_SOUND", G, "Menu4")
    R += sprite("DRAG_TARGET_SCENE", G, "RockDragTarget") + tr("DRAG_TARGET_SCENE", G, "RockDragTarget")
    c = G + "#RockDragTarget:SnapToTarget:"
    R += [("DRAG_TARGET_DASH_SPRITE", c + "dashTarget"), ("DRAG_TARGET_DOT_SPRITE", c + "dotTarget")]
    R += [("CAM_FOCUS_PLAYER_IN_ROVER", G + "#Main Camera:CamerFocusSwitcher:playerIsInRover")]

    # ---- Game: Player
    R += tr("PLAYER", G, "Player") + rigid("PLAYER_RB", G, "Player")
    R += circle("PLAYER_TRIGGER", G, "Player") + poly("PLAYER_BODY", G, "Player")
    R += sprite("PLAYER", G, "Player")
    R += [("PLAYER_ANIMATOR_CONTROLLER", G + "#Player:Animator:m_Controller"),
          ("PLAYER_ANIMATOR_APPLY_ROOT_MOTION", G + "#Player:Animator:m_ApplyRootMotion"),
          ("PLAYER_TAG", G + "#Player:@node:tag")]
    R += audio("PLAYER_STRAIN", G, "Player") + audio("PLAYER_FOOTSTEPS", G, "Player/footsteps")
    R += audio("PLAYER_AMBIENCE", G, "Player/pivot/Sci-fi_AmbienceLoop1")
    R += tr("PLAYER_PIVOT", G, "Player/pivot", euler=True) + local_tr("PLAYER_PIVOT", G, "Player/pivot")
    R += tr("PLAYER_LISTENER", G, "Player/pivot/audioListener") + local_tr("PLAYER_FOOTSTEPS", G, "Player/footsteps")

    # ---- Game: Rover
    R += tr("ROVER", G, "Rover") + audio("ROVER_NEW_MESSAGE", G, "Rover") + reverb("ROVER_REVERB", G, "Rover/Reverb")
    R += tr("ROVER_BODY", G, "Rover/RoverBody") + sprite("ROVER_BODY", G, "Rover/RoverBody") + \
        rigid("ROVER_BODY_RB", G, "Rover/RoverBody") + box("ROVER_BODY_TRIGGER", G, "Rover/RoverBody") + \
        [("ROVER_BODY_TAG", G + "#Rover/RoverBody:@node:tag")]
    R += tr("ROVER_ROOF", G, "Rover/bis_rover_v2_roof") + sprite("ROVER_ROOF", G, "Rover/bis_rover_v2_roof") + \
        rigid("ROVER_ROOF_RB", G, "Rover/bis_rover_v2_roof") + poly("ROVER_ROOF_COLLIDER", G, "Rover/bis_rover_v2_roof") + \
        [("ROVER_ROOF_TAG", G + "#Rover/bis_rover_v2_roof:@node:tag")]
    R += tr("ROCK_REPELLER", G, "Rover/rockRepeller") + circle("ROCK_REPELLER", G, "Rover/rockRepeller") + \
        [("ROCK_REPELLER_TAG", G + "#Rover/rockRepeller:@node:tag")]
    R += tr("ROVER_FOCUS_TARGET", G, "Rover/roverFocusTarget")
    p = "Rover/playerInsideRoverTrigger"
    R += tr("ROVER_INSIDE_TRIGGER", G, p) + circle("ROVER_INSIDE_TRIGGER", G, p)
    c = G + "#" + p + ":RoverInsideTrigger:"
    R += [("ROVER_INSIDE_SET_ON_PITCH", c + "setOnPitch"), ("ROVER_INSIDE_SET_OFF_PITCH", c + "setOffPitch")]
    R += audio("ROVER_ENTER_SOUND", G, p) + audio("ROVER_CHATTER", G, p + "/chatter")
    R += tr("ROVER_UPDATES_INDICATOR", G, "Rover/updatesIndicator") + sprite("ROVER_UPDATES_INDICATOR", G, "Rover/updatesIndicator")

    # ---- Prefabs
    P = "prefabs/RockPrefab.json"
    R += tr("ROCK", P, "RockPrefab") + rigid("ROCK_RB", P, "RockPrefab") + circle("ROCK", P, "RockPrefab") + \
        sprite("ROCK", P, "RockPrefab") + audio("ROCK_DRAG", P, "RockPrefab", curve=True)
    c = P + "#RockPrefab:DistanceJoint2D:"
    R += [("ROCK_JOINT_DISTANCE", c + "m_Distance"), ("ROCK_JOINT_ANCHOR", c + "m_Anchor"),
          ("ROCK_JOINT_CONNECTED_ANCHOR", c + "m_ConnectedAnchor"), ("ROCK_JOINT_MAX_DISTANCE_ONLY", c + "m_MaxDistanceOnly"),
          ("ROCK_JOINT_COLLIDE_CONNECTED", c + "m_CollideConnected"), ("ROCK_JOINT_ENABLED", c + "m_Enabled"),
          ("ROCK_DARK_SPRITE", P + "#RockPrefab:Rock:darkSprite"), ("ROCK_LIGHT_SPRITE", P + "#RockPrefab:Rock:lightSprite"),
          ("ROCK_SHADOW_PREFAB", P + "#RockPrefab:Rock:rockShadowPrefab"),
          ("ROCK_MATERIAL", P + "#RockPrefab:SpriteRenderer:m_Materials[0]")]
    P = "prefabs/RockShadowPrefab.json"
    R += tr("ROCK_SHADOW", P, "RockShadowPrefab") + sprite("ROCK_SHADOW", P, "RockShadowPrefab")
    P = "prefabs/RockPlaceholderPrefab.json"
    R += tr("PLACEHOLDER", P, "RockPlaceholderPrefab") + sprite("PLACEHOLDER", P, "RockPlaceholderPrefab") + \
        circle("PLACEHOLDER", P, "RockPlaceholderPrefab") + audio("PLACEHOLDER_CORRECT", P, "RockPlaceholderPrefab")
    c = P + "#RockPlaceholderPrefab:RockPlaceholder:"
    R += [("PLACEHOLDER_DASH_SPRITE", c + "dashSprite"), ("PLACEHOLDER_DOT_SPRITE", c + "dotSprite"),
          ("PLACEHOLDER_INCORRECT_SPRITE", c + "incorrectSprite"), ("PLACEHOLDER_CORRECT_SPRITE", c + "correctSprite")]
    P = "prefabs/RockDragTarget.json"
    R += tr("DRAG_TARGET", P, "RockDragTarget") + sprite("DRAG_TARGET", P, "RockDragTarget")
    P = "prefabs/LetterPrefab.json"
    c = P + "#LetterPrefab:TextMesh:"
    R += tr("LETTER", P, "LetterPrefab") + [("LETTER_" + n, c + k) for n, k in [
        ("TEXT", "m_Text"), ("FONT", "m_Font"), ("FONT_SIZE", "m_FontSize"), ("FONT_STYLE", "m_FontStyle"),
        ("CHARACTER_SIZE", "m_CharacterSize"), ("ANCHOR", "m_Anchor"), ("ALIGNMENT", "m_Alignment"),
        ("COLOR", "m_Color"), ("LINE_SPACING", "m_LineSpacing"), ("OFFSET_Z", "m_OffsetZ"),
        ("RICH_TEXT", "m_RichText"), ("TAB_SIZE", "m_TabSize")]]
    R += [("LETTER_MESH_RENDERER_ENABLED", P + "#LetterPrefab:MeshRenderer:m_Enabled")]
    P = "prefabs/RockShatterPrefab.json"
    R += tr("SHATTER", P, "RockShatterPrefab") + audio("SHATTER_SOUND", P, "RockShatterPrefab", curve=True)
    R += [("SHATTER_TIME_OUT", P + "#RockShatterPrefab:TimedObjectDestructor:timeOut"),
          ("SHATTER_DETACH_CHILDREN", P + "#RockShatterPrefab:TimedObjectDestructor:detachChildren")]
    R += emitter("SHATTER", P, "RockShatterPrefab/large flames")
    R += [("SHATTER_EMITTER_ANIMATION", P + "#RockShatterPrefab/large flames:Animation:m_Animation"),
          ("SHATTER_EMITTER_PLAY_AUTOMATICALLY", P + "#RockShatterPrefab/large flames:Animation:m_PlayAutomatically"),
          ("SHATTER_EMITTER_WRAP_MODE", P + "#RockShatterPrefab/large flames:Animation:m_WrapMode"),
          ("SHATTER_STOP_EMIT_CURVE", "animation/Stop Emitting ,5 sec.anim.json#summary.float_curves[0].curve.m_Curve"),
          ("SHATTER_STOP_EMIT_SAMPLE_RATE", "animation/Stop Emitting ,5 sec.anim.json#summary.sample_rate"),
          ("SHATTER_MATERIAL_COLOR", "materials/Fire Smoke.mat.json#summary.colors._Color"),
          ("SHATTER_MATERIAL_TINT", "materials/Fire Smoke.mat.json#summary.colors._TintColor"),
          ("SHATTER_MATERIAL_MAIN_TEX", "materials/Fire Smoke.mat.json#summary.textures._MainTex"),
          ("SHATTER_MATERIAL_ALPHA_TEX", "materials/Fire Smoke.mat.json#summary.textures._AlphaTex")]
    P = "prefabs/RocketPrefab.json"
    R += tr("ROCKET", P, "RocketPrefab") + tr("ROCKET_BODY", P, "RocketPrefab/RocketBody") + \
        sprite("ROCKET_BODY", P, "RocketPrefab/RocketBody") + rigid("ROCKET_BODY_RB", P, "RocketPrefab/RocketBody") + \
        poly("ROCKET_BODY_COLLIDER", P, "RocketPrefab/RocketBody") + \
        [("ROCKET_BODY_TAG", P + "#RocketPrefab/RocketBody:@node:tag")]
    R += tr("ROCKET_COVER", P, "RocketPrefab/RocketCover") + sprite("ROCKET_COVER", P, "RocketPrefab/RocketCover") + \
        box("ROCKET_COVER_TRIGGER", P, "RocketPrefab/RocketCover") + \
        [("ROCKET_COVER_TAG", P + "#RocketPrefab/RocketCover:@node:tag")]
    R += tr("ROCKET_REPELLER", P, "RocketPrefab/rockRepeller") + box("ROCKET_REPELLER", P, "RocketPrefab/rockRepeller") + \
        [("ROCKET_REPELLER_TAG", P + "#RocketPrefab/rockRepeller:@node:tag")]
    R += tr("ROCKET_INSIDE_TRIGGER", P, "RocketPrefab/InsideTrigger") + box("ROCKET_INSIDE_TRIGGER", P, "RocketPrefab/InsideTrigger")
    R += reverb("ROCKET_REVERB", P, "RocketPrefab/Reverb") + tr("ROCKET_REVERB", P, "RocketPrefab/Reverb")
    return R


# --------------------------------------------------------------------------
# C header writers
# --------------------------------------------------------------------------

HEADER_NOTE = "/* Generated by tools/extract.py from the dumps in extract/. Do not edit by hand. */\n"


def c_ident(s):
    s = re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").upper()
    return ("_" + s) if s and s[0].isdigit() else s


def c_str(s):
    """C string literal. Everything outside printable ASCII becomes octal escapes of its UTF-8 bytes."""
    assert "??" not in s, "trigraph in " + repr(s)
    out = ""
    for ch in s:
        if ch in '\\"':
            out += "\\" + ch
        elif " " <= ch <= "~":
            out += ch
        else:
            out += "".join("\\%03o" % b for b in ch.encode("utf-8"))
    out = '"' + out + '"'
    assert ast.literal_eval("b" + out) == s.encode("utf-8"), "string does not round-trip: " + repr(s)
    return out


def c_float(x):
    assert math.isfinite(x), "NaN or infinity in a value"
    return repr(f32(float(x))) + "f"



def c_scalar(v, refmap):
    """C text of a bool, int, float or string. A string is an enum name when refmap knows it as an asset."""
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, int):
        assert -2 ** 31 <= v < 2 ** 32, "does not fit 32 bits: %d" % v  # packed Color32 values use the top bit

        return str(v)
    if isinstance(v, float):
        return c_float(v)
    if v in refmap:
        return refmap[v]
    base = v.split("#")[0]
    assert base in UNUSED_ASSETS or not base.lower().endswith(IMAGE_EXT + AUDIO_EXT + FONT_EXT), \
        "asset not in asset_tables.h: " + v

    return c_str(v)


def is_scalar(v):
    return isinstance(v, (bool, int, float, str)) or v is None


def emit_value(name, v, refmap):
    """C lines for one value: a #define, or an array with a _COUNT, or several of those for a dict."""
    assert re.fullmatch(r"[A-Z][A-Z0-9_]*", name), "not a C name: " + name
    if v is None:
        return ["/* %s: null reference in the source, no constant */" % name]
    if is_scalar(v):
        return ["#define %s %s" % (name, c_scalar(v, refmap))]
    if isinstance(v, dict):
        return [line for k in sorted(v) for line in emit_value("%s_%s" % (name, c_ident(k)), v[k], refmap)]
    assert isinstance(v, list) and v, "cannot emit %s" % name
    if all(is_scalar(x) and x is not None for x in v):
        assert all(isinstance(x, str) == isinstance(v[0], str) for x in v), name + ": mixed list"
        t = "float" if any(isinstance(x, float) for x in v) else "int"
        if isinstance(v[0], str):
            assert all(x in refmap for x in v), name + ": a list of strings must be asset names"
        return ["#define %s_COUNT %d" % (name, len(v)),
                "static const %s %s[%d] = { %s };" % (t, name, len(v), ", ".join(c_scalar(x, refmap) for x in v))]
    if all(isinstance(x, list) and x and all(isinstance(p, list) and len(p) == 2 for p in x) for x in v):
        pts = ", ".join("%s, %s" % (c_float(p[0]), c_float(p[1])) for x in v for p in x)
        return ["#define %s_COUNT %d" % (name, len(v)),
                "static const int %s_SIZES[%d] = { %s };" % (name, len(v), ", ".join(str(len(x)) for x in v)),
                "/* %s_POINTS: x, y pairs, pieces one after another */" % name,
                "static const float %s_POINTS[%d] = { %s };" % (name, sum(len(x) for x in v) * 2, pts)]
    assert all(isinstance(x, dict) and sorted(x) == sorted(v[0]) for x in v), name + ": unsupported list"
    keys = sorted(v[0])
    return ["#define %s_COUNT %d" % (name, len(v)),
            "/* %s: per entry the fields %s */" % (name, ", ".join(keys)),
            "static const float %s[%d] = { %s };" % (name, len(v) * len(keys), ", ".join(c_float(x[k]) for x in v for k in keys))]


def write_values_h(rows, dumps, refmap, player_h, slop, path):
    out = [HEADER_NOTE, "#ifndef ROCKMORSE_VALUES_H\n#define ROCKMORSE_VALUES_H\n\n",
           '#include <math.h>\n#include "asset_tables.h"\n\n',
           "/* Each constant has a comment with its source: <dump file>#<GameObject>:<component>:<field>.\n"
           "   Collider sizes with _WORLD in the name are multiplied by the object's world scale.\n"
           "   Polygon pieces are in world-scaled collider-local units (relative to the GameObject origin,\n"
           "   before any rotation), convex, at most 8 points, counter-clockwise. */\n\n",
           "/* Box2D lengthUnitsPerMeter: height of the player body collider in world units.\n"
           "   Source: scenes/Game.json#Player:PolygonCollider2D, derived.height_world.\n"
           "   B2_LINEAR_SLOP = 0.005 * this value. Piece corners bend by more than 2 * slop. */\n",
           "#define LENGTH_UNITS_PER_METER %s\n" % c_float(player_h),
           "#define B2_LINEAR_SLOP_USED %s\n\n" % c_float(slop)]
    assert len({n for n, _ in rows}) == len(rows), "duplicate C name in the value table"
    nulls = set()
    for name, src in rows:
        v = dumps.get(src)
        if v is None:
            nulls.add(name)
        if src.startswith("derived:"):
            kind, _, loc = src[len("derived:"):].partition("|")
            out.append("/* %s, follows the 2016 Unity 5.4.3 Mac build (checked by tools/compare_build.py).\n"
                       "   The source scene has panLevel 1.0 for every AudioSource. In the build %s. */\n" % (
                           "audio_source " + loc,
                           "spatialBlend is that value for 3D clips (importer flag 3D) and 0.0 for all others"
                           if kind == "spatial_blend" else
                           "reverbZoneMix is 1.0 for 3D clips and 0.0 for all others (a field Unity 5 added)"))
        else:
            out.append("/* %s */\n" % src)
        out.append("\n".join(emit_value(name, v, refmap)) + "\n")

    assert nulls == NULL_ROWS, "rows that resolve to null: %s" % sorted(nulls)
    out.append("\n#endif\n")
    write_header(path, out)


NULL_ROWS = {"MARS_TILE_SPRITE"}  # the tile prefab has no sprite, Mars.cs assigns them


def write_header(path, parts):
    """Write a generated header. The compiler rejects a repeated array or enum name but accepts
    an identical repeated #define, so only the #defines are checked here."""
    text = "".join(parts)
    names = re.findall(r"^#define (\w+)", text, re.M)
    dup = sorted(n for n in set(names) if names.count(n) > 1)
    assert not dup, "%s: names defined twice: %s" % (os.path.basename(path), dup)
    assert text.isascii(), "non-ASCII text in " + path
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def write_tables_h(levels, morse, path):
    out = [HEADER_NOTE, "#ifndef ROCKMORSE_TABLES_H\n#define ROCKMORSE_TABLES_H\n\n",
           "/* Source: Assets/Levels/levels.json (8 entries, in order).\n"
           "   The question text keeps the original double spaces. The last entry has an empty answer. */\n",
           "typedef struct { const char *question; const char *decoded; } Level;\n",
           "#define LEVEL_COUNT %d\n" % len(levels), "static const Level LEVELS[LEVEL_COUNT] = {\n"]
    for lv in levels:
        out.append("    { %s,\n      %s },\n" % (c_str(lv["question"]), c_str(lv["decoded"])))
    out.append("};\n\n")
    out.append("/* Source: Assets/Levels/morsecodes.json. Fields: primary (character or name), alternate character, code (. and -). The game tests both.\n"
               "   The game's JSON parser does not unescape strings, so DOUBLE_QUOTE has the two characters \\\" as its\n"
               "   alternate and a real double quote never matches. The file also gives CLOSE_PAREN the code of\n"
               "   OPEN_PAREN. Both are kept as they are. */\n")
    out.append("typedef struct { const char *primary; const char *alt; const char *code; } MorseEntry;\n")
    out.append("#define MORSE_COUNT %d\n" % len(morse))
    out.append("static const MorseEntry MORSE_TABLE[MORSE_COUNT] = {\n")
    for m in morse:
        out.append("    { %s, %s, %s },\n" % (c_str(m[0]), c_str(m[1]), c_str(m[2])))
    out.append("};\n\n#endif\n")
    write_header(path, out)


def stem(path):
    return os.path.splitext(os.path.basename(path))[0]


# Textures whose .meta has filterMode -1. The 2016 build's Texture2D has m_FilterMode 1 (extract/build_comparison.json).
BUILD_FILTER_MODE = {"Sprites/superdamage.png": 1, "Sprites/Slides/rm_tutorial_v4_2.png": 1}


def write_asset_tables_h(
imgs, snds, fonts, clips, controller, path):
    refmap = {}
    out = [HEADER_NOTE, "#ifndef ROCKMORSE_ASSET_TABLES_H\n#define ROCKMORSE_ASSET_TABLES_H\n\n"]
    # images
    assert {i["file"] for i in imgs if i["filterMode"] == -1} == set(BUILD_FILTER_MODE), "stale BUILD_FILTER_MODE"
    assert {i["filterMode"] for i in imgs} <= {-1, 0, 1, 2}
    img_names = {i["file"]: "IMG_" + c_ident(stem(i["file"])) for i in imgs}
    out.append("/* Image files. Source: extract/assets/<path>.json (import settings). filter 0 = point, 1 = bilinear.\n"
               "   Sprites/superdamage.png and Sprites/Slides/rm_tutorial_v4_2.png have filterMode -1 (not set) in their\n"
               "   .meta files. The 2016 Mac build (extract/build_comparison.json) has 1 (bilinear) for both, so that is used. */\n")
    out.append("typedef struct { const char *path; int width; int height; int pixels_per_unit; int filter_mode; int sprite_mode; } ImageInfo;\n")
    out.append("enum {\n" + "".join("    %s,\n" % img_names[i["file"]] for i in imgs) + "    IMG_COUNT\n};\n")
    out.append("static const ImageInfo IMAGES[IMG_COUNT] = {\n")
    for i in imgs:
        out.append("    { %s, %d, %d, %s, %s, %s }, /* %s */\n" % (
            c_str(i["file"]), i["width"], i["height"], c_scalar(i["pixelsPerUnit"], {}),
            BUILD_FILTER_MODE.get(i["file"], i["filterMode"]), c_scalar(i["spriteMode"], {}), img_names[i["file"]]))
        refmap[i["file"]] = img_names[i["file"]]
    out.append("};\n\n")
    # sprites
    sprs = [(i, s) for i in imgs for s in i["sprites"]]
    spr_names = {i["file"] + "#" + s["name"]: "SPR_" + c_ident(s["name"]) for i, s in sprs}
    out.append("/* Sprites. x and y_from_bottom follow Unity (origin bottom left); y_from_top is for raylib.\n"
               "   pivot is the effective pivot (0..1, origin bottom left). size_world = pixels / pixels_per_unit. */\n")
    out.append("typedef struct { const char *name; int image; int x; int y_from_bottom; int y_from_top; int w; int h; "
               "float pivot_x; float pivot_y; float pixels_per_unit; float size_world_x; float size_world_y; } SpriteInfo;\n")
    out.append("enum {\n" + "".join("    %s,\n" % spr_names[i["file"] + "#" + s["name"]] for i, s in sprs) + "    SPR_COUNT\n};\n")
    out.append("static const SpriteInfo SPRITES[SPR_COUNT] = {\n")
    for i, s in sprs:
        key = i["file"] + "#" + s["name"]
        out.append("    { %s, %s, %d, %d, %d, %d, %d, %s, %s, %s, %s, %s },\n" % (
            c_str(s["name"]), img_names[i["file"]], s["x"], s["y"], i["height"] - s["y"] - s["h"], s["w"], s["h"],
            c_float(s["pivot"][0]), c_float(s["pivot"][1]), c_float(i["pixelsPerUnit"]),
            c_float(s["size_world"][0]), c_float(s["size_world"][1])))
        refmap[key] = spr_names[key]
    out.append("};\n\n")
    # sounds
    snd_names = {s["file"]: "SND_" + c_ident(stem(s["file"])) for s in snds}
    out.append("/* Sounds. Source: extract/assets/<path>.json. path is the file the port loads, a 16 bit PCM WAV under\n"
               "   port/assets. For 7 clips it is the source WAV (same PCM as the 2016 build). For the 8 MP3 clips and for\n"
               "   Menu4.wav it is the build's own decoded audio (the build re-encoded the MP3 files to Vorbis, so its\n"
               "   lengths differ from the MP3 files). channels, sample_rate and frames are the build's: length in seconds\n"
               "   is frames / sample_rate. is_3d is the importer's 3D flag. The dump holds the source length as well. */\n")
    out.append("typedef struct { const char *path; int is_3d; int channels; int sample_rate; int frames; } SoundInfo;\n")
    out.append("enum {\n" + "".join("    %s,\n" % snd_names[s["file"]] for s in snds) + "    SND_COUNT\n};\n")
    out.append("static const SoundInfo SOUNDS[SND_COUNT] = {\n")
    for s in snds:
        b = s["build"]
        out.append("    { %s, %d, %d, %d, %d }, /* %s, %s */\n" % (
            c_str(s["port_file"]), 1 if s["is_3d"] else 0, b["channels"], b["sample_rate"], b["frames"],
            snd_names[s["file"]], "source file" if s["port_file_origin"] == "source" else "from the build"))
        refmap[s["file"]] = snd_names[s["file"]]
    out.append("};\n\n")
    # fonts
    fnt_names = {f["file"]: "FONT_" + c_ident(stem(f["file"])) for f in fonts}
    out.append("/* Fonts. */\ntypedef struct { const char *path; } FontInfo;\n")
    out.append("enum {\n" + "".join("    %s,\n" % fnt_names[f["file"]] for f in fonts) + "    FONT_COUNT\n};\n")
    out.append("static const FontInfo FONTS[FONT_COUNT] = {\n")
    for f in fonts:
        out.append("    { %s },\n" % c_str(f["file"]))
        refmap[f["file"]] = fnt_names[f["file"]]
    out.append("};\n\n")
    # clips
    out.append("/* Sprite animation clips. Source: extract/animation/<clip>.json. Times are seconds. */\n")
    out.append("typedef struct { float time; int sprite; } ClipKey;\n")
    out.append("typedef struct { const char *name; float sample_rate; int loop; float length; int key_count; const ClipKey *keys; } Clip;\n")
    clip_ids = []
    for name, c in clips:
        keys = c["keys"]
        cid = "CLIP_" + c_ident(name)
        clip_ids.append(cid)
        out.append("static const ClipKey %s_KEYS[%d] = {\n" % (cid, len(keys)))
        for k in keys:
            out.append("    { %s, %s },\n" % (c_float(k["time"]), refmap[k["sprite"]]))
        out.append("};\n")
    out.append("enum {\n" + "".join("    %s,\n" % c for c in clip_ids) + "    CLIP_COUNT\n};\n")
    out.append("static const Clip CLIPS[CLIP_COUNT] = {\n")
    for (name, c), cid in zip(clips, clip_ids):
        out.append("    { %s, %s, %d, %s, %d, %s_KEYS },\n" % (
            c_str(name), c_float(c["sample_rate"]), 1 if c["loop"] else 0, c_float(c["length"]), len(c["keys"]), cid))
    out.append("};\n\n")
    out.append("/* Animator controller Sprites/run/Player.controller. Each state plays one clip. No transitions. */\n")
    out.append("typedef struct { const char *name; int clip; float speed; } AnimState;\n")
    out.append("#define ANIM_STATE_COUNT %d\n" % len(controller["states"]))
    out.append("static const AnimState ANIM_STATES[ANIM_STATE_COUNT] = {\n")
    for s in controller["states"]:
        out.append("    { %s, %s, %s },\n" % (c_str(s["name"]), s["clip_id"], c_float(s["speed"])))
    out.append("};\n")
    out.append("#define ANIM_DEFAULT_STATE %d /* %s */\n\n#endif\n" % (controller["default_index"], controller["default_name"]))
    write_header(path, out)
    return refmap


# --------------------------------------------------------------------------
# code-only constants (port/src/code_constants.h)
#
# Strings and numbers that live only in the C# code, not in the scenes. One row per constant:
#   K(C name, file under Assets/, line or tuple of lines, the literal exactly as written, occurrence, note)
# `occurrence` picks the n-th token with that text when a line holds it twice. Each row is checked against the
# source: the literal must be a token of that line. Then every string literal and every number other than the
# plain ints 0, 1 and 2 in the game code must be in a row or on a line in CODE_IGNORED (with the reason), so a new
# or moved literal fails loudly. Plain 0, 1, 2 are loop starts, "is zero" tests and halves. Where one of them
# is a real value (a volume, a scene index, a Morse code) it has a row.
# --------------------------------------------------------------------------

def K(name, file, lines, literal, occ=0, note=""):
    return (name, file, lines if isinstance(lines, tuple) else (lines,), literal, occ, note)


CODE_CONSTANTS = [
    # CamerFocusSwitcher.cs
    K("FOCUS_SWITCHER_KEY_OVERVIEW", "Scripts/CamerFocusSwitcher.cs", 46, '"m"', note="Input.GetKey name, held to see the whole level"),
    K("FOCUS_SWITCHER_SPEED_OVERVIEW", "Scripts/CamerFocusSwitcher.cs", 49, "1.0f", note="iTween.MoveUpdate time argument"),
    K("FOCUS_SWITCHER_SPEED_ROVER", "Scripts/CamerFocusSwitcher.cs", 53, "1.0f"),
    K("FOCUS_SWITCHER_SPEED_PLAYER", "Scripts/CamerFocusSwitcher.cs", 56, "0.2f"),
    K("NIGHT_OVERLAY_DARKNESS_FACTOR", "Scripts/CamerFocusSwitcher.cs", 63, "0.85f", note="target alpha = darkness * this"),
    K("NIGHT_OVERLAY_EASE_DIVISOR", "Scripts/CamerFocusSwitcher.cs", 63, "5", note="alpha moves 1/5 of the way per frame"),
    K("FOCUS_SWITCHER_SIZE_EASE_DIVISOR", "Scripts/CamerFocusSwitcher.cs", 71, "10", note="camera size moves 1/10 of the way per frame"),
    K("FOCUS_SWITCHER_OVERVIEW_MIN_RATIO", "Scripts/CamerFocusSwitcher.cs", 106, "0.9f"),
    # Earth.cs
    K("EARTH_DECODED_MESSAGE_DISPLAY_DURATION", "Scripts/Earth.cs", 35, "2.0f"),
    K("EARTH_INDICATOR_START_ALPHA", "Scripts/Earth.cs", 56, "0"),
    K("EARTH_START_AUDIO_VOLUME", "Scripts/Earth.cs", 60, "1", note="AudioListener.volume in Start"),
    K("EARTH_RESUME_TIME_SCALE", "Scripts/Earth.cs", 109, "1.0f"),
    K("EARTH_RESUME_AUDIO_VOLUME", "Scripts/Earth.cs", 110, "1.0f"),
    K("EARTH_MESSAGE_SOUND_VOLUME_IDLE", "Scripts/Earth.cs", 114, "0", note="rover message sound volume while no level waits"),
    K("EARTH_INDICATOR_FADE_RATE", "Scripts/Earth.cs", 117, "1.2f", note="alpha per second"),
    K("EARTH_INDICATOR_ALPHA_WRAP", "Scripts/Earth.cs", 118, "1", note="alpha at or above this goes back to the next row"),
    K("EARTH_INDICATOR_ALPHA_AFTER_WRAP", "Scripts/Earth.cs", 118, "0"),
    K("EARTH_PAUSE_TIME_SCALE", "Scripts/Earth.cs", 190, "0.0f"),
    K("EARTH_PAUSE_AUDIO_VOLUME", "Scripts/Earth.cs", 191, "0.0f"),
    K("EARTH_FORCE_NEXT_LEVEL_NONE", "Scripts/Earth.cs", 200, "-1"),
    # EndGame.cs, NextSceneOnSpace.cs, Intro.cs, Title.cs, Rocket.cs
    K("END_SCREEN_DURATION", "Scripts/EndGame.cs", 7, "5"),
    K("END_SCREEN_KEY_DISMISS", "Scripts/EndGame.cs", 21, '"space"'),
    K("END_SCREEN_MOUSE_BUTTON", "Scripts/EndGame.cs", 21, "0", note="left button"),
    K("END_SCREEN_NEXT_SCENE_INDEX", "Scripts/EndGame.cs", 28, "0", note="build index 0 is Intro"),
    K("NEXT_SCENE_KEY_DISMISS", "Scripts/NextSceneOnSpace.cs", 22, '"space"'),
    K("NEXT_SCENE_MOUSE_BUTTON", "Scripts/NextSceneOnSpace.cs", 22, "0", note="left button"),
    K("INTRO_DURATION", "Scripts/Intro.cs", 7, "2.0f"),
    K("INTRO_LOGO_DELAY_DOUBLE", "Scripts/Intro.cs", 18, "0.1",
      note="a double in the C# source: the test is introDuration <= (dur - 0.1) done in double precision"),
    K("INTRO_NEXT_SCENE", "Scripts/Intro.cs", 23, '"Title"'),
    K("TITLE_SITE_URL", "Scripts/Title.cs", 22, '@"http://superdamage.com"'),
    K("TITLE_PLAY_NEXT_SCENE", "Scripts/Title.cs", 27, '"Story1"'),
    K("ROCKET_WIN_SCENE", "Scripts/Rocket.cs", 21, '"Win"'),
    # LevelLoader.cs
    K("LEVEL_LOADER_JSON_KEY_QUESTION", "Scripts/LevelLoader.cs", 123, '"question"'),
    K("LEVEL_LOADER_JSON_KEY_DECODED", "Scripts/LevelLoader.cs", (124, 126), '"decoded"'),
    K("LEVEL_LOADER_PLACEHOLDER_COMPONENT", "Scripts/LevelLoader.cs", 224, '"RockPlaceholder"', note="GetComponent by name"),
    K("LEVEL_LOADER_WORD_SEPARATOR", "Scripts/LevelLoader.cs", 301, "' '", note="Split(' ')"),
    K("LEVEL_LOADER_DECODED_Y_OFFSET", "Scripts/LevelLoader.cs", 303, "-1.394218f"),
    K("LEVEL_LOADER_LETTER_SEPARATOR", "Scripts/LevelLoader.cs", 324, '" "', note="appended after every letter, so each line of decoded text ends with a space"),
    K("LEVEL_LOADER_LETTERS_ALPHA_CORRECT", "Scripts/LevelLoader.cs", 342, "0.5f"),
    K("LEVEL_LOADER_LETTERS_ALPHA_WRONG", "Scripts/LevelLoader.cs", 342, "0.06f"),
    # Mars.cs
    K("MARS_SOL_HOURS_BASE", "Scripts/Mars.cs", 31, "24", note="SOL_IN_HOURS = 24 * 1.027f"),
    K("MARS_SOL_HOURS_FACTOR", "Scripts/Mars.cs", 31, "1.027f"),
    K("MARS_SOL_COUNTER_DURATION", "Scripts/Mars.cs", 56, "3.0f"),
    K("MARS_MINUTES_PER_HOUR", "Scripts/Mars.cs", 65, "60", 0, "SOL_IN_SECONDS = SOL_IN_HOURS * 60 * 60"),
    K("MARS_SECONDS_PER_MINUTE", "Scripts/Mars.cs", 65, "60", 1),
    K("MARS_TIME_OFFSET", "Scripts/Mars.cs", 69, "0.0f"),
    K("MARS_TILE_SPRITE_COMPONENT", "Scripts/Mars.cs", 118, '"SpriteRenderer"'),
    K("MARS_TILE_CODE_SCALE_Z", "Scripts/Mars.cs", 123, "1", note="Mars sets the tile scale to (tileSize, tileSize, 1), so z is 1 and not the prefab 5"),
    K("MARS_SOL_COUNTER_PREFIX", "Scripts/Mars.cs", 158, '"SOL "', note="text is PREFIX + currentSol + NEWLINE + lifeSupport + SUFFIX"),
    K("MARS_SOL_COUNTER_NEWLINE", "Scripts/Mars.cs", 158, '"\\n"'),
    K("MARS_SOL_COUNTER_SUFFIX", "Scripts/Mars.cs", 158, '" LEFT"', note="leading space"),
    K("MARS_DARKNESS_OFFSET", "Scripts/Mars.cs", 161, "0.5f", 0, "darkness = abs(((1 - left) - OFFSET) / DIVISOR)"),
    K("MARS_DARKNESS_DIVISOR", "Scripts/Mars.cs", 161, "0.5f", 1),
    K("MARS_EARTH_VISIBLE_MAX_DARKNESS_DOUBLE", "Scripts/Mars.cs", 162, "0.5", note="a double in the C# source"),
    # Morse.cs
    K("MORSE_CODE_GAP", "Scripts/Morse.cs", 11, "0"),
    K("MORSE_CODE_DOT", "Scripts/Morse.cs", 12, "1"),
    K("MORSE_CODE_DASH", "Scripts/Morse.cs", 13, "2"),
    K("MORSE_CHAR_DOT_UNUSED", "Scripts/Morse.cs", 15, "1", note="field char_dot, never read"),
    K("MORSE_CHAR_DASH_UNUSED", "Scripts/Morse.cs", 16, "2", note="field char_dash, never read"),
    K("MORSE_FIELD_CHAR", "Scripts/Morse.cs", 18, "0", note="index of the primary character in a morsecodes.json row"),
    K("MORSE_FIELD_ALT", "Scripts/Morse.cs", 19, "1"),
    K("MORSE_FIELD_CODE", "Scripts/Morse.cs", 20, "2"),
    K("MORSE_WORD_SEPARATOR", "Scripts/Morse.cs", 35, "' '", note="Split(' ')"),
    K("MORSE_TEXT_DASH", "Scripts/Morse.cs", 83, '"-"'),
    K("MORSE_TEXT_DOT", "Scripts/Morse.cs", 84, '"."'),
    # Rock.cs, RockDragger.cs, RockGenerator.cs, RockPlaceholder.cs
    K("ROCK_SFX_MIN_VELOCITY", "Scripts/Rock.cs", 15, "0.2f"),
    K("ROCK_SPRITE_COMPONENT", "Scripts/Rock.cs", 24, '@"SpriteRenderer"'),
    K("ROCK_AUDIO_COMPONENT", "Scripts/Rock.cs", 33, '"AudioSource"'),
    K("ROCK_TORQUE_FACTOR", "Scripts/Rock.cs", 67, "13", note="AddTorque(-t * 13)"),
    K("ROCK_SHADOW_OFFSET_Y", "Scripts/Rock.cs", 74, "0.3f", note="subtracted from the rock y"),
    K("ROCK_SHADOW_Z", "Scripts/Rock.cs", 75, "-0.6f"),
    K("ROCK_REPELLER_TAG", "Scripts/Rock.cs", (82, 94, 100, 106), '"Rover"'),
    K("ROCK_DRAGGER_KEY_PULL", "Scripts/RockDragger.cs", (26, 36), '"space"'),
    K("ROCK_GENERATOR_COLOUR_RANGE_MIN", "Scripts/RockGenerator.cs", 29, "0", 0, "Random.Range(MIN, MAX) == DARK_WHEN, int version: MAX is exclusive"),
    K("ROCK_GENERATOR_COLOUR_RANGE_MAX", "Scripts/RockGenerator.cs", 29, "2"),
    K("ROCK_GENERATOR_DARK_WHEN", "Scripts/RockGenerator.cs", 29, "0", 1),
    K("ROCK_GENERATOR_SPAWN_FRACTION", "Scripts/RockGenerator.cs", (53, 54), "0.5f", note="half width and height of the world times this"),
    K("PLACEHOLDER_CLEAR_TIME", "Scripts/RockPlaceholder.cs", 24, "0.6f"),
    K("PLACEHOLDER_SPRITE_COMPONENT", "Scripts/RockPlaceholder.cs", 48, '"SpriteRenderer"'),
    # trigger tags
    K("ROVER_TRIGGER_PLAYER_TAG", "Scripts/Rover.cs", (20, 29), '"Player"'),
    K("ROVER_INSIDE_PLAYER_TAG", "Scripts/RoverInsideTrigger.cs", (28, 44), '"Player"'),
    K("ROCKET_INSIDE_PLAYER_TAG", "Scripts/InsideRocketTrigger.cs", 18, '"Player"'),
    K("VISIBLE_ON_TRIGGER_PLAYER_TAG", "Scripts/VisibleOnTrigger.cs", (20, 29), '"Player"'),
    # RoverScreen.cs
    K("ROVER_SCREEN_MAX_CHARS_PER_LINE", "Scripts/RoverScreen.cs", 12, "31"),
    K("ROVER_SCREEN_LINE_CAPACITY", "Scripts/RoverScreen.cs", 17, "12"),
    K("ROVER_SCREEN_LIFE_SUPPORT_BONUS", "Scripts/RoverScreen.cs", 34, "1", note="startingLifeSupport += 1"),
    K("ROVER_SCREEN_HEADER", "Scripts/RoverScreen.cs", 44, '"EARTH:\\n"'),
    K("ROVER_SCREEN_HEADER_RULE", "Scripts/RoverScreen.cs", 45, '"------\\n"'),
    K("ROVER_SCREEN_HEADER_LINES", "Scripts/RoverScreen.cs", 52, "3", note="lineCount starts at 3: the two header lines count, plus one"),
    K("ROVER_SCREEN_NEWLINE", "Scripts/RoverScreen.cs", (57, 71, 78), '"\\n"'),
    K("ROVER_SCREEN_SEPARATOR", "Scripts/RoverScreen.cs", 76, '"\\n_______________________________"', note="newline and 31 underscores"),
    K("ROVER_SCREEN_PAD_WIDTH", "Scripts/RoverScreen.cs", (82, 88, 89, 97), "2"),
    K("ROVER_SCREEN_PAD", "Scripts/RoverScreen.cs", (82, 88, 89, 97), '"0"'),
    K("ROVER_SCREEN_LIFE_SUPPORT_LABEL", "Scripts/RoverScreen.cs", 83, '"\\nLIFE SUPPORT LEFT:      "', note="six spaces after the colon"),
    K("ROVER_SCREEN_LIFE_SUPPORT_UNIT", "Scripts/RoverScreen.cs", 83, '" SOLS"', note="leading space"),
    K("ROVER_SCREEN_MINUTES_DIVISOR", "Scripts/RoverScreen.cs", 87, "60", 0, "(int)((secondsUntilNextSol / DIVISOR) % MODULUS)"),
    K("ROVER_SCREEN_MINUTES_MODULUS", "Scripts/RoverScreen.cs", 87, "60", 1),
    K("ROVER_SCREEN_HOURS_LABEL", "Scripts/RoverScreen.cs", 90, '"\\nHOURS UNTIL NEXT SOL:     "', note="five spaces after the colon"),
    K("ROVER_SCREEN_TIME_COLON", "Scripts/RoverScreen.cs", 90, '":"'),
    K("ROVER_SCREEN_COVERAGE_LABEL", "Scripts/RoverScreen.cs", 93, '"\\nSATELITE IMAGERY COVERAGE:  "', note="two spaces after the colon, spelling as in the source"),
    K("ROVER_SCREEN_COVERAGE_YES", "Scripts/RoverScreen.cs", 93, '"YES"'),
    K("ROVER_SCREEN_COVERAGE_NO", "Scripts/RoverScreen.cs", 93, '" NO"', note="leading space, so YES and NO end at the same column"),
    K("ROVER_SCREEN_VISIBILITY_OFFSET", "Scripts/RoverScreen.cs", 96, "0.5f", 0, "round((1 - darkness + OFFSET) * PERCENT)"),
    K("ROVER_SCREEN_VISIBILITY_PERCENT", "Scripts/RoverScreen.cs", 96, "100"),
    K("ROVER_SCREEN_VISIBILITY_PREFIX", "Scripts/RoverScreen.cs", 98, '"VISIBILITY IS TOO LOW\\n"'),
    K("ROVER_SCREEN_VISIBILITY_SUFFIX", "Scripts/RoverScreen.cs", 98, '"%, WAIT FOR SOL"'),
    K("ROVER_SCREEN_DEATH_THRESHOLD", "Scripts/RoverScreen.cs", 111, "-1", note="dies when lifeSupport <= this"),
    K("ROVER_SCREEN_DEATH_SCENE", "Scripts/RoverScreen.cs", 113, '"Death"'),
    # TopDownControls.cs
    K("PLAYER_SFX_MIN_VELOCITY", "Scripts/TopDownControls.cs", 14, "0.5f"),
    K("PLAYER_AGILITY", "Scripts/TopDownControls.cs", 26, "10f"),
    K("PLAYER_SPEED", "Scripts/TopDownControls.cs", 27, "10f"),
    K("PLAYER_AGILITY_DIVISOR", "Scripts/TopDownControls.cs", 29, "5", note="walkSpeed = Speed + Agility / 5"),
    K("PLAYER_WALK_SPEED_FACTOR", "Scripts/TopDownControls.cs", 32, "0.6f"),
    K("PLAYER_AXIS_HORIZONTAL", "Scripts/TopDownControls.cs", 43, '"Horizontal"'),
    K("PLAYER_AXIS_VERTICAL", "Scripts/TopDownControls.cs", 44, '"Vertical"'),
    K("PLAYER_VELOCITY_LERP_T", "Scripts/TopDownControls.cs", (43, 44), "0.8f", note="Mathf.Lerp(0, axis * speed, T)"),
    K("PLAYER_DRAGGER_COMPONENT", "Scripts/TopDownControls.cs", 47, '"RockDragger"'),
    K("PLAYER_FOOTSTEP_PITCH_DRAGGING", "Scripts/TopDownControls.cs", 49, "0.9f"),
    K("PLAYER_FOOTSTEP_PITCH_WALKING", "Scripts/TopDownControls.cs", 52, "1.9f"),
    K("PLAYER_FACING_LEFT_MAX_VELOCITY_X", "Scripts/TopDownControls.cs", 64, "0", 0, "facing left when velocityX <= this"),
    K("PLAYER_Y_WHEN_FACING_LEFT", "Scripts/TopDownControls.cs", 64, "180", note="written into the quaternion y component, not an angle"),
    K("PLAYER_Y_WHEN_FACING_RIGHT", "Scripts/TopDownControls.cs", 64, "0", 1),
    K("PLAYER_LISTENER_Y_WHEN_FACING_LEFT", "Scripts/TopDownControls.cs", 68, "0", 1),
    K("PLAYER_LISTENER_Y_WHEN_FACING_RIGHT", "Scripts/TopDownControls.cs", 68, "180"),
    K("PLAYER_ANIM_DRAG", "Scripts/TopDownControls.cs", 72, '"drag"'),
    K("PLAYER_ANIM_RUN", "Scripts/TopDownControls.cs", 72, '"run"'),
    K("PLAYER_ANIM_IDLE", "Scripts/TopDownControls.cs", 78, '"idle"'),
    K("PLAYER_CLAMP_MARGIN_X", "Scripts/TopDownControls.cs", 82, "0.5f"),
    K("PLAYER_CLAMP_MARGIN_Y", "Scripts/TopDownControls.cs", 83, "0.8f"),
    # third party code the game calls
    K("ITWEEN_UPDATE_TIME_PERCENTAGE", "iTween/Plugins/iTween.cs", 199, ".05f", note="MoveUpdate smooth time = time argument * this"),
    K("TIMED_DESTRUCTOR_METHOD", "Standard Assets/Particles/Legacy Particles/TimedObjectDestructor.js", 6, '"DestroyNow"', note="Invoke(name, timeOut)"),
]

# Lines whose literals are deliberately not constants, with the reason.
_DEFAULT = "script field default; the scene value (in values.h) replaces it"
_STATE = "initial value of runtime state"
_DEBUG = "feeds only a Debug.Log string"
CODE_IGNORED = {
    ("Scripts/Earth.cs", 21): _DEFAULT, ("Scripts/Earth.cs", 25): _DEFAULT, ("Scripts/Earth.cs", 39): _DEFAULT,
    ("Scripts/LevelLoader.cs", 9): _DEFAULT, ("Scripts/LevelLoader.cs", 28): _DEFAULT,
    ("Scripts/LevelLoader.cs", 165): _DEBUG, ("Scripts/LevelLoader.cs", 364): _DEBUG,
    ("Scripts/Mars.cs", 9): _DEFAULT, ("Scripts/Mars.cs", 11): _DEFAULT, ("Scripts/Mars.cs", 12): _DEFAULT,
    ("Scripts/Mars.cs", 43): _STATE,
    ("Scripts/NextSceneOnSpace.cs", 7): _DEFAULT, ("Scripts/RockGenerator.cs", 8): _DEFAULT,
    ("Scripts/RoverInsideTrigger.cs", 10): _DEFAULT, ("Scripts/RoverInsideTrigger.cs", 11): _DEFAULT,
    ("Scripts/RoverScreen.cs", 14): _DEFAULT,
    ("Standard Assets/Particles/Legacy Particles/TimedObjectDestructor.js", 1): _DEFAULT,
}
_CODE_TOK = re.compile(r'@"(?:[^"]|"")*"|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)\'|(?<![\w.])(\d+\.\d*[fFdD]?|\.\d+[fFdD]?|\d+[fFdD]?)(?![\w.])')
_SIMPLE_INTS = {"0", "1", "2"}


def code_lines(proj, rel):
    """Lines of a source file with comments removed (string contents kept). Index 0 is line 1."""
    with open(os.path.join(proj.assets, rel), encoding="utf-8-sig", newline="") as fh:
        text = fh.read().replace("\r\n", "\n")
    comments, _ = scan_source(text)
    out, last = [], 0
    for a, b, _kind in comments:
        out.append(text[last:a] + "".join(c if c == "\n" else " " for c in text[a:b]))
        last = b
    out.append(text[last:])
    return "".join(out).split("\n")


def line_tokens(line):
    """Literal tokens of one line in order: strings, chars and numbers. A unary minus stays with its number."""
    toks = []
    for m in _CODE_TOK.finditer(line):
        t = m.group(0)
        if m.group(1) is not None:
            before = line[:m.start()].rstrip()
            if before.endswith("-") and not re.search(r"[\w)\]]$", before[:-1].rstrip()):
                t = "-" + t
        toks.append(t)
    return toks


def literal_value(tok):
    """(kind, value) of a C# literal token: kind is string, char, int, float or double."""
    if tok.startswith('@"'):
        return "string", tok[2:-1].replace('""', '"')
    if tok.startswith('"') or tok.startswith("'"):
        body, esc, out, i = tok[1:-1], {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}, "", 0
        while i < len(body):
            if body[i] == "\\":
                assert body[i + 1] in esc, "escape in " + tok
                out += esc[body[i + 1]]
                i += 2
            else:
                out += body[i]
                i += 1
        return ("char" if tok[0] == "'" else "string"), out
    neg = tok.startswith("-")
    t = tok.lstrip("-")
    suffix = t[-1] if t[-1] in "fFdD" else ""
    num = t[:-1] if suffix else t
    if suffix in ("f", "F"):
        return "float", (-1 if neg else 1) * f32_bits(float(num))
    if suffix in ("d", "D") or "." in num:
        return "double", (-1 if neg else 1) * float(num)
    return "int", (-1 if neg else 1) * int(num)


def c_code_literal(tok):
    kind, v = literal_value(tok)
    if kind == "string":
        return c_str(v)
    if kind == "char":
        return "'%s'" % (v if v not in "\\'" else "\\" + v)
    if kind == "int":
        return str(v)
    if kind == "float":
        return c_float(v)
    return repr(v) if "." in repr(v) or "e" in repr(v) else repr(v) + ".0"


def write_code_constants(proj, path):
    cache = {}

    def lines_of(rel):
        if rel not in cache:
            cache[rel] = code_lines(proj, rel)
        return cache[rel]
    names = [r[0] for r in CODE_CONSTANTS]
    assert len(set(names)) == len(names), "duplicate name in CODE_CONSTANTS"
    covered = set()  # (file, line, token, occurrence)
    out = [HEADER_NOTE, "#ifndef ROCKMORSE_CODE_CONSTANTS_H\n#define ROCKMORSE_CODE_CONSTANTS_H\n\n",
           "/* Strings and numbers that exist only in the C# code. Each comment gives <file under Assets/>:<line>.\n"
           "   tools/extract.py checks every row against the source, so a moved or edited literal stops the build.\n"
           "   A literal marked double is a double in C#; the C value has no f suffix. Plain ints 0, 1 and 2 that are\n"
           "   loop starts, zero tests and halves have no row. Key codes (H, Escape, P, Space, C, LeftShift) are\n"
           "   enum names in the C# code, not literals, and are listed in INVENTORY.md. Script field defaults that a scene\n"
           "   value replaces are in values.h, not here. */\n\n"]
    for name, rel, lines, lit, occ, note in CODE_CONSTANTS:
        ls = lines_of(rel)
        for ln in lines:
            toks = line_tokens(ls[ln - 1])
            assert toks.count(lit) > occ, "%s:%d: literal %s (occurrence %d) not found; the line has %s" % (rel, ln, lit, occ, toks)
            covered.add((rel, ln, lit, occ))
        out.append("/* %s:%s%s */\n#define %s %s\n" % (
            rel, ",".join(str(x) for x in lines), (" " + note) if note else "", name, c_code_literal(lit)))
    # completeness: no uncovered literal in the game code
    files = comment_sources(proj)  # the game scripts and the .js; iTween rows are checked row by row only
    files = [f for f in files if f != "Scripts/CameraFollowBounds.cs"]  # attached to nothing
    missing = []
    for rel in files:
        ls = lines_of(rel)
        for i, line in enumerate(ls, 1):
            if "Debug.Log" in line or (rel, i) in CODE_IGNORED:
                continue
            seen = {}
            for t in line_tokens(line):
                occ = seen.get(t, 0)
                seen[t] = occ + 1
                if t in ('""',) or t in _SIMPLE_INTS:
                    continue
                if (rel, i, t, occ) not in covered:
                    missing.append("%s:%d %s" % (rel, i, t))
    assert not missing, "literals without a row in CODE_CONSTANTS or CODE_IGNORED: %s" % missing
    for (rel, ln), why in CODE_IGNORED.items():  # an ignore for a line that has no literal is stale
        assert line_tokens(lines_of(rel)[ln - 1]), "stale CODE_IGNORED %s:%d" % (rel, ln)
    out.append("\n#endif\n")
    write_header(path, out)


# --------------------------------------------------------------------------
# source comments, kept for history (extract/source_comments.json)
# --------------------------------------------------------------------------

JS_DESTRUCTOR = "Standard Assets/Particles/Legacy Particles/TimedObjectDestructor.js"
_COMMENT_RX = re.compile(r'@"(?:[^"]|"")*"|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)\'|//[^\n]*|/\*[\s\S]*?\*/')


def comment_sources(proj):
    """Relative paths (to Assets) of the files whose comments are kept: all game scripts and TimedObjectDestructor.js.
    Third-party code (iTween, JSONObject, other Standard Assets) is left out."""
    names = sorted(f for f in os.listdir(os.path.join(proj.assets, "Scripts")) if f.endswith(".cs"))
    assert len(names) == 24, "expected 24 game scripts, found %d" % len(names)
    return ["Scripts/" + n for n in names] + [JS_DESTRUCTOR]


def scan_source(text):
    """Walk C# or UnityScript text once. Returns (comments, code) where comments are
    (start offset, end offset, kind) and code is the text with comments and string/char contents blanked
    (same length, newlines kept) so braces and identifiers can be read safely."""
    comments, code, i, n = [], list(text), 0, len(text)

    def blank(a, b):
        for k in range(a, b):
            if code[k] != "\n":
                code[k] = " "
    while i < n:
        c = text[i]
        if c == "/" and text[i:i + 2] == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((i, j, "line"))
            blank(i, j)
            i = j
        elif c == "/" and text[i:i + 2] == "/*":
            j = text.index("*/", i + 2) + 2
            comments.append((i, j, "block"))
            blank(i, j)
            i = j
        elif c == '@' and text[i:i + 2] == '@"':
            j = i + 2
            while True:
                if text[j] == '"':
                    if text[j:j + 2] == '""':
                        j += 2
                        continue
                    break
                j += 1
            blank(i + 2, j)
            i = j + 1
        elif c in '"\'':
            j = i + 1
            while text[j] != c:
                assert text[j] != "\n", "unterminated literal"
                j += 2 if text[j] == "\\" else 1
            blank(i + 1, j)
            i = j + 1
        else:
            i += 1
    return comments, "".join(code)


def members_of(code, base_depth, default_class):
    """[(start line, end line, "Class.member")] for the members at brace depth base_depth of blanked code."""
    line_of = []
    ln = 1
    for ch in code:
        line_of.append(ln)
        ln += ch == "\n"
    out, depth, cls, start, stmt_start = [], 0, default_class, None, 0
    i = 0
    while i < len(code):
        ch = code[i]
        if depth == base_depth and start is None and not ch.isspace() and ch not in "{};":
            start = i
        if ch == "{":
            if depth == base_depth - 1 and base_depth == 1:  # class header
                m = re.search(r"\bclass\s+(\w+)", code[stmt_start:i])
                if m:
                    cls = m.group(1)
                start = None
                stmt_start = i + 1
            elif depth == base_depth and start is not None:
                head = re.sub(r"\[[^\]]*\]", " ", code[start:i])
                m = re.search(r"(\w+)\s*\(", head) or re.search(r"(\w+)\s*$", head.strip())
                m2 = re.search(r"(?:function|var)\s+(\w+)", head)
                name = (m2 or m).group(1) if (m2 or m) else "?"
                out.append([line_of[start], None, "%s.%s" % (cls, name)])
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == base_depth and out and out[-1][1] is None:
                out[-1][1] = line_of[i]
                start = None
            elif depth == base_depth - 1:
                start, stmt_start = None, i + 1
        elif ch == ";" and depth == base_depth and start is not None:
            head = re.sub(r"\[[^\]]*\]", " ", code[start:i])
            head = head.split("=")[0]
            m = re.search(r"(\w+)\s*$", head.strip())
            out.append([line_of[start], line_of[i], "%s.%s" % (cls, m.group(1) if m else "?")])
            start = None
        i += 1
    assert all(o[1] is not None for o in out), "unclosed member"
    return out


def extract_comments(proj, path):
    result, counts = {}, {}
    for rel in comment_sources(proj):
        with open(os.path.join(proj.assets, rel), encoding="utf-8-sig", newline="") as fh:
            text = fh.read().replace("\r\n", "\n")
        comments, code = scan_source(text)
        # independent count: one regex pass that skips string and char literals
        rx_count = sum(1 for m in _COMMENT_RX.finditer(text) if m.group(0).startswith(("//", "/*")))
        assert rx_count == len(comments), "%s: %d comments by scan, %d by regex" % (rel, len(comments), rx_count)
        cls = os.path.splitext(os.path.basename(rel))[0]
        js = rel.endswith(".js")
        mem = members_of(code, 0 if js else 1, cls)
        line_start = [0]
        for m in re.finditer("\n", text):
            line_start.append(m.end())

        def line_at(off):
            return bisect_right(line_start, off)
        rows = []
        for a, b, kind in comments:
            l1, l2 = line_at(a), line_at(b - 1) if b > a else line_at(a)
            inside = [m for m in mem if m[0] <= l1 and l2 <= m[1]]
            if inside:
                owner, rel_pos = inside[-1][2], "inside"
            else:
                after = [m for m in mem if m[0] > l2]
                before = [m for m in mem if m[1] < l1]
                if after:
                    owner, rel_pos = after[0][2], "before"
                elif before:
                    owner, rel_pos = before[-1][2], "after"
                else:
                    owner, rel_pos = cls, "class"
            body = text[a:b]
            looks_like_code = bool(re.search(r"[;{}]\s*$|\w\s*=[^=]|\w+\s*\(.*\)", body.lstrip("/* ").strip()))
            rows.append({"line": l1, "end_line": l2, "kind": kind, "text": body,
                         "enclosing_member": owner, "position": rel_pos, "looks_like_commented_out_code": looks_like_code})
        result[rel] = rows
        counts[rel] = len(rows)
    return result, counts


def write_source_comments(proj, ext):
    files, counts = extract_comments(proj, None)
    write_json(os.path.join(ext, "source_comments.json"), {
        "note": ("Every comment of the 24 game scripts and TimedObjectDestructor.js, verbatim (spelling not fixed). "
                 "Line endings are normalised to \\n. text includes the // or /* */ marks. kind is line or block. "
                 "enclosing_member is Class.member: the member the comment is in (position inside), or the next member "
                 "(before), or the last member (after). looks_like_commented_out_code is a rough guess. "
                 "Third-party code (iTween, JSONObject, other Standard Assets) is not included."),
        "counts": counts, "total": sum(counts.values()), "files": files})
    return counts


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

# Facts about the input that the inventory and the port rely on. A changed project fails here.
PREFABS = ["LetterPrefab", "MarsTile", "RockDragTarget", "RockPlaceholderPrefab", "RockPrefab", "RockShadowPrefab",
           "RockShatterPrefab", "RocketPrefab"]
GAMEOBJECTS = {"Intro": 3, "Title": 8, "Story1": 3, "Story2": 3, "Game": 34, "Death": 3, "Win": 4,
               "LetterPrefab": 1, "MarsTile": 1, "RockDragTarget": 1, "RockPlaceholderPrefab": 1, "RockPrefab": 1,
               "RockShadowPrefab": 1, "RockShatterPrefab": 2, "RocketPrefab": 6}
PIECES = {("scenes/Game.json", "Player"): [3, 5, 4],
          ("scenes/Game.json", "Rover/bis_rover_v2_roof"): [3, 3, 3, 4, 4, 4, 5],
          ("prefabs/RocketPrefab.json", "RocketPrefab/RocketBody"): [3, 3, 3, 7, 5, 4, 6]}
WIPED = ("scenes", "prefabs", "animation", "materials", "settings", "assets", "derived", "build")  # subfolders of extract/
SUMMARIES = {".mat": material_summary, ".anim": clip_summary, ".controller": controller_summary}


def dest_for(rel):
    sub = {".mat": "materials", ".anim": "animation", ".controller": "animation"}[os.path.splitext(rel)[1]]
    return "%s/%s.json" % (sub, os.path.basename(rel))


def check_pieces(dumps_data, piece_report, slop):
    """The pieces are what the port builds Box2D bodies from, so each written piece is checked against
    the Box2D rules (at most 8 points, convex, no weldable or collinear points)."""
    assert {(r["dump"], r["gameobject"]): r["points_per_piece"] for r in piece_report} == PIECES
    for dumpname, d in dumps_data.items():
        for n in d["gameobjects"]:
            for c in n["components"]:
                if c["type"] != "PolygonCollider2D":
                    continue
                for pc in c["derived"]["paths"][0]["pieces"]:  # as written to the dump, which is what the port reads
                    assert box2d_hull_ok(pc, slop), \
                        "%s %s: piece breaks a Box2D rule" % (dumpname, n["path"])


def check_dumps(dumps_data):
    counts = {os.path.splitext(os.path.basename(k))[0]: len(d["gameobjects"]) for k, d in dumps_data.items()}
    assert counts == GAMEOBJECTS, "GameObject counts changed: %s" % counts
    nodes = [n for d in dumps_data.values() for n in d["gameobjects"]]

    comps = [c for n in nodes for c in n["components"]]
    assert len(comps) == 209, len(comps)
    scripts = [c["script"] for c in comps if c["type"] == "MonoBehaviour"]
    assert len(scripts) == 28 and len(set(scripts)) == 23, "game scripts attached: %d, distinct %d" % (
        len(scripts), len(set(scripts)))
    assert all(n["layer"] == 0 for n in nodes), "a GameObject is not on the Default layer"
    build = [os.path.splitext(os.path.basename(s["path"]))[0] for s in
             read_json(os.path.join(REPO, "extract", "settings", "EditorBuildSettings.json"))["objects"][0]["fields"]["m_Scenes"]]
    assert build == SCENES, "build order is %s" % build


def check_headers(port_src, dumps, snd_infos):
    """Compile a small program against the headers and compare what it prints with the dumps."""
    chatter = [i for i in snd_infos if stem(i["file"]) == "static"][0]
    prog = ('#include <stdio.h>\n#include "values.h"\n#include "tables.h"\n#include "code_constants.h"\nint main(void) {\n'
            '    printf("%.9g %.9g %.9g\\n%s\\n%s\\n%d %d\\n[%s]\\n", (double)ROCK_POS_X, (double)LENGTH_UNITS_PER_METER,\n'
            '           (double)PLAYER_BODY_PIECES_POINTS[0], SOUNDS[ROVER_CHATTER_CLIP].path, LEVELS[1].decoded,\n'
            '           SOUNDS[ROVER_CHATTER_CLIP].sample_rate, SOUNDS[ROVER_CHATTER_CLIP].frames,\n'
            '           ROVER_SCREEN_COVERAGE_NO);\n'
            '    return 0;\n}\n')
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = os.path.join(tmp, "t.c"), os.path.join(tmp, "t")
        with open(src, "w") as fh:
            fh.write(prog)
        subprocess.run(["cc", "-std=c17", "-Wall", "-Wextra", "-Werror", "-I", port_src, "-o", exe, src], check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.splitlines()
    G = "scenes/Game.json"
    want = [dumps.get("prefabs/RockPrefab.json#RockPrefab:@transform:world_position")["x"],
            dumps.get(G + "#Player:PolygonCollider2D:derived.height_world"),
            dumps.get(G + "#Player:PolygonCollider2D:derived.paths[0].pieces")[0][0][0]]
    assert [f32_bits(float(x)) for x in out[0].split()] == [f32_bits(w) for w in want], out[0]
    assert out[1] == chatter["port_file"], out[1]  # the port loads the build's WAV, not the .mp3 named in the scene
    assert out[2] == "DENY", out[2]  # the answer of level 2 in levels.json
    assert out[3] == "%d %d" % (chatter["build"]["sample_rate"], chatter["build"]["frames"]) == "11000 163213", out[3]
    assert out[4] == "[ NO]", out[4]  # the leading space of " NO" survives


def check_assets(images, sounds, fonts, img_infos):
    assert (len(images), len(sounds), len(fonts)) == (59, 16, 1), (len(images), len(sounds), len(fonts))
    for i in img_infos:
        for s in i["sprites"]:
            assert s["w"] > 0 and s["h"] > 0 and s["x"] >= 0 and s["y"] >= 0, (i["file"], s["name"])
            assert s["x"] + s["w"] <= i["width"] and s["y"] + s["h"] <= i["height"], \
                "sprite %s#%s lies outside its texture" % (i["file"], s["name"])


# Sounds whose file in port/assets comes from the 2016 build and not from the source project. The 8 MP3 files were
# re-encoded to Vorbis by the build, so their decoded length and rate differ. Menu4.wav is a 32 bit float WAV in the
# source and 16 bit PCM in the build (every sample within 1 of the source). A drift here fails loudly.
PORT_FILE_FROM_BUILD = {"Arcade_S-wwwbeat-8526_hifi", "FOLEY HORN PARTY TOY BLOW OUT 01", "Menu4", "Victory_-arewhyae-8093_hifi",
                        "bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm", "drag", "newMessage", "player_strain",
                        "static"}
# The lengths that differ most, as the build decodes them: (frames, sample rate)
BUILD_AUDIO_FACTS = {"newMessage": (79488, 44100), "Arcade_S-wwwbeat-8526_hifi": (26496, 44100),
                     "Victory_-arewhyae-8093_hifi": (812160, 44100),
                     "bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm": (112896, 32000), "static": (163213, 11000)}


def check_audio(snd_infos, clips):
    names = {stem(i["file"]) for i in snd_infos}
    assert names == set(clips), "build clips and used sounds differ: %s" % sorted(names ^ set(clips))
    assert {stem(i["file"]) for i in snd_infos if i["port_file_origin"] == "build"} == PORT_FILE_FROM_BUILD
    assert {stem(i["file"]) for i in snd_infos if i["file"].endswith(".mp3")} <= PORT_FILE_FROM_BUILD
    for n, (frames, rate) in BUILD_AUDIO_FACTS.items():
        assert (clips[n]["frames"], clips[n]["sample_rate"]) == (frames, rate), n
    for i in snd_infos:  # read the written file back
        with open(os.path.join(REPO, "port", "assets", i["port_file"]), "rb") as fh:
            raw = fh.read()
        fmt, data = build_extract.wav_parts(raw)
        c = clips[stem(i["file"])]
        assert fmt[0] == 1 and fmt[5] == 16 and data == c["pcm"], i["port_file"]
        assert (fmt[1], fmt[2], len(data) // fmt[4]) == (c["channels"], c["sample_rate"], c["frames"]), i["port_file"]
        assert i["port_file"].endswith(".wav")


def dump_build(build, clips, snd_infos, fnt_infos, ext):
    """extract/build/*.json: facts that exist only in the 2016 build (audio, shaders, materials, font, colour space)."""
    out = os.path.join(ext, "build")
    rows = []
    for i in sorted(snd_infos, key=lambda x: x["file"]):
        b, sr = i["build"], i["source"]
        rows.append({"source_file": i["file"], "port_file": i["port_file"], "port_file_origin": i["port_file_origin"],
                     "source_container": sr["container"], "source_channels": sr["channels"], "source_sample_rate": sr["sample_rate"],
                     "source_seconds": sr["seconds"], "source_seconds_from": sr["seconds_from"],
                     "build_compression": b["compression"], "build_channels": b["channels"], "build_sample_rate": b["sample_rate"],
                     "build_frames": b["frames"], "build_seconds": i["seconds_build"],
                     "seconds_difference": i["seconds_difference"], "sample_rate_differs": i["sample_rate_differs"],
                     "build_file": "%s#%d" % (b["build_file"], b["path_id"]), "build_stored_bytes": b["stored_bytes"]})
    write_json(os.path.join(out, "audio.json"), {
        "note": "Every AudioClip of the 2016 Mac build, decoded to 16 bit PCM by UnityPy (FMOD). The port plays these.",
        "clips": clips_clean(rows)})
    sm = build_extract.shaders_and_materials(build)
    d = Dumps(ext)
    src_mats = {"nightMat": "materials/nightMat.mat.json", "cutout": "materials/cutout.mat.json",
                "Fire Smoke": "materials/Fire Smoke.mat.json"}
    mats = {m["name"]: m for m in sm["materials"]}
    links = []
    for name, rel in sorted(src_mats.items()):
        sm_src = d.load(rel)["summary"]
        m = mats[name]
        for k, v in sm_src["colors"].items():  # same colours on both sides, so the shader link is the right material
            if k in m["colors"]:
                assert all(abs(v[c] - m["colors"][k][c]) < 1e-6 for c in "rgba"), (name, k)
        links.append({"material": name, "source_shader_ref": sm_src["shader"], "build_shader": m["shader"],
                      "build_shader_name": [s["shader_name"] for s in sm["shaders"] if [s["build_file"], s["path_id"]] == m["shader"]][0]})
    links.append({"material": "Sprites-Default",
                  "source_shader_ref": "builtin:library/unity default resources#Sprites-Default (Material)",
                  "build_shader": mats["Sprites-Default"]["shader"],
                  "build_shader_name": [s["shader_name"] for s in sm["shaders"] if [s["build_file"], s["path_id"]] == mats["Sprites-Default"]["shader"]][0]})
    sm["source_to_build"] = links
    sm["note"] = ("The source scenes name built-in shaders by Unity 4 ids (resources/unity_builtin_extra#30, "
                  "library/unity default resources#10753). The build resolves them to the shaders listed here. The build wins.")
    write_json(os.path.join(out, "shaders.json"), clean(sm))
    ttf = fnt_infos[0]["ttf"]
    f = build_extract.font_object(build, open(os.path.join(REPO, "port", "assets", fnt_infos[0]["file"]), "rb").read())
    size, upem = f["m_FontSize"], ttf["unitsPerEm"]
    assert f["font_data_is_project_ttf"] and f["dynamic"]
    assert abs(f["m_Ascent"] - ttf["hhea_ascent"] * size / upem) < 1e-3, "Unity's ascent is not hhea ascent at the font size"
    assert abs(f["m_Descent"] - ttf["hhea_descent"] * size / upem) < 1e-3
    assert abs(f["m_LineSpacing"] - ttf["line_height_units"] * size / upem) < 1e-3
    write_json(os.path.join(out, "font.json"), clean({
        "note": "Font object of the 2016 build (sharedassets1.assets), the TTF metrics, and the same numbers at the font size.",
        "build_font": f, "ttf": ttf, "import": fnt_infos[0]["import"],
        "ttf_scaled_to_font_size": {"size": size, "ascent": ttf["hhea_ascent"] * size / upem,
                                    "descent": ttf["hhea_descent"] * size / upem,
                                    "line_height": ttf["line_height_units"] * size / upem,
                                    "advance_width": ttf["advance_width"] * size / upem}}))
    gm = [o for o in build.files["globalgamemanagers"].objects.values() if o.type.name == "PlayerSettings"]
    assert len(gm) == 1
    cs = gm[0].read_typetree(check_read=False)["m_ActiveColorSpace"]
    src_cs = d.get("settings/ProjectSettings.json#m_ActiveColorSpace")
    assert cs == src_cs == 0, "colour space is not Gamma"
    write_json(os.path.join(out, "color_space.json"), {
        "m_ActiveColorSpace": cs, "name": "Gamma", "note": "0 is Gamma, 1 is Linear. Same value in the project and the build.",
        "source": "settings/ProjectSettings.json#m_ActiveColorSpace", "build": "globalgamemanagers PlayerSettings"})


def clips_clean(rows):
    return clean(rows)


def check_tables(levels, morse):
    assert len(levels) == 8 and len(morse) == 54
    assert [m[0] for m in morse[:36]] == list("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    assert all(re.fullmatch(r"[.-]+", m[2]) for m in morse), "a Morse code with a character other than . and -"
    encodable = {c for m in morse for c in m[:2]}
    for i, lv in enumerate(levels):
        assert lv["question"] and ("decoded" in lv) and (lv["decoded"] != "") == (i < 7), "level %d" % i
        missing = set(lv["decoded"].replace(" ", "")) - encodable
        assert not missing, "level %d answer has characters the Morse table cannot encode: %s" % (i, missing)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: python -I tools/extract.py <path to Unity project> <Data folder of the Mac build>")
    proj = Project(sys.argv[1])
    build = build_extract.Build(sys.argv[2])
    clips = build_extract.audio_clips(build)
    ext = os.path.join(REPO, "extract")
    port_src = os.path.join(REPO, "port", "src")
    port_assets = os.path.join(REPO, "port", "assets")
    # Only these subfolders are wiped. Anything else in extract/ (decompiled/, build_comparison*.json) stays.
    for d in [os.path.join(ext, x) for x in WIPED] + [port_assets]:
        if os.path.isdir(d):
            shutil.rmtree(d)
    os.makedirs(port_src, exist_ok=True)

    # --- settings first (layer names are needed by the scene dumps)

    dump_settings(os.path.join(proj.root, "ProjectSettings"), os.path.join(ext, "settings"))
    layers = read_layers(proj)

    # --- load scenes and prefabs
    prefab_paths = sorted(p for p in proj.meta if p.endswith(".prefab") and not p.startswith("Standard Assets/"))
    assert sorted(os.path.basename(p) for p in prefab_paths) == [n + ".prefab" for n in PREFABS], prefab_paths

    skipped_prefabs = sorted(p for p in proj.meta if p.endswith(".prefab") and p.startswith("Standard Assets/"))
    pfiles = {rel: UFile(proj, os.path.join(proj.assets, rel)) for rel in prefab_paths}
    index_prefabs(proj, pfiles)
    sfiles = {n: UFile(proj, os.path.join(proj.assets, "Scenes", n + ".unity")) for n in SCENES}

    # --- dump scenes and prefabs
    jobs = [("scenes/%s.json" % n, sfiles[n]) for n in SCENES]
    jobs += [("prefabs/%s.json" % os.path.splitext(os.path.basename(r))[0], pfiles[r]) for r in prefab_paths]
    dumps_data = {outname: dump_file(uf, layers) for outname, uf in jobs}
    colliders = [(outname, n, c) for outname, d in dumps_data.items() for n in d["gameobjects"] for c in n["components"]
                 if c["type"] in ("CircleCollider2D", "BoxCollider2D", "PolygonCollider2D")]
    for _, n, c in colliders:
        collider_derived(n, c)
    check_dumps(dumps_data)

    # --- lengthUnitsPerMeter (height of the player body collider, world units) and the Box2D linear slop
    [player] = [c for _, n, c in colliders if n["path"] == "Player" and c["type"] == "PolygonCollider2D"]
    player_h = player["derived"]["height_world"]
    slop = r32(0.005 * player_h)

    # --- convex pieces for the polygon colliders, then write the dumps
    piece_report = []
    for outname, n, c in colliders:
        if c["type"] == "PolygonCollider2D":
            add_pieces(c, slop)
            piece_report.append({"dump": outname, "gameobject": n["path"], "pieces": c["derived"]["piece_total"],
                                 "points_per_piece": c["derived"]["paths"][0]["piece_point_counts"]})
    check_pieces(dumps_data, piece_report, slop)
    for outname, d in dumps_data.items():
        write_json(os.path.join(ext, outname), d)

    # --- find used assets, dump materials, clips and the controller on the way
    known = {p for p in proj.meta if os.path.isfile(os.path.join(proj.assets, p))}
    refs = {}  # asset path -> set of dumps that mention it
    for dumpname, d in sorted(dumps_data.items()):
        hits = set()
        for n in d["gameobjects"]:
            if (dumpname, n["path"]) in IGNORED_OBJECTS:
                continue
            for c in n["components"]:
                scan_refs(c["fields"], known, hits)
        scan_refs(d["scene_settings"], known, hits)
        for h in hits:
            refs.setdefault(h, set()).add(dumpname)
    for key in IGNORED_OBJECTS:
        assert key[1] in [n["path"] for n in dumps_data[key[0]]["gameobjects"]], "stale IGNORED_OBJECTS entry %s" % (key,)
    asset_dumps, skipped = {}, set()
    todo = sorted(a for a in refs if a.endswith(DUMP_EXT))
    while todo:
        a = todo.pop(0)
        dest = dest_for(a)
        assert dest not in asset_dumps, "two assets map to " + dest
        d = dump_asset(proj, os.path.join(proj.assets, a))
        d["summary"] = SUMMARIES[os.path.splitext(a)[1]](d)
        write_json(os.path.join(ext, dest), d)
        asset_dumps[dest] = d
        hits = set()
        scan_refs(d["objects"], known, hits)  # not d["file"], which names the asset itself
        skip = set(IGNORED_REFS.get(dest, {}))
        skipped |= {(dest, h) for h in hits & skip}
        for h in hits - skip:
            if h.endswith(DUMP_EXT) and h not in refs:
                todo.append(h)
            refs.setdefault(h, set()).add(dest)
    assert skipped == {(dest, a) for dest, ignored in IGNORED_REFS.items() for a in ignored}, "stale IGNORED_REFS entry"

    used = sorted(refs)
    images = [a for a in used if a.lower().endswith(IMAGE_EXT)]
    sounds = [a for a in used if a.lower().endswith(AUDIO_EXT)]
    fonts = [a for a in used if a.lower().endswith(FONT_EXT)]
    code_files = [a for a in used if a.lower().endswith(SCRIPT_EXT)]
    img_infos = [texture_info(proj, a) for a in images]
    audio = [audio_info(proj, a, clips) for a in sounds]
    snd_infos = [i for i, _ in audio]
    fnt_infos = [font_info(proj, a) for a in fonts]
    for info in img_infos + snd_infos + fnt_infos:
        write_json(os.path.join(ext, "assets", info["file"] + ".json"), info)

    # --- copy used files. Sounds: the source WAV or the build's decoded WAV (audio_info decides)
    for a in images + fonts:
        dst = os.path.join(port_assets, a)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(proj.assets, a), dst)
    for info, data in audio:
        dst = os.path.join(port_assets, info["port_file"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(data)
    check_assets(images, sounds, fonts, img_infos)
    check_audio(snd_infos, clips)
    dump_build(build, clips, snd_infos, fnt_infos, ext)

    # --- animation clips and controller for the C tables
    ctrl_dump = next(d for k, d in asset_dumps.items() if k.endswith(".controller.json"))
    cs = ctrl_dump["summary"]
    clip_files = sorted({s["motion"] for s in cs["states"]})
    clips, clip_id = [], {}
    for cf in clip_files:
        s = asset_dumps[dest_for(cf)]["summary"]
        assert s["stop_time"] > 0 and s["keys"], cf
        clips.append((s["name"], {"sample_rate": s["sample_rate"], "loop": bool(s["loop_time"]),
                                  "length": s["stop_time"], "keys": s["keys"]}))
        clip_id[cf] = "CLIP_" + c_ident(s["name"])
    default_id = int(cs["default_state"].split("#")[-1])
    controller = {"states": []}
    for o in sorted((o for o in ctrl_dump["objects"] if o["type"] == "AnimatorState"), key=lambda o: o["id"]):
        f = o["fields"]
        controller["states"].append({"name": f["m_Name"], "speed": f["m_Speed"], "clip_id": clip_id[f["m_Motions"][0]]})
        if o["id"] == default_id:
            controller["default_index"], controller["default_name"] = len(controller["states"]) - 1, f["m_Name"]
    assert "default_index" in controller, "default animator state not found"

    # --- C headers
    refmap = write_asset_tables_h(img_infos, snd_infos, fnt_infos, clips, controller,
                                  os.path.join(port_src, "asset_tables.h"))
    rows = build_values()
    write_values_h(rows, Dumps(ext), refmap, player_h, slop, os.path.join(port_src, "values.h"))
    levels = read_json(os.path.join(proj.assets, "Levels", "levels.json"))
    # The game's JSONObject parser keeps string text raw (no unescaping), so read it the same way.
    with open(os.path.join(proj.assets, "Levels", "morsecodes.json"), encoding="utf-8") as fh:
        morse = re.findall(r'\[\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\]', fh.read())
    check_tables(levels, morse)
    write_tables_h(levels, morse, os.path.join(port_src, "tables.h"))
    comment_counts = write_source_comments(proj, ext)
    write_code_constants(proj, os.path.join(port_src, "code_constants.h"))
    check_headers(port_src, Dumps(ext), snd_infos)

    # --- derived files
    matrix = Dumps(ext).get("settings/Physics2DSettings.json#m_LayerCollisionMatrix")
    pairs = [[i, j] for i in range(32) for j in range(i, 32) if (matrix[i] >> j) & 1]
    used_layers = sorted({n["layer"] for d in dumps_data.values() for n in d["gameobjects"]})
    assert len(pairs) == 32 * 33 // 2, "the layer note below is wrong"
    write_json(os.path.join(ext, "derived", "layer_collision.json"), {
        "layer_names": layers, "matrix_rows": matrix, "used_layers": used_layers,
        "all_pairs_collide": True, "colliding_pairs_between_used_layers": [[0, 0]],
        "note": "Row i, bit j set means layers i and j collide. Every body in the game sits on layer 0 (Default)."})
    write_json(os.path.join(ext, "derived", "polygon_pieces.json"), {
        "lengthUnitsPerMeter": player_h, "b2_linear_slop": slop,
        "rule": "triangulate (widest ear first), merge neighbours while convex, <= 8 points, every corner more than 2 x slop from collinear",
        "colliders": piece_report})

    # used / excluded asset report
    converted = ["Levels/levels.json", "Levels/morsecodes.json"]
    dumped = sorted(a for a in used if a.endswith(DUMP_EXT)) + prefab_paths
    all_files = sorted(os.path.relpath(os.path.join(dp, fn), proj.assets).replace(os.sep, "/")
                       for dp, _, fs in os.walk(proj.assets) for fn in fs if not fn.endswith(".meta"))
    ignored = {k: v for d in IGNORED_REFS.values() for k, v in d.items()}
    copied = set(images + sounds + fonts + converted + dumped)
    excluded = [{"path": f, "reason": ignored.get(f, "not referenced by any scene, prefab, clip or material")}
                for f in all_files
                if f not in copied and not f.startswith(("Scenes/", "ProjectSettings/")) and not f.endswith(SCRIPT_EXT)]
    write_json(os.path.join(ext, "derived", "used_assets.json"), {
        "copied_images": images, "copied_sounds": sounds, "copied_fonts": fonts,
        "sound_port_files": {i["file"]: i["port_file"] for i in snd_infos},
        "converted_to_c_tables": converted, "dumped_to_json": dumped,
        "scripts_referenced_by_scenes_or_prefabs": code_files,
        "scripts_in_project": [f for f in all_files if f.endswith(SCRIPT_EXT)],
        "referenced_by": {a: sorted(refs[a]) for a in used},
        "excluded": excluded,
        "standard_assets_prefabs_not_dumped": skipped_prefabs})

    n_go = sum(len(d["gameobjects"]) for d in dumps_data.values())
    n_comp = sum(len(n["components"]) for d in dumps_data.values() for n in d["gameobjects"])
    print("scenes %d, prefabs %d, gameobjects %d, components %d" % (len(SCENES), len(prefab_paths), n_go, n_comp))
    print("lengthUnitsPerMeter %s, linear slop %s" % (player_h, slop))
    for r in piece_report:
        print("pieces %-28s %-34s %d %s" % (r["dump"], r["gameobject"], r["pieces"], r["points_per_piece"]))
    print("assets: %d images, %d sounds, %d fonts copied; %d excluded; %d value rows" % (
        len(images), len(sounds), len(fonts), len(excluded), len(rows)))


if __name__ == "__main__":
    main()
