# Workstream build fragments

`build-os3.sh` sources every `build.d/*.sh` (writer, calc, present, base)
before it copies the toolbar pictures, with its variables (CC, CFLAGS_COMMON,
OWFSRC, OUT, HERE and the rest) set. See docs/openoffice-parity-plan.md.
