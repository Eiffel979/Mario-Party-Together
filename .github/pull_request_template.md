## Summary

<!-- What changes and why. Link the Phase 1 item (e.g. P1-16) and any ADR. -->

## Checklist

- [ ] Scoped to one owner area (`docs/PLAN.md` section 6). Changes to `core/` interfaces have an accepted ADR.
- [ ] No Nintendo material: no dumps, extracted assets, save images, game source or disassembly, in tests and fixtures too (`CLAUDE.md` rule 1). `python tests/compliance/check_repo.py` passes.
- [ ] Determinism: no wall-clock time, unseeded RNG or thread timing in runtime, adapter, rules or input logic.
- [ ] Tests added or updated. CI is green on Windows and Linux.
- [ ] Dump lane (required for `runtimes/gc_wii/`, `adapters/mp4/` or replay format changes, ADR 0006; see `tests/dump_lane/README.md`):
      command: `...` / OS: ... / result: ... / game-source pins: ... (or "not required")
