#!/usr/bin/env python3
"""Zip packages read without z.library:

    test_inflate_samples.py OWCONVERT OWCONVERT_NOZLIBRARY [SAMPLE...]

OWCONVERT_NOZLIBRARY is OWConvert built with OWF_TEST_NO_ZLIBRARY: zlib's
calls fail as z.library's do on an Amiga without it, so libowf reads each
deflated zip entry with its own inflate (libowf/src/owf_inflate.c) and
writes every entry stored.

1. Each sample (by default the repository's samples/*.docx and *.odt, and
   the workbook fixtures tests/book/fixtures/*.xlsx and *.ods), and each
   again packed at deflate levels 1 and 9, is read both ways and saved:
   documents as text, HTML, ODT and DOCX, workbooks as CSV, XLSX and ODS.
   Text, HTML and CSV must be the same byte for byte; ODT, DOCX, XLSX and
   ODS the same part for part, those written without z.library all stored. A sample whose
   format this OWConvert does not read yet is passed over.
2. Damaged documents give the message for what is wrong, both ways: a wrong
   CRC or broken deflate data, "damaged"; another compression method or an
   encrypted entry, "cannot do that". Cut-short and bit-flipped copies of
   every sample give an answer, never a crash.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import glob
import io
import os
import random
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OWCONVERT = os.path.abspath(sys.argv[1])
NOZLIB = os.path.abspath(sys.argv[2])
ROOT = os.path.join(HERE, "..", "..")
SAMPLES = sys.argv[3:] or [p for pattern in ("samples/*.docx", "samples/*.odt", "tests/book/fixtures/*.xlsx",
                                             "tests/book/fixtures/*.ods")
                           for p in sorted(glob.glob(os.path.join(ROOT, pattern)))]
WORKBOOK = (".xlsx", ".ods")
DAMAGED = "the document is damaged"
UNSUPPORTED = "OpenWrite cannot do that with this format yet"
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append(name)
        print(f"FAIL {name} {detail}")


def run(tool, src, dst):
    r = subprocess.run([tool, src, dst], capture_output=True, timeout=120)
    return r.returncode, (r.stdout + r.stderr).decode("utf-8", "replace")


def repack(data, level):
    """The same package, every entry but ODF's mimetype deflated at level."""
    out = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(data)) as src, zipfile.ZipFile(out, "w") as dst:
        for info in src.infolist():
            body = src.read(info.filename)
            if info.filename == "mimetype":
                dst.writestr(info.filename, body, compress_type=zipfile.ZIP_STORED)
            else:
                dst.writestr(info.filename, body, compress_type=zipfile.ZIP_DEFLATED, compresslevel=level)
    return out.getvalue()


def entries(data):
    """name -> (local header offset, central directory offset)"""
    end = data.rfind(b"PK\5\6")
    count, _, dir_offset = struct.unpack("<HII", data[end + 10:end + 20])
    found, p = {}, dir_offset
    for _ in range(count):
        n, extra, comment = struct.unpack("<HHH", data[p + 28:p + 34])
        local = struct.unpack("<I", data[p + 42:p + 46])[0]
        found[data[p + 46:p + 46 + n].decode("utf-8")] = (local, p)
        p += 46 + n + extra + comment
    return found


def main_part(data):
    names = entries(data)
    for name in ("word/document.xml", "xl/workbook.xml", "content.xml"):
        if name in names:
            return name
    return None


def patched(data, name, field, value, size):
    """data with one field of name's local and central headers changed (its
    offset in the local header; the central header has it 2 bytes further on)"""
    local, central = entries(data)[name]
    b = bytearray(data)
    fmt = "<H" if size == 2 else "<I"
    struct.pack_into(fmt, b, local + field, value)
    struct.pack_into(fmt, b, central + field + 2, value)
    return bytes(b)


def body_offset(data, name):
    local = entries(data)[name][0]
    n, extra = struct.unpack("<HH", data[local + 26:local + 30])
    return local + 30 + n + extra


