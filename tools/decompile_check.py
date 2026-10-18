"""Compare C# source with ILSpy output, member by member.

Usage: python3 -I tools/decompile_check.py <source_dir> <decompiled_dir> [<source_dir> <decompiled_dir> ...]
Each pair is scanned for *.cs (source) and *.cs (decompiled). Source .js is not handled
(TimedObjectDestructor.js is read by hand). Output goes to stdout.

Normalisation (both sides): comments, attributes, access modifiers, `this.`, `Object.`,
`UnityEngine.` prefixes, Debug.Log statements (counted), local variable names -> L,
local declaration types, float literals (rounded to float32, f/d suffix dropped),
string.Empty -> "", `var`. Members are matched by class + name + parameter count.
Iterator methods (source has `yield`) are paired with the generated MoveNext and flagged
for manual review. This is a filter: every remaining diff needs a human read.
"""
import re, struct, sys, os, difflib

KW = set("""abstract as base bool break byte case catch char checked class const continue decimal default delegate do double
else enum event explicit extern false finally fixed float for foreach goto if implicit in int interface internal is lock long
namespace new null object operator out override params private protected public readonly ref return sbyte sealed short sizeof
stackalloc static string struct switch this throw true try typeof uint ulong unchecked unsafe ushort using virtual void volatile
while yield var get set value""".split())
MODS = {"private", "public", "protected", "internal", "sealed", "override", "virtual", "readonly", "partial", "unsafe", "extern", "new_mod"}
TOK = re.compile(r'''//[^\n]*|/\*.*?\*/|@"(?:[^"]|"")*"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])+'|\#[^\n]*|
    \d[\d_]*\.?\d*(?:[eE][+-]?\d+)?[fFdDmMuUlL]*|\.\d+(?:[eE][+-]?\d+)?[fFdD]?|0[xX][0-9a-fA-F]+[uUlL]*|[A-Za-z_]\w*|
    <<=|>>=|\?\?|\+\+|--|&&|\|\||==|!=|<=|>=|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|=>|::|\S''', re.S | re.X)


UNITY_DEFINES = {"UNITY_5", "UNITY_5_4", "UNITY_5_4_3", "UNITY_STANDALONE", "UNITY_STANDALONE_OSX", "UNITY_5_4_OR_NEWER", "ENABLE_MONO"}


def preprocess(text):
    """Evaluate #if/#elif/#else/#endif/#define; excluded and directive lines become blank (line numbers kept)."""
    defs, out, stack = set(UNITY_DEFINES), [], []   # stack: (parent_active, taken, active)
    def ev(expr):
        e = re.sub(r"//.*", "", expr)
        e = re.sub(r"[A-Za-z_]\w*", lambda m: "True" if m.group(0) in defs else ("False" if m.group(0) not in ("true", "false") else m.group(0).capitalize()), e)
        return bool(eval(e.replace("&&", " and ").replace("||", " or ").replace("!", " not "), {"__builtins__": {}}))
    for line in text.split("\n"):
        m = re.match(r"\s*#\s*(\w+)\s*(.*)", line)
        act = all(x[2] for x in stack)
        if m:
            d, arg = m.groups()
            if d == "define":
                if act: defs.add(arg.split()[0].split("/")[0])
            elif d == "if":
                v = ev(arg) if act else False
                stack.append((act, v, v))
            elif d == "elif":
                par, taken, _ = stack.pop()
                v = (not taken) and par and ev(arg)
                stack.append((par, taken or v, v))
            elif d == "else":
                par, taken, _ = stack.pop()
                stack.append((par, True, par and not taken))
            elif d == "endif":
                stack.pop()
            out.append("")
            continue
        out.append(line if act else "")
    return "\n".join(out)


def tokenize(text):
    out, line = [], 1
    for m in TOK.finditer(text):
        t = m.group(0)
        start_line = line
        line += t.count("\n")
        pos_nl = text.count("\n", 0, m.start()) + 1
        if t.startswith("//") or t.startswith("/*") or t.startswith("#"):
            continue
        out.append((t, pos_nl))
    return out


