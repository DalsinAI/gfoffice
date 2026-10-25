#!/usr/bin/env python3
"""odf_check.py FILE...: checks that each file is a valid ODF package.

- The zip: "mimetype" first, stored, without extra fields; every member
  listed in META-INF/manifest.xml and every listed file present.
- Every XML part well formed.
- Every element and attribute in an ODF namespace allowed where it is, and
  the required attributes there, by odfpy's grammar (odf.grammar, made from
  the OASIS ODF 1.2 schema). Foreign namespaces are allowed, as ODF's
  extended conformance allows them.
- Every style, list style, master page, layout and drawing definition a
  part names is defined where that part can see it.
- odfpy itself loads the package.

Prints one line per problem; exit 1 when there is any. Used by
test_decks.py; odfpy is for the tests only (pip install odfpy).
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import io
import sys
import xml.etree.ElementTree as ET
import zipfile

try:
    from odf import grammar
    from odf.opendocument import load
except ImportError:  # pragma: no cover
    print("odf_check: odfpy is not installed (pip install odfpy)")
    sys.exit(2)

ODF_NS = {
    "urn:oasis:names:tc:opendocument:xmlns:office:1.0", "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
    "urn:oasis:names:tc:opendocument:xmlns:text:1.0", "urn:oasis:names:tc:opendocument:xmlns:table:1.0",
    "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0", "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
    "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0", "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0",
    "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0", "urn:oasis:names:tc:opendocument:xmlns:animation:1.0",
    "urn:oasis:names:tc:opendocument:xmlns:meta:1.0", "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0",
    "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0",
}
XLINK = "http://www.w3.org/1999/xlink"
XML = "http://www.w3.org/XML/1998/namespace"


def split(tag):
    if tag.startswith("{"):
        ns, name = tag[1:].split("}", 1)
        return ns, name
    return "", tag


def check_tree(part, root, problems):
    def walk(el, parent):
        ns, name = split(el.tag)
        q = (ns, name)
        if ns in ODF_NS and ns != "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0":
            if parent is not None:
                pns, pname = split(parent.tag)
                allowed = grammar.allowed_children.get((pns, pname))
                if pns in ODF_NS and allowed is not None and q not in allowed:
                    problems.append(f"{part}: <{name}> not allowed in <{pname}>")
            attrs = grammar.allowed_attributes.get(q)
            for a in el.attrib:
                ans, aname = split(a)
                if ans not in ODF_NS and ans not in (XLINK, XML):
                    continue
                if attrs is not None and (ans, aname) not in attrs:
                    problems.append(f"{part}: attribute {aname} not allowed in <{name}>")
            for r in grammar.required_attributes.get(q) or ():
                if "{%s}%s" % r not in el.attrib:
                    problems.append(f"{part}: <{name}> needs attribute {r[1]}")
        for c in el:
            walk(c, el)
    walk(root, None)


REF_ATTRS = {"style-name", "parent-style-name", "text-style-name", "list-style-name", "master-page-name",
             "page-layout-name", "presentation-page-layout-name", "data-style-name", "next-style-name",
             "fill-image-name", "fill-gradient-name", "fill-hatch-name", "marker-start", "marker-end",
             "stroke-dash", "opacity-name"}
NAME_ATTRS = ("{urn:oasis:names:tc:opendocument:xmlns:style:1.0}name",
              "{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}name")


def names_in(el):
    out = set()
    for e in el.iter() if el is not None else ():
        for a in NAME_ATTRS:
            if a in e.attrib and split(e.tag)[1] not in ("page", "frame", "custom-shape", "rect", "ellipse", "line",
                                                         "polygon", "g", "layer", "connector", "image"):
                out.add(e.attrib[a])
    return out


def check_refs(trees, problems):
    """Every style, list, master page, layout and drawing definition named is
    there: content.xml's from its own automatic styles and styles.xml's
    common ones, master pages and layouts; styles.xml's from its own."""
    O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
    styles = trees.get("styles.xml")
    content = trees.get("content.xml")
    common = names_in(styles.find(O + "styles")) if styles is not None else set()
    masters = names_in(styles.find(O + "master-styles")) if styles is not None else set()
    styles_auto = names_in(styles.find(O + "automatic-styles")) if styles is not None else set()
    scope = {"styles.xml": common | masters | styles_auto}
    if content is not None:
        page_layouts = {e.get(NAME_ATTRS[0]) for e in styles.iter("{urn:oasis:names:tc:opendocument:xmlns:style:1.0}presentation-page-layout")} if styles is not None else set()
        scope["content.xml"] = common | masters | page_layouts | names_in(content.find(O + "automatic-styles"))
    def walk(el):
        """The part's elements, but not an embedded object's own document
        (a chart in flat XML), whose names are its own."""
        yield el
        for c in el:
            if c.tag != O + "document":
                yield from walk(c)

    for part, root in trees.items():
        if part not in scope:
            continue
        for e in walk(root):
            for a, v in e.attrib.items():
                ans, aname = split(a)
                if ans not in ODF_NS or aname not in REF_ATTRS or not v:
                    continue
                if v not in scope[part]:
                    problems.append(f"{part}: {aname}={v!r} names nothing there")


def check(path):
    problems = []
    with open(path, "rb") as f:
        data = f.read()
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        return [f"{path}: not a zip"]
    infos = z.infolist()
    if not infos or infos[0].filename != "mimetype":
        problems.append("mimetype is not the first member")
    else:
        mi = infos[0]
        if mi.compress_type != zipfile.ZIP_STORED:
            problems.append("mimetype is compressed")
        if data[26:28] != b"\0\0" or data[28:30] != b"\0\0":
            if int.from_bytes(data[28:30], "little"):
                problems.append("mimetype has an extra field")
        mime = z.read("mimetype").decode("ascii", "replace")
        if not mime.startswith("application/vnd.oasis.opendocument."):
            problems.append(f"mimetype is {mime!r}")
    names = {i.filename for i in infos}
    try:
        man = ET.fromstring(z.read("META-INF/manifest.xml"))
    except (KeyError, ET.ParseError) as e:
        return problems + [f"manifest: {e}"]
    M = "{urn:oasis:names:tc:opendocument:xmlns:manifest:1.0}"
    listed = {e.get(M + "full-path") for e in man.iter(M + "file-entry")}
    for e in man.iter(M + "file-entry"):
        if e.get(M + "media-type") is None:
            problems.append(f"manifest entry {e.get(M + 'full-path')} has no media type")
    for n in names - {"mimetype", "META-INF/manifest.xml"}:
        if n not in listed and not n.endswith("/"):
            problems.append(f"{n} is not in the manifest")
    for n in listed:
        if n != "/" and not n.endswith("/") and n not in names:
            problems.append(f"the manifest lists {n}, which is not there")
    trees = {}
    for n in sorted(names):
        if not n.endswith(".xml") or "/" in n and not n.startswith("META-INF"):
            continue
        try:
            root = ET.fromstring(z.read(n))
        except ET.ParseError as e:
            problems.append(f"{n}: {e}")
            continue
        if n != "META-INF/manifest.xml":
            check_tree(n, root, problems)
            trees[n] = root
    check_refs(trees, problems)
    try:
        load(io.BytesIO(data))
    except Exception as e:  # noqa: BLE001 - odfpy raises many kinds
        problems.append(f"odfpy cannot load it: {e}")
    return problems


def main():
    bad = 0
    for path in sys.argv[1:]:
        for p in check(path):
            print(f"{path}: {p}")
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