def compare(work, label, path, exts):
    same = 0
    for ext in exts:
        outs = []
        for tool in (OWCONVERT, NOZLIB):
            dst = os.path.join(work, f"out-{os.path.basename(tool)}.{ext}")
            if os.path.exists(dst):
                os.remove(dst)
            rc, text = run(tool, path, dst)
            check(f"{label} -> {ext} with {os.path.basename(tool)}", rc == 0, text.strip())
            outs.append(open(dst, "rb").read() if rc == 0 else b"")
        if ext in ("txt", "html", "csv"):
            check(f"{label} -> {ext}: the same both ways", outs[0] == outs[1] and outs[0],
                  f"{len(outs[0])} and {len(outs[1])} bytes")
            same += outs[0] == outs[1]
            continue
        try:
            a, b = zipfile.ZipFile(io.BytesIO(outs[0])), zipfile.ZipFile(io.BytesIO(outs[1]))
        except zipfile.BadZipFile as e:
            check(f"{label} -> {ext}: zips", False, str(e))
            continue
        names = [i.filename for i in a.infolist()]
        check(f"{label} -> {ext}: the same parts", names == [i.filename for i in b.infolist()])
        parts_same = all(a.read(n) == b.read(n) for n in names if n in b.namelist())
        check(f"{label} -> {ext}: each part the same", parts_same)
        check(f"{label} -> {ext}: stored without z.library",
              all(i.compress_type == zipfile.ZIP_STORED for i in b.infolist()))
        check(f"{label} -> {ext}: deflated with it",
              any(i.compress_type == zipfile.ZIP_DEFLATED for i in a.infolist()))
        same += parts_same
    return same


def expect_message(work, label, data, want):
    path = os.path.join(work, "damaged" + (".docx" if b"word/" in data else ".odt"))
    with open(path, "wb") as f:
        f.write(data)
    for tool in (OWCONVERT, NOZLIB):
        rc, text = run(tool, path, os.path.join(work, "damaged.txt"))
        check(f"{label} with {os.path.basename(tool)}: exit 10", rc == 10, f"exit {rc}: {text.strip()}")
        check(f"{label} with {os.path.basename(tool)}: says \"{want}\"", want in text, text.strip())


def main():
    work = tempfile.mkdtemp(prefix="owf-inflate-", dir=os.path.join(HERE, "..", "..", "build"))
    rng = random.Random(1951)
    compared = tried = 0
    for sample in SAMPLES:
        data = open(sample, "rb").read()
        label = os.path.basename(sample)
        workbook = sample.endswith(WORKBOOK)
        exts = ("csv", "xlsx", "ods") if workbook else ("txt", "html", "odt", "docx")
        rc, text = run(OWCONVERT, sample, os.path.join(work, "probe." + exts[0]))
        if rc != 0 and "not a document format" in text:
            print(f"{label}: not read by this OWConvert yet, passed over")
            continue
        compared += compare(work, label, sample, exts)
        for level in (1, 9):
            path = os.path.join(work, f"level{level}-{label}")
            with open(path, "wb") as f:
                f.write(repack(data, level))
            compared += compare(work, f"{label} at level {level}", path, exts)

        main = main_part(data)
        local, central = entries(data)[main]      # sizes from the central directory, as libowf takes them
        method = struct.unpack("<H", data[central + 10:central + 12])[0]
        check(f"{label}: {main} is deflated", method == 8)
        if not workbook:
            crc = struct.unpack("<I", data[central + 16:central + 20])[0]
            expect_message(work, f"{label}, {main} with a wrong CRC", patched(data, main, 14, crc ^ 1, 4), DAMAGED)
            broken = bytearray(data)
            broken[body_offset(data, main)] = 0xFF          # last block, type 3: there is none
            expect_message(work, f"{label}, {main} not deflate", bytes(broken), DAMAGED)
            expect_message(work, f"{label}, {main} as bzip2", patched(data, main, 8, 12, 2), UNSUPPORTED)
            expect_message(work, f"{label}, {main} encrypted", patched(data, main, 6, 1, 2), UNSUPPORTED)

        # cut short, and bits flipped in the main part's deflate data: an answer, never a crash
        start, packed = body_offset(data, main), struct.unpack("<I", data[central + 20:central + 24])[0]
        path = os.path.join(work, "hurt" + os.path.splitext(sample)[1])
        for i in range(60):
            if i < 20:
                hurt = data[:len(data) * i // 20]
            else:
                b = bytearray(data)
                for _ in range(1 + i % 3):
                    b[start + rng.randrange(packed)] ^= 1 << rng.randrange(8)
                hurt = bytes(b)
            with open(path, "wb") as f:
                f.write(hurt)
            for tool in (OWCONVERT, NOZLIB):
                rc, text = run(tool, path, os.path.join(work, "hurt." + exts[0]))
                tried += 1
                check(f"{label} hurt {i} with {os.path.basename(tool)}", rc in (0, 10),
                      f"exit {rc}: {text[-400:]}")
    print(f"{compared} conversions the same with and without z.library; {tried} damaged copies read safely")
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s); files in {work}")
    sys.exit(1 if failures else 0)


main()
