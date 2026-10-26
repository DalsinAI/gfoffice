#!/bin/sh
# fetch_stress_samples.sh [--extract] DIR: downloads the real Final Writer,
# Wordworth, ProWrite and PageStream documents that tests/amiga/fixtures/
# PROVENANCE.md and tests/pub/stress/PROVENANCE.md list but don't check in
# (their licences don't say we may redistribute them), as Aminet serves them,
# into DIR, with their readmes, and checks each archive's SHA-256.
#
# With --extract, each archive is also unpacked into DIR/NAME/ with 7z (which
# keeps paths inside DIR), or lha when 7z is missing. Then
#     owconvert --report "DIR/FW_ACMsys/ACE-System" out.odt
# reads one. Aminet is not reachable from the agents' shell; run it on a PC.
#
# Needs curl and sha256sum (or shasum).
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -u

EXTRACT=0
[ "${1:-}" = "--extract" ] && { EXTRACT=1; shift; }
[ $# -eq 1 ] || { echo "usage: $0 [--extract] DIR" >&2; exit 2; }
DEST=$1
mkdir -p "$DEST" || exit 1
FAILED=""

sha() { sha256sum "$1" 2>/dev/null || shasum -a 256 "$1"; }

# path on Aminet, SHA-256 of the archive (fetched 25 October 2026)
while read -r path sum; do
    [ -n "$path" ] || continue
    name=$(basename "$path")
    if [ ! -s "$DEST/$name" ]; then
        curl -fsSL --retry 2 -o "$DEST/$name" "https://aminet.net/$path" ||
            { FAILED="$FAILED $name"; rm -f "$DEST/$name"; continue; }
    fi
    curl -fsSL --retry 2 -o "$DEST/${name%.*}.readme" "https://aminet.net/${path%.*}.readme" || true
    got=$(sha "$DEST/$name" | cut -d' ' -f1)
    if [ "$got" != "$sum" ]; then
        echo "$name: SHA-256 $got, expected $sum (Aminet's copy changed?)" >&2
        FAILED="$FAILED $name"
        continue
    fi
    if [ $EXTRACT -eq 1 ]; then
        out="$DEST/${name%.*}"
        mkdir -p "$out"
        # 7z first; lha for the old methods 7z lacks (-lh1-, in .lzh files)
        if command -v 7z >/dev/null && 7z x -y -bd -o"$out" "$DEST/$name" >/dev/null 2>&1; then
            :
        elif command -v lha >/dev/null && ( cd "$out" && lha xqf "../$name" ); then
            :
        else
            echo "$name: not extracted (7z and lha missing or failed)" >&2
            FAILED="$FAILED $name(extract)"
        fi
    fi
    echo "$path ok"
done <<'EOF'
dev/c/FW_ACMdev.lha 5dff9ad6956576b5df117be81f42d1601ba40f4014b5c9e9fbe751ea006be42e
dev/c/FW_ACMgfx.lha 567fc52d9a512e4e2ecb0fcaebdd7fc615ed3002b79797e7633555e6a7465ea7
dev/c/FW_ACMint.lha 4cb238152836161e881b67b4bd6d7f870afdba9b2ba94c534ce55e17599bd4d1
dev/c/FW_ACMsys.lha 0d61ba451992303d380619ac864d881b7ff4a825dc83f2144c4ab0467f9ec6b0
biz/dbase/GenTreeFWDocs.lzh 21decadd0ab3ceaca010b9b4b09bc7b3ab7564ccc7619010812b4bb4e06e7ed8
text/fwrit/Vorlagen.FW.lha cd5b22a5932d200d95020ee96e8925bdc37cc6342b2c627579a1f109b3ace7f3
text/fwrit/FinalTable.lha 21c76ffa898d08a58787bebefaadac801c0b44e8067eaf66fbb2388578600a2e
text/fwrit/FWEtikett3350.lha 5308bf9450154fd9fedfbb28d0d10b16c2421823da5c07b7b85833b2e7e3ab2c
text/dtp/CDCover.lha 2bc1897d45ac0a4afa210287cb923c36fdf7595193c3f74814ba57aaf379cf95
dev/e/E_Ref.ww4-7.lha dc1f5e3afbae56847ecadf44eb595c9af741766cd462a7b5b72d474261f86da9
text/dtp/WW6_Anl1_00.lha d5400f3ad431f11a0b08ae4f022d70b85363212df4d5bac5d71799e235ebf2cf
docs/misc/ALEM_0019.lha c7c3d11854c588c6e2165fdcaee5603515fa05863a27a1d5c33742361b40dccc
docs/misc/ALEM_0025.lha c29b07db45c297abf59c568ede1068c4112010c52c22ee41eb3e9d64bbf88671
docs/misc/TapeLabel.lha 3b3cc88a692d540a2e36f5978076ceaa813c805533a9918603d7d966cfe8825f
docs/misc/Ww2008CalendarITA.lha aa1f4be99e3319a0d6402646e61b79694bf73247697d1c5d06341a39fcf0ad6e
docs/misc/pwrdbamy.lha 46a2ae99bb928254425ca4d4df4ad6c00c989d611753d91c03ffaf3d30c818a6
text/dtp/WW-Farben.lha 59f461e4abf38484383e4526c22760d6af5641bad59284803655d764193bf8a7
text/dtp/WordWorth2demo.lha 9782e971b7c571977930b3af932c49c9415dcb095b14e67e38ef3043dd624221
text/misc/pwconv202.lha 6475cb1c2cb422730adbb13a80f4538c2a843785a6cf37700ba59bead547145d
text/dtp/8mmlabel.lha ee72c602be9e9f237b481db13fa0e2ac7199fb86dd62ba88f0272bf6901d0450
mags/misc/ahug0193.lha 9bbbc3a997baa33a40156315f3c872c65a8f0bf786c299191f52f310b6455ef9
mags/misc/daug0193.lha 6d27bc1d3e5029d04dead5ac9253c3f713389a9b7b6bdae0b82fe2d22f5b5eac
mags/misc/daug0293.lha 7931706e16746d07af0fd3be8e5d8a45a662f6aac377db626bb73e5d351a65fc
mags/misc/daug0393.lha 4ecebea0290d6ac921554057cf4cea313009ebce324221a99e53934349d2f3a8
util/misc/OpticalMousepd.lha 57d8a4e99b1557c57d9a5db831beb725eaeec4ec42605a8f233d83cbdeb4a969
game/role/CharacterSheet.lha 16482b53577383497a7de492663192a27772af6d2957b27a7b41e891760926fd
text/dtp/BusinessCards.lha 289a99273f2d9298230157056a1c5f261ed4173c44276bb29c5952fcebba1e3f
text/dtp/PgS3sampleDoc.lha 85f7ddb6ea03dca58b3f4326fac8a85707910631248daf1ac9bf01a8fd4bd13c
mags/misc/FLAK9505.lha 852174ab6b305c15efa71a9620029c12e66dba9c9c43a93b4e9160b0aca0560b
mags/misc/FLAK9701.lha bf29c56ce89aaf836137a1c42941370a337bd9d7d0272181749654bc842fe052
misc/sci/PeriodicTable.lha 941888a0f38fcc95c1fad9971f0f14545e24ebf445b352a2a9404b6576a90f26
text/dtp/ProWriteDEMO.lzh 9c219bbb7e3908ff5fc460b2a909ec6d64e063482dd048cf10341c07993bedeb
text/dtp/pgs22demo.lha 27ca18de945c675d8f38c84260e19555509a686330923e2f9f65040dcbace2d2
text/fwrit/MultiRead.lha 5769f2f1951674271dfd5d996aab639412fa23a9f7ebed741d9d9c6ee62a2dc7
EOF

[ -z "$FAILED" ] || { echo "not fetched or not checked:$FAILED" >&2; exit 1; }
