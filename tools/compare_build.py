#!/usr/bin/env python3
"""Compare the Unity source project with the 2016 Mac build, and check Assembly-CSharp.dll.

Usage:
    python -I tools/compare_build.py <Unity project> <build> <report.json>

<build> is the Data folder of the Mac player (.../RockMorse.app/Contents/Resources/Data) or the
Unity data bundle of the WebGL build (the gunzipped Release/RockMorse.datagz). The WebGL build has
no Assembly-CSharp.dll (IL2CPP), so the DLL check only runs for the Mac player.

Run tools/extract.py first. This script reads the dumps in extract/ for the source side and
the build files (levelN, sharedassetsN.assets, globalgamemanagers, Assembly-CSharp.dll) for the
build side. Nothing is written except the report. The report lists every value that differs.

The build holds no type trees for MonoBehaviour scripts, so their fields are decoded from the
raw bytes with a layout read from the C# source. If the decoded size matches the byte count of
every object, the field layout in the build matches the source.
"""
import json
import math
import os
import re
import struct
import sys

import UnityPy

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXT = os.path.join(REPO, "extract")
SCENES = ["Intro", "Title", "Story1", "Story2", "Game", "Death", "Win"]
TOL = 1e-4


def load_json(rel):
    with open(os.path.join(EXT, rel), encoding="utf-8") as fh:
        return json.load(fh)


# --------------------------------------------------------------------------
# build side: GameObject trees
# --------------------------------------------------------------------------

class BuildFile:
    def __init__(self, env_files, name):
        self.name = name
        self.sf = env_files[name]
        self.objs = {o.path_id: o for o in self.sf.objects.values()}
        self.cls = {pid: o.type.name for pid, o in self.objs.items()}
        self.tree = {}
        self.env_files = env_files
        self.script_name_cache = {}
        for pid, o in self.objs.items():
            if self.cls[pid] in ("Texture2D", "AudioClip", "Sprite", "Font", "Shader", "PreloadData", "MonoScript",
                                 "Material", "Mesh", "AnimationClip"):
                continue
            try:
                self.tree[pid] = o.read_typetree(check_read=False)
            except Exception as ex:
                self.tree[pid] = {"__error__": str(ex)}
        self.externals = [os.path.basename(e.path) for e in self.sf.externals]
        self._index()

    def deref(self, ref):
        fid, pid = ref["m_FileID"], ref["m_PathID"]
        if pid == 0:
            return None, None
        if fid == 0:
            return self, pid
        name = self.externals[fid - 1]
        other = self.env_files.get(name)
        if other is None:
            return None, ("external", name, pid)
        return other, pid

    def script_name(self, pid):
        t = self.tree.get(pid) or {}
        of, opid = self.deref(t.get("m_Script", {"m_FileID": 0, "m_PathID": 0}))
        if of is None or not isinstance(of, BuildFile) and not hasattr(of, "objects"):
            return None
        obj = (of.objs if isinstance(of, BuildFile) else {o.path_id: o for o in of.objects.values()}).get(opid)
        if obj is None:
            return None
        return obj.read_typetree().get("m_ClassName")

    def _index(self):
        gos = [p for p in self.objs if self.cls[p] == "GameObject"]
        tr_of = {}
        for g in gos:
            for c in self.tree[g].get("m_Component", []):
                cid = c["component"]["m_PathID"] if "component" in c else (c[1]["m_PathID"] if isinstance(c, (list, tuple)) else c["m_PathID"])
                if self.cls.get(cid) == "Transform":
                    tr_of[g] = cid
        go_of_tr = {v: k for k, v in tr_of.items()}
        parent, children = {}, {}
        for g, tr in tr_of.items():
            father = self.tree[tr]["m_Father"]["m_PathID"]
            parent[g] = go_of_tr.get(father)
            children[g] = [go_of_tr[c["m_PathID"]] for c in self.tree[tr]["m_Children"] if c["m_PathID"] in go_of_tr]
        roots = [g for g in gos if parent.get(g) is None]
        names = {g: self.tree[g]["m_Name"] for g in gos}
        path = {}

        def uniq(ids):
            return {i: (names[i] if sum(names[j] == names[i] for j in ids) == 1
                        else "%s[%d]" % (names[i], [j for j in ids if names[j] == names[i]].index(i))) for i in ids}

        def walk(g, prefix, label):
            path[g] = prefix + label
            u = uniq(children[g])
            for c in children[g]:
                walk(c, path[g] + "/", u[c])
        ur = uniq(roots)
        for r in roots:
            walk(r, "", ur[r])
        self.go_path, self.tr_of, self.parent, self.roots, self.children = path, tr_of, parent, roots, children
        self.comps = {}
        for g in gos:
            keys, counts = [], {}
            for c in self.tree[g]["m_Component"]:
                cid = c["component"]["m_PathID"] if "component" in c else (c[1]["m_PathID"] if isinstance(c, (list, tuple)) else c["m_PathID"])
                if cid not in self.objs:
                    continue
                n = self.cls[cid]
                if n == "MonoBehaviour":
                    n = self.script_name(cid) or "MonoBehaviour?"
                keys.append((cid, n))
                counts[n] = counts.get(n, 0) + 1
            seen, out = {}, []
            for cid, n in keys:
                if counts[n] > 1:
                    k = "%s[%d]" % (n, seen.get(n, 0))
                    seen[n] = seen.get(n, 0) + 1
                else:
                    k = n
                out.append((k, cid))
            self.comps[g] = out

    def label(self, ref):
        """Readable label for a reference, comparable with the source side."""
        of, pid = self.deref(ref)
        if pid is None:
            return None
        if of is None:
            return "builtin:%s" % (pid,)
        if not isinstance(of, BuildFile):
            obj = {o.path_id: o for o in of.objects.values()}.get(pid)
            if obj is None:
                return "?"
            try:
                t = obj.read_typetree()
            except Exception:
                return obj.type.name
            return norm_name(t.get("m_Name") or t.get("m_ClassName") or obj.type.name)
        c = of.cls.get(pid)
        if c is None:
            return "?"
        if c == "GameObject":
            return "@" + of.go_path.get(pid, "?")
        for g, comps in of.comps.items():
            for k, cid in comps:
                if cid == pid:
                    return "@%s:%s" % (of.go_path[g], k)
        o = of.objs[pid]
        try:
            return norm_name(o.read_typetree().get("m_Name", c))
        except Exception:
            return c


