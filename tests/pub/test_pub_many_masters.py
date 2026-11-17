#!/usr/bin/env python3
"""tests/pub/test_pub_many_masters.py PUB_DUMP: the .pub reader on 20,000
left halves of two-page masters, each with its own name, then one real pair
(tools/pubgen/make_pub.py many_halves). Pairing the halves takes one pass
through a table of their names (libowf/src/in_pub.c, pair_masters), so the
file reads in well under the time limit; the halves without a pair are
one-page masters and the pair is still found.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'pubgen'))
import make_pub  # noqa: E402

N = 20000
LIMIT = 20                       # seconds


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'many-halves.pub')
        with open(path, 'wb') as f:
            f.write(make_pub.many_halves(N))
        start = time.time()
        try:
            p = subprocess.run([sys.argv[1], path], capture_output=True, timeout=LIMIT)
        except subprocess.TimeoutExpired:
            print('FAIL many masters: pub_dump took over %d s' % LIMIT)
            return 1
        took = time.time() - start
        out = p.stdout.decode('utf-8', 'replace')
        if p.returncode != 0 or p.stderr:
            failures.append('exit %d %s' % (p.returncode, p.stderr.decode('utf-8', 'replace')[:300]))
        for want in ('facing-pages\n', 'masters %d\n' % (N + 2), 'master %d "Pair" frames 0 right with %d\n' % (N + 1, N + 2),
                     'master %d "Pair" frames 0 left with %d\n' % (N + 2, N + 1), 'master 1 "Half 000000" frames 0\n'):
            if want not in out:
                failures.append('no %r in the dump' % want)
        if out.count(' with ') != 2:
            failures.append('%d paired masters, not 2' % out.count(' with '))
    for f in failures:
        print('FAIL many masters: ' + f)
    print('pub-many-masters: %d unpaired halves and a pair read in %.1f s, %d failed' % (N, took, len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
