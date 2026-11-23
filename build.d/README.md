# Workstream build fragments

`build-os3.sh` sources every `build.d/*.sh` (writer, calc, present, base)
before it copies the toolbar pictures, with its variables (CC, CFLAGS_COMMON,
OWFSRC, OUT, HERE and the rest) set. See docs/openoffice-parity-plan.md.

`calc-extdb.sh` is not a stream: it holds only functions, which `build-os3.sh`
calls before it builds GFOpenCalc (which SQLite its External Data reads
databases with), so the loop sourcing it again does nothing.