def norm_name(s):
    s = str(s).split("#")[-1]
    s = os.path.basename(s)
    s = re.sub(r"\.(png|wav|mp3|ttf|prefab|mat|anim|controller|json|cs|js|psd|tif)$", "", s)
    return s.lower()


# --------------------------------------------------------------------------
# flatten and compare
# --------------------------------------------------------------------------

def is_ref(v):
    return isinstance(v, dict) and set(v) == {"m_FileID", "m_PathID"}


def flatten(v, prefix, out, labeler):
    if is_ref(v):
        out[prefix] = ("ref", labeler(v))
    elif isinstance(v, dict):
        for k, x in v.items():
            flatten(x, prefix + "." + k if prefix else k, out, labeler)
    elif isinstance(v, (list, tuple)):
        for i, x in enumerate(v):
            flatten(x, "%s[%d]" % (prefix, i), out, labeler)
        out[prefix + ".#len"] = len(v)
    elif isinstance(v, (bytes, bytearray)):
        out[prefix] = "bytes%d" % len(v)
    else:
        out[prefix] = v
    return out


def same(a, b):
    if isinstance(a, tuple) and isinstance(b, str) or isinstance(b, tuple) and isinstance(a, str):
        a = a[1] if isinstance(a, tuple) else a
        b = b[1] if isinstance(b, tuple) else b
    if isinstance(a, tuple) and isinstance(b, tuple):
        return norm_ref(a[1]) == norm_ref(b[1])
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(float(a) - float(b)) <= TOL * max(1.0, abs(float(a)), abs(float(b)))
    return a == b


def norm_ref(x):
    if x is None:
        return None
    x = str(x)
    m = re.match(r"^.*\.prefab#(\w+):", x)
    if m:  # a reference into a prefab, the build only keeps the component type
        return m.group(1).lower()
    if x.startswith("@"):
        return x
    if x.startswith("builtin:"):
        return "builtin"
    return norm_name(x)


def src_label(v):
    return v if v is None or isinstance(v, str) else str(v)


ALIAS = {  # source field -> build field, per component type (Unity 4 -> 5 renames)
    "CircleCollider2D": {"m_Center": "m_Offset"},
    "BoxCollider2D": {"m_Center": "m_Offset"},
    "PolygonCollider2D": {"m_Poly": "m_Points"},
}
IGNORE = {"m_ObjectHideFlags", "m_PrefabParentObject", "m_PrefabInternal", "m_PrefabInstance", "m_PrefabAsset",
          "m_CorrespondingSourceObject", "m_EditorHideFlags", "m_EditorClassIdentifier", "m_GameObject", "m_Script",
          "m_StaticEditorFlags", "m_Icon", "m_NavMeshLayer", "m_TagString", "m_Tag", "m_Component"}


def compare_fields(ctype, src_fields, build_fields, build_label, diffs, where):
    """Compare two field dicts. src_fields hold readable ref strings, build_fields hold PPtr dicts."""
    alias = ALIAS.get(ctype, {})
    s_flat, b_flat = {}, {}
    for k, v in src_fields.items():
        if k in IGNORE:
            continue
        flat_src_value(v, alias.get(k, k), s_flat)
    for k, v in build_fields.items():
        if k in IGNORE:
            continue
        flatten(v, k, b_flat, build_label)
    only_s = sorted(k for k in s_flat if k not in b_flat)
    only_b = sorted(k for k in b_flat if k not in s_flat)
    compare_fields.last_extras = {k: (b_flat[k][1] if isinstance(b_flat[k], tuple) else b_flat[k]) for k in only_b}
    for k in sorted(set(s_flat) & set(b_flat)):
        a, b = s_flat[k], b_flat[k]
        if k == "m_LightmapIndex" or (k.startswith("m_Materials[") and isinstance(b, tuple)
                                      and b[1] in ("sprites-default", "font material")):
            continue  # lightmap index width changed in Unity 5, material labels differ only in naming
        if k.startswith("m_Children["):
            continue  # compared as a set below
        if not same(a, b):
            diffs.append({"where": where, "field": k, "source": a if not isinstance(a, tuple) else a[1],
                          "build": b if not isinstance(b, tuple) else b[1]})
    sc = sorted(str(v[1]) for k, v in s_flat.items() if k.startswith("m_Children[") and not k.endswith("#len"))
    bc = sorted(str(v[1]) for k, v in b_flat.items() if k.startswith("m_Children[") and not k.endswith("#len"))
    if sc != bc:
        diffs.append({"where": where, "field": "m_Children (as a set)", "source": sc, "build": bc})
    return only_s, only_b


def flat_src_value(v, prefix, out):
    if isinstance(v, dict):
        for k, x in v.items():
            flat_src_value(x, prefix + "." + k, out)
    elif isinstance(v, list):
        for i, x in enumerate(v):
            flat_src_value(x, "%s[%d]" % (prefix, i), out)
        out[prefix + ".#len"] = len(v)
    elif isinstance(v, str) and (v.startswith("@") or "#" in v or re.search(r"\.(png|wav|mp3|ttf|prefab|mat|anim|controller|json|cs|js|psd|tif)$", v)
                                 or v.startswith("builtin:")):
        out[prefix] = ("ref", v)
    elif v is None:
        out[prefix] = ("ref", None)
    else:
        out[prefix] = v


# --------------------------------------------------------------------------
# MonoBehaviour layouts from the C# source
# --------------------------------------------------------------------------

_CODE_TOKEN = re.compile(r'@"(?:[^"]|"")*"|"(?:[^"\\\n]|\\.)*"|/\*.*?\*/|//[^\n]*', re.S)


def strip_code(text):
    """Remove comments but keep string literals (a // inside a string is not a comment)."""
    def repl(m):
        t = m.group(0)
        if t.startswith(("/*", "//")):
            return "\n" * t.count("\n")
        return t
    return _CODE_TOKEN.sub(repl, text)