def canon_str(t):
    if t.startswith('@"'):
        return '"' + t[2:-1].replace('\\', '\\\\').replace('""', '\\"') + '"'
    return t


def canon_num(t):
    if re.fullmatch(r'0[xX][0-9a-fA-F]+[uUlL]*', t):
        return str(int(t.rstrip("uUlL"), 16))
    if re.match(r'[\d.]', t):
        s = t.rstrip("fFdDmMuUlL").replace("_", "")
        if re.fullmatch(r'\d+', s):
            return s
        try:
            v = float(s)
        except ValueError:
            return t
        if t[-1] in "fF":
            v = struct.unpack("f", struct.pack("f", v))[0]
            suf = "f"
        else:
            suf = "d" if "." in s or "e" in s.lower() else ""
        return str(int(v)) if v == int(v) and abs(v) < 1e9 else repr(v) + suf
    return t


def strip_attrs(toks):
    """Remove [..] attribute groups at member starts. Returns tokens; attributes kept as ('@attr', text)."""
    out, i, depth_b = [], 0, 0
    while i < len(toks):
        t = toks[i][0]
        prev = out[-1][0] if out else ";"
        if t == "[" and prev in (";", "{", "}", ")") and not (prev == ")" ):
            j, d = i, 0
            while True:
                if toks[j][0] == "[": d += 1
                elif toks[j][0] == "]":
                    d -= 1
                    if d == 0: break
                j += 1
            attr = "".join(x[0] for x in toks[i + 1:j])
            out.append(("\x00" + attr, toks[i][1]))
            i = j + 1
            continue
        out.append(toks[i]); i += 1
    return out


def match_brace(toks, i):
    d = 0
    for j in range(i, len(toks)):
        if toks[j][0] == "{": d += 1
        elif toks[j][0] == "}":
            d -= 1
            if d == 0: return j
    raise ValueError("unbalanced")


def parse_types(toks, prefix=""):
    """Return list of (qualified class name, body tokens [inside braces], header tokens)."""
    res, i, n = [], 0, len(toks)
    while i < n:
        t = toks[i][0]
        if t in ("class", "struct") and i + 1 < n and re.match(r"[\w<]", toks[i + 1][0]):
            name = toks[i + 1][0]
            j = i + 2
            while toks[j][0] != "{": j += 1
            end = match_brace(toks, j)
            q = prefix + name
            body = toks[j + 1:end]
            res.append((q, body, toks[i:j]))
            res += parse_types(body, q + "+")
            i = end + 1
        else:
            i += 1
    return res


def top_members(body):
    """Split a class body into members at depth 0 (nested types are skipped but kept as members of kind 'type')."""
    members, i, n = [], 0, len(body)
    while i < n:
        j, d = i, 0
        while j < n:
            t = body[j][0]
            if t in "([": d += 1
            elif t in ")]": d -= 1
            elif d == 0 and t == ";": break
            elif d == 0 and t == "{": break
            j += 1
        head = body[i:j]
        if j >= n or body[j][0] == ";":
            if head: members.append((head, None))
            i = j + 1
        else:
            end = match_brace(body, j)
            blk = body[j:end + 1]
            # field with object initialiser: head ends with '=' / 'new X(..)'
            if head and head[-1][0] in ("=",) or any(h[0] == "=" for h in head) and "(" not in [h[0] for h in head][:3]:
                # initialiser block, continue to the ';'
                k = end + 1
                while k < n and body[k][0] != ";": k += 1
                members.append((head + body[j:k], None)); i = k + 1
                continue
            if head and head[-1][0] == "=>":
                pass
            members.append((head, blk)); i = end + 1
    return members


DECL_STOP = KW - {"var"} | {"out", "ref"}


