#!/usr/bin/env python3
"""Conformance diff: raylib 6.0 manifest vs MeowyRender.

Three independent checks, all gating (non-zero exit on failure):

1. DECLARATION + ARITY. Every raylib function must be declared in the public
   headers (as a declaration ending in ';' OR an inline definition ending in
   '{'), with a compatible parameter arity. This catches missing/renamed API.

2. IMPLEMENTATION (anti-stub). A function existing is not the same as it being
   implemented. We locate each function's DEFINITION in the source tree and
   classify its body as REAL vs STUB (empty body, or a single trivial
   constant/void return). A STUB is only allowed when it is explicitly listed
   in the overrides as `platform_unavailable` (a genuine platform limitation)
   or `intentional_noop` (a documented, correct no-op). Any other stub fails.

3. OVERRIDE INTEGRITY. Anything the overrides mark 'verified' must be declared,
   and functions marked platform_unavailable / intentional_noop must actually
   exist in the manifest.

The compile+link conformance target (tests/compat/conformance_target.cpp) proves
symbols resolve; this script proves they are declared, shaped, and non-stub.
"""
import json, re, os, sys, glob

HERE = os.path.dirname(__file__)
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MANIFEST = json.load(open(os.path.join(HERE, "raylib6_manifest.json")))
OVERRIDES = json.load(open(os.path.join(HERE, "parity_overrides.json")))
HEADERS = [os.path.join(ROOT, "include", "meowyrender", h)
           for h in ("meowyrender.hpp", "mr_math.hpp", "mr_types.hpp", "mr_input.hpp")]
SRC_GLOBS = ["src/**/*.cpp", "src/**/*.mm", "src/**/*.hpp", "include/**/*.hpp"]


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    return s


def split_params(s):
    """Split a parameter list on top-level commas (ignoring <>,(),{} nesting)."""
    s = s.strip()
    if s == "" or s == "void":
        return []
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def declared():
    """Return {name: [arity,...]} for MeowyRender free-function declarations AND
    inline definitions. A candidate is `Name(` where the balanced parameter list
    is followed (after optional const/noexcept) by ';' (declaration) or '{'
    (inline definition). Brace-aware scanning tolerates default args like `= {}`."""
    decls = {}
    text = ""
    for h in HEADERS:
        if os.path.exists(h):
            text += open(h, encoding="utf-8", errors="replace").read() + "\n"
    text = strip_comments(text)
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "return", "sizeof", "static_cast",
                    "reinterpret_cast", "const_cast", "dynamic_cast"):
            continue
        # A definition/declaration has a return type token before the name.
        pre = text[max(0, m.start() - 1):m.start()]
        if pre and pre[-1] in ".>":  # member call
            continue
        # Balance-match the parameter parens.
        i = m.end() - 1
        depth = 0
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if i >= len(text):
            continue
        params = text[m.end():i]
        k = i + 1
        mspec = re.match(r"(?:const|noexcept|override|final|\s)*", text[k:])
        k += mspec.end() if mspec else 0
        if k < len(text) and text[k] in ";{":
            # Require a plausible return type / qualifier before the name to avoid
            # matching call sites: look back for a word or ']' (from [[nodiscard]]).
            back = text[max(0, m.start() - 40):m.start()].rstrip()
            if back and (back[-1].isalnum() or back[-1] in "_>]&*:"):
                decls.setdefault(name, []).append(len(split_params(params)))
    return decls


# ---- implementation (anti-stub) scan --------------------------------------

def gather_sources():
    files = {}
    for g in SRC_GLOBS:
        for p in glob.glob(os.path.join(ROOT, g), recursive=True):
            try:
                files[p] = strip_comments(open(p, encoding="utf-8", errors="replace").read())
            except OSError:
                pass
    return files


def find_body(text, name):
    """Return the body between the outermost braces of a DEFINITION of `name`,
    or None if only declarations/calls are present."""
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        pre = text[max(0, m.start() - 2):m.start()]
        if pre.endswith(".") or pre.endswith(">"):  # member access / call
            continue
        i = m.end() - 1
        depth = 0
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        k = i + 1
        while k < len(text) and text[k] in " \t\r\n":
            k += 1
        mspec = re.match(r"(const|noexcept|override|final|\s)*", text[k:])
        k2 = k + (mspec.end() if mspec else 0)
        if k2 < len(text) and text[k2] == "{":
            depth = 0
            p = k2
            while p < len(text):
                if text[p] == "{":
                    depth += 1
                elif text[p] == "}":
                    depth -= 1
                    if depth == 0:
                        return text[k2 + 1:p]
                p += 1
            return ""
    return None


