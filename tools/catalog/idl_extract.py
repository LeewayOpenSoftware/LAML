#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Leeway Open Software
"""Extract the public XAML type surface from the Microsoft.UI.Xaml IDL files into the LAML text
catalog format (data/winui-api.types).

Input: the four MIT-licensed IDL files of microsoft/microsoft-ui-xaml at a pinned commit
(dxaml/xcp/dxaml/idl/winrt/{controls,core}/*.idl). The script reads FACTS from them - type names,
base types, content properties, property names and value types, enum members and numbers - and
copies no code and no text beyond those names (see PROVENANCE.md). Nothing here is hand-edited:
regenerate the data file with this script and diff.

  python -I tools/catalog/idl_extract.py <commit> <out.types> <idl> [<idl> ...]

Scope filter (reported, not silent): classes in the namespaces listed in SKIP_NAMESPACES and
classes named *EventArgs are left out; they are not used by markup. Enums and structs are always
kept. Types that the IDL files only refer to (Windows.Foundation.Point, ...) are NOT declared:
they stay unresolved in the catalog (reported by Resolve as warnings), not guessed.
"""
import re
import sys

SKIP_NAMESPACES = (
    "Microsoft.UI.Xaml.Automation.Peers",
    "Microsoft.UI.Xaml.Automation.Provider",
    "Microsoft.UI.Xaml.Hosting",
    "Microsoft.UI.Xaml.Printing",
    "Microsoft.UI.Xaml.Interop",
    "Windows.UI.Xaml.Interop",
)

PRIMITIVES = {
    "Object": "LamlCore.Object", "String": "LamlCore.String", "Boolean": "LamlCore.Boolean",
    "Double": "LamlCore.Double", "Int32": "LamlCore.Int32", "Int64": "LamlCore.Int64",
    "UInt32": "LamlCore.UInt32", "UInt64": "LamlCore.UInt64", "Single": "LamlCore.Single",
    "Byte": "LamlCore.Byte", "Char16": "LamlCore.Char16", "Int16": "LamlCore.Int16",
    "UInt16": "LamlCore.UInt16",
}

ATTR = r"\[(?:[^\[\]]|\[[^\]]*\])*\]"


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"^\s*#[^\n]*", "", text, flags=re.M)
    return text