def parse_cs(path):
    """Returns (class name, serialized public fields [(type, name)], all fields, method names)."""
    with open(path, encoding="utf-8-sig") as fh:
        text = strip_code(fh.read())
    m = re.search(r"\bclass\s+(\w+)", text)
    cls = m.group(1)
    depth, pub, allf, meths, props = 0, [], [], [], []
    body_start = text.index("{", m.end())
    for line in text[body_start:].splitlines():
        d0 = depth
        depth += line.count("{") - line.count("}")
        if d0 != 1:
            continue
        s = line.strip()
        if not s or s.startswith(("[", "using ")):
            continue
        fm = re.match(r"^(?:(public|private|protected|internal)\s+)?(static\s+)?(const\s+)?(event\s+|delegate\s+)?"
                      r"([\w<>\[\],\.]+)\s+(\w+)\s*(=[^;]*)?;$", s)
        if fm and not fm.group(4):
            access, static, const, _, typ, name = fm.group(1), fm.group(2), fm.group(3), fm.group(4), fm.group(5), fm.group(6)
            if typ in ("return", "else"):
                continue
            allf.append((typ, name, access or "private", bool(static or const)))
            if access == "public" and not static and not const:
                pub.append((typ, name))
            continue
        cm = re.match(r"^(?:public\s+)?(\w+)\s*\(", s)
        if cm and cm.group(1) == cls:
            meths.append(".ctor")
            continue
        pm = re.match(r"^(?:(public|private|protected|internal)\s+)?(?:(static|override|virtual)\s+)?([\w<>\[\],\.]+)\s+(\w+)\s*\(", s)
        if pm and pm.group(3) not in ("new", "return", "if", "for", "foreach", "while", "switch"):
            meths.append(pm.group(4))
            continue
        prm = re.match(r"^(?:(public|private)\s+)?([\w<>\[\],\.]+)\s+(\w+)\s*\{", s)
        if prm:
            props.append(prm.group(3))
    return cls, pub, allf, sorted(set(meths)), props


def decode_fields(raw, offset, fields, aligned_bool=True):
    """Decode script fields. Returns (dict, end offset). Refs come back as PPtr dicts."""
    pos = offset
    out = {}

    def align():
        nonlocal pos
        pos = (pos + 3) & ~3

    def read(typ):
        nonlocal pos
        t = typ.strip()
        if t.endswith("[]") or t.startswith("List<"):
            elem = t[:-2] if t.endswith("[]") else t[5:-1]
            n = struct.unpack_from("<i", raw, pos)[0]
            pos += 4
            return [read(elem) for _ in range(n)]
        if t in ("int", "uint"):
            v = struct.unpack_from("<i", raw, pos)[0]
            pos += 4
            return v
        if t == "float":
            v = struct.unpack_from("<f", raw, pos)[0]
            pos += 4
            return v
        if t == "bool":
            v = raw[pos]
            pos += 1
            if aligned_bool:
                align()
            return bool(v)
        if t == "string":
            n = struct.unpack_from("<i", raw, pos)[0]
            pos += 4
            s = raw[pos:pos + n].decode("utf-8")
            pos += n
            align()
            return s
        if t in ("Vector2",):
            v = struct.unpack_from("<2f", raw, pos)
            pos += 8
            return {"x": v[0], "y": v[1]}
        if t in ("Vector3",):
            v = struct.unpack_from("<3f", raw, pos)
            pos += 12
            return {"x": v[0], "y": v[1], "z": v[2]}
        if t == "Rect":
            v = struct.unpack_from("<4f", raw, pos)
            pos += 16
            return {"x": v[0], "y": v[1], "width": v[2], "height": v[3]}
        if t == "Color":
            v = struct.unpack_from("<4f", raw, pos)
            pos += 16
            return {"r": v[0], "g": v[1], "b": v[2], "a": v[3]}
        fid, pid = struct.unpack_from("<iq", raw, pos)
        pos += 12
        return {"m_FileID": fid, "m_PathID": pid}

    for typ, name in fields:
        out[name] = read(typ)
    return out, pos


# --------------------------------------------------------------------------
# scene and prefab comparison
# --------------------------------------------------------------------------

def src_nodes(dump):
    return {n["path"]: n for n in dump["gameobjects"]}


def compare_tree(label, src_dump, bf, roots, schemas, report):
    """Compare one source dump with the GameObjects of a build file below the given roots."""
    s = src_nodes(src_dump)
    inc = set()

    def walk(g):
        inc.add(g)
        for c in bf.children[g]:
            walk(c)
    for r in roots:
        walk(r)
    b = {bf.go_path[g]: g for g in inc}
    res = {"label": label, "gameobjects_source": len(s), "gameobjects_build": len(b),
           "only_in_source": sorted(set(s) - set(b)), "only_in_build": sorted(set(b) - set(s)),
           "node_diffs": [], "component_set_diffs": [], "value_diffs": [], "field_name_diffs": [],
           "monobehaviour_layout_mismatch": []}
    for path in sorted(set(s) & set(b)):
        sn, g = s[path], b[path]
        bt = bf.tree[g]
        if bool(bt["m_IsActive"]) != sn["active"]:
            res["node_diffs"].append({"go": path, "field": "active", "source": sn["active"], "build": bool(bt["m_IsActive"])})
        if bt["m_Layer"] != sn["layer"]:
            res["node_diffs"].append({"go": path, "field": "layer", "source": sn["layer"], "build": bt["m_Layer"]})
        tag = bt.get("m_TagString", "")
        if tag and tag != sn["tag"]:
            res["node_diffs"].append({"go": path, "field": "tag", "source": sn["tag"], "build": tag})
        s_keys = [c["key"] for c in sn["components"]]
        b_keys = [k for k, _ in bf.comps[g]]
        if sorted(s_keys) != sorted(b_keys):
            res["component_set_diffs"].append({"go": path, "source": s_keys, "build": b_keys})
        bmap = dict(bf.comps[g])
        for c in sn["components"]:
            if c["key"] not in bmap:
                continue
            cid = bmap[c["key"]]
            ctype = c["type"]
            fields = c.get("fields", {})
            if c["type"] == "MonoBehaviour":
                cls = c["script"]
                raw = bf.objs[cid].get_raw_data()
                sch = schemas.get(cls)
                if sch is None:
                    res["monobehaviour_layout_mismatch"].append({"go": path, "script": cls, "problem": "no C# source layout"})
                    continue
                ok = False
                for aligned in (True, False):
                    try:
                        vals, end = decode_fields(raw, 32, sch, aligned)
                    except Exception:
                        continue
                    if end == len(raw):
                        ok = True
                        break
                if not ok:
                    res["monobehaviour_layout_mismatch"].append({"go": path, "script": cls, "bytes": len(raw),
                                                                  "problem": "decoded size differs from byte count"})
                    continue
                bfields = vals
                bfields["m_Enabled"] = bf.tree[cid].get("m_Enabled")
                ctype = "MonoBehaviour"
            else:
                bfields = bf.tree[cid]
                if "__error__" in bfields:
                    continue
            only_s, only_b = compare_fields(ctype, fields, bfields, bf.label, res["value_diffs"], "%s :: %s" % (path, c["key"]))
            if only_s or only_b:
                res["field_name_diffs"].append({"go": path, "component": c["key"], "only_in_source": only_s,
                                                "only_in_build": only_b,
                                                "build_values_of_new_fields": {
                                                    k: v for k, v in compare_fields.last_extras.items()
                                                    if not k.startswith(("m_Lightmap", "m_LightProbe", "m_Reflection",
                                                                         "m_MotionVectors", "m_ProbeAnchor"))}})
        # transform
        tr = bf.tree[bf.tr_of[g]]
        t = sn["transform"]
        for key, bk in (("local_position", "m_LocalPosition"), ("local_rotation", "m_LocalRotation"), ("local_scale", "m_LocalScale")):
            for ax in "xyzw":
                if ax in t[key] and not same(t[key][ax], tr[bk][ax]):
                    res["value_diffs"].append({"where": path + " :: Transform", "field": key + "." + ax,
                                               "source": t[key][ax], "build": tr[bk][ax]})
    return res


