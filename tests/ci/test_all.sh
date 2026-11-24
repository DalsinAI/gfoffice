#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence.
# SPDX-License-Identifier: MIT
# Every one of the repository's own tests, as pull request CI runs them (LV-16: a PR is green only when its
# repository's tests ran). The tests are found, not listed: in every tests/ folder of the tree, run.sh,
# run_*.sh, pipe_m0.sh, test_*.sh and test_*.py, so a new test is in CI the day it is added. The same
# file works in any repository (copy tests/ci/ and .github/workflows/repo-tests.yml).
# A test that needs what CI's machine has not got (a stove, an Amiga, a library) is named in
# tests/ci/skip.txt with why; a test may also exit 77 to say it skipped itself. A top Makefile's
# 'check' (else 'test') target runs first, as make:check.
#   tests/ci/test_all.sh [-list]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SKIP=$HERE/skip.txt
cd "$ROOT" || exit 2

found() {
    # A top Makefile's own suite first ('make check', else 'make test'), as the repository's docs run it.
    if [ -f Makefile ]; then
        if grep -q '^check:' Makefile; then echo "make:check"; elif grep -q '^test:' Makefile; then echo "make:test"; fi
    fi
    # Every tests/ folder in the tree, not only the top one (a part's own tests, e.g. library/x/tests/run.sh).
    find . \( -name .git -o -name build -o -name third_party -o -name vendor -o -name external -o -name upstream \
           -o -name node_modules -o -path ./tests/ci \) -prune -o -type f \
           \( -path '*/tests/run.sh' -o -path '*/tests/run_*.sh' -o -path '*/tests/pipe_m0.sh' \
              -o -path '*/tests/*test_*.sh' -o -path '*/tests/*test_*.py' \) -print | sed 's|^\./||' | sort
}
skipped() { [ -f "$SKIP" ] && grep -v '^#' "$SKIP" | awk '{print $1}' | grep -qx "$1"; }

if [ "${1:-}" = "-list" ]; then found; exit 0; fi

# Each test gets TEST_TIMEOUT seconds (default 900), so one that hangs fails by name instead of holding the job.
TO=${TEST_TIMEOUT:-900}
T=""; command -v timeout >/dev/null && T="timeout $TO"
pass=0 fail=0 skip=0 failed=""
LOG=${OUT:-$ROOT/build/tests}/ci
mkdir -p "$LOG"
for t in $(found); do
    if skipped "$t"; then echo "SKIP $t ($(grep "^$t " "$SKIP" | cut -d' ' -f2-))"; skip=$((skip + 1)); continue; fi
    l=$LOG/$(echo "$t" | tr '/:' '__').log
    echo "RUN  $t"
    case "$t" in
        make:*) $T make -j"${MAKE_JOBS:-4}" "${t#make:}" >"$l" 2>&1 ;;
        *.py) d=$(dirname "$t"); m=$(basename "$t" .py)
              if python3 -c 'import pytest' 2>/dev/null; then (cd "$d" && $T python3 -m pytest -q "$m.py") >"$l" 2>&1
              else (cd "$d" && $T python3 -m unittest -v "$m") >"$l" 2>&1; fi ;;
        *)    $T sh "$t" >"$l" 2>&1 ;;
    esac
    rc=$?
    if [ $rc -eq 0 ]; then echo "PASS $t"; pass=$((pass + 1))
    elif [ $rc -eq 77 ]; then echo "SKIP $t (skipped itself)"; skip=$((skip + 1))
    elif [ $rc -eq 124 ]; then echo "FAIL $t (over ${TO}s), its last lines:"; tail -n 30 "$l" | sed 's/^/    /'; fail=$((fail + 1)); failed="$failed $t"
    else echo "FAIL $t (exit $rc), its last lines:"; tail -n 30 "$l" | sed 's/^/    /'; fail=$((fail + 1)); failed="$failed $t"; fi
done
echo "REPO_TESTS pass=$pass fail=$fail skip=$skip"
[ $fail -eq 0 ] || { echo "failed:$failed"; exit 1; }
