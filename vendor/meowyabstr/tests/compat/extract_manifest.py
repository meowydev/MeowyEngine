#!/usr/bin/env python3
"""Extract the raylib 6.0 public API into a machine-readable manifest.

Reads the pinned reference headers (third_party/raylib_reference/*.h) and
produces tests/compat/raylib6_manifest.json: every RLAPI function grouped by the
'// Module:' banner comments in raylib.h, plus enums and callback typedefs.

Provenance is recorded in the manifest. PDF wrapping is avoided by parsing the
header directly (the cheatsheet is a human-readable subset of these symbols).
"""
import json, re, hashlib, os, sys

REF = os.path.join(os.path.dirname(__file__), "..", "..", "third_party", "raylib_reference")
OUT = os.path.join(os.path.dirname(__file__), "raylib6_manifest.json")

def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

# ---------------------------------------------------------------------------
# Function extraction. RLAPI declarations may span lines; join until ';'.
# ---------------------------------------------------------------------------
RLAPI_RE = re.compile(r"\bRLAPI\b")
# raylib.h groups functions with banner comments "... (Module: core)" etc.
# Map raylib's internal module names to the cheatsheet module names.
MODULE_MAP = {"core": "rcore", "shapes": "rshapes", "textures": "rtextures",
              "text": "rtext", "models": "rmodels", "audio": "raudio"}
MODULE_RE = re.compile(r"\(Module:\s*([a-z]+)\)")
# The "Basic shapes drawing functions" banner starts rshapes before its Module tag.
SHAPES_RE = re.compile(r"^//\s*Basic shapes drawing functions")
SECTION_RE = re.compile(r"^//\s*([A-Z][A-Za-z0-9 ,/()\-\+&']+? functions.*)$")

def parse_functions(path, default_module):
    funcs = []
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    module = default_module
    section = ""
    i = 0
    while i < len(lines):
        line = lines[i]
        m = MODULE_RE.search(line)
        if m:
            module = MODULE_MAP.get(m.group(1).lower(), m.group(1).lower())
        elif SHAPES_RE.match(line.strip()):
            module = "rshapes"
        s = SECTION_RE.match(line.strip())
        if s:
            section = s.group(1).strip()
        if RLAPI_RE.search(line):
            decl = line
            while ";" not in decl and i + 1 < len(lines):
                i += 1
                decl += lines[i]
            decl = decl.split("//")[0]  # strip trailing comment
            decl = re.sub(r"\s+", " ", decl).strip()
            # decl looks like: RLAPI <ret> Name(<params>);
            mm = re.match(r"RLAPI\s+(.+?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*;", decl)
            if mm:
                ret = mm.group(1).strip()
                name = mm.group(2)
                params = mm.group(3).strip()
                funcs.append({
                    "name": name,
                    "return": ret,
                    "params": params,
                    "module": module,
                    "section": section,
                    "signature": f"{ret} {name}({params})",
                })
        i += 1
    return funcs

def parse_enums(path):
    enums = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    for m in re.finditer(r"typedef enum\s*\{([^}]*)\}\s*([A-Za-z_][A-Za-z0-9_]*)\s*;", text, re.S):
        body, name = m.group(1), m.group(2)
        members = []
        for line in body.split(","):
            line = line.split("//")[0].strip()
            if not line:
                continue
            member = re.match(r"([A-Za-z_][A-Za-z0-9_]*)", line)
            if member:
                members.append(member.group(1))
        enums[name] = members
    return enums

def parse_callbacks(path):
    cbs = []
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    for m in re.finditer(r"typedef\s+[A-Za-z_][\w ]*\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(([^)]*)\)\s*;", text):
        cbs.append({"name": m.group(1), "params": m.group(2).strip()})
    return cbs

def main():
    raylib = os.path.join(REF, "raylib.h")
    raymath = os.path.join(REF, "raymath.h")
    rlgl = os.path.join(REF, "rlgl.h")

    functions = parse_functions(raylib, "rcore")
    # raymath: header-only inline math (RMAPI). Extract for completeness.
    math_funcs = []
    with open(raymath, encoding="utf-8", errors="replace") as f:
        for line in f:
            if re.search(r"\bRMAPI\b", line):
                mm = re.match(r"\s*RMAPI\s+(.+?)\b([A-Za-z_]\w*)\s*\((.*?)\)", line)
                if mm:
                    math_funcs.append({
                        "name": mm.group(2), "return": mm.group(1).strip(),
                        "params": mm.group(3).strip(), "module": "raymath",
                        "section": "raymath", "signature": f"{mm.group(1).strip()} {mm.group(2)}({mm.group(3).strip()})",
                    })

    enums = parse_enums(raylib)
    callbacks = parse_callbacks(raylib)

    manifest = {
        "provenance": {
            "source": "raylib public headers",
            "repo": "https://github.com/raysan5/raylib",
            "tag": "6.0",
            "commit": "dbc56a87da87d973a9c5baa4e7438a9d20121d28",
            "version": "6.0",
            "headers": {
                "raylib.h": sha256(raylib),
                "raymath.h": sha256(raymath),
                "rlgl.h": sha256(rlgl),
            },
            "note": "Extracted directly from pinned headers (authoritative for signatures); the v6.0 cheatsheet PDF is a human-readable subset of these symbols.",
        },
        "counts": {
            "functions": len(functions),
            "raymath_functions": len(math_funcs),
            "enums": len(enums),
            "callbacks": len(callbacks),
        },
        "functions": functions,
        "raymath_functions": math_funcs,
        "enums": enums,
        "callbacks": callbacks,
    }
    with open(OUT, "w") as f:
        json.dump(manifest, f, indent=1)
    # Module breakdown for a quick sanity print.
    by_mod = {}
    for fn in functions:
        by_mod[fn["module"]] = by_mod.get(fn["module"], 0) + 1
    print(f"functions={len(functions)} raymath={len(math_funcs)} enums={len(enums)} callbacks={len(callbacks)}")
    print("by module:", dict(sorted(by_mod.items())))

if __name__ == "__main__":
    main()
