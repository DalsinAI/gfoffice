#!/usr/bin/env python3
"""tools/pubgen/make_pub.py's own test: it makes the same bytes every time
(the expected dumps and the changed copies' seeds rely on that), every file
it names, each a compound file ([MS-CFB]) whose streams are where its
directory says, and no outside .pub is kept in the repository.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
GEN = os.path.join(ROOT, 'tools', 'pubgen', 'make_pub.py')
EXPECTED = os.path.join(HERE, 'expected')


def make(dest):
    subprocess.run([sys.executable, '-I', GEN, dest], check=True)
    return {n: open(os.path.join(dest, n), 'rb').read() for n in sorted(os.listdir(dest)) if n.endswith('.pub')}


def streams(d):
    """Each stream's name and bytes, read back through the FAT and mini FAT."""
    ss = 512
    nfat = struct.unpack_from('<I', d, 0x2C)[0]
    fat = []
    for i in range(nfat):
        s = struct.unpack_from('<I', d, 0x4C + 4 * i)[0]
        fat += struct.unpack_from('<128I', d, (s + 1) * ss)

    def chain(s):
        out = []
        while s < 0xFFFFFFFA:
            out.append(s)
            s = fat[s]
        return out

    def read(s, n):
        return b''.join(d[(k + 1) * ss:(k + 2) * ss] for k in chain(s))[:n]
    dirb = read(struct.unpack_from('<I', d, 0x30)[0], 1 << 30)
    ents = [dirb[i:i + 128] for i in range(0, len(dirb), 128)]
    root = ents[0]
    mini = read(*struct.unpack_from('<II', root, 0x74))
    mfs = struct.unpack_from('<I', d, 0x3C)[0]
    minifat = list(struct.unpack_from('<%dI' % (len(read(mfs, 1 << 30)) // 4), read(mfs, 1 << 30))) if mfs < 0xFFFFFFFA else []
    out = {}
    for e in ents:
        if e[0x42] != 2:
            continue
        name = e[:struct.unpack_from('<H', e, 0x40)[0] - 2].decode('utf-16-le')
        start, size = struct.unpack_from('<II', e, 0x74)
        if size < 4096:
            data, s = b'', start
            while s < 0xFFFFFFFA and len(data) < size:
                data += mini[s * 64:(s + 1) * 64]
                s = minifat[s]
            out[name] = data[:size]
        else:
            out[name] = read(start, size)
    return out


def main():
    failures = []
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        one, two = make(a), make(b)
    if one != two:
        failures.append('two runs made different bytes: %s' % [n for n in one if one[n] != two.get(n)])
    want = sorted(n[:-4] + '.pub' for n in os.listdir(EXPECTED) if n.endswith('.txt'))
    if sorted(one) != want:
        failures.append('made %s, expected dumps for %s' % (sorted(one), want))
    for name, data in one.items():
        if name == 'gen-cut.pub':
            continue                         # cut short on purpose
        if data[:8] != b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1' or len(data) % 512:
            failures.append('%s: not a whole compound file' % name)
            continue
        try:
            st = streams(data)
        except (struct.error, IndexError) as e:
            failures.append('%s: its directory or FAT does not read: %s' % (name, e))
            continue
        if name not in ('gen-notpub.pub', 'gen-damaged.pub') and \
                (not st.get('Contents', b'')[:2] == b'\xe8\xac' or 'CONTENTS' not in st):
            failures.append('%s: no Contents or text storage' % name)
    kept = subprocess.run(['git', '-C', ROOT, 'ls-files', '*.pub'], capture_output=True, text=True)
    if kept.returncode == 0 and kept.stdout.strip():
        failures.append('outside .pub files are in the repository: %s' % kept.stdout.split())
    for f in failures:
        print('FAIL', f)
    print('pubgen: %d files, made the same twice, each a whole compound file' % len(one))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
