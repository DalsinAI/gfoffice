#!/bin/sh
# The SDK headers (FD, clib/, inline/, pragmas/, proto/ and the LVO
# equates) for GFOffice's libraries (libowf/library/sdk: owfwrite.library,
# owfcalc.library, owfpub.library and owfpresent.library from their
# <name>_lib.sfd, and owf.library 2 from owf_lib.sfd) and for the z.library
# calls libowf makes (libowf/amiga/sdk, from z_lib.sfd), with OpenSDK's
# generator (DalsinAI/openamigasdk OpenNDK/tools/ndkgen.py), as OpenNDK
# makes its own; and each of the four libraries' calls (<name>_calls.h,
# the library side of its SFD) with scripts/gen-lib-calls.py. Edit an SFD,
# then run this.
#
#   OPENSDK_SRC   an openamigasdk checkout (default ../openamigasdk)
#
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK=${OPENSDK_SRC:-"$HERE/../openamigasdk"}
LIB="$HERE/libowf/library"
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$LIB/sdk" "$LIB/owf_lib.sfd" "$LIB/owfwrite_lib.sfd" \
    "$LIB/owfcalc_lib.sfd" "$LIB/owfpub_lib.sfd" "$LIB/owfpresent_lib.sfd"
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$HERE/libowf/amiga/sdk" "$HERE/libowf/amiga/z_lib.sfd"
for name in owfwrite owfcalc owfpub owfpresent; do
    python3 "$HERE/scripts/gen-lib-calls.py" "$LIB/${name}_lib.sfd"
done
