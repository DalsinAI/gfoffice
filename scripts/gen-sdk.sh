#!/bin/sh
# The SDK headers (FD, clib/, inline/, pragmas/, proto/ and the LVO
# equates) for owf.library (libowf/library/sdk, from owf_lib.sfd) and for
# the z.library calls libowf makes (libowf/amiga/sdk, from z_lib.sfd), with
# OpenSDK's generator (DalsinAI/openamigasdk OpenNDK/tools/ndkgen.py), as
# OpenNDK makes its own. Edit an SFD, then run this.
#
#   OPENSDK_SRC   an openamigasdk checkout (default ../openamigasdk)
#
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK=${OPENSDK_SRC:-"$HERE/../openamigasdk"}
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$HERE/libowf/library/sdk" "$HERE/libowf/library/owf_lib.sfd"
python3 "$SDK/OpenNDK/tools/ndkgen.py" --out "$HERE/libowf/amiga/sdk" "$HERE/libowf/amiga/z_lib.sfd"