def type_end(toks, i):
    """If a type starts at i, return index after it; else -1."""
    n = len(toks)
    t = toks[i][0]
    if not re.match(r"[A-Za-z_]", t) or (t in KW and t not in ("var", "int", "float", "bool", "string", "char", "byte", "double", "long", "short", "uint", "object", "decimal")):
        return -1
    j = i + 1
    while j + 1 < n and toks[j][0] == "." and re.match(r"[A-Za-z_]", toks[j + 1][0]): j += 2
    if j < n and toks[j][0] == "<":
        d, k = 0, j
        while k < n:
            if toks[k][0] == "<": d += 1
            elif toks[k][0] == ">":
                d -= 1
                if d == 0: break
            elif not re.match(r"[\w.,\[\]?]", toks[k][0]): return -1
            k += 1
        else: return -1
        j = k + 1
    while j + 1 < n and toks[j][0] == "[" and toks[j + 1][0] in ("]", ","):
        while toks[j][0] != "]": j += 1
        j += 1
    return j


def find_locals(toks):
    locs = set()
    for i in range(len(toks)):
        if i and toks[i - 1][0] in (".",): continue
        e = type_end(toks, i)
        if e < 0 or e >= len(toks): continue
        nm = toks[e][0]
        if not re.match(r"[A-Za-z_]", nm) or nm in KW: continue
        nxt = toks[e + 1][0] if e + 1 < len(toks) else ""
        if nxt in ("=", ";", ",", ")", "in", ":"):
            if i and toks[i - 1][0] in ("return", "new", "else", "case", "throw", "is", "as", "yield", "in"): continue
            locs.add((i, e))
    return locs


INHERITED = set("""transform gameObject renderer audio camera collider rigidbody animation enabled name tag
collider2D rigidbody2D guiText guiTexture light hingeJoint particleSystem constantForce networkView hideFlags
useGUILayout isActiveAndEnabled""".split())


CONSTS = {("float", "PositiveInfinity"): "Infinity", ("Mathf", "Infinity"): "Infinity",
          ("float", "NegativeInfinity"): "-Infinity", ("Mathf", "NegativeInfinity"): "-Infinity"}


def post(t):
    """Token-level equivalences. t: list of (tok, line)."""
    r, i, n = [], 0, len(t)
    while i < n:
        a = t[i][0]
        if i + 2 < n and (a, t[i + 1][0]) == ("float", ".") or (a == "Mathf" and i + 2 < n and t[i + 1][0] == "."):
            k = CONSTS.get((a, t[i + 2][0]))
            if k: r.append((k, t[i][1])); i += 3; continue
        # named argument `name :` right after '(' or ','
        if i + 1 < n and re.match(r"[A-Za-z_]", a) and t[i + 1][0] == ":" and r and r[-1][0] in ("(", ",") and a not in KW:
            i += 2; continue
        if a == "new" and i + 3 < n and t[i + 1][0] in ("Vector2", "Vector3", "Rect") and t[i + 2][0] == "(" and t[i + 3][0] == ")":
            r.append(("default", t[i][1])); i += 4; continue
        # (float) cast dropped; counted via FLOATCASTS
        if a == "(" and i + 2 < n and t[i + 1][0] in ("float", "double") and t[i + 2][0] == ")":
            FLOATCASTS[0] += 1; i += 3; continue
        if a == "(" and i + 2 < n and t[i + 1][0] == "bool" and t[i + 2][0] == ")" and i + 3 < n and re.match(r"[A-Za-z_]", t[i + 3][0]):
            i += 3; continue
        # x == false -> ! x ; x == true -> x
        if a == "==" and i + 1 < n and t[i + 1][0] in ("true", "false"):
            if t[i + 1][0] == "false":
                # wrap handled below by marking
                r.append(("==false", t[i][1]))
            i += 2; continue
        # `0 - x` as negation
        if a == "-" and r and r[-1][0] == "0" and (len(r) < 2 or r[-2][0] in ("=", "(", ",", "return", "?", ":")):
            r.pop(); r.append(("NEG", t[i][1])); i += 1; continue
        if a == "-" and (not r or r[-1][0] in ("=", "(", ",", "return", "?", ":")):
            r.append(("NEG", t[i][1])); i += 1; continue
        r.append(t[i]); i += 1
    # ILSpy ref locals: `ref L = ref X ; ... L = v` -> `X = v` (single and chained forms)
    r = simplify(r)
    # `ident ==false` -> `! ident`: only the simple identifier-chain form
    o, i = [], 0
    while i < len(r):
        if r[i][0] == "==false":
            j = len(o) - 1
            while j >= 0 and (re.match(r"[A-Za-z_]\w*$", o[j][0]) or o[j][0] == "."): j -= 1
            o.insert(j + 1, ("!", r[i][1])); i += 1; continue
        o.append(r[i]); i += 1
    # drop `as X` cast / `( ... )` of redundant grouping is left to the human
    p, i = [], 0
    while i < len(o):
        if o[i][0] == "as" and i + 1 < len(o): i += 2; continue
        p.append(o[i]); i += 1
    return p