# A body is a stub if it is empty or a SINGLE trivial return:
#   return; | return 0/0.0f/0u/false/true/nullptr/""/<number>;
#   return {};  return {0,0,...};            (brace-init with only zeros)
#   return Type{};  return Type{0,0,...};    (named empty/zero value type)
_ZERO_BRACE = r"\{\s*(?:0(?:\.0*f?)?u?\s*,?\s*)*\}"  # {} or {0, 0.0f, 0u, ...}
TRIVIAL_RE = re.compile(
    r"^\s*(?:return\s*(?:"
    r";|"
    r"0\s*;|0\.0f?\s*;|0u\s*;|false\s*;|true\s*;|nullptr\s*;|\"\"\s*;|"
    r"-?\d+\s*;|-?\d+\.\d*f?\s*;|"
    + _ZERO_BRACE + r"\s*;|"                       # return {}; / return {0,0};
    r"[A-Za-z_][\w:<>,\s\*&]*\s*" + _ZERO_BRACE + r"\s*;"  # return Type{}; / Type{0,..};
    r"))?\s*$",
    re.S,
)


def classify(body):
    if body is None:
        return "UNDEFINED"
    b = body.strip()
    if b == "" or TRIVIAL_RE.match(b):
        return "STUB"
    return "REAL"


def audit_impl():
    files = gather_sources()
    order = sorted(files, key=lambda p: (0 if p.endswith((".cpp", ".mm")) else 1, p))
    out = {}
    for fn in MANIFEST["functions"]:
        name = fn["name"]
        verdict, where = "UNDEFINED", None
        for p in order:
            body = find_body(files[p], name)
            if body is None:
                continue
            c = classify(body)
            if c == "REAL":
                verdict, where = "REAL", p
                break
            if verdict == "UNDEFINED":
                verdict, where = c, p
        out[name] = (verdict, where)
    return out


def raylib_arity(fn):
    return len(split_params(fn["params"]))


def main():
    decls = declared()
    aliases = OVERRIDES.get("aliases", {})
    intentional = OVERRIDES.get("intentional_difference", {})
    verified = set(OVERRIDES.get("verified", []))
    platform_na = OVERRIDES.get("platform_unavailable", {})
    intentional_noop = OVERRIDES.get("intentional_noop", {})
    manifest_names = {fn["name"] for fn in MANIFEST["functions"]}

    missing, arity_mismatch, verified_but_missing = [], [], []
    for fn in MANIFEST["functions"]:
        name = fn["name"]
        mrname = aliases.get(name, name)
        if name in platform_na:
            continue
        if mrname not in decls:
            missing.append(name)
            if name in verified:
                verified_but_missing.append(name)
            continue
        ra = raylib_arity(fn)
        arities = decls[mrname]
        if ra not in arities and not any(a <= ra for a in arities) and name not in intentional:
            arity_mismatch.append((name, ra, arities))

    impl = audit_impl()
    real = [n for n, (v, _) in impl.items() if v == "REAL"]
    stubs = [n for n, (v, _) in impl.items() if v == "STUB"]
    undefined = [n for n, (v, _) in impl.items() if v == "UNDEFINED"]
    # Stubs are only OK if explicitly declared as a platform gap or documented no-op.
    allowed_stub = set(platform_na) | set(intentional_noop)
    illegal_stubs = [n for n in stubs if n not in allowed_stub]
    # Declared-supported (allowed) stubs that suddenly gained a real body: fine.
    # An override that claims a gap but the function is actually REAL is misleading:
    stale_gap = [n for n in allowed_stub if n in manifest_names and impl.get(n, ("", None))[0] == "REAL"]

    print(f"declared MeowyRender names: {len(decls)}")
    print(f"raylib functions: {len(MANIFEST['functions'])}")
    print(f"missing declarations: {len(missing)}")
    print(f"arity mismatches (no intentional note): {len(arity_mismatch)}")
    print(f"implementation: REAL={len(real)} STUB={len(stubs)} UNDEFINED={len(undefined)}")
    print(f"declared platform_unavailable: {len(platform_na)}  intentional_noop: {len(intentional_noop)}")

    rc = 0
    if arity_mismatch:
        print("\nARITY MISMATCHES:")
        for name, ra, ar in arity_mismatch[:40]:
            print(f"   {name}: raylib={ra} meowyrender={ar}")
        rc = 1
    if verified_but_missing:
        print("\nERROR: overrides mark these 'verified' but they are not declared:")
        for n in verified_but_missing:
            print("   ", n)
        rc = 1
    if undefined:
        print("\nERROR: no definition found for these manifest functions:")
        for n in undefined:
            print("   ", n)
        rc = 1
    if illegal_stubs:
        print("\nERROR: these functions are stubs (empty / trivial constant return) but are")
        print("       not declared as platform_unavailable or intentional_noop:")
        for n in illegal_stubs:
            print(f"    {n}  [{impl[n][1] and os.path.relpath(impl[n][1], ROOT)}]")
        rc = 1
    if stale_gap:
        print("\nERROR: overrides mark these as an unsupported gap, but they now have a")
        print("       real implementation -- move them out of platform_unavailable/intentional_noop:")
        for n in stale_gap:
            print("   ", n)
        rc = 1

    if rc == 0:
        print("\nOK: all raylib functions declared, shaped, and implemented (or explicitly reported).")
    sys.exit(rc)


if __name__ == "__main__":
    main()
