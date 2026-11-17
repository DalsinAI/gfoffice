#!/usr/bin/env python3
"""tests/pub/test_pressmaster_name.py: the publishing app is GFOpenPressmaster (renamed from GFOpenPublisher
on 16 November 2026). Checks the program name, the version string, the build output, the ARexx port and its
old-name alias, and that the old name is left only where it is kept on purpose."""
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fails = 0


def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        fails += 1


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8', errors='replace') as f:
        return f.read()


app = read('app/openpresent.c')
block = re.search(r'#ifdef OPENPRESSMASTER\n(.*?)#else', app, re.S)
check(block is not None, 'app/openpresent.c has an OPENPRESSMASTER block')
block = block.group(1) if block else ''
check('#define PROGRAM "GFOpenPressmaster"' in block, 'PROGRAM is GFOpenPressmaster')
check(re.search(r'#define VERSION_TEXT "GFOpenPressmaster \d+\.\d+\.\d+ \(\d+\.\d+\.\d{4}\)"', block) is not None,
      'VERSION_TEXT names GFOpenPressmaster with a version and date')
check('#define REXX_PORT_NAME "GFOPENPRESSMASTER"' in block, 'ARexx port is GFOPENPRESSMASTER')
check('#define REXX_OLD_PORT_NAME "GFOPENPUBLISHER"' in block, 'old port name GFOPENPUBLISHER is kept as an alias')
check('rexx_old_port = o;' in app and 'RemPort(rexx_old_port)' in app and 'FreeVec(rexx_old_port)' in app,
      'the alias port is added, removed and freed')
check('o->mp_SigBit = p->mp_SigBit;' in app, 'the alias port shares the main port\'s signal')

build = read('build-os3.sh')
check('-o "$OUT/GFOpenPressmaster"' in build, 'build-os3.sh writes GFOpenPressmaster')
check('-DOPENPRESSMASTER' in build, 'build-os3.sh defines OPENPRESSMASTER')

for f in ('include/openpressmaster_core.h', 'libowf/include/openpressmaster_merge.h',
          'libowf/include/openpressmaster_template.h', 'docs/gfoffice/Design-OpenPressmaster.md',
          'docs/parity/pressmaster.md'):
    check(os.path.exists(os.path.join(ROOT, f)), f + ' exists')

for name in ('catalog', 'merge', 'print', 'template'):
    rx = read('tests/arexx/pressmaster-%s.rexx' % name)
    check("port = 'GFOPENPRESSMASTER'" in rx, 'tests/arexx/pressmaster-%s.rexx addresses GFOPENPRESSMASTER' % name)

mk = read('Makefile')
for t in ('core', 'merge', 'merge-show', 'template'):
    for k in ('check', 'test'):
        new = 'pressmaster-%s-%s' % (t, k)
        check(re.search(r'(?m)^' + re.escape(new) + r':', mk) is not None, 'make target ' + new)
        check(re.search(r'(?m)^' + k + ': ' + re.escape(new) + r'$', mk) is not None, new + ' is part of make ' + k)
        check(('publisher-%s-%s: %s\n' % (t, k, new)) in mk, 'old target publisher-%s-%s still runs %s' % (t, k, new))

# the old name is left only in the alias, the notes that say it was renamed, and the team-notes plan path
ALLOWED = ('REXX_OLD_PORT_NAME', 'called GFOpenPublisher before', 'GFOPENPUBLISHER-PARITY-PLAN',
           'renamed from GFOpenPublisher', 'Renamed from GFOpenPublisher', 'test_pressmaster_name.py', 'GFOPENPUBLISHER still answers',
           'GFOpenPublisher before 16 November')
try:
    out = subprocess.run(['git', 'grep', '-n', '-i', 'openpublisher'], cwd=ROOT, capture_output=True,
                         text=True).stdout
except OSError:
    out = ''
left = [l for l in out.splitlines() if not any(a in l for a in ALLOWED)]
for l in left:
    print('     old name: ' + l)
check(not left, 'no other GFOpenPublisher / OPENPUBLISHER names are left')

print('%s: %d failed' % (os.path.basename(__file__), fails))
sys.exit(1 if fails else 0)