DELEGATES = {"ApplyTween", "EasingFunction", "AddJSONConents", "FieldNotFound", "GetFieldResponse"}
WEAK_PREV = {"=", "(", ",", "return", "<", ">", "<=", ">=", "==", "!=", "&&", "||", "?", ":", "+=", "-=", "*=", "/=", "[", "NEG"}
WEAK_NEXT = {";", ")", ",", "<", ">", "<=", ">=", "==", "!=", "&&", "||", "?", ":", "]"}
ARITH = {"+", "-", "*", "/", "%", ".", "NEG", "(", ")", "[", "]", "L", ","}

def strip_itween(r):
    out, i = [], 0
    while i < len(r):
        if r[i][0] == "iTween" and i + 1 < len(r) and r[i + 1][0] == "." and (i == 0 or r[i - 1][0] != "."):
            i += 2; continue
        out.append(r[i]); i += 1
    return out


def flip_ternaries(r):
    """ILSpy `( ! C ? A : B )` -> `C ? B : A` (C a primary chain or one parenthesised group)."""
    changed = True
    while changed:
        changed = False
        for a in range(len(r) - 2):
            if r[a][0] == "(" and r[a + 1][0] == "!":
                d, k, q, c = 0, a, None, None
                while k < len(r):
                    t = r[k][0]
                    if t in "([": d += 1
                    elif t in ")]":
                        d -= 1
                        if d == 0: break
                    elif d == 1 and t == "?" and q is None: q = k
                    elif d == 1 and t == ":" and q is not None and c is None: c = k
                    k += 1
                if q is None or c is None or k >= len(r): continue
                cond = r[a + 2:q]
                dd = 0; ok = True
                for t, _ in cond:
                    if t in "([": dd += 1
                    elif t in ")]": dd -= 1
                    elif dd == 0 and t not in (".", ) and not re.match(r"[\w\"']", t) and t != "<" and t != ">": ok = False
                if not ok or not cond: continue
                A, B = r[q + 1:c], r[c + 1:k]
                if any(t == "?" for t, _ in A + B): continue
                r = r[:a] + cond + [("?", r[q][1])] + B + [(":", r[c][1])] + A + r[k + 1:]
                changed = True
                break
    return r


