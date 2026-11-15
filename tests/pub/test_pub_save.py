#!/usr/bin/env python3
"""GFOpenPublisher's .pub writer (libowf/src/out_pub.c), through pub_resave
and pub_dump.

    test_pub_save.py PUB_RESAVE PUB_DUMP

For each file tools/pubgen/make_pub.py makes (PUB_SAMPLES, as test_pub.py):

1. pub_resave reads it and writes it again; the written file must dump
   exactly as tests/pub/expected/resaved/NAME.txt (pub_dump --stories, less
   the lines that name the writing program: version, build-mark,
   written-by, and the notes).
2. For the files whose every part the writer keeps (LOSSLESS), that dump is
   the same as the first file's.
3. Writing the written file again gives the same bytes (nothing drifts),
   and so does writing the first file twice.
4. The writer's report names what it could not keep (NOTES).
5. Files the reader refuses are refused by pub_resave too (exit 10), and
   nothing goes to stderr (where ASan and UBSan report).

The expected files were checked by hand against the first files' dumps:
the only differences are what the writer does not write yet (WordArt,
groups' own records, the scratch area) and the 98/2000 forms'
own shape numbers.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import difflib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = os.environ.get('PUB_SAMPLES') or os.path.join(HERE, '..', '..', 'build', 'host', 'pubgen')
EXPECTED = os.path.join(HERE, 'expected', 'resaved')
LOSSLESS = ['gen-2007-plain', 'gen-2010-styles', 'gen-2003-names', 'gen-cut']
REFUSED = ['gen-notpub', 'gen-damaged']
# a note pub_resave must print for the file (the writer's report)
NOTES = {'gen-colours': '5 run(s) of text past the first 255 colours saved in the automatic colour'}
SKIP = ('note ', 'version ', 'build-mark ', 'written-by ')


def run(*cmd):
    p = subprocess.run(list(cmd), capture_output=True, timeout=120)
    return p.returncode, p.stdout.decode('utf-8', 'replace'), p.stderr.decode('utf-8', 'replace')


def dump(exe, path, failures, name):
    rc, out, err = run(exe, '--stories', path)
    if rc != 0 or err:
        failures.append('%s: pub_dump exit %d %s' % (name, rc, err.strip()[:300]))
    return ''.join(line + '\n' for line in out.splitlines() if not line.startswith(SKIP))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    resave, pubdump = sys.argv[1], sys.argv[2]
    update = os.environ.get('PUB_SAVE_UPDATE') == '1'
    failures, checked = [], 0
    names = sorted(f[:-4] for f in os.listdir(SAMPLES) if f.endswith('.pub'))
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            src = os.path.join(SAMPLES, name + '.pub')
            once, twice, again = (os.path.join(tmp, name + x + '.pub') for x in ('-1', '-2', '-3'))
            rc, out, err = run(resave, src, once)
            if err:
                failures.append('%s: stderr from pub_resave: %s' % (name, err.strip()[:300]))
            if name in REFUSED:
                if rc != 10:
                    failures.append('%s: pub_resave exit %d, wanted 10 (refused)' % (name, rc))
                continue
            if rc != 0:
                failures.append('%s: pub_resave exit %d' % (name, rc))
                continue
            if name in NOTES and NOTES[name] not in out:
                failures.append('%s: no note "%s" from pub_resave' % (name, NOTES[name]))
            got = dump(pubdump, once, failures, name)
            exp = os.path.join(EXPECTED, name + '.txt')
            if update:
                os.makedirs(EXPECTED, exist_ok=True)
                with open(exp, 'w', encoding='utf-8') as f:
                    f.write(got)
            if not os.path.exists(exp):
                failures.append('%s: no expected file %s' % (name, os.path.relpath(exp)))
            else:
                with open(exp, encoding='utf-8') as f:
                    want = f.read()
                if got != want:
                    diff = ''.join(list(difflib.unified_diff(want.splitlines(True), got.splitlines(True),
                                                             'expected', 'got'))[:40])
                    failures.append('%s: the written file dumps differently from %s\n%s' % (name, os.path.relpath(exp), diff))
            if name in LOSSLESS:
                first = dump(pubdump, src, failures, name)
                if first != got:
                    diff = ''.join(list(difflib.unified_diff(first.splitlines(True), got.splitlines(True),
                                                             'read', 'written'))[:40])
                    failures.append('%s: not kept whole\n%s' % (name, diff))
            rc2, _, err2 = run(resave, once, twice)
            rc3, _, err3 = run(resave, src, again)
            if rc2 or rc3 or err2 or err3:
                failures.append('%s: writing again: exit %d %d %s' % (name, rc2, rc3, (err2 + err3).strip()[:300]))
            else:
                with open(once, 'rb') as a, open(twice, 'rb') as b, open(again, 'rb') as c:
                    x, y, z = a.read(), b.read(), c.read()
                if x != y:
                    failures.append('%s: writing the written file again changes it' % name)
                if x != z:
                    failures.append('%s: writing the same file twice gives different bytes' % name)
            checked += 1
    for f in failures:
        print('FAIL ' + f)
    if failures:
        return 1
    print('pub-save: %d files written and read back as expected, %d of them whole' %
          (checked, len([n for n in LOSSLESS if n in names])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
