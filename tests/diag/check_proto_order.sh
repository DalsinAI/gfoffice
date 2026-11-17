#!/bin/sh
# A file on owfdiag.library (OWF_USE_DIAG_LIBRARY) may include the diagram headers in any order:
# include/opendiag_proto.h brings in the library's calls once, after every header is complete. This compiles a
# file that starts with each header in turn, against a stand-in <proto/owfdiag.h> (tests/diag/proto_standin) that
# needs every type the calls take, and with odg_port_point (opendiag_proto.h's, over odg_port_xy).
#   tests/diag/check_proto_order.sh [CC]
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
CC=${1:-cc}
HERE=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
n=0
for h in owf_diagram opendiag_core opendiag_clip opendiag_order opendiag_align opendiag_mind opendiag_style \
         opendiag_stencil opendiag_rexx; do
    cat > "$T/t.c" <<EOT
#include "$h.h"
#include "opendiag_rexx.h"
static owf_dpoint east(const owf_dnum *box) { return odg_port_point(box, ODG_PORT_E); }
int main(void)
{
    owf_dnum box[4] = { 0, 0, 100, 100 };
    return (int)(east(box).x + (long)odg_standin_complete(0, 0, 0, 0, 0, 0, 0, 0, 0)) & 0;
}
EOT
    "$CC" -std=gnu99 -Wall -Werror -fsyntax-only -DOWF_USE_DIAG_LIBRARY -I"$HERE/tests/diag/proto_standin" \
        -I"$HERE/include" -I"$HERE/libowf/include" "$T/t.c" || { echo "check_proto_order.sh: starting with $h.h fails"; exit 1; }
    n=$((n + 1))
done
echo "check_proto_order.sh: the library's calls come once, after every header, whichever of the $n comes first"
