#!/bin/sh
# fetch_samples.sh DIR: downloads the sample files tests/pub/CORPUS.md lists
# but doesn't check in, into DIR, and prints each file's SHA-256.
#
# It takes the files only, never code: GitHub repositories are cloned
# without their files' contents (--filter=blob:none --sparse) and only the
# sample files are checked out. It verifies nothing and extracts nothing:
# archives (the Amiga .lha packages) stay as they are. Then
#     PUB_CORPUS=DIR make pub-test
# reads every .pub in DIR as well.
#
# Needs git and curl; python3 for the bug tracker's attachments.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -u

[ $# -eq 1 ] || { echo "usage: $0 DIR" >&2; exit 2; }
DEST=$1
mkdir -p "$DEST/src" || exit 1
FAILED=""

# repo commit path... : a sparse, blobless clone at that commit, then the
# paths are copied into DEST (with the repo's name in front when asked).
fetch_git() {
    repo=$1; commit=$2; prefix=$3; shift 3
    dir="$DEST/src/$(echo "$repo" | tr '/' '_')"
    if [ ! -d "$dir/.git" ]; then
        git clone -q --depth 1 --filter=blob:none --sparse --no-checkout "https://github.com/$repo" "$dir" ||
            { FAILED="$FAILED $repo"; return; }
    fi
    # the pinned commit when the server gives it, else the default branch's
    git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit" 2>/dev/null && use=$commit || use=HEAD
    git -C "$dir" sparse-checkout set --no-cone "$@" || { FAILED="$FAILED $repo"; return; }
    git -C "$dir" checkout -q "$use" || { FAILED="$FAILED $repo"; return; }
    for p in "$@"; do
        for f in "$dir"/$p; do
            [ -f "$f" ] && cp "$f" "$DEST/$prefix$(basename "$f")"
        done
    done
    echo "$repo at $(git -C "$dir" rev-parse HEAD)"
}

fetch_url() {
    url=$1; name=$2
    curl -fsSL --retry 2 -o "$DEST/$name" "$url" || { FAILED="$FAILED $name"; rm -f "$DEST/$name"; }
}

# ---- .pub on GitHub ----
fetch_git apache/poi 4f3c25563919e7efa4a75feccf6cef1ac3d05a04 "" \
    '/test-data/publisher/*.pub'
fetch_git LibreOffice/core 7d2daf6ce9564281493a6b644b3a56cd08490e76 "" \
    '/writerperfect/qa/unit/data/draw/libmspub/pass/*.pub'
fetch_git aspose-pub/Aspose.PUB-for-.NET beee619f9a4b7e2c8908e0fa132f4f4650b08975 "" \
    '/Examples/Data/*.pub'
fetch_git gabriel-vasile/mimetype 7feb76447ef040104e1f283c376ec51f625a7a21 "mimetype-" \
    '/testdata/pub.pub'
# apache/tika's testPUBLISHER.pub is the same file as POI's Simple.pub.

# ---- .pub elsewhere (a PC; not reachable from the session's shell) ----
# freedesktop.org bug 82362's attachments, through the tracker's REST
# interface: the files only (the discussion is not to be read).
fetch_url "https://bugs.freedesktop.org/rest/bug/82362/attachment" bug-82362-attachments.json
if [ -f "$DEST/bug-82362-attachments.json" ] && command -v python3 >/dev/null; then
    python3 -I - "$DEST/bug-82362-attachments.json" "$DEST" <<'EOF'
import base64, json, os, sys
doc = json.load(open(sys.argv[1]))
for bug in doc.get('bugs', {}).values():
    for a in bug:
        name = 'bug82362-%s-%s' % (a.get('id'), os.path.basename(a.get('file_name') or 'attachment'))
        with open(os.path.join(sys.argv[2], name), 'wb') as f:
            f.write(base64.b64decode(a.get('data') or ''))
EOF
fi

# ---- PageStream (Amiga): no sample found on GitHub ----
for p in pgs22demo pgs3 pgs3f pgsmodam; do
    fetch_url "https://aminet.net/text/dtp/$p.lha" "aminet-$p.lha"
    fetch_url "https://aminet.net/text/dtp/$p.readme" "aminet-$p.readme"
done
# the PageStream 3 beta's help (a cover CD's archive on discmaster.textfiles.com):
# what this URL gives (the file or a page about it) needs a look on the PC
fetch_url "https://discmaster.textfiles.com/file/20447/EnigmA%20AMIGA%20RUN%2002%20(1995)(G.R.%20Edizioni)(IT)%5B!%5D%5Bissue%201995-11%5D%5BSkylink%20CD%5D.iso/earcd/patch/ps3b3.lha/Help.LHA/PGS.INT/PGS.pdf" discmaster-ps3b3-PGS.INT-PGS.pdf

echo "SHA-256:"
( cd "$DEST" && for f in *; do [ -f "$f" ] && { sha256sum "$f" 2>/dev/null || shasum -a 256 "$f"; }; done )
[ -z "$FAILED" ] || { echo "not fetched:$FAILED" >&2; exit 1; }