def match_brace(text, open_index):
    depth = 0
    for i in range(open_index, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced braces")


def statements(body):
    """Split a body into statements at depth 0: ends at ';' or after a balanced {...} (plus an
    optional ';'). A property written 'Type Name{ get; };' is one statement."""
    out = []
    i, start, n = 0, 0, len(body)
    while i < n:
        c = body[i]
        if c == "{":
            j = match_brace(body, i)
            i = j + 1
            rest = body[i:i + 8]
            m = re.match(r"\s*;", rest)
            if m:
                i += m.end()
            out.append(body[start:i].strip().rstrip(";").strip())
            start = i
        elif c == ";":
            out.append(body[start:i].strip())
            i += 1
            start = i
        else:
            i += 1
    tail = body[start:].strip()
    if tail:
        out.append(tail)
    return [s for s in out if s]


def norm_space(s):
    return re.sub(r"\s+", " ", s).strip()


class Catalog:
    def __init__(self):
        self.types = {}          # full name -> dict
        self.interfaces = set()  # names that are interfaces (not emitted)
        self.skipped = {}        # reason -> count


def parse_file(path, cat):
    text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
    pos = 0
    for m in re.finditer(r"\bnamespace\s+([\w.]+)\s*\{", text):
        if m.start() < pos:
            continue
        ns = m.group(1)
        end = match_brace(text, m.end() - 1)
        parse_namespace(ns, text[m.end():end], cat)
        pos = end


def parse_namespace(ns, body, cat):
    for stmt in statements(body):
        attrs = re.findall(ATTR, stmt)
        head = norm_space(re.sub(ATTR, " ", stmt))
        brace = head.find("{")
        header = head if brace < 0 else head[:brace].strip()
        inner = "" if brace < 0 else head[brace:]
        # the attribute texts are needed for [contentproperty] and [flags]: they sit before the header
        if re.match(r"(?:unsealed\s+)?runtimeclass\s+\w+\s*;?$", header) and brace < 0:
            continue   # forward declaration
        m = re.match(r"(unsealed\s+)?runtimeclass\s+(\w+)\s*(?::\s*(.*))?$", header)
        if m:
            parse_class(ns, m, attrs, stst_body(stmt), cat)
            continue
        m = re.match(r"enum\s+(\w+)$", header)
        if m:
            parse_enum(ns, m.group(1), attrs, stst_body(stmt), cat)
            continue
        m = re.match(r"struct\s+(\w+)$", header)
        if m:
            parse_struct(ns, m.group(1), attrs, stst_body(stmt), cat)
            continue
        m = re.match(r"interface\s+(\w+)", header)
        if m:
            cat.interfaces.add(ns + "." + m.group(1))
            continue
        # delegates, api contracts, attributes and anything else: not types of the object model


def stst_body(stmt):
    i = stmt.find("{")
    if i < 0:
        return ""
    j = match_brace(stmt, i)
    return stmt[i + 1:j]


def skipped(cat, why):
    cat.skipped[why] = cat.skipped.get(why, 0) + 1


def parse_enum(ns, name, attrs, body, cat):
    full = ns + "." + name
    flags = any(re.match(r"\[\s*flags\s*\]", a) for a in attrs)
    values, nxt = [], 0
    for item in re.sub(ATTR, " ", body).split(","):
        item = norm_space(item)
        if not item:
            continue
        m = re.match(r"(\w+)\s*(?:=\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+)))?$", item)
        if not m:
            raise ValueError("enum %s: cannot read '%s'" % (full, item))
        if m.group(2) is not None:
            nxt = int(m.group(2), 0)
        values.append((m.group(1), nxt))
        nxt += 1
    cat.types[full] = dict(kind="enum", ns=ns, name=name, flags=flags, values=values, props=[], base="")


def parse_struct(ns, name, attrs, body, cat):
    full = ns + "." + name
    props = []
    for s in statements(body):
        m = re.match(r"([\w.<>]+)\s+(\w+)$", norm_space(s))
        if m:
            props.append((m.group(2), m.group(1), False, False, False))
    cat.types[full] = dict(kind="struct", ns=ns, name=name, props=props, base="")


def parse_class(ns, m, attrs, body, cat):
    sealed = m.group(1) is None
    name = m.group(2)
    full = ns + "." + name
    base = ""
    if m.group(3):
        first = m.group(3).split(",")[0].strip()
        if first and not first.startswith("[") and re.match(r"[\w.]+$", first):
            base = first
    content = ""
    for a in attrs:
        c = re.match(r'\[\s*contentproperty\(\s*"(\w+)"\s*\)\s*\]', a)
        if c:
            content = c.group(1)
    stmts = [norm_space(re.sub(ATTR, " ", s)) for s in statements(body)]
    dps = set()
    for s in stmts:
        d = re.match(r"static\s+[\w.]*DependencyProperty\s+(\w+)Property\s*\{", s)
        if d:
            dps.add(d.group(1))
    props, seen = [], set()
    for s in stmts:
        if s.startswith(("event ", "overridable ", "protected ")):
            continue
        if "(" in s:
            g = re.match(r"static\s+(.+?)\s+Get(\w+)\(\s*([\w.]+)\s+\w+\s*\)$", s)
            if g and g.group(2) in dps and g.group(2) not in seen:
                seen.add(g.group(2))
                props.append((g.group(2), g.group(1), False, True, True))
            continue
        if s.startswith("static "):
            continue
        p = re.match(r"([\w.<>, ]+?)\s+(\w+)\s*(\{\s*get\s*;\s*(set\s*;\s*)?\})?$", s)
        if not p:
            continue
        pname = p.group(2)
        if pname in seen:
            continue
        seen.add(pname)
        readonly = bool(p.group(3)) and not p.group(4)
        props.append((pname, p.group(1).strip(), readonly, False, pname in dps))
    cat.types[full] = dict(kind="class", ns=ns, name=name, sealed=sealed, base=base, content=content, props=props)


def keep(t):
    if t["kind"] != "class":
        return True        # enums and structs are value types other types refer to: always kept
    if t["ns"] in SKIP_NAMESPACES:
        return False
    if t["name"].endswith("EventArgs"):
        return False
    return True


def has_instance_property(kept, qualify, full, name):
    """True when the class or one of its bases declares an instance property of that name."""
    seen = set()
    while full and full in kept and full not in seen:
        seen.add(full)
        t = kept[full]
        if any(p[0] == name and not p[3] for p in t["props"]):
            return True
        full = qualify(t["base"], t["ns"]) if t.get("base") else ""
    return False


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    commit, out_path, files = argv[1], argv[2], argv[3:]
    cat = Catalog()
    for f in files:
        parse_file(f, cat)
    kept = {k: v for k, v in cat.types.items() if keep(v)}
    dropped = len(cat.types) - len(kept)

    # nested names: an IDL type name may be written fully qualified or relative to its namespace
    def qualify(name, ns):
        if name in kept:
            return name
        if (ns + "." + name) in kept:
            return ns + "." + name
        return name

    primitives_used = set()
    notes = []
    out = []
    w = out.append
    w("# LAML catalog extracted from the Microsoft.UI.Xaml IDL (MIT). GENERATED by tools/catalog/idl_extract.py;")
    w("# do not edit by hand: regenerate and diff. Facts only (names, base types, property value types,")
    w("# enum members and numbers); no code is copied. Source and commit are pinned below.")
    w("laml-types 1")
    w("")
    w("source winui-idl https://github.com/microsoft/microsoft-ui-xaml/tree/%s/dxaml/xcp/dxaml/idl/winrt %s MIT" % (commit, commit))
    w("")
    namespaces = sorted({t["ns"] for t in kept.values()})
    for ns in namespaces:
        w("xmlns http://schemas.microsoft.com/winfx/2006/xaml/presentation %s" % ns)
    w("")

    lines = []
    unresolved = {}
    collisions = {}
    short = {}
    for full in sorted(kept):
        t = kept[full]
        short.setdefault(t["name"], []).append(full)
        opts = ["src=winui-idl"]
        if t["kind"] == "class":
            if t["sealed"]:
                opts.append("sealed")
            base = qualify(t["base"], t["ns"]) if t["base"] else ""
            # `: IFoo` after the class name lists interfaces when the class has no base class; the WinRT
            # naming rule (I + capital letter) tells them apart for interfaces declared outside these files
            if base and base not in kept and (base in cat.interfaces or re.search(r"\.I[A-Z]\w*$", base)):
                base = ""
            if base and base not in kept:
                unresolved[base] = unresolved.get(base, 0) + 1
            if base:
                opts.append("base=" + base)
            if t["content"]:
                if has_instance_property(kept, qualify, full, t["content"]):
                    opts.append("content=" + t["content"])
                else:
                    notes.append("%s: content property '%s' is not a member in the IDL (XAML-only), left out" % (full, t["content"]))
            lines.append("type %s %s" % (full, " ".join(opts)))
        elif t["kind"] == "enum":
            lines.append("type %s kind=enum %s%s" % (full, "flags " if t["flags"] else "", " ".join(opts)))
            for n, v in t["values"]:
                lines.append("enumval %s %s=%d" % (full, n, v))
        else:
            lines.append("type %s kind=struct %s" % (full, " ".join(opts)))
        for (pname, ptype, readonly, attached, dp) in t["props"]:
            nullable = False
            ptype = norm_space(ptype)
            r = re.match(r"Windows\.Foundation\.IReference<\s*([\w.]+)\s*>$", ptype)
            if r:
                nullable, ptype = True, r.group(1)
            if "<" in ptype:
                ptype = ptype.split("<")[0]
            if ptype in PRIMITIVES:
                primitives_used.add(PRIMITIVES[ptype])
                ptype = PRIMITIVES[ptype]
            else:
                ptype = qualify(ptype, t["ns"])
                if ptype not in kept:
                    unresolved[ptype] = unresolved.get(ptype, 0) + 1
            o = []
            if attached:
                o.append("attached")
            if readonly:
                o.append("readonly")
            if dp:
                o.append("dependency")
            if nullable:
                o.append("nullable")
            lines.append("prop %s.%s %s%s" % (full, pname, ptype, (" " + " ".join(o)) if o else ""))
    w("# primitives (the value types the IDL names without a namespace)")
    for p in sorted(set(PRIMITIVES.values())):
        w("type %s kind=primitive src=winui-idl" % p)
    w("")
    out.extend(lines)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")

    for k, v in sorted(short.items()):
        if len(v) > 1:
            collisions[k] = v
    sys.stderr.write("types parsed %d, kept %d, left out by the scope filter %d; interfaces seen %d\n"
                     % (len(cat.types), len(kept), dropped, len(cat.interfaces)))
    for n in notes:
        sys.stderr.write("note: %s\n" % n)
    sys.stderr.write("short-name collisions: %s\n" % (collisions or "none"))
    sys.stderr.write("referenced but not in the kept set (%d distinct): %s\n" % (
        len(unresolved), ", ".join("%s(%d)" % kv for kv in sorted(unresolved.items(), key=lambda kv: -kv[1])[:60])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