# --------------------------------------------------------------------------
# settings, assets, animation
# --------------------------------------------------------------------------

def compare_settings(files, report):
    out = {}
    gm = files["globalgamemanagers"]
    by = {o.type.name: o for o in gm.objects.values()}
    pairs = [("TimeManager", "settings/TimeManager.json"), ("Physics2DSettings", "settings/Physics2DSettings.json"),
             ("InputManager", "settings/InputManager.json"), ("AudioManager", "settings/AudioManager.json"),
             ("QualitySettings", "settings/QualitySettings.json"), ("PlayerSettings", "settings/ProjectSettings.json"),
             ("TagManager", "settings/TagManager.json")]
    for cname, rel in pairs:
        o = by.get(cname)
        if o is None:
            out[cname] = {"error": "not in build"}
            continue
        try:
            b = o.read_typetree(check_read=False)
        except Exception as ex:
            out[cname] = {"error": str(ex)}
            continue
        s = load_json(rel)["objects"][0]["fields"]
        diffs = []
        only_s, only_b = compare_fields(cname, s, b, lambda r: None, diffs, cname)
        out[cname] = {"value_diffs": diffs, "fields_only_in_source": len(only_s), "fields_only_in_build": len(only_b),
                      "only_in_build_names": only_b[:40], "only_in_source_names": only_s[:40]}
    return out


def compare_assets(env, files):
    """Sprites, textures, audio, fonts in the build against the .meta based dumps."""
    res = {"sprites": [], "textures": [], "audio": [], "fonts": [], "counts": {}}
    src_sprites = {}
    for root, _, fs in os.walk(os.path.join(EXT, "assets")):
        for fn in fs:
            d = json.load(open(os.path.join(root, fn), encoding="utf-8"))
            if "sprites" in d:
                for sp in d["sprites"]:
                    src_sprites[norm_name(sp["name"])] = (d, sp)
            if "is_3d" in d:
                src_sprites["audio:" + norm_name(d["file"])] = (d, None)
            if d["file"].endswith(".ttf"):
                src_sprites["font:" + norm_name(d["file"])] = (d, None)
            if "width" in d and d.get("sprites") == [] or "spriteMode" in d:
                src_sprites["tex:" + norm_name(d["file"])] = (d, None)
    seen_sprite = set()
    for fname, sf in sorted(files.items()):
        if not hasattr(sf, "objects"):
            continue
        for o in sf.objects.values():
            t = o.type.name
            if t == "Sprite":
                d = o.read_typetree()
                nm = norm_name(d["m_Name"])
                seen_sprite.add(nm)
                if nm not in src_sprites:
                    res["sprites"].append({"name": d["m_Name"], "problem": "not in source dumps"})
                    continue
                meta, sp = src_sprites[nm]
                r = d["m_Rect"]
                bad = []
                for key, bv, sv in (("x", r["x"], sp["x"]), ("y", r["y"], sp["y"]), ("w", r["width"], sp["w"]),
                                    ("h", r["height"], sp["h"]), ("ppu", d["m_PixelsToUnits"], meta["pixelsPerUnit"])):
                    if abs(bv - sv) > 1e-3:
                        bad.append({"field": key, "source": sv, "build": bv})
                pv = d["m_Pivot"]
                if abs(pv["x"] - sp["pivot"][0]) > 1e-3 or abs(pv["y"] - sp["pivot"][1]) > 1e-3:
                    bad.append({"field": "pivot", "source": sp["pivot"], "build": [pv["x"], pv["y"]]})
                if bad:
                    res["sprites"].append({"name": d["m_Name"], "diffs": bad})
            elif t == "Texture2D":
                d = o.read_typetree()
                nm = norm_name(d["m_Name"])
                key = "tex:" + nm
                if key not in src_sprites:
                    continue
                meta = src_sprites[key][0]
                bad = []
                ts = d.get("m_TextureSettings", {})
                if meta.get("width") and (d["m_Width"], d["m_Height"]) != (meta["width"], meta["height"]):
                    bad.append({"field": "size", "source": [meta["width"], meta["height"]], "build": [d["m_Width"], d["m_Height"]]})
                if ts.get("m_FilterMode") != meta.get("filterMode"):
                    bad.append({"field": "filterMode", "source": meta.get("filterMode"), "build": ts.get("m_FilterMode")})
                if ts.get("m_WrapMode") != meta.get("wrapMode"):
                    bad.append({"field": "wrapMode", "source": meta.get("wrapMode"), "build": ts.get("m_WrapMode")})
                bad.append({"field": "info", "build_format": d.get("m_TextureFormat"), "build_mipcount": d.get("m_MipCount")}) \
                    if d.get("m_TextureFormat") not in (4, 5, 3, 1, 63) else None
                res["textures"].append({"name": d["m_Name"], "build_format": d.get("m_TextureFormat"),
                                        "build_size": [d["m_Width"], d["m_Height"]], "diffs": [b for b in bad if b and b["field"] != "info"]})
            elif t == "AudioClip":
                d = o.read_typetree(check_read=False)
                nm = norm_name(d["m_Name"])
                key = "audio:" + nm
                if key not in src_sprites:
                    res["audio"].append({"name": d["m_Name"], "problem": "not in source dumps"})
                    continue
                meta = src_sprites[key][0]
                bad = []
                is3d = d.get("m_3D")
                if is3d is not None and bool(is3d) != bool(meta["is_3d"]):
                    bad.append({"field": "3D", "source": meta["is_3d"], "build": bool(is3d)})
                srcinfo = meta.get("source") or {}  # the source file's own facts, written by tools/extract.py
                if d.get("m_Channels") and srcinfo.get("channels") and d["m_Channels"] != srcinfo["channels"]:
                    bad.append({"field": "channels", "source": srcinfo["channels"], "build": d["m_Channels"]})
                fr = d.get("m_Frequency")
                if fr and srcinfo.get("sample_rate") and fr != srcinfo["sample_rate"]:
                    bad.append({"field": "sample_rate", "source": srcinfo["sample_rate"], "build": fr})
                ln = d.get("m_Length")
                if ln and srcinfo.get("seconds") and abs(ln - srcinfo["seconds"]) > 0.005:  # one MPEG frame is 0.026
                    bad.append({"field": "length", "source": srcinfo["seconds"], "build": ln})
                res["audio"].append({"name": d["m_Name"], "build": {"3D": is3d, "channels": d.get("m_Channels"),
                                     "frequency": fr, "length": ln, "compression": d.get("m_CompressionFormat"),
                                     "load_type": d.get("m_LoadType"), "stream": d.get("m_Stream")}, "diffs": bad})
            elif t == "Font":
                d = o.read_typetree(check_read=False)
                res["fonts"].append({"name": d["m_Name"], "font_size": d.get("m_FontSize"),
                                     "ascent": d.get("m_Ascent"), "character_spacing": d.get("m_CharacterSpacing")})
    res["counts"] = {"sprites_in_build": len(seen_sprite),
                     "source_sprites_missing_in_build": sorted(k for k in src_sprites
                                                                if ":" not in k and k not in seen_sprite)}
    return res