def simplify(r):
    """Drop `else`, `new Delegate ( X )` wrappers and redundant parentheses (conservative)."""
    r = [x for x in r if x[0] != "else"]
    r = flip_ternaries(strip_itween(r))
    o, i = [], 0
    while i < len(r):
        if r[i][0] == "new" and i + 3 < len(r) and r[i + 1][0] in DELEGATES and r[i + 2][0] == "(":
            d, j = 0, i + 2
            while True:
                if r[j][0] == "(": d += 1
                elif r[j][0] == ")":
                    d -= 1
                    if d == 0: break
                j += 1
            o.extend(r[i + 3:j]); i = j + 1; continue
        o.append(r[i]); i += 1
    # parentheses
    changed = True
    while changed:
        changed = False
        stack = []
        for k, (t, ln) in enumerate(o):
            if t == "(": stack.append(k)
            elif t == ")" and stack:
                a = stack.pop()
                inner = [x[0] for x in o[a + 1:k]]
                prev = o[a - 1][0] if a else ";"
                nxt = o[k + 1][0] if k + 1 < len(o) else ";"
                call = bool(re.match(r"[A-Za-z_>\]\)]", prev)) and prev not in ("return", "if", "while", "for", "foreach", "switch", "in", "is", "as", "new", "typeof", "&&", "||") and prev not in WEAK_PREV
                if call or prev in ("if", "while", "for", "foreach", "switch", "lock", "using", "typeof", "sizeof", "catch"): continue
                cast = len(inner) == 1 and re.match(r"[A-Za-z_]", inner[0]) and (re.match(r"[A-Za-z_\d\"'(!]", nxt) and nxt not in ("is", "as"))
                if cast or not inner: continue
                # assignment inside parens: a = ( b = v )
                depth0_eq = any(x == "=" for x in inner)
                single = all(x not in ("+", "-", "*", "/", "%", "&&", "||", "?", ":", "<", ">", "==", "!=", "<=", ">=", ",") for x in inner)
                arith_ok = prev in WEAK_PREV and nxt in WEAK_NEXT and all(x in ARITH or re.match(r"[\w\"']", x) for x in inner) and "," not in inner
                if (single and not depth0_eq and prev not in ("typeof",) and nxt not in ("(",)) or arith_ok or (depth0_eq and prev == "=" and nxt in (";", ")")):
                    o = o[:a] + o[a + 1:k] + o[k + 1:]
                    changed = True
                    break
        if not changed:
            o2 = flip_ternaries(o)
            if o2 != o: o, changed = o2, True
    return o


ALIASES = {"Int32": "int", "Double": "double", "Single": "float", "String": "string", "Boolean": "bool"}


def collapse_refs(r):
    """Rewrite `ref L = ref EXPR ;` followed by uses of the bare ref-local `L` into EXPR. Only the plain pattern."""
    out, i = [], 0
    while i < len(r):
        if r[i][0] == "ref" and i + 3 < len(r) and r[i + 1][0] in ("L",) and r[i + 2][0] == "=" and r[i + 3][0] == "ref":
            j = i + 4
            while j < len(r) and r[j][0] != ";": j += 1
            expr = r[i + 4:j]
            # next statement: `L = value ;` where L is the ref local -> `EXPR = value ;`
            if j + 2 < len(r) and r[j + 1][0] == "L" and r[j + 2][0] == "=":
                out.extend(expr); i = j + 2; continue
        out.append(r[i]); i += 1
    return out


FLOATCASTS = [0]
STRICT = [None]


def resolve_refs(toks):
    """ILSpy `ref T name = ref EXPR ;` -> drop the statement, substitute EXPR for `name` (lvalue use)."""
    out, i, m = [], 0, {}
    while i < len(toks):
        if toks[i][0] == "ref" and i + 1 < len(toks):
            e = type_end(toks, i + 1)
            if e > 0 and e + 2 < len(toks) and toks[e + 1][0] == "=" and toks[e + 2][0] == "ref":
                j = e + 3
                while toks[j][0] != ";": j += 1
                m[toks[e][0]] = toks[e + 3:j]
                i = j + 1
                continue
        if toks[i][0] in m and (i == 0 or toks[i - 1][0] != "."):
            out.extend(m[toks[i][0]]); i += 1; continue
        out.append(toks[i]); i += 1
    return out


