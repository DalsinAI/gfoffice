# Workstream make fragments

Each workstream in docs/openoffice-parity-plan.md keeps its host test targets in
its own `mk/<stream>.mk` (writer, calc, present, base). The Makefile includes
every `mk/*.mk`.