def compare_clips(files):
    out = []
    src = {}
    for fn in os.listdir(os.path.join(EXT, "animation")):
        d = load_json("animation/" + fn)
        if fn.endswith(".anim.json") and d["summary"]:
            src[norm_name(d["summary"]["name"])] = d["summary"]
    for fname, sf in sorted(files.items()):
        if not hasattr(sf, "objects"):
            continue
        bf = None
        for o in sf.objects.values():
            if o.type.name != "AnimationClip":
                continue
            bf = bf or BuildFile(files, fname)
            d = o.read_typetree(check_read=False)
            nm = norm_name(d["m_Name"])
            mc = d.get("m_MuscleClip") or {}
            mapping = (d.get("m_ClipBindingConstant") or {}).get("pptrCurveMapping") or []
            sprites = [bf.label(r) for r in mapping]
            row = {"name": d["m_Name"], "file": fname, "sample_rate": d.get("m_SampleRate"), "frames": len(mapping),
                   "loop_time": mc.get("m_LoopTime"), "stop_time": mc.get("m_StopTime"), "start_time": mc.get("m_StartTime"),
                   "build_sprite_order": sprites}
            if nm in src:
                s = src[nm]
                row["diffs"] = [{"field": f, "source": sv, "build": bv} for f, sv, bv in (
                    ("sample_rate", s["sample_rate"], d.get("m_SampleRate")), ("key_count", s["key_count"], len(mapping)),
                    ("loop_time", s["loop_time"], mc.get("m_LoopTime")),
                    ("stop_time", s["stop_time"], mc.get("m_StopTime"))) if sv is not None and not same(sv, bv)]
                if [norm_name(k["sprite"]) for k in s["keys"]] != sprites and s["keys"]:
                    row["diffs"].append({"field": "sprite_order", "source": [norm_name(k["sprite"]) for k in s["keys"]],
                                         "build": sprites})
                times = [i / s["sample_rate"] for i in range(len(s["keys"]))]
                if any(abs(a - k["time"]) > 1e-4 for a, k in zip(times, s["keys"])):
                    row["diffs"].append({"field": "key_times_not_frame_aligned"})
            out.append(row)
    return out


def audio_sources(files):
    """Every AudioSource in the build with the effective 3D settings (spatial blend, rolloff, reverb mix)."""
    rows = []
    for fname in sorted(files):
        if not (fname.startswith("level") or fname.startswith("sharedassets")) or not hasattr(files[fname], "objects"):
            continue
        bf = BuildFile(files, fname)
        for g, comps in sorted(bf.go_path.items(), key=lambda kv: kv[1]) and [(g, bf.comps[g]) for g in sorted(bf.go_path, key=bf.go_path.get)]:
            for k, cid in comps:
                if not k.startswith("AudioSource"):
                    continue
                t = bf.tree[cid]
                rows.append({"file": fname, "gameobject": bf.go_path[g], "clip": bf.label(t["m_audioClip"]),
                             "volume": t["m_Volume"], "pitch": t["m_Pitch"], "loop": bool(t["Loop"]),
                             "play_on_awake": bool(t["m_PlayOnAwake"]), "enabled": bool(t["m_Enabled"]),
                             "spatial_blend": t["panLevelCustomCurve"]["m_Curve"][0]["value"],
                             "spread": t["spreadCustomCurve"]["m_Curve"][0]["value"],
                             "reverb_zone_mix": t["reverbZoneMixCustomCurve"]["m_Curve"][0]["value"],
                             "rolloff_mode": t["rolloffMode"], "min_distance": t["MinDistance"],
                             "max_distance": t["MaxDistance"], "pan2d": t["Pan2D"], "priority": t["Priority"],
                             "doppler": t["DopplerLevel"], "bypass_reverb_zones": bool(t["BypassReverbZones"]),
                             "bypass_effects": bool(t["BypassEffects"]), "mute": bool(t["Mute"])})
    return rows


# --------------------------------------------------------------------------
# Assembly-CSharp.dll metadata reader (pure Python, no decompiler)
# --------------------------------------------------------------------------