def normalize(toks, own, numbered=False):
    """own: member names of the class (kept as-is). Returns list of (token, line)."""
    toks = [t for t in toks if not t[0].startswith("\x00")]
    toks = resolve_refs(toks)
    decls = find_locals(toks)
    declpos = {e for _, e in decls}
    typepos = set()
    for s, e in decls:
        typepos.update(range(s, e))
    names = {toks[e][0] for _, e in decls}
    out, seen = [], {}
    i, n = 0, len(toks)
    while i < n:
        t, ln = toks[i]
        prev = toks[i - 1][0] if i else ""
        if i in typepos:
            i += 1; continue
        if t in MODS or t == "const": i += 1; continue
        if t == "this" and i + 1 < n and toks[i + 1][0] == ".": i += 2; continue
        if t in ("Object", "UnityEngine", "System") and i + 1 < n and toks[i + 1][0] == "." and prev != ".":
            i += 2
            if toks[i - 1][0] == "." and t == "UnityEngine" and toks[i][0] == "Object" and toks[i + 1][0] == ".": i += 2
            continue
        if t == "string" and i + 2 < n and toks[i + 1][0] == "." and toks[i + 2][0] == "Empty":
            out.append(('""', ln)); i += 3; continue
        if t == "base" and i + 1 < n and toks[i + 1][0] == "." and toks[i + 2][0] in INHERITED:
            i += 2; continue
        if re.match(r"[A-Za-z_]", t) and prev != "." and t in names:
            if numbered: seen.setdefault(t, "L%d" % len(seen))
            out.append((seen[t] if numbered else "L", ln)); i += 1; continue
        if t in ALIASES and prev != ".": t = ALIASES[t]
        if t == "System" : pass
        if t.startswith('@"'): t = canon_str(t)
        if re.match(r"[\d.]", t): t = canon_num(t)
        out.append((t, ln)); i += 1
    # post passes on the token list
    out = post(out)
    # drop bare `L ;` (declaration without initialiser)
    res = []
    for k, (t, ln) in enumerate(out):
        if re.fullmatch(r"L\d*", t) and k + 1 < len(out) and out[k + 1][0] == ";" and (k == 0 or out[k - 1][0] in (";", "{", "}")):
            continue
        res.append((t, ln))
    # drop Debug.Log statements, count them
    final, i, logs = [], 0, 0
    while i < len(res):
        if res[i][0] == "Debug" and i + 2 < len(res) and res[i + 1][0] == "." and res[i + 2][0].startswith("Log"):
            d, j = 0, i
            while j < len(res):
                if res[j][0] == "(": d += 1
                elif res[j][0] == ")":
                    d -= 1
                    if d == 0: break
                j += 1
            logs += 1
            i = j + 2 if j + 1 < len(res) and res[j + 1][0] == ";" else j + 1
            continue
        final.append(res[i]); i += 1
    return final, logs


def statements_raw(toks):
    """Split at ';' (paren depth 0) and after the ')' of if/while/for/foreach/switch and after 'else'. Braces are dropped."""
    out, cur, ln0, depth = [], [], None, 0
    def flush():
        nonlocal cur, ln0
        if cur: out.append((" ".join(cur), ln0))
        cur, ln0 = [], None
    for t, ln in toks:
        if t in ("{", "}"): flush(); continue
        if ln0 is None: ln0 = ln
        cur.append(t)
        if t == "(": depth += 1
        elif t == ")":
            depth -= 1
            if depth == 0 and cur[0] in ("if", "while", "for", "foreach", "switch", "lock", "using") or (depth == 0 and cur[:2] == ["else", "if"]):
                flush()
        elif t == ";" and depth == 0: flush()
        elif t == "else" and len(cur) == 1: flush()
    flush()
    return out


def statements(toks):
    return [x for x in statements_raw(toks) if x[0] != ";"]


def member_key(head, blk, own_names):
    names = [t[0] for t in head if not t[0].startswith("\x00")]
    if "(" in names and not ("=" in names and names.index("=") < names.index("(")):
        k = names.index("(")
        name = names[k - 1]
        if name == "operator": name = "operator" + names[k - 1]
        # count params
        d, depth, cnt, empty = 0, 0, 1, True
        for t in names[k:]:
            if t in "([<": depth += 1
            elif t in ")]>":
                depth -= 1
                if depth == 0 and t == ")": break
            elif t == "," and depth == 1: cnt += 1
            elif depth >= 1: empty = False
        return ("method", name, 0 if empty else cnt)
    if blk is not None:
        return ("prop", names[-1] if names else "?", 0)
    if "=" in names:
        return ("field", names[names.index("=") - 1], 0)
    return ("field", names[-1] if names else "?", 0)


