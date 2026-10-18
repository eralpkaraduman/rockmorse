#!/usr/bin/env python3
"""Reads the 2016 Unity 5.4.3 Mac build for tools/extract.py. Nothing here touches the Unity project.

extract.py imports this file and passes the Data folder of the Mac player
(.../RockMorse.app/Contents/Resources/Data). It needs UnityPy with pyfmodex for the audio.

What it reads:
    audio_clips   every AudioClip of the build, decoded to 16 bit PCM WAV by UnityPy (FMOD)
    shaders       the Shader objects the game's materials use, parsed into JSON
    materials     the Materials of the build with the shader each one points to
    font          the Font object of the build, and whether its font data is the project's TTF

The asserts check facts the inventory relies on, so never run it with -O.
"""
import hashlib
import io
import os
import re
import struct

import lz4.block  # comes with UnityPy
import UnityPy

# Queue tag names to Unity's queue numbers (the "+N" offset is added to them).
QUEUES = {"Background": 1000, "Geometry": 2000, "AlphaTest": 2450, "Transparent": 3000, "Overlay": 4000}


def find_builtin_files(data_dir):
    """Paths of the two built-in resource files. They sit next to the Data folder or inside it."""
    res = os.path.dirname(os.path.abspath(data_dir))
    extra = [p for p in (os.path.join(data_dir, "Resources", "unity_builtin_extra"),
                         os.path.join(res, "unity_builtin_extra")) if os.path.isfile(p)]
    default = [p for p in (os.path.join(res, "unity default resources"),
                           os.path.join(data_dir, "Resources", "unity default resources")) if os.path.isfile(p)]
    assert len(extra) == 1 and len(default) == 1, "built-in resource files: %s %s" % (extra, default)
    return extra[0], default[0]


class Build:
    """The serialized files of the build by base name, plus the two built-in resource files."""

    def __init__(self, data_dir):
        self.data_dir = os.path.abspath(data_dir)
        self.files = {}
        env = UnityPy.load(self.data_dir)
        for k, v in env.files.items():
            if hasattr(v, "objects"):
                self.files[os.path.basename(k)] = v
        extra, default = find_builtin_files(self.data_dir)
        for key, path in (("resources/unity_builtin_extra", extra), ("library/unity default resources", default)):
            sub = UnityPy.load(path)
            assert len(sub.files) == 1
            self.files[key] = list(sub.files.values())[0]
        for key in ("globalgamemanagers.assets", "sharedassets4.assets"):
            assert key in self.files, key

    def object(self, fname, pid):
        return self.files[fname].objects[pid]

    def deref(self, fname, ref):
        """(file key, path id) a PPtr of a serialized file points to."""
        fid, pid = ref["m_FileID"], ref["m_PathID"]
        if fid == 0:
            return fname, pid
        path = self.files[fname].externals[fid - 1].path  # e.g. "library/unity default resources"
        key = path if path in self.files else os.path.basename(path)
        assert key in self.files, "external file %s is not loaded" % path
        return key, pid


# --------------------------------------------------------------------------
# audio
# --------------------------------------------------------------------------

# AudioClip.m_CompressionFormat values seen in the build
COMPRESSION = {0: "PCM", 1: "Vorbis"}