TABLES = {
    0x00: ["u2", "str", "guid", "guid", "guid"],
    0x01: ["res:0,26,35,1", "str", "str"],
    0x02: ["u4", "str", "str", "tdor", "tbl:4", "tbl:6"],
    0x03: ["tbl:4"], 0x04: ["u2", "str", "blob"], 0x05: ["tbl:6"],
    0x06: ["u4", "u2", "u2", "str", "blob", "tbl:8"], 0x07: ["tbl:8"], 0x08: ["u2", "u2", "str"],
    0x09: ["tbl:2", "tdor"], 0x0A: ["mrp", "str", "blob"], 0x0B: ["u2", "hc", "blob"],
    0x0C: ["hca", "cat", "blob"], 0x0D: ["hfm", "blob"], 0x0E: ["u2", "hds", "blob"], 0x0F: ["u2", "u4", "tbl:2"],
    0x10: ["u4", "tbl:4"], 0x11: ["blob"], 0x12: ["tbl:2", "tbl:20"], 0x13: ["tbl:20"], 0x14: ["u2", "str", "tdor"],
    0x15: ["tbl:2", "tbl:23"], 0x16: ["tbl:23"], 0x17: ["u2", "str", "blob"], 0x18: ["u2", "tbl:6", "hs"],
    0x19: ["tbl:2", "mdor", "mdor"], 0x1A: ["str"], 0x1B: ["blob"], 0x1C: ["u2", "mf", "str", "tbl:26"],
    0x1D: ["u4", "tbl:4"], 0x1E: ["u4", "u4"], 0x1F: ["u4"], 0x20: ["u4", "u2", "u2", "u2", "u2", "u4", "blob", "str", "str"],
    0x21: ["u4"], 0x22: ["u4", "u4", "u4"], 0x23: ["u2", "u2", "u2", "u2", "u4", "blob", "str", "str", "blob"],
    0x24: ["u4", "tbl:35"], 0x25: ["u4", "u4", "u4", "tbl:35"], 0x26: ["u4", "str", "blob"],
    0x27: ["u4", "u4", "str", "str", "impl"], 0x28: ["u4", "u4", "str", "impl"], 0x29: ["tbl:2", "tbl:2"],
    0x2A: ["u2", "u2", "to", "str"], 0x2B: ["mdor", "blob"], 0x2C: ["tbl:42", "tdor"],
}
CODED = {
    "tdor": ([0x02, 0x01, 0x1B], 2), "hc": ([0x04, 0x08, 0x17], 2), "hca": (
        [0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B], 5),
    "hfm": ([0x04, 0x08], 1), "hds": ([0x02, 0x06, 0x20], 2), "mrp": ([0x02, 0x01, 0x1A, 0x06, 0x1B], 3),
    "hs": ([0x14, 0x17], 1), "mf": ([0x04, 0x06], 1), "impl": ([0x26, 0x23, 0x27], 2),
    "cat": ([None, None, 0x06, 0x0A, None], 3), "mdor": ([0x06, 0x0A], 1), "to": ([0x02, 0x06], 1),
    "res": ([0x00, 0x1A, 0x23, 0x01], 2),
}


class DotNet:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        d = self.d
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from("<H", d, opt)[0]
        dd = opt + (96 if magic == 0x10B else 112)
        cli_rva = struct.unpack_from("<I", d, dd + 14 * 8)[0]
        sec = opt + opt_size
        self.secs = [struct.unpack_from("<IIII", d, sec + i * 40 + 8) for i in range(nsec)]
        self.cli = self.rva(cli_rva)
        md_rva = struct.unpack_from("<I", d, self.cli + 8)[0]
        md = self.rva(md_rva)
        assert d[md:md + 4] == b"BSJB"
        vlen = struct.unpack_from("<I", d, md + 12)[0]
        p = md + 16 + vlen + 2
        ns = struct.unpack_from("<H", d, p)[0]
        p += 2
        self.streams = {}
        for _ in range(ns):
            off, size = struct.unpack_from("<II", d, p)
            p += 8
            end = d.index(b"\0", p)
            name = d[p:end].decode()
            p = (end + 4) & ~3
            self.streams[name] = (md + off, size)
        self._tables()

    def rva(self, rva):
        for vs, va, rs, ro in self.secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        raise ValueError("rva")

    def heap(self, name, idx):
        off, size = self.streams[name]
        return off + idx

    def string(self, idx):
        o = self.heap("#Strings", idx)
        return self.d[o:self.d.index(b"\0", o)].decode("utf-8", "replace")

    def blob(self, idx):
        o = self.heap("#Blob", idx)
        b = self.d[o]
        if b & 0x80 == 0:
            n, s = b, 1
        elif b & 0xC0 == 0x80:
            n, s = ((b & 0x3F) << 8) | self.d[o + 1], 2
        else:
            n, s = ((b & 0x1F) << 24) | (self.d[o + 1] << 16) | (self.d[o + 2] << 8) | self.d[o + 3], 4
        return self.d[o + s:o + s + n]

    def _tables(self):
        d = self.d
        off, size = self.streams["#~"]
        heap_sizes = d[off + 6]
        valid = struct.unpack_from("<Q", d, off + 8)[0]
        p = off + 24
        self.rows = {}
        for t in range(64):
            if valid >> t & 1:
                self.rows[t] = struct.unpack_from("<I", d, p)[0]
                p += 4
        sz_str = 4 if heap_sizes & 1 else 2
        sz_guid = 4 if heap_sizes & 2 else 2
        sz_blob = 4 if heap_sizes & 4 else 2

        def tbl_size(t):
            return 4 if self.rows.get(t, 0) > 0xFFFF else 2

        def coded_size(name):
            tabs, bits = CODED[name]
            mx = max(self.rows.get(t, 0) for t in tabs if t is not None)
            return 4 if mx >= (1 << (16 - bits)) else 2
        self.cols = {}
        for t in sorted(self.rows):
            cols = []
            for c in TABLES[t]:
                if c == "u2":
                    cols.append(2)
                elif c == "u4":
                    cols.append(4)
                elif c == "str":
                    cols.append(sz_str)
                elif c == "guid":
                    cols.append(sz_guid)
                elif c == "blob":
                    cols.append(sz_blob)
                elif c.startswith("tbl:"):
                    cols.append(tbl_size(int(c[4:])))
                elif c.startswith("res:"):
                    cols.append(coded_size("res"))
                else:
                    cols.append(coded_size(c))
            self.cols[t] = cols
        self.data = {}
        for t in sorted(self.rows):
            rows = []
            for _ in range(self.rows[t]):
                row = []
                for w in self.cols[t]:
                    row.append(struct.unpack_from("<H" if w == 2 else "<I", d, p)[0])
                    p += w
                rows.append(row)
            self.data[t] = rows

    def types(self):
        """TypeDef rows: name, namespace, field names, method names, and field flags."""
        td, fd, md = self.data[2], self.data.get(4, []), self.data.get(6, [])
        out = []
        for i, r in enumerate(td):
            f0 = r[4] - 1
            f1 = (td[i + 1][4] - 1) if i + 1 < len(td) else len(fd)
            m0 = r[5] - 1
            m1 = (td[i + 1][5] - 1) if i + 1 < len(td) else len(md)
            out.append({"name": self.string(r[1]), "namespace": self.string(r[2]),
                        "fields": [(self.string(fd[k][1]), fd[k][0]) for k in range(f0, f1)],
                        "methods": [self.string(md[k][3]) for k in range(m0, m1)],
                        "method_rvas": [md[k][0] for k in range(m0, m1)]})
        return out

    def user_strings(self):
        off, size = self.streams["#US"]
        d, p, out = self.d, off + 1, set()
        end = off + size
        while p < end:
            b = d[p]
            if b & 0x80 == 0:
                n, s = b, 1
            elif b & 0xC0 == 0x80:
                n, s = ((b & 0x3F) << 8) | d[p + 1], 2
            else:
                n, s = ((b & 0x1F) << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3], 4
            if n > 1:
                out.add(d[p + s:p + s + n - 1].decode("utf-16-le", "replace"))
            p += s + n
        return out


