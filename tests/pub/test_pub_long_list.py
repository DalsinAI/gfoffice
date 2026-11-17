#!/usr/bin/env python3
"""tests/pub/test_pub_long_list.py PUB_DUMP: the .pub reader on a page list
that names one master 200,000 times (tools/pubgen/make_pub.py long_list).
Finding the masters takes one pass over the list (libowf/src/in_pub.c,
mark_named_masters), so the file reads in well under the time limit, and a
master the list repeats is still one master.

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

LIMIT = 20                       # seconds; the old quadratic search took hours


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'long-list.pub')
        with open(path, 'wb') as f:
            f.write(make_pub.long_list(200000))
        start = time.time()
        try:
            p = subprocess.run([sys.argv[1], path], capture_output=True, timeout=LIMIT)
        except subprocess.TimeoutExpired:
            print('FAIL long list: pub_dump took over %d s' % LIMIT)
            return 1
        took = time.time() - start
        out = p.stdout.decode('utf-8', 'replace')
        if p.returncode != 0 or p.stderr:
            failures.append('exit %d %s' % (p.returncode, p.stderr.decode('utf-8', 'replace')[:300]))
        for want in ('masters 2', 'pages 2', 'page 1 master 1 ', 'page 2 master 2 '):
            if want not in out:
                failures.append('no "%s" in the dump' % want)
    for f in failures:
        print('FAIL long list: ' + f)
    print('pub-long-list: a 200000-entry page list read in %.1f s, %d failed' % (took, len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
