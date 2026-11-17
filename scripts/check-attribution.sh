#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence.
# Fails if the tree carries tool attribution: assistant config files, an
# assistant or vendor named in a tracked file, or any Co-Authored-By line.
# The names are kept encoded so this file doesn't carry them.
# Dictionaries/ is left out: its word lists hold ordinary English words.
# Usage: scripts/check-attribution.sh [REPO_DIR]
dir=${1:-.}
cd "$dir" || exit 2
d() { printf '%s' "$1" | base64 -d; }
A=$(d Y2xhdWRl)        # the assistant
B=$(d YW50aHJvcGlj)    # its vendor
bad=0

files=$(git ls-files | grep -iE "(^|/)($A\.md|\.$A/|agents\.md)" )
if [ -n "$files" ]; then
    echo "attribution-check: assistant files in the tree:"
    echo "$files" | sed 's/^/  /'
    bad=1
fi

hits=$(git grep -I -n -iE "(\\b($A|$B)\\b|^[[:space:]]*co-authored-by:)" -- . ':!Dictionaries' 2>/dev/null)
if [ -n "$hits" ]; then
    echo "attribution-check: tool attribution in tracked files:"
    echo "$hits" | cut -c1-200 | sed 's/^/  /'
    bad=1
fi

if [ $bad -ne 0 ]; then
    echo "attribution-check: FAILED"
    exit 1
fi
echo "attribution-check: OK"
