# SPEC: Censor UI — Grid Overlay, Label Palette, Visualization
# Domain: Rapida (UI host), Censor (data provider)
# Phase: v0.1
# Status: Draft

---

## Purpose

The user-facing classification interface inside Rapida. Renders the grid overlay,
label palette, debug visualizations, and confidence coloring. All data comes from
Censor's plugin API — Rapida handles rendering only.

Based on original Vector Classifier spec §6, §8.

---

## 1. Activation

- Hotkey `C` toggles classification mode without switching context.
- Classification mode is an overlay on the existing canvas — does not replace
  the normal viewing/markup mode.
- Active tool persists (if user was drawing rectangles, they still can).
- Classification mode adds the grid overlay, label palette, and confidence
  coloring on top.

---

## 2. Grid Overlay

- Drawn as thin lines (1px, 30% opacity) over the canvas.
- Cells not yet evaluated: no fill.
- Cells with classified clusters: subtle tint based on dominant class.
- Active cell (clicked): highlighted border.
- Grid responds to zoom — lines stay 1px screen-space.

### Cell Click → Popout

- Click a grid cell → popout window appears.
- Popout is an independent ImGui window showing just that cell's contents.
- Resize and zoom the popout independently of the main canvas.
- Clusters in the popout are clickable for labeling.
- Dismiss popout: Esc or click outside.

---

## 3. Label Palette

Persistent floating toolbar. Always visible in classification mode. Docked
to right edge by default, user can move.

```
┌──────────────────────────────┐
│ [W] Wall         ████░░ 8/10 │
│ [D] Duct         ██░░░░ 4/10 │
│ [P] Pipe         █░░░░░ 2/10 │
│ [F] Furniture    ░░░░░░ 0/10 │
│ [C] Ceiling      ██████10/10 │
│ [G] Grid line    ░░░░░░ 0/10 │
│ [T] Text         ░░░░░░ 0/10 │
│ [M] Dimension    ░░░░░░ 0/10 │
│ [E] Equipment    ░░░░░░ 0/10 │
│ [X] Skip                     │
│──────────────────────────────│
│ [+] Custom...                │
└──────────────────────────────┘
```

### Behavior

- Press hotkey (W/D/P/F/C/G/T/M/E) to select active label class.
- Shift-click a cluster in the popout → label applied.
- Progress bar shows count toward `SEEDING_THRESHOLD_PER_CLASS` (10).
- Below threshold: bar partially filled, predictions inactive for that class.
- At threshold: bar full, confidence overlay activates for that class.
- Inline hint: "Need 7 more Wall labels before predictions activate."
- `[X] Skip` — skip this cluster without labeling.
- `[+] Custom...` — text input to create new user-defined label.
- Color swatch per class (user-configurable).

### Label Data Flow

1. User presses label hotkey → active class set in UI state.
2. User shift-clicks cluster in popout → Rapida calls
   `censor_label_cluster(session, cluster_id, label, false)`.
3. Censor stores in SQLite, updates classifier, returns updated overlay.
4. Rapida re-renders overlay with new colors/confidences.

---

## 4. Continuous Click Mode

- **Default: Add mode** — clicking/box-dragging adds clusters to current label.
  Green border indicator.
- **Shift toggles to Remove mode** — clicking/box-dragging removes labels.
  Red/pink border indicator.
- Continuous clicking keeps adding/removing without re-selecting label.
- Box drag selects multiple clusters at once.
- Clear visual indicator of current mode at all times.

---

## 5. Rejection + Reclassification

When the classifier suggests a label and it's wrong:

- `N` alone — rejection. Censor records false positive. Still useful signal.
- `N` then label key (e.g. `N` then `D`) — rejection + reclassification.
  "Not a wall, it's a duct." Two keystrokes, two training events.
- No mandatory friction. `N` alone is always sufficient.

### Implementation

- `N` key → `censor_reject_suggestion(session, suggestion_id, NULL)`
- `N` then `D` → `censor_reject_suggestion(session, suggestion_id, "Duct")`
- UI shows brief "Rejected" / "Reclassified → Duct" feedback.

---

## 6. Confidence Color Overlay

- Each label class has a user-defined color, changeable via palette.
- Overlay is per-cluster, not per-cell — multiple classes visible in one cell.
- Confidence expressed as opacity:
  - High confidence → saturated color
  - Low confidence → washed out
  - Your eye immediately sees where to label next
- Only activates per class once seeding threshold reached.
- Toggle "show predictions" on/off globally.

### Rendering

