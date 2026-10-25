#!/usr/bin/env python3
"""owf.library's SFD against the C it stands for, on the PC (make sfd-check).

    check_sfd.py [REPO]

For every call in libowf/library/owf_lib.sfd:
  - the public headers (owf.h, owf_calc.h, owf_pub.h, owf_zip.h) declare it
    with the same return type, the same number of arguments and the same
    types;
  - owf_library.c has its L_ function: the same return type, the SFD's
    registers in the SFD's order (a double as two data registers, high word
    first), the same types, and a body that calls the function with its
    arguments in order;
  - lib_vectors lists the L_ functions in the SFD's order, after the four
    standard vectors, so each call's offset is the SFD's;
  - no call takes a6 or a7, no register twice, and a double only in two
    data registers;
  - the committed LVO file (sdk/Include_I/lvo/owf_lib.i) has the SFD's
    offsets, so a stale SDK shows here without an openamigasdk checkout
    (build-library.sh checks the whole SDK with ndkgen when it has one).
Prints what differs; exit status 1 when anything does.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import re
import sys

REPO = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
LIB = os.path.join(REPO, "libowf", "library")
HEADERS = [os.path.join(REPO, "libowf", "include", h) for h in ("owf.h", "owf_calc.h", "owf_pub.h", "owf_zip.h")]
REGS = ["d%d" % i for i in range(8)] + ["a%d" % i for i in range(8)]

problems = []


def problem(text):
    problems.append(text)


def norm_type(t):
    """A type as text, spaced one way: 'const char *', 'owf_book **'."""
    t = re.sub(r"\bCONST\b", "const", t)
    t = re.sub(r"\s*\*\s*", " *", t.strip())
    t = re.sub(r"\*\s+\*", "**", t)
    t = re.sub(r"\s+", " ", t)
    t = t.replace(" *", " *").replace("* *", "**")
    return t.strip()


def split_args(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return [a.strip() for a in out if a.strip()]


def arg_type(decl):
    """'const char *name' -> ('const char *', 'name'); a function pointer
    keeps its whole text as the type."""
    decl = decl.strip()
    m = re.match(r"(.*\(\s*\*\s*)(\w+)(\s*\).*)$", decl)
    if m:
        return norm_type(m.group(1) + m.group(3)), m.group(2)
    m = re.match(r"(.*?)(\w+)\s*$", decl)
    if not m or not m.group(1).strip():
        return norm_type(decl), ""               # no name: "...", or a type alone
    return norm_type(m.group(1)), m.group(2)


# ---- the SFD -----------------------------------------------------------------

def read_sfd(path):
    calls, bias, version = [], 30, 0
    for n, line in enumerate(open(path, encoding="latin-1").read().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("*"):
            continue
        if line.startswith("=="):
            word, _, rest = line[2:].partition(" ")
            if word == "bias":
                bias = int(rest)
            elif word == "version":
                version = int(rest)
            elif word == "end":
                break
            continue
        m = re.match(r"(.+?)\b(\w+)\s*\((.*)\)\s*\(([^()]*)\)\s*$", line)
        if not m:
            problem("owf_lib.sfd:%d: not a call: %s" % (n, line))
            continue
        args = [] if m.group(3).strip() in ("", "void", "VOID") else split_args(m.group(3))
        regs = [r.strip().lower() for r in re.split(r"[,/-]", m.group(4)) if r.strip()]
        calls.append({"ret": norm_type(m.group(1)), "name": m.group(2), "args": [arg_type(a) for a in args],
                      "regs": regs, "bias": bias, "version": version, "line": n})
        bias += 6
    return calls


# ---- the headers ---------------------------------------------------------------

def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"^\s*#.*$", " ", text, flags=re.M)


def header_prototypes(wanted):
    protos = {}
    for h in HEADERS:
        text = re.sub(r"\s+", " ", strip_comments(open(h, encoding="utf-8").read()))
        for m in re.finditer(r"([A-Za-z_][\w \*]*?)\b(\w+)\s*\(([^;{}]*?)\)\s*;", text):
            ret, name, args = m.group(1), m.group(2), m.group(3)
            if name not in wanted or ret.strip() in ("return", "typedef"):
                continue
            ret = re.sub(r"^.*\b(extern)\b", "", ret)
            protos.setdefault(name, (norm_type(ret.split(";")[-1].split("}")[-1]),
                                     [] if args.strip() in ("", "void") else [arg_type(a) for a in split_args(args)],
                                     os.path.basename(h)))
    return protos


# ---- the glue ------------------------------------------------------------------

def glue_functions(text):
    funcs = {}
    for m in re.finditer(r"static\s+([\w \*]+?)\s*\bL_(\w+)\s*\((.*?)\)\s*\{(.*?)\n\}", text, flags=re.S):
        ret, name, params, body = m.group(1), m.group(2), m.group(3), m.group(4)
        regs = []
        for p in re.finditer(r"REG\(\s*(\w+)\s*,\s*([^()]*(?:\([^()]*\)[^()]*)*)\)", params):
            reg, decl = p.group(1), p.group(2)
            if reg == "a6":
                continue
            regs.append((reg,) + arg_type(decl))
        funcs[name] = {"ret": norm_type(ret), "regs": regs, "body": body}
    return funcs


def vectors(text):
    m = re.search(r"lib_vectors\[\]\s*=\s*\{(.*?)\};", text, flags=re.S)
    return re.findall(r"\(APTR\)\s*(-?\w+)", m.group(1)) if m else []


def lvo_file():
    path = os.path.join(LIB, "sdk", "Include_I", "lvo", "owf_lib.i")
    out = {}
    for m in re.finditer(r"_LVO(\w+)\s+EQU\s+-(\d+)", open(path, encoding="latin-1").read()):
        out[m.group(1)] = int(m.group(2))
    return out


def main():
    calls = read_sfd(os.path.join(LIB, "owf_lib.sfd"))
    protos = header_prototypes(set(c["name"] for c in calls))
    glue_text = open(os.path.join(LIB, "owf_library.c"), encoding="utf-8").read()
    glue = glue_functions(glue_text)
    vec = vectors(glue_text)
    lvo = lvo_file()
    names = [c["name"] for c in calls]
    if len(set(names)) != len(names):
        problem("owf_lib.sfd names a call twice")

    for c in calls:
        name = c["name"]
        where = "owf_lib.sfd:%d %s" % (c["line"], name)
        # registers
        if any(r not in REGS for r in c["regs"]):
            problem("%s: a register that isn't one: %s" % (where, c["regs"]))
        if any(r in ("a6", "a7") for r in c["regs"]):
            problem("%s: a6 and a7 are not for arguments" % where)
        if len(set(c["regs"])) != len(c["regs"]):
            problem("%s: a register twice" % where)
        want = sum(2 if t == "double" else 1 for t, _ in c["args"])
        if want != len(c["regs"]):
            problem("%s: %d registers for %d argument words" % (where, len(c["regs"]), want))
            continue
        regs_of, i = [], 0
        for t, _ in c["args"]:
            n = 2 if t == "double" else 1
            regs_of.append(c["regs"][i:i + n])
            if n == 2 and not all(r.startswith("d") for r in c["regs"][i:i + 2]):
                problem("%s: a double in %s, not two data registers" % (where, c["regs"][i:i + 2]))
            i += n
        # the C prototype
        p = protos.get(name)
        if not p:
            problem("%s: no prototype in the public headers" % where)
        else:
            if p[0] != c["ret"]:
                problem("%s: returns %r in the SFD, %r in %s" % (where, c["ret"], p[0], p[2]))
            if len(p[1]) != len(c["args"]):
                problem("%s: %d arguments in the SFD, %d in %s" % (where, len(c["args"]), len(p[1]), p[2]))
            else:
                for (st, sn), (ht, hn) in zip(c["args"], p[1]):
                    if st != ht:
                        problem("%s: argument %s is %r in the SFD, %r in %s" % (where, sn, st, ht, p[2]))
        # the glue
        g = glue.get(name)
        if not g:
            problem("%s: no L_%s in owf_library.c" % (where, name))
            continue
        if g["ret"] != c["ret"]:
            problem("%s: L_%s returns %r, the SFD %r" % (where, name, g["ret"], c["ret"]))
        flat = [r for r, _, _ in g["regs"]]
        if flat != c["regs"]:
            problem("%s: L_%s takes %s, the SFD says %s" % (where, name, flat, c["regs"]))
        else:
            k = 0
            for (t, an), rs in zip(c["args"], regs_of):
                if t == "double":
                    for r, half in zip(rs, ("hi", "lo")):
                        if g["regs"][k][1] not in ("ULONG", "unsigned long") or g["regs"][k][2] != "%s_%s" % (an, half):
                            problem("%s: L_%s's %s should be ULONG %s_%s" % (where, name, r, an, half))
                        k += 1
                else:
                    if g["regs"][k][1] != t:
                        problem("%s: L_%s's %s is %r, the SFD's %r" % (where, name, rs[0], g["regs"][k][1], t))
                    k += 1
        callargs = ", ".join(("%s.d" % an) if t == "double" else an for t, an in c["args"])
        if not re.search(r"\b%s\(%s\);" % (re.escape(name), re.escape(callargs)), g["body"]):
            problem("%s: L_%s doesn't call %s(%s)" % (where, name, name, callargs))
        if lvo.get(name) != c["bias"]:
            problem("%s: the SFD puts it at -%d, sdk/Include_I/lvo/owf_lib.i at %s (run scripts/gen-sdk.sh)"
                    % (where, c["bias"], "-%d" % lvo[name] if name in lvo else "nothing"))

    # the vector table: the four standard ones, then the SFD's order
    if vec[:4] != ["lib_open", "lib_close", "lib_expunge", "lib_null"] or not vec or vec[-1] != "-1":
        problem("lib_vectors: not open, close, expunge, null ... -1")
    listed = [v[2:] for v in vec[4:-1]]
    if listed != names:
        for i, (a, b) in enumerate(zip(listed, names)):
            if a != b:
                problem("lib_vectors: slot %d (offset -%d) is L_%s, the SFD's is %s" % (i, 30 + 6 * i, a, b))
                break
        if len(listed) != len(names):
            problem("lib_vectors: %d calls, the SFD has %d" % (len(listed), len(names)))
    extra = set(lvo) - set(names)
    if extra:
        problem("sdk/Include_I/lvo/owf_lib.i has calls the SFD hasn't: %s" % ", ".join(sorted(extra)))

    v2 = [c for c in calls if c["version"] >= 2]
    if problems:
        for p in problems:
            print("check_sfd.py: " + p)
        print("check_sfd.py: %d problems" % len(problems))
        return 1
    print("check_sfd.py: owf_lib.sfd's %d calls (%d of version 2, -%d to -%d) agree with the headers, "
          "owf_library.c's registers and vectors, and the SDK's offsets"
          % (len(calls), len(v2), v2[0]["bias"] if v2 else 0, v2[-1]["bias"] if v2 else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
