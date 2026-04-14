# Archived Censor Specs

This directory holds specs superseded by the 2026-04-13 architectural pivot. They are kept as historical record only.

## Policy

Per locked decision from the 2026-04-13 pivot: **supersede + banner + fresh rewrite — no amendments.** When a spec is obsoleted, it moves here, a banner is added to the top, and any surviving content gets pulled into a fresh spec in `_Docs/Specs/`. Original specs in this directory must not be edited.

## What changed for Censor

Before the pivot, Censor was designed as a downstream plugin consuming Tessera's display list via a Tessera/Censor contract. The pivot flipped that: Tessera is dormant, and Censor is now Rapida's **semantic sidecar** — it sits beside Rapida's PDF engine (PDFium primary, MuPDF second implementation) and consumes the new `IVectorEngine` stream directly.

Surviving Censor specs (unchanged in `_Docs/Specs/`):

- `SPEC-censor-core.md` — clustering, 12-dim feature vector, k-NN, active learning, SQLite, grid system. All still valid. Fresh rewrite later to remove Tessera-display-list assumptions and add "Censor owns its own engine device bridge."
- `SPEC-censor-ui.md` — grid overlay, label palette, debug visualization. All still valid.
- `SPEC-censor-plugin.md` — DLL-plugin architecture. Confirmed direction. Fresh rewrite later: data source is Censor's own engine bridge, not Tessera queries.

See `~/gt/Rapida/mayor/rig/_Docs/Specs/SPEC-pivot-2026-04-13.md` (in progress) for the complete pivot record.
