# Censor — Project Context for Claude

## What This Is

Censor is an AEC vector element classifier that ships as an independent plugin
(DLL). It identifies walls, ducts, pipes, furniture, ceiling grids, and
user-defined element types from PDF vector primitives that Rapida extracts
from its active engine (PDFium primary, MuPDF second) and hands across a
plain C ABI shim.

**Core principle:** AEC drawings encode meaning in geometry. Censor reads that
geometry directly — no rasterization, no pixel ML. Clean vector features,
k-NN classification, ambient labeling UX.

**Ships as:** `censor.dll` — Rapida loads it dynamically. Can be sold or
distributed separately.

---

## Architecture

```
Rapida (host)                            Censor (plugin DLL)
     │                                         │
     │  rapida_vector_engine_c.h  (C ABI)      │
     │ ─────────────────────────────────────>  │
     │   - enumerate pages / primitives        │
     │   - host callbacks (log, progress,      │
     │     cancel, producer metadata)          │
     │ <─────────────────────────────────────  │
     │   IVectorEngine handoff back to Censor  │
```

- Rapida owns the PDF parsing (PDFium/MuPDF). Censor never links libmupdf or
  libpdfium — it only consumes primitives through the C ABI shim.
- The boundary is plain C (no C++ symbols across the DLL edge) so Censor can
  be built with a different compiler/toolchain than Rapida.
- Censor is read-only — it never mutates Rapida's document state.
- Censor owns its own spatial index (R-tree via nanoflann, built lazily off
  the tile hot path) — it does NOT reuse a Rapida/Tessera index.
- All classification data stored in local SQLite (`%APPDATA%/Censor/censor.db`).
- On load failure or ABI mismatch, Rapida falls back to rendering without
  Censor (graceful degradation, per SPEC-censor-integration).

---

## Stack Decisions (locked)

| Decision | Choice | Reason |
|----------|--------|--------|
| Language | C++17 internal, plain C at DLL boundary | C++ has no stable binary ABI across compilers |
| Build | CMake | Consistent with stack |
| Classifier v1 | k-NN cosine similarity | Works with small label counts, no training phase, interpretable |
| Classifier interface | Swappable (IClassifier) | v2 can be neural net without API change |
| Storage | SQLite (amalgamation) | Zero external deps, ships inside DLL |
| Labels | Open string, no enum | Firms have their own vocabulary |
| Spatial index | Own R-tree (nanoflann), lazy | Built on first interactive query, off the tile hot path (SPEC-pivot §6 Phase 3) |
| Producer metadata | Table copied from Tessera, not code | Static lookup of AutoCAD/Revit/Bluebeam producers |
| Test framework | Google Test | Standard |

---

## Current State (2026-04-16)

**Built and on main:**
- `include/censor_abi.h` — 5 exported C ABI functions + RapidaHostCallbacks
- `src/censor_plugin.cpp` — stub DLL with poison banner, atomic state mgmt
- `tests/test_poison_banner.cpp` — 7 GTest cases
- `tests/test_smoke_dlopen.cpp` — 2 GTest cases (dlopen lifecycle + missing DLL fallback)
- `CMakeLists.txt` — C++17, shared lib target, GTest v1.14 via FetchContent

**Not built yet:** clustering, feature extraction, k-NN, SQLite, grid,
active learning, debug viz, completeness — all tracked as beads.

## Build

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build
```

## Adding a Module

Source files go in `src/<module>/` (clustering, storage, classifier, grid,
overlay). All source files are part of the `censor` shared library target.
Test executables: one per module (`test_<module>`), linking censor + GTest.
Shared types: `#include "censor_types.h"` (src/censor_types.h).
Test fixtures: `#include "test_utils.h"` (tests/test_utils.h).

## Spec Trust Hierarchy

When specs conflict, trust in this order:
1. **SPEC-censor-integration.md** (Rapida rig) — authoritative C ABI contract
2. **SPEC-censor-core.md** — algorithms (clustering, features, k-NN, SQLite)
3. **SPEC-censor-ui.md** — UI data APIs (Rapida renders, Censor provides data)
4. **SPEC-censor-plugin.md** — §2-§5 are STALE (pre-pivot Tessera API).
   §1, §6-§8 still valid.

**"Tessera" in specs = IVectorEngine post-pivot.** Censor consumes
`rapida_vector_engine_c.h`, never links Tessera/MuPDF/PDFium.

---

## Specs

| Spec | Rig | Covers |
|------|-----|--------|
| SPEC-censor-integration | Rapida | **Authoritative.** Rapida↔Censor DLL boundary: loading, C ABI shim 6 entry points, host callbacks, IVectorEngine handoff, graceful fallback |
| SPEC-censor-plugin | Censor | Plugin architecture — DLL lifecycle, internal layout |
| SPEC-censor-core | Censor | Clustering, feature extraction, k-NN, active learning, SQLite |
| SPEC-censor-ui | Censor | Grid overlay, label palette, debug viz, confidence coloring |

SPEC-censor-integration lives in the Rapida rig (`Rapida/_Docs/Specs/`) because
Rapida owns the C ABI header — Censor consumes it. SPEC-censor-tessera-contract
has been archived (pre-pivot, Tessera is dormant as of `tessera-v0-final`).

---

## What NOT to Do

- Do not link libmupdf or libpdfium at compile time — consume primitives
  through `rapida_vector_engine_c.h` only
- Do not expose C++ symbols across the DLL boundary — plain C or nothing
- Do not hardcode stroke weight thresholds — learn per drawing
- Do not use fixed label enums — open string from day one
- Do not build classifier before debug overlay exists — seeding run needs it
- Do not build active learning before basic classifier works
- Do not add fill tools (fill copy, area calc, revision diff) — those are
  separate Rapida features that consume Censor output later

---

## Spec / Doc Edit Discipline

Docs have no compiler. Specs, CLAUDE.md files, and roadmap/strategy
documents are the most failure-prone writes in this repo because
nothing automated tells you when a claim is wrong. When editing
any such file here, follow the rule:

1. **Grep-verify every factual claim before writing it.** Paste
   the grep result into chat before the Edit tool call.
2. **Read the target file's current state.** Do not rely on recall.
3. **One fix per commit.** Small reviewable diffs.
4. **Show the diff in chat before push.** Actual `git diff
   --cached` output, not a summary.
5. **Post-fix grep must return the expected result.**
6. **Scope discipline.** Fixes only; drive-by rewrites file a bead.

See `~/.claude/projects/-home-giacomo-gt-mayor/memory/feedback_spec_fix_workflow.md`
for the durable version of the rule.


<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:ca08a54f -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

## Session Completion

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd dolt push
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
<!-- END BEADS INTEGRATION -->