# IL operand sizes, used to walk method bodies and collect numeric constants
_OP1 = set(range(0x0E, 0x14)) | {0x1F} | set(range(0x2B, 0x38))
_OP4 = {0x20, 0x22, 0x27, 0x28, 0x29, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x79, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x8C, 0x8D, 0x8F, 0xA3, 0xA4, 0xA5, 0xC2, 0xC6, 0xD0} | set(range(0x38, 0x45))
_FE_OP = {0x06: 4, 0x07: 4, 0x09: 2, 0x0A: 2, 0x0B: 2, 0x0C: 2, 0x0D: 2, 0x0E: 2, 0x12: 1, 0x15: 4, 0x16: 4, 0x19: 1, 0x1C: 4}


def il_constants(net, rva):
    """Numeric constants (ldc.*) in one method body: returns (ints, floats)."""
    d = net.d
    if rva == 0:
        return set(), set()
    p = net.rva(rva)
    b = d[p]
    if b & 3 == 2:
        size, p = b >> 2, p + 1
    else:
        hdr = struct.unpack_from("<H", d, p)[0]
        size = struct.unpack_from("<I", d, p + 4)[0]
        p += (hdr >> 12) * 4
    end, ints, floats = p + size, set(), set()
    while p < end:
        op = d[p]
        p += 1
        if op == 0xFE:
            sub = d[p]
            p += 1 + _FE_OP.get(sub, 0)
        elif op == 0x45:
            n = struct.unpack_from("<I", d, p)[0]
            p += 4 + 4 * n
        elif op == 0x21:
            ints.add(struct.unpack_from("<q", d, p)[0])
            p += 8
        elif op == 0x23:
            floats.add(round(struct.unpack_from("<d", d, p)[0], 6))
            p += 8
        elif op == 0x22:
            floats.add(round(struct.unpack_from("<f", d, p)[0], 5))
            p += 4
        elif op == 0x20:
            ints.add(struct.unpack_from("<i", d, p)[0])
            p += 4
        elif op == 0x1F:
            ints.add(struct.unpack_from("<b", d, p)[0])
            p += 1
        elif 0x15 <= op <= 0x1E:
            ints.add(op - 0x16)  # ldc.i4.m1 .. ldc.i4.8
        elif op in _OP1:
            p += 1
        elif op in _OP4:
            p += 4
    return ints, floats


def cs_numeric_literals(path):
    with open(path, encoding="utf-8-sig") as fh:
        text = strip_code(fh.read())
    text = re.sub(r'@"(?:[^"]|"")*"|"(?:[^"\\\n]|\\.)*"', '""', text)
    ints, floats = set(), set()
    for m in re.finditer(r"(?<![\w.])(\d+\.\d*|\.\d+|\d+)([fFdD]?)(?![\w.])", text):
        tok, suf = m.group(1), m.group(2)
        if "." in tok or suf:
            floats.add(round(float(tok), 6))
        else:
            ints.add(int(tok))
    return ints, floats


def cs_string_literals(path):
    with open(path, encoding="utf-8-sig") as fh:
        text = strip_code(fh.read())
    lits = set()
    for m in re.finditer(r'@"((?:[^"]|"")*)"|"((?:[^"\\\n]|\\.)*)"', text):
        if m.group(1) is not None:
            lits.add(m.group(1).replace('""', '"'))
        else:
            s = m.group(2)
            s = s.replace("\\n", "\n").replace("\\t", "\t").replace('\\"', '"').replace("\\\\", "\\").replace("\\'", "'")
            lits.add(s)
    return {x for x in lits if x}


