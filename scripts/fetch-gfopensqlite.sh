#!/bin/sh
# sqlite.library and its link library for a GFOffice build, from gfopensqlite's release package
# (DalsinAI/gfopensqlite, GFOpenSQLite-<version>.zip), laid out as build.d/base.sh's OPENSQLITE_LIB:
#
#   DEST/include/              sqlite3.h, sqlite3_link.h, libraries/ inline/ proto/
#   DEST/lib-soft/libsqlite3_link.a
#   DEST/libs/sqlite.library   for the package's Libs
#
# deps/gfopensqlite.ref names the release: one line, its tag and the .zip's SHA-256. The .zip is checked
# against that sum before a file is taken from it, so a build of a GFOffice commit always gets the same
# library, and the one that was checked in a lab.
#
# The .zip is looked for in the vault first: the LumaVAULT kitchen's shelf, where a release is put as the
# ingredient gfopensqlite <version> (SHELF/<version>/files/GFOpenSQLite-<version>.zip). Only when it is not
# there is it fetched from RELEASE_BASE.
#
#   REF            the ref file (default deps/gfopensqlite.ref)
#   UNZIP          the unzip to use (default unzip); when there is none, python3 -m zipfile unpacks
#   SHELF          gfopensqlite on the kitchen's shelf. When it is not set, the first of these that holds the
#                  version: beside the job's stove (STOVE/../../shelf/...), LumaVAULT's kitchen
#                  (/srv/samsung/kitchen/shelf/...), a kitchen at its own default (/srv/kitchen/shelf/...)
#   RELEASE_BASE   where releases are, when the shelf hasn't it (default https://github.com/DalsinAI/gfopensqlite/releases/download)
#
# usage: scripts/fetch-gfopensqlite.sh DEST
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST=${1:?usage: fetch-gfopensqlite.sh DEST}
REF=${REF:-$HERE/deps/gfopensqlite.ref}
RELEASE_BASE=${RELEASE_BASE:-https://github.com/DalsinAI/gfopensqlite/releases/download}
[ -f "$REF" ] || { echo "fetch-gfopensqlite: no $REF" >&2; exit 1; }
read -r TAG SUM REST < "$REF" || true
case "$TAG" in
    v[0-9]*) ;;
    *) echo "fetch-gfopensqlite: $REF does not begin with a release tag (as v3.53.4-2)" >&2; exit 1 ;;
esac
case "$TAG" in
    *[!0-9A-Za-z.-]*) echo "fetch-gfopensqlite: the tag in $REF may hold letters, digits, '.' and '-' only" >&2; exit 1 ;;
esac
[ ${#SUM} = 64 ] || { echo "fetch-gfopensqlite: $REF has no SHA-256 after the tag" >&2; exit 1; }
ZIP=GFOpenSQLite-${TAG#v}.zip
if [ -z "${SHELF:-}" ]; then
    for s in ${STOVE:+"$(dirname -- "$(dirname -- "$STOVE")")/shelf/amicradle/gfopensqlite"} \
             /srv/samsung/kitchen/shelf/amicradle/gfopensqlite /srv/kitchen/shelf/amicradle/gfopensqlite; do
        SHELF=$s
        [ -f "$SHELF/${TAG#v}/files/$ZIP" ] && break
    done
fi
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
if [ -f "$SHELF/${TAG#v}/files/$ZIP" ]; then
    cp "$SHELF/${TAG#v}/files/$ZIP" "$W/$ZIP"
    FROM="the shelf ($SHELF/${TAG#v})"
else
    curl -sSfL -o "$W/$ZIP" "$RELEASE_BASE/$TAG/$ZIP" ||
        { echo "fetch-gfopensqlite: $ZIP is not on the shelf ($SHELF/${TAG#v}) and couldn't be fetched from $RELEASE_BASE/$TAG/$ZIP" >&2; exit 1; }
    FROM=$RELEASE_BASE
fi
echo "$SUM  $W/$ZIP" | sha256sum -c - >/dev/null 2>&1 ||
    { echo "fetch-gfopensqlite: $ZIP from $FROM is not the file $REF names (SHA-256 differs)" >&2; exit 1; }
# unzip where there is one; a kitchen without it (LumaVAULT's, 23 Nov 2026) has Python's zipfile
if command -v "${UNZIP:-unzip}" >/dev/null 2>&1; then
    "${UNZIP:-unzip}" -qo "$W/$ZIP" -d "$W/x"
else
    mkdir -p "$W/x"
    python3 -m zipfile -e "$W/$ZIP" "$W/x" ||
        { echo "fetch-gfopensqlite: no unzip, and python3 -m zipfile couldn't unpack $ZIP" >&2; exit 1; }
fi
S=$W/x/GFOpenSQLite
for f in Libs/sqlite.library SDK/lib/soft-float/libsqlite3_link.a SDK/Include/sqlite3.h SDK/Include/sqlite3_link.h \
         SDK/Include/libraries/sqlite.h SDK/Include/inline/sqlite.h SDK/Include/proto/sqlite.h; do
    [ -f "$S/$f" ] || { echo "fetch-gfopensqlite: $ZIP has no $f" >&2; exit 1; }
done
rm -rf "$DEST/include" "$DEST/lib-soft" "$DEST/libs"
mkdir -p "$DEST/include" "$DEST/lib-soft" "$DEST/libs"
cp -R "$S/SDK/Include/." "$DEST/include/"
cp "$S/SDK/lib/soft-float/libsqlite3_link.a" "$DEST/lib-soft/"
cp "$S/Libs/sqlite.library" "$DEST/libs/"
echo "fetch-gfopensqlite: $TAG from $FROM, sqlite.library $(wc -c < "$DEST/libs/sqlite.library") bytes"
