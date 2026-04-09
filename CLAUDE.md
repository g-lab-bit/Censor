# Censor — Project Context for Claude

## What This Is

Censor is an AEC vector element classifier that ships as an independent plugin
(DLL). It identifies walls, ducts, pipes, furniture, ceiling grids, and
user-defined element types from PDF vector primitives provided by Tessera's
display list.

**Core principle:** AEC drawings encode meaning in geometry. Censor reads that
geometry directly — no rasterization, no pixel ML. Clean vector features,
k-NN classification, ambient labeling UX.

**Ships as:** `censor.dll` — Rapida loads it dynamically. Can be sold or
distributed separately.

---

## Architecture

```
Rapida (host)  ──loads──>  Censor (plugin DLL)
       │                          │
       │                    reads vector data via
       └──> Tessera ──────> tessera_query_elements() API
```

- Censor never links Tessera directly. Rapida mediates via host callbacks.
- Censor is read-only — never modifies Tessera's display list or Rapida's document.
- All classification data stored in local SQLite (`%APPDATA%/Censor/censor.db`).

---

## Stack Decisions (locked)

| Decision | Choice | Reason |
|----------|--------|--------|
| Language | C++17 internal, C API boundary | Matches Tessera/Rapida pattern |
| Build | CMake | Consistent with stack |
| Classifier v1 | k-NN cosine similarity | Works with small label counts, no training phase, interpretable |
| Classifier interface | Swappable (IClassifier) | v2 can be neural net without API change |
| Storage | SQLite (amalgamation) | Zero external deps, ships inside DLL |
| Labels | Open string, no enum | Firms have their own vocabulary |
| Spatial index | Query via Tessera R-tree | Don't duplicate — use the contract API |
| Test framework | Google Test | Standard |

---

## Build Sequence (gates — don't skip ahead)

1. **Plugin scaffold** — CMake, DLL target, public API header, SQLite
2. **Tessera contract** — query API in Tessera, Rapida host callbacks
3. **Clustering + features** — spatial grouping, 12-dim feature vector, grid
4. **Debug overlay** — feature vectors on click, stroke weight bands
5. **Seeding run** — calibrate constants on real PDFs
6. **Classifier** — k-NN, confidence overlay, per-class activation
7. **Active learning** — confusable negatives, N/N+label reclassification
8. **Completeness** — % understood HUD, discovery queue

---

## Specs

| Spec | Covers |
|------|--------|
| SPEC-censor-tessera-contract | Tessera query API — how Censor reads vector data |
| SPEC-censor-plugin | Plugin architecture — DLL lifecycle, Rapida integration |
| SPEC-censor-core | Clustering, feature extraction, k-NN, active learning, SQLite |
| SPEC-censor-ui | Grid overlay, label palette, debug viz, confidence coloring |

---

## What NOT to Do

- Do not link Tessera at compile time — use host callbacks only
- Do not hardcode stroke weight thresholds — learn per drawing
- Do not use fixed label enums — open string from day one
- Do not build classifier before debug overlay exists — seeding run needs it
- Do not build active learning before basic classifier works
- Do not add fill tools (fill copy, area calc, revision diff) — those are
  separate Rapida features that consume Censor output later


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