def load(path):
    with open(path, encoding="utf-8-sig") as f:
        return f.read()


def js_to_cs(text, name):
    """Minimal UnityScript -> C# for the tiny TimedObjectDestructor.js: `var x = 1.0;` and `function F ()`."""
    text = re.sub(r"\bvar\s+(\w+)\s*=\s*(true|false)\s*;", r"bool \1 = \2;", text)
    text = re.sub(r"\bvar\s+(\w+)\s*=\s*([\d.]+)\s*;", lambda m: "float %s = %sf;" % (m.group(1), m.group(2)), text)
    text = re.sub(r"\bfunction\s+(\w+)\s*\(\s*\)", r"void \1()", text)
    return "class %s {\n%s\n}" % (name, text)


def lift_ctor_inits(members, cname):
    """ILSpy (UnityScript) puts `x = 1f;` field defaults in the ctor. Move them to the field initialisers."""
    out, inits = [], {}
    for head, blk in members:
        names = [t[0] for t in head]
        if blk is not None and names and names[-1] == ")" and "(" in names and names[names.index("(") - 1] == cname:
            body = [t for t in blk[1:-1]]
            i = 0
            while i < len(body):
                j = i
                while body[j][0] != ";": j += 1
                if body[i + 1][0] == "=": inits[body[i][0]] = body[i + 2:j]
                i = j + 1
            continue
        out.append((head, blk))
    res = []
    for head, blk in out:
        names = [t[0] for t in head]
        if blk is None and names and names[-1] in inits and "=" not in names:
            head = head + [("=", head[-1][1])] + inits[names[-1]]
        res.append((head, blk))
    return res


def class_members(path):
    text = load(path)
    if path.endswith(".js"): text = js_to_cs(text, os.path.basename(path)[:-3])
    toks = strip_attrs(tokenize(preprocess(text)))
    types = parse_types(toks)
    res = {}
    for q, body, hdr in types:
        # remove nested type bodies from body so they do not pollute members
        mem = top_members(body)
        res[q] = (hdr, lift_ctor_inits(mem, q.split("+")[-1]) if "UnityScript" in path or path.endswith(".js") else mem)
    return res


def build(members):
    """key -> (head, blk, line)."""
    d = {}
    for head, blk in members:
        if not head: continue
        if head[0][0] in ("class", "struct", "enum", "interface") or any(h[0] in ("class", "struct", "enum", "interface") for h in head[:6]) and blk is not None and not any(h[0] == "(" for h in head):
            continue
        k = member_key(head, blk, None)
        d.setdefault(k, []).append((head, blk, head[0][1]))
    return d


def render(items, own, field=False):
    if field:
        names = [t[0] for t in items]
        items = items[names.index("=") + 1:] if "=" in names else []
    FLOATCASTS[0] = 0
    toks, logs = normalize(items, own)
    casts = FLOATCASTS[0]
    if [t[0] for t in toks] in (["false"], ["0"], ["null"], ["default"]): toks = []
    FLOATCASTS[0] = 0
    toks2, _ = normalize(items, own, True)
    if [t[0] for t in toks2] in (["false"], ["0"], ["null"], ["default"]): toks2 = []
    STRICT[0] = statements(toks2)
    return statements(toks), logs + 1000 * casts


