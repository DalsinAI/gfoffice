#!/usr/bin/env python3
"""OpenPublisher's .pub reader (libowf/src/in_pub.c), through pub_dump.

    test_pub.py PUB_DUMP [RUNS]

1. Each sample in tests/pub/samples with an expected file in
   tests/pub/expected (NAME.txt) must dump exactly as expected. The
   expected files were checked by hand against what each file shows: its
   own thumbnail (the picture Publisher saves in the file's summary), the
   same document saved by other versions, and its text.
2. Every sample, cut short and with bytes changed, must be read or refused
   cleanly: exit 0 or 10, nothing on stderr (where ASan and UBSan report),
   no hang. RUNS changed copies per sample (default 40); the bytes changed
   are mostly in the streams the reader parses, found through the compound
   file's own tables ([MS-CFB]).
3. With PUB_CORPUS set to a folder (tests/pub/fetch_samples.sh fills one),
   every .pub there is read the same way, and a summary is printed.

MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = os.path.join(HERE, 'samples')
EXPECTED = os.path.join(HERE, 'expected')


def run(exe, path, *args):
    p = subprocess.run([exe, *args, path], capture_output=True, timeout=120)
    return p.returncode, p.stdout.decode('utf-8', 'replace'), p.stderr.decode('utf-8', 'replace')


def stream_offsets(d):
    """File offsets of the streams the reader parses, after [MS-CFB]: for
    aiming changed bytes where they matter. Best effort; [] when unsure."""
    try:
        ss = 1 << struct.unpack_from('<H', d, 30)[0]
        nfat = struct.unpack_from('<I', d, 0x2C)[0]
        fat = []
        for i in range(min(nfat, 109)):
            o = (struct.unpack_from('<I', d, 0x4C + 4 * i)[0] + 1) * ss
            if o + ss <= len(d):
                fat += struct.unpack_from('<%dI' % (ss // 4), d, o)

        def chain(s):
            out = []
            while s < len(fat) and len(out) < 100000:
                out.append(s)
                s = fat[s]
            return out
        dirb = b''.join(d[(s + 1) * ss:(s + 2) * ss] for s in chain(struct.unpack_from('<I', d, 0x30)[0]))
        entries = [dirb[i:i + 128] for i in range(0, len(dirb) - 127, 128)]
        mini = chain(struct.unpack_from('<I', entries[0], 0x74)[0])
        minifat = []
        for s in chain(struct.unpack_from('<I', d, 0x3C)[0]):
            o = (s + 1) * ss
            if o + ss <= len(d):
                minifat += struct.unpack_from('<%dI' % (ss // 4), d, o)
        cutoff = struct.unpack_from('<I', d, 0x38)[0] or 4096
        want = {'Contents', 'CONTENTS', 'EscherStm', 'EscherDelayStm', '\x05DocumentSummaryInformation'}
        offs = []
        for e in entries:
            n = struct.unpack_from('<H', e, 0x40)[0]
            if e[0x42] != 2 or e[:max(0, n - 2)].decode('utf-16-le', 'replace') not in want:
                continue
            start, size = struct.unpack_from('<II', e, 0x74)
            if size < cutoff:
                s, k = start, 0
                while s < len(minifat) and k < 10000:
                    mo = s * 64
                    if mo // ss >= len(mini):
                        break
                    base = (mini[mo // ss] + 1) * ss + mo % ss
                    offs += range(base, base + 64)
                    s, k = minifat[s], k + 1
            else:
                for s in chain(start):
                    offs += range((s + 1) * ss, (s + 2) * ss)
        return [o for o in offs if o < len(d)]
    except (struct.error, IndexError, ValueError):
        return []


def changed_copies(data, runs, seed):
    rnd = random.Random(seed)
    yield 'empty', b''
    for cut in sorted({1, 8, 511, 512, 513, 1024, len(data) // 4, len(data) // 2, len(data) - 1}):
        if 0 < cut < len(data):
            yield 'cut at %d' % cut, data[:cut]
    targets = stream_offsets(data) or list(range(len(data)))
    for i in range(runs):
        d = bytearray(data)
        for _ in range(rnd.choice([1, 1, 2, 3, 8, 32])):
            k = rnd.choice(targets) if rnd.random() < 0.85 else rnd.randrange(len(d))
            d[k] = rnd.choice([0, 0xFF, 0x7F, 0x80, rnd.randrange(256), d[k] ^ (1 << rnd.randrange(8))])
        if rnd.random() < 0.15:
            d = d[:rnd.randrange(512, len(d) + 1)]
        yield 'changed %d (seed %d)' % (i, seed), bytes(d)


def robust(exe, path, runs, tmpdir, seed):
    data = open(path, 'rb').read()
    failures = []
    tmp = os.path.join(tmpdir, 'changed.pub')
    for what, d in changed_copies(data, runs, seed):
        with open(tmp, 'wb') as f:
            f.write(d)
        try:
            code, out, err = run(exe, tmp)
        except subprocess.TimeoutExpired:
            failures.append('%s: %s: hung' % (os.path.basename(path), what))
            continue
        if code not in (0, 10) or err:
            keep = os.path.join(tmpdir, 'FAIL-%s-%d.pub' % (os.path.basename(path), len(failures)))
            os.replace(tmp, keep)
            failures.append('%s: %s: exit %d %s (kept as %s)' % (os.path.basename(path), what, code, err[:400], keep))
    return failures


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    runs = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    failures = []
    names = sorted(f for f in os.listdir(SAMPLES) if f.endswith('.pub'))
    checked = 0
    for name in names:
        path = os.path.join(SAMPLES, name)
        exp = os.path.join(EXPECTED, name[:-4] + '.txt')
        code, out, err = run(exe, path)
        if err:
            failures.append('%s: stderr: %s' % (name, err[:400]))
        if os.path.exists(exp):
            want = open(exp, encoding='utf-8').read()
            if out != want:
                import difflib
                diff = ''.join(list(difflib.unified_diff(want.splitlines(True), out.splitlines(True), 'expected', 'got'))[:40])
                failures.append('%s: differs from %s\n%s' % (name, os.path.relpath(exp), diff))
            checked += 1
        else:
            failures.append('%s: no expected file %s' % (name, os.path.relpath(exp)))
    tmpdir = tempfile.mkdtemp(prefix='pubtest')
    copies = 0
    for k, name in enumerate(names):
        failures += robust(exe, os.path.join(SAMPLES, name), runs, tmpdir, 1000 + k)
        copies += runs + 10
    print('pub: %d samples as expected, %d changed copies read or refused cleanly' % (checked, copies))
    corpus = os.environ.get('PUB_CORPUS')
    if corpus and os.path.isdir(corpus):
        for name in sorted(os.listdir(corpus)):
            if not name.lower().endswith('.pub'):
                continue
            path = os.path.join(corpus, name)
            code, out, err = run(exe, path)
            if err or code not in (0, 10):
                failures.append('%s (PUB_CORPUS): exit %d %s' % (name, code, err[:400]))
            head = [l for l in out.splitlines() if l.split(' ')[0] in ('version', 'pages', 'stories', 'pictures', 'error')]
            print('  %-40s %s' % (name[:40], ' | '.join(head)))
            failures += robust(exe, path, max(10, runs // 4), tmpdir, 5000)
    for f in failures:
        print('FAIL', f)
    if not failures:
        for f in os.listdir(tmpdir):
            os.remove(os.path.join(tmpdir, f))
        os.rmdir(tmpdir)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