- Censor returns `CensorOverlayData` with per-cluster color + confidence.
- Rapida renders as semi-transparent filled rects over the canvas.
- Drawn after PDF tiles, before markup annotations.
- Does not interfere with markup selection or drawing tools.

---

## 7. Confusable Negative Surfacing

After labeling a cluster, Censor surfaces 3-5 most similar unlabeled clusters:

- Small popover: "Also a Wall?" with cluster thumbnail/bounds.
- `Y` → confirms. New positive + contextual negatives.
- `N` → rejects. High-value negative.
- `N+label` → rejects + reclassifies. Maximum signal.
- Surfaces lowest confidence predictions first.
- Dismissible — user can ignore and continue.

---

## 8. Debug Visualization Overlay

When classification mode is active, clicking any cluster shows:

- Computed feature vector as readable numbers overlaid on the cluster.
- Stroke weight value.
- Cluster boundary highlight.
- Which weight band this cluster falls in.
- Number of elements in cluster.

This IS the observation tool. The debug overlay is how constants get
calibrated during the seeding run. If a wall and a duct have similar feature
vectors, fix the feature, not the classifier.

---

## 9. Completeness Metric HUD

Persistent display in classification mode:

```
┌──────────────────┐
│ 73% understood   │
│ ████████░░░░     │
└──────────────────┘
```

- Shows percentage of clusters on current page that are classified
  (user-labeled or above confidence threshold).
- Updates live as user labels.
- When it plateaus → signal to open discovery queue.

---

## 10. Discovery Queue

Side panel showing top 10 unknown clusters sorted by distance from all
labeled examples in feature space.

- "Here are things I don't recognize — want to name any of them?"
- Click entry → popout opens at that cluster's location.
- User names one → new class added to palette → completeness moves.
- Furthest from known examples = most interesting.

---

## 11. Session Feedback

End-of-session toast when classifier mode was used:

> "47 elements classified today, model confidence up 12%."

Progress without being intrusive. Data from Censor's SQLite
(count labels added this session, mean confidence delta).

---

## Tasks

### Phase 1 — Grid + Palette (no classifier)

- [ ] TASK-CUI-001: Implement grid overlay rendering — 1px lines at 30% opacity, zoom-independent, cell highlight on hover/click.
- [ ] TASK-CUI-002: Implement cell popout window — independent ImGui window, shows cell contents at larger scale, cluster outlines clickable.
- [ ] TASK-CUI-003: Implement label palette — floating toolbar, 9 default classes + Custom, hotkey activation, progress bars, color swatches.
- [ ] TASK-CUI-004: Wire shift-click labeling — palette active class + cluster click → `censor_label_cluster()` call, overlay refresh.
- [ ] TASK-CUI-005: Implement continuous click mode — add/remove toggle with shift, green/red indicator, box drag multi-select.
- [ ] TASK-CUI-006: Implement `[+] Custom` label creation — text input, adds to palette, assigns next available hotkey.

### Phase 2 — Debug Visualization

- [ ] TASK-CUI-007: Implement feature vector debug overlay — click cluster → show 12 feature values as text overlay on canvas.
- [ ] TASK-CUI-008: Implement stroke weight band visualization — color-code elements by weight band on demand.
- [ ] TASK-CUI-009: Implement cluster boundary highlight — draw bounding rects of clustered elements when cluster is hovered/selected.

### Phase 4 — Classifier Visualization

- [ ] TASK-CUI-010: Implement confidence color overlay rendering — read `CensorOverlayData`, draw semi-transparent filled rects per cluster.
- [ ] TASK-CUI-011: Implement "show predictions" toggle — global on/off for confidence overlay.
- [ ] TASK-CUI-012: Implement per-class color picker — user changes class color in palette, overlay updates.

### Phase 5 — Active Learning UI

- [ ] TASK-CUI-013: Implement confusable negative popover — "Also a Wall?" with Y/N/N+label interaction.
- [ ] TASK-CUI-014: Implement rejection feedback — brief "Rejected" / "Reclassified → Duct" toast on N/N+label.

### Phase 6 — Completeness UI

- [ ] TASK-CUI-015: Implement completeness HUD — percentage bar, live update on label events.
- [ ] TASK-CUI-016: Implement discovery queue panel — top 10 unknowns, click to navigate, label to dismiss.
- [ ] TASK-CUI-017: Implement session feedback toast — label count + confidence delta on session end.

---

<!-- SECTION: TASKS TO DO -->
<!-- SECTION: RUN LOG -->