def compare_class(q, src, dec, report):
    sh, smem = src
    dh, dmem = dec
    sb, db = build(smem), build(dmem)
    own = set(k[1] for k in sb) | set(k[1] for k in db)
    status = {}
    # field/prop with same name merge: ILSpy turns auto-props to props
    for k in sorted(set(sb) | set(db)):
        s_items, d_items = sb.get(k), db.get(k)
        label = "%s %s/%d" % k
        if k[0] == "field" and not s_items and ("prop", k[1], 0) in sb: continue
        if not s_items:
            # ILSpy-only: synthesised default ctor / Main / compiler fields
            status[label] = ("DEC_ONLY", [], [], 0, 0, d_items[0][2])
            continue
        if not d_items:
            status[label] = ("SRC_ONLY", [], [], 0, 0, s_items[0][2])
            continue
        sh_, sblk, sl = s_items[0]; dh_, dblk, dl = d_items[0]
        sflat = sh_ + (sblk or [])
        dflat = dh_ + (dblk or [])
        iterator = any(t[0] == "yield" for t in sflat)
        isf = k[0] == "field"
        sst, slog = render(sflat, own, isf); sstr = STRICT[0]
        dst, dlog = render(dflat, own, isf); dstr = STRICT[0]
        strict = [x for x, _ in sstr] == [x for x, _ in dstr]
        sa = sorted(t[0][1:] for t in sflat if t[0].startswith("\x00")); da = sorted(t[0][1:] for t in dflat if t[0].startswith("\x00"))
        if sa != da: sst = sst + [("ATTRS src=%s" % sa, sl)]; dst = dst + [("ATTRS dec=%s" % da, dl)]
        status[label] = ("ITER" if iterator else "", sst, dst, slog, dlog, (sl, dl, strict))
    return status


def diff_lines(sst, dst):
    a = [s for s, _ in sst]; b = [s for s, _ in dst]
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    out = []
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal": continue
        for s, l in sst[i1:i2]: out.append("  src:%d  - %s" % (l, s))
        for s, l in dst[j1:j2]: out.append("  dec:%d  + %s" % (l, s))
    return out


def main(argv):
    if len(argv) < 2 or len(argv) % 2:
        print(__doc__); return 2
    for si in range(0, len(argv), 2):
        sdir, ddir = argv[si], argv[si + 1]
        sfiles = {}
        for root, _, fs in os.walk(sdir):
            for f in fs:
                if f.endswith((".cs", ".js")):
                    for q, v in class_members(os.path.join(root, f)).items():
                        sfiles[q] = (os.path.join(root, f), v)
        dfiles = {}
        for root, _, fs in os.walk(ddir):
            for f in fs:
                if f.endswith(".cs"):
                    for q, v in class_members(os.path.join(root, f)).items():
                        dfiles[q] = (os.path.join(root, f), v)
        for q in sorted(sfiles):
            if q not in dfiles:
                print("CLASS %s: only in source (%s)" % (q, sfiles[q][0])); continue
            st = compare_class(q, sfiles[q][1], dfiles[q][1], None)
            nd = sum(1 for k, v in st.items() if v[0] == "" and (diff_lines(v[1], v[2]) or v[3] != v[4]))
            print("=== CLASS %s  src=%s dec=%s  members=%d differing=%d" % (q, sfiles[q][0], dfiles[q][0], len(st), nd))
            for label, (kind, sst, dst, slog, dlog, lines) in st.items():
                if kind in ("DEC_ONLY", "SRC_ONLY"):
                    print("  [%s] %s (line %s)" % (kind, label, lines)); continue
                d = diff_lines(sst, dst)
                if kind == "ITER":
                    print("  [ITERATOR, manual] %s src=%s dec=%s" % (label, lines[0], lines[1])); continue
                if not d and slog == dlog:
                    print("  [SAME%s] %s" % ("" if lines[2] else "-LOOSE", label))
                elif not d and slog % 1000 == dlog % 1000:
                    print("  [SAME%s+CAST %d/%d] %s" % ("" if lines[2] else "-LOOSE", slog // 1000, dlog // 1000, label))
                else:
                    print("  [DIFF] %s src:%s dec:%s  debuglog %d/%d floatcasts %d/%d" % (label, lines[0], lines[1], slog % 1000, dlog % 1000, slog // 1000, dlog // 1000))
                    for l in d: print(l)
        extra = sorted(set(dfiles) - set(sfiles))
        print("decompiled classes with no counterpart in this source dir: %d" % len(extra))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