def compare_dll(project, data_dir):
    dll = os.path.join(data_dir, "Managed", "Assembly-CSharp.dll")
    net = DotNet(dll)
    types = {t["name"]: t for t in net.types()}
    us = net.user_strings()
    scripts = os.path.join(project, "Assets", "Scripts")
    res = {"dll_types": sorted(n for n in types if not n.startswith("<")), "scripts": [], "summary": {}}
    for fn in sorted(os.listdir(scripts)):
        if not fn.endswith(".cs"):
            continue
        cls, pub, allf, meths, props = parse_cs(os.path.join(scripts, fn))
        events = set(re.findall(r"\bevent\s+[\w.]+\s+(\w+)", strip_code(open(os.path.join(scripts, fn), encoding="utf-8-sig").read())))
        t = types.get(cls)
        row = {"script": fn, "class": cls}
        if t is None:
            row["problem"] = "class not in the DLL"
            res["scripts"].append(row)
            continue
        dll_fields = sorted(n for n, _ in t["fields"] if not n.startswith("<") and "k__BackingField" not in n)
        src_fields = sorted(n for _, n, _, _ in allf)
        dll_methods = sorted(set(t["methods"]) - {n for n in t["methods"] if n.startswith(("get_", "set_", "add_", "remove_"))})
        src_methods = sorted(set(meths))
        lits = cs_string_literals(os.path.join(scripts, fn))
        missing = sorted(x for x in lits if x not in us)
        d_ints, d_floats = set(), set()
        for rva in t["method_rvas"]:
            i, f = il_constants(net, rva)
            d_ints |= i
            d_floats |= f
        s_ints, s_floats = cs_numeric_literals(os.path.join(scripts, fn))
        implicit_ctor = ".ctor" in dll_methods and ".ctor" not in src_methods
        if implicit_ctor:
            dll_methods = [m for m in dll_methods if m != ".ctor"]
        dll_fields = [f for f in dll_fields if f not in events]
        src_fields = [f for f in src_fields if f not in events]
        row.update({
            "fields_only_in_source": sorted(set(src_fields) - set(dll_fields)),
            "fields_only_in_dll": sorted(set(dll_fields) - set(src_fields)),
            "methods_only_in_source": sorted(set(src_methods) - set(dll_methods)),
            "methods_only_in_dll": sorted(set(dll_methods) - set(src_methods)),
            "string_literals_in_source": len(lits), "string_literals_missing_in_dll": missing,
            "int_literals_in_source_not_in_dll": sorted(s_ints - d_ints - {0, 1}),
            "float_literals_in_source_not_in_dll": sorted(x for x in s_floats if not any(abs(x - y) <= 1e-5 * max(1, abs(x)) for y in d_floats)),
            "int_constants_in_dll_not_in_source": sorted(d_ints - s_ints - {-1, 0, 1, 2}),
            "float_constants_in_dll_not_in_source": sorted(d_floats - {round(x, 5) for x in s_floats} - {0.0, 1.0, 2.0})})
        res["scripts"].append(row)
    ok = [r for r in res["scripts"] if not r.get("problem") and not r["fields_only_in_source"] and not r["fields_only_in_dll"]
          and not r["methods_only_in_source"] and not r["methods_only_in_dll"] and not r["string_literals_missing_in_dll"]
          and not r["float_literals_in_source_not_in_dll"] and not r["int_literals_in_source_not_in_dll"]]
    res["summary"] = {"scripts_checked": len(res["scripts"]), "scripts_with_identical_names_and_strings": len(ok),
                      "dll_user_string_count": len(us)}
    return res


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    if len(sys.argv) != 4:
        sys.exit("usage: python -I tools/compare_build.py <Unity project> <Mac Data folder or WebGL data bundle> <report.json>")
    project, data_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    env = UnityPy.load(data_dir)
    files = {}
    for k, v in env.files.items():
        if hasattr(v, "objects"):
            files[os.path.basename(k)] = v
        elif hasattr(v, "files"):  # a Unity bundle holding the serialized files
            for k2, v2 in v.files.items():
                if hasattr(v2, "objects"):
                    files[os.path.basename(k2)] = v2
    # layouts of all scripts
    schemas = {}
    for fn in os.listdir(os.path.join(project, "Assets", "Scripts")):
        if fn.endswith(".cs"):
            cls, pub, _, _, _ = parse_cs(os.path.join(project, "Assets", "Scripts", fn))
            schemas[cls] = pub
    js = os.path.join(project, "Assets", "Standard Assets", "Particles", "Legacy Particles", "TimedObjectDestructor.js")
    if os.path.exists(js):  # UnityScript: "var name = value;" lines, type from the value
        with open(js, encoding="utf-8-sig") as fh:
            schemas["TimedObjectDestructor"] = [
                ("bool" if m.group(2) in ("true", "false") else "float" if "." in m.group(2) else "int", m.group(1))
                for m in re.finditer(r"^var\s+(\w+)\s*=\s*([\w.]+)\s*;", fh.read(), re.M)]
    report = {"scenes": [], "prefabs": [], "settings": None, "assets": None, "clips": None, "dll": None}
    for i, name in enumerate(SCENES):
        bf = BuildFile(files, "level%d" % i)
        report["scenes"].append(compare_tree(name, load_json("scenes/%s.json" % name), bf, bf.roots, schemas, report))
    # prefabs: roots found in sharedassets files, matched to source prefabs by root name
    prefab_dumps = {}
    for fn in os.listdir(os.path.join(EXT, "prefabs")):
        d = load_json("prefabs/" + fn)
        prefab_dumps[d["gameobjects"][0]["name"]] = d
    for fname in sorted(files):
        if not fname.startswith("sharedassets"):
            continue
        bf = BuildFile(files, fname)
        for r in bf.roots:
            nm = bf.tree[r]["m_Name"]
            if nm in prefab_dumps:
                report["prefabs"].append(compare_tree("%s (%s)" % (nm, fname), prefab_dumps[nm], bf, [r], schemas, report))
    report["prefabs_not_found_in_build"] = sorted(set(prefab_dumps) - {p["label"].split(" (")[0] for p in report["prefabs"]})
    report["settings"] = compare_settings(files, report)
    report["assets"] = compare_assets(env, files)
    report["clips"] = compare_clips(files)
    report["audio_sources_in_build"] = audio_sources(files)
    report["dll"] = (compare_dll(project, data_dir)
                     if os.path.exists(os.path.join(data_dir, "Managed", "Assembly-CSharp.dll")) else None)
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1, sort_keys=True, ensure_ascii=False, default=str)
        fh.write("\n")
    # short summary
    for part in ("scenes", "prefabs"):
        for r in report[part]:
            print("%-8s %-40s source %3d build %3d  only-src %d only-build %d comp-set %d value-diffs %d mb-layout-bad %d" % (
                part, r["label"], r["gameobjects_source"], r["gameobjects_build"], len(r["only_in_source"]),
                len(r["only_in_build"]), len(r["component_set_diffs"]), len(r["value_diffs"]),
                len(r["monobehaviour_layout_mismatch"])))
    print("dll:", report["dll"]["summary"] if report["dll"] else "not checked (no Assembly-CSharp.dll in this build)")


if __name__ == "__main__":
    main()