def wav_parts(b):
    """(fmt tuple, data bytes) of a RIFF WAVE file. fmt = (format tag, channels, rate, bytes/s, align, bits)."""
    assert b[:4] == b"RIFF" and b[8:12] == b"WAVE", "not a WAVE file"
    p, fmt, data = 12, None, None
    while p + 8 <= len(b):
        cid, n = b[p:p + 4], struct.unpack_from("<I", b, p + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", b, p + 8)
        elif cid == b"data":
            data = b[p + 8:p + 8 + n]
        p += 8 + n + (n & 1)
    assert fmt is not None and data is not None, "WAVE file without fmt or data chunk"
    return fmt, data


def audio_clips(build):
    """name -> {meta..., "wav": bytes}. Every AudioClip of the build (16 clips), names unique."""
    out = {}
    for fname in sorted(build.files):
        if not fname.startswith(("level", "sharedassets", "globalgamemanagers")):
            continue
        for pid, o in sorted(build.files[fname].objects.items()):
            if o.type.name != "AudioClip":
                continue
            c = o.read()
            assert len(c.samples) == 1, c.m_Name
            [(wav_name, wav)] = c.samples.items()
            fmt, data = wav_parts(wav)
            assert fmt[0] == 1 and fmt[5] == 16, "decoded clip %s is not 16 bit PCM" % c.m_Name
            frames = len(data) // fmt[4]
            assert len(data) % fmt[4] == 0
            t = o.read_typetree(check_read=False)
            assert t["m_Channels"] == fmt[1] and t["m_Frequency"] == fmt[2], c.m_Name
            assert abs(t["m_Length"] - frames / fmt[2]) < 1e-4, "%s: m_Length is not frames / rate" % c.m_Name
            assert c.m_Name not in out, "two AudioClips named " + c.m_Name
            out[c.m_Name] = {
                "name": c.m_Name, "build_file": fname, "path_id": pid,
                "compression": COMPRESSION[t["m_CompressionFormat"]], "channels": fmt[1], "sample_rate": fmt[2],
                "bits_per_sample": fmt[5], "frames": frames, "seconds": t["m_Length"],
                "load_type": t["m_LoadType"], "is_3d": bool(t["m_3D"]) if t.get("m_3D") is not None else None,
                "stored_bytes": t["m_Resource"]["m_Size"], "resource": t["m_Resource"]["m_Source"],
                "pcm": data, "wav": wav}
    return out


# --------------------------------------------------------------------------
# shaders and materials
# --------------------------------------------------------------------------

_TAG = re.compile(r'"([^"]+)"="([^"]*)"')
_PROP = re.compile(r'^\s*(?:\[[^\]]*\]\s*)*(\w+)\s*\("([^"]*)",\s*(\w+(?:\s*\([^)]*\))?)\)\s*=\s*(.*)$')
_STATE = re.compile(r"^\s*(ZWrite|ZTest|Cull|Blend|ColorMask|Offset|Lighting|AlphaToMask|LOD|Fallback)\b\s*(.*)$")


def parse_shader_script(text):
    """Header, properties, tags and render state from the text of a Shader (Unity's decompiled form).
    Only the first SubShader is read. The GPU programs are not in the text, only 'GpuProgramIndex' lines."""
    lines = text.split("\n")
    m = re.match(r'^Shader "([^"]*)" \{$', lines[0])
    assert m, "unexpected shader text: " + lines[0]
    sh = {"shader_name": m.group(1), "properties": [], "lod": None, "fallback": None, "subshader_tags": {},
          "subshader_state": {}, "subshader_count": 0, "passes": []}
    in_props, cur = False, None
    for ln in lines[1:]:
        s = ln.strip()
        if in_props:
            if s == "}":
                in_props = False
                continue
            pm = _PROP.match(ln)
            assert pm, "property line: " + ln
            sh["properties"].append({"name": pm.group(1), "label": pm.group(2), "type": pm.group(3),
                                     "default": pm.group(4)})
            continue
        if s == "Properties {":
            in_props = True
        elif s == "SubShader {":
            sh["subshader_count"] += 1
            cur = None
        elif sh["subshader_count"] == 1 and s == "Pass {":
            cur = {"name": None, "tags": {}, "state": {}}
            sh["passes"].append(cur)
        elif sh["subshader_count"] <= 1:
            tm = re.match(r"^Tags \{ (.*) \}$", s)
            nm = re.match(r'^Name "([^"]*)"$', s)
            st = _STATE.match(ln)
            if tm:
                (sh["subshader_tags"] if cur is None else cur["tags"]).update(_TAG.findall(tm.group(1)))
            elif nm and cur is not None:
                cur["name"] = nm.group(1)
            elif st and st.group(1) == "LOD":
                sh["lod"] = int(st.group(2))
            elif st and st.group(1) == "Fallback":
                sh["fallback"] = st.group(2).strip('"')
            elif st:
                (sh["subshader_state"] if cur is None else cur["state"])[st.group(1)] = st.group(2)
    q = sh["subshader_tags"].get("QUEUE")
    if q is not None:
        base = re.match(r"^([A-Za-z]+)([+-]\d+)?$", q)
        sh["queue"] = q
        sh["queue_number"] = QUEUES[base.group(1)] + int(base.group(2) or 0)
    return sh


def blob_features(t):
    """Facts read from the compiled GPU programs of a Shader (LZ4 compressed text of all platforms).
    The blob holds GLSL (opengl), Metal and GLSL core text. We look for the names the programs use."""
    blob = bytes(t["m_SubProgramBlob"])
    if not blob:
        return None
    raw = lz4.block.decompress(blob, uncompressed_size=t["decompressedSize"])
    names = sorted({n.decode() for n in re.findall(
        rb"\b(?:unity_\w+|_\w+Color\w*|_\w*Light\w*|_MainTex\w*|_Cutoff|_Color|PixelSnap|glstate_\w+|in_[A-Z]+\d|gl_Color|gl_Normal|"
        rb"gl_MultiTexCoord\d|_glesColor|_glesNormal|_glesMultiTexCoord\d)\b", raw)})
    text = raw.decode("latin1")
    first = text.index("#version 120")  # the first desktop OpenGL program of the blob, as evidence
    return {
        "program_text_bytes": len(raw), "program_text_md5": hashlib.md5(raw).hexdigest(),
        "first_opengl_program": text[first:text.index("#endif", text.index("#ifdef FRAGMENT", first)) + 6],
        "uses_vertex_colour": bool(re.search(rb"\bgl_Color\b|\bin_COLOR\d\b|\b_glesColor\b|\bCOLOR\d\b", raw)),
        "uses_normal": bool(re.search(rb"\bgl_Normal\b|\bin_NORMAL0\b", raw)),
        "uses_scene_lights": bool(re.search(rb"\b_WorldSpaceLightPos0\b|\b_LightColor0\b|\bunity_LightColor\b", raw)),
        "uses_ambient_sh": bool(re.search(rb"\bunity_SHAr\b", raw)),
        "samples_main_tex": bool(re.search(rb"\b_MainTex\b", raw)),
        "names_found": names}


def shader_info(build, fname, pid):
    t = build.object(fname, pid).read_typetree(check_read=False)
    sh = parse_shader_script(t["m_Script"])
    sh.update({"build_file": fname, "path_id": pid, "asset_name": t["m_Name"],
               "dependencies": [list(build.deref(fname, d)) for d in t["m_Dependencies"]],
               "baked": bool(t["m_ShaderIsBaked"]), "programs": blob_features(t)})
    pr = sh["programs"]
    states = [p["state"] for p in sh["passes"]]
    sh["summary"] = {
        "blend_per_pass": [p.get("Blend") for p in states], "zwrite_per_pass": [p.get("ZWrite") for p in states],
        "cull_per_pass": [p.get("Cull") for p in states], "colour_mask_per_pass": [p.get("ColorMask") for p in states],
        "ztest_per_pass": [p.get("ZTest") for p in states],
        "queue": sh.get("queue"), "queue_number": sh.get("queue_number"),
        "lit_by_scene_lights": pr["uses_scene_lights"], "ambient_from_spherical_harmonics": pr["uses_ambient_sh"],
        "uses_vertex_colour": pr["uses_vertex_colour"],
        "premultiplied_alpha": [p.get("Blend") == "One OneMinusSrcAlpha" for p in states]}
    return sh


def material_info(build, fname, pid):
    o = build.object(fname, pid)
    t = o.read_typetree(check_read=False)
    sfile, spid = build.deref(fname, t["m_Shader"])
    sp = t["m_SavedProperties"]
    return {"name": t["m_Name"], "build_file": fname, "path_id": pid, "shader": [sfile, spid],
            "custom_render_queue": t["m_CustomRenderQueue"], "keywords": t["m_ShaderKeywords"],
            "colors": {k["name"]: v for k, v in sp["m_Colors"]}, "floats": {k["name"]: v for k, v in sp["m_Floats"]},
            "textures": {k["name"]: list(build.deref(fname, v["m_Texture"])) if v["m_Texture"]["m_PathID"] else None
                         for k, v in sp["m_TexEnvs"]}}


def shaders_and_materials(build):
    """Every Material of the game's serialized files, and the Shader each one uses. Returns a dump dict."""
    mats = []
    for fname in sorted(build.files):
        for pid, o in sorted(build.files[fname].objects.items()):
            if o.type.name == "Material" and fname in ("globalgamemanagers.assets", "sharedassets1.assets",
                                                       "sharedassets4.assets"):
                mats.append(material_info(build, fname, pid))
    used = sorted({tuple(m["shader"]) for m in mats})
    shaders = [shader_info(build, f, p) for f, p in used]
    return {"materials": mats, "shaders": shaders}


# --------------------------------------------------------------------------
# font
# --------------------------------------------------------------------------

def font_object(build, ttf_bytes):
    """The one Font object of the build, with the facts about it. ttf_bytes is the project's font file."""
    found = []
    for fname in sorted(build.files):
        for pid, o in sorted(build.files[fname].objects.items()):
            if o.type.name == "Font" and fname.startswith("sharedassets"):
                found.append((fname, pid, o))
    assert len(found) == 1, "expected one Font object, found %d" % len(found)
    fname, pid, o = found[0]
    t = o.read_typetree(check_read=False)
    data = bytes(t["m_FontData"])
    out = {"build_file": fname, "path_id": pid}
    for k in ("m_Name", "m_FontSize", "m_Ascent", "m_Descent", "m_LineSpacing", "m_CharacterSpacing", "m_CharacterPadding",
              "m_Tracking", "m_ConvertCase", "m_AsciiStartOffset", "m_PixelScale", "m_DefaultStyle", "m_FontNames",
              "m_FontRenderingMode"):
        out[k] = t[k]
    out["dynamic"] = t["m_ConvertCase"] == -2 and len(t["m_CharacterRects"]) == 0
    out["character_rects"] = len(t["m_CharacterRects"])
    out["kerning_values"] = len(t["m_KerningValues"])
    out["font_data_bytes"] = len(data)
    out["font_data_is_project_ttf"] = data == ttf_bytes
    return out
