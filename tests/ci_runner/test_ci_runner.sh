#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence.
# SPDX-License-Identifier: MIT
# tests/ci/test_all.sh on a made-up repository: it finds every kind of test, a failing one makes it fail,
# exit 77 and tests/ci/skip.txt skip, a test outside the patterns is not run, and
# tests/ci/extra.txt's commands run.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/tests/ci" "$T/tests/a" "$T/tests/b"
cp "$HERE/../ci/test_all.sh" "$T/tests/ci/"
printf 'exit 0\n'  > "$T/tests/run.sh"
printf 'exit 0\n'  > "$T/tests/a/test_good.sh"
printf 'exit 77\n' > "$T/tests/a/test_self_skip.sh"
printf 'exit 1\n'  > "$T/tests/a/test_listed.sh"
printf 'exit 1\n'  > "$T/tests/a/helper.sh"
printf 'import unittest\nclass T(unittest.TestCase):\n    def test_ok(self): pass\n' > "$T/tests/b/test_ok.py"
printf '# why\ntests/a/test_listed.sh needs an Amiga\n' > "$T/tests/ci/skip.txt"
out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh")
echo "$out" | grep -qx 'REPO_TESTS pass=3 fail=0 skip=2' || { echo "FAIL all-good tree: $out"; exit 1; }
echo "$out" | grep -q 'helper.sh' && { echo "FAIL ran a non-test"; exit 1; }
printf 'import unittest\nclass T(unittest.TestCase):\n    def test_bad(self): self.fail("broken")\n' > "$T/tests/b/test_bad.py"
if out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh"); then echo "FAIL a failing test passed: $out"; exit 1; fi
echo "$out" | grep -q '^FAIL tests/b/test_bad.py' || { echo "FAIL not named: $out"; exit 1; }
echo "$out" | grep -q 'broken' || { echo "FAIL no log tail: $out"; exit 1; }
# tests/ci/extra.txt's commands run too, and a failing one fails the run
rm "$T/tests/b/test_bad.py"
printf '# name: command\nmake-check: true\n' > "$T/tests/ci/extra.txt"
out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh")
echo "$out" | grep -qx 'PASS extra:make-check' || { echo "FAIL extra.txt not run: $out"; exit 1; }
printf 'make-check: false\n' > "$T/tests/ci/extra.txt"
if out=$(OUT=$T/out sh "$T/tests/ci/test_all.sh"); then echo "FAIL a failing extra.txt command passed: $out"; exit 1; fi
echo "PASS ci runner: finds, fails, skips, extra.txt"
