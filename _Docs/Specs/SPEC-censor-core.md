# SPEC: Censor Core — Clustering, Feature Extraction, Classification
# Domain: Censor plugin internals
# Phase: v0.1
# Status: Draft

---

## Purpose

> **POST-PIVOT NOTE (2026-04-13):** This spec was written pre-pivot.
> References to "Tessera display list" and "Tessera query API" throughout
> mean **IVectorEngine element data** post-pivot. Censor consumes
> Rapida's `IVectorEngine` stream via `rapida_vector_engine_c.h` C ABI
> shim — it never links Tessera, MuPDF, or PDFium directly.
> See SPEC-pivot-2026-04-13.md §3 and SPEC-censor-integration.md.
> The algorithms described here are unchanged; only the data source changed.

The classification engine inside Censor. Takes raw vector elements from
the host's vector engine, groups them into spatial clusters, extracts feature
vectors, and classifies them via k-NN with cosine similarity.

This spec covers the build sequence from the original Vector Classifier spec
§3-§7, §9, §11-§12.

---

## 1. Cluster Definition — Composite Spatial Assembly

A cluster is a group of spatially coherent vector elements that together form
a recognizable AEC element (wall assembly, duct run, furniture group, etc.).

### Grouping Rules

1. **Spatial proximity** — elements within `CLUSTER_PROXIMITY_TOLERANCE` points
   of each other's bounding boxes. Named constant, placeholder value, calibrated
   during seeding run.
2. **Stroke weight coherence** — elements in the same weight band grouped
   together. Weight bands determined per-drawing by histogram peak detection
   on all stroke widths (see §2).
3. **Fill containment** — fill paths geometrically contained between parallel
   stroke paths are absorbed into the stroke cluster.

### Execution

- Runs once per page at first access (lazy). Result cached per page.
- Input: all DisplayListElements on the page (via Tessera query API).
- Output: list of `Cluster` objects, each containing element indices,
  pre-computed bounding box, and feature vector.
- Cluster IDs are stable per page per session (index-based).

### Boundary Handling

When a cluster straddles a grid cell boundary, selection snaps to the full
cluster regardless of which cells it crosses. The grid is the UI navigation
mechanism, not a hard constraint on selection. Label applied to a cross-boundary
cluster applies to all cells it touches.

---

## 2. Stroke Weight Calibration — Learned Per Drawing

Do not hardcode a stroke weight threshold. Each new document gets a first-pass
histogram analysis:

1. Collect all stroke widths on the page into a sorted array.
2. Build a histogram (bin width = 0.1pt or similar).
3. Find natural peaks (2-4 distinct weight bands typical in AEC drawings).
4. Assign bands: heaviest = walls, medium = partitions, lighter = MEP,
   lightest = furniture/annotations.

### Implementation

Isolated function with its own unit tests:
```
Input:  float[] stroke_widths
Output: StrokeWeightBands { int band_count; float thresholds[]; }
```

Testable with synthetic data before any real PDF is loaded.

---

## 3. Ceiling Grid Pre-Pass

Ceiling grids are deterministically detectable before classification — regular
repeating lattice, two orthogonal directions, consistent stroke weight.

### Implementation

Isolated module with its own test suite:
```
Input:  float[] horizontal_positions, float[] vertical_positions
Output: CeilingGridResult { bool is_grid; float cell_width; float cell_height; }
```

Run before classification. Detected grid elements removed from the
classification problem entirely — cleaner feature space for everything else.

---

## 4. Feature Vector (per cluster)

| # | Feature | Type | Notes |
|---|---------|------|-------|
| 1 | Total stroke length | float | Sum of all segment lengths in cluster |
| 2 | Dominant orientation | float | Angle in radians, histogram of segment angles |
| 3 | Stroke weight (mean) | float | Mean stroke width of elements in cluster |
| 4 | Stroke weight band | int | Which band from §2 calibration |
| 5 | Closed loop count | int | Number of closed subpaths |
| 6 | Closed loop total area | float | Sum of shoelace areas of closed loops |
| 7 | Connectivity to adjacent cells | int | How many neighboring cells contain part of this cluster |
| 8 | Fill presence | bool→float | 1.0 if any fill element in cluster, 0.0 otherwise |
| 9 | Aspect ratio | float | Bounding box width / height |
| 10 | Bounding box area | float | Width × height of cluster bounds |
| 11 | Element count | int | Number of display list elements in cluster |
| 12 | Parallel offset distance | float | If two dominant parallel strokes detected, distance between them (normalized to drawing scale). 0 if not applicable. |

**Note:** All normalization values are named constants with placeholder values.
Actual values determined during seeding run using debug visualization overlay.

### Deferred features (add if discrimination is poor after seeding)

- Spacing regularity (FFT periodicity)
- Symmetry axes
- Assembly internal structure (parallel face count, offset variance)
- Termination behavior (what do endpoints connect to)

Start with the 12 above. The debug overlay will show which features
discriminate and which are noise.

---

## 5. Grid System

The grid is a spatial sampling mesh. Agnostic, evenly divided, not
scientifically sized.

- **Default:** PDF height × width divided into approximate squares.
  Number of divisions = `GRID_DEFAULT_DIVISIONS` (named constant, placeholder).
- **User control:** Single global slider — coarser ↔ finer. Adjustable anytime.
- **Lazy evaluation:** Cells compute nothing until clicked or queried.
- **Each cell is an independent training sample.** Labeling walls in cells
  4, 5, 6 gives the classifier the same pattern across three independent
  spatial samples.

---

## 6. Classifier — k-NN with Cosine Similarity

### Interface (swappable)

```cpp
class IClassifier {
public:
    virtual ~IClassifier() = default;

    // Predict label for a feature vector.
    virtual ClassifyResult predict(
        const FeatureVector& features) const = 0;

    // Add a labeled example.
    virtual void add_example(
        const FeatureVector& features,
        const std::string& label,
        bool is_negative) = 0;

    // Number of examples per label.
    virtual std::map<std::string, int> label_counts() const = 0;
};

struct ClassifyResult {
    std::string label;        // Predicted label ("" if below threshold)
    float confidence;         // 0.0–1.0
    bool above_threshold;     // True if confidence >= CLASSIFIER_CONFIDENCE_THRESHOLD
};
```

### k-NN Implementation (v1)

- Cosine similarity between query feature vector and all stored examples.
- k = 5 (or `KNN_K`, named constant).
- Weighted voting: closer neighbors have more weight.
- Confidence = fraction of k neighbors agreeing on winning label.
- Per-class activation: predictions for a class only active when that class
  has >= `SEEDING_THRESHOLD_PER_CLASS` (default 10) labeled examples.

### Why k-NN

- Works with small label counts (10 per class is enough).
- No training phase — new label immediately affects predictions.
- Interpretable — "this was classified as a wall because it's most similar
  to these 5 labeled walls."
- Swappable — v2 can be a small neural net behind the same interface.

---

## 7. Active Learning Loop

1. User labels cluster → stored in SQLite via `add_example()`.
2. System finds `CONFUSABLE_NEGATIVE_COUNT` (default 5) most similar
   unlabeled clusters → surfaces as suggestions.
3. User confirms (`Y`) or denies (`N` / `N+label`).
4. Each interaction is a new training event.
5. Loop compounds — corrections sharpen decision boundaries.

### Confusable Negative Selection

- Compute cosine similarity between newly labeled cluster and all unlabeled
  clusters on the same page.
- Return top-K most similar that are NOT already labeled.
- Only surface low-confidence predictions — high-confidence ones don't need
  confirmation.

---

## 8. Named Constants

| Constant | Purpose | Placeholder |
|----------|---------|-------------|
| `CLUSTER_PROXIMITY_TOLERANCE` | Spatial grouping radius (points) | TBD |
| `GRID_DEFAULT_DIVISIONS` | Default grid cells across sheet width | TBD |
| `CLASSIFIER_CONFIDENCE_THRESHOLD` | Minimum confidence for "understood" | 0.80 |
| `SEEDING_THRESHOLD_PER_CLASS` | Labels before predictions activate | 10 |
| `CONFUSABLE_NEGATIVE_COUNT` | Similar clusters to surface after labeling | 5 |
| `KNN_K` | Number of nearest neighbors | 5 |
| `STROKE_HISTOGRAM_BIN_WIDTH` | Bin width for stroke weight analysis (points) | 0.1 |

All TBD values calibrated during seeding run using debug visualization overlay.

---

## 9. Data Storage — SQLite

### `labeled_clusters`

| Column | Type | Notes |
|--------|------|-------|
| id | TEXT (UUID) | |
| feature_vector | BLOB | 12 floats, packed binary |
| label | TEXT | User-defined string — no enum |
| source_file | TEXT | File hash |
| page_number | INTEGER | |
| timestamp | TEXT (ISO 8601) | |
| confidence | REAL | Classifier confidence at label time |
| is_negative | INTEGER | 1 = high-value negative example |

### `pending_suggestions`

| Column | Type | Notes |
|--------|------|-------|
| id | TEXT (UUID) | |
| cluster_ref | TEXT | Reference to geometry (file_hash:page:cluster_id) |
| predicted_label | TEXT | |
| confidence | REAL | |
| confirmed | INTEGER | NULL = pending, 1 = confirmed, 0 = rejected |
| corrected_label | TEXT | Populated if rejected with N+label |

### `pattern_library`

| Column | Type | Notes |
|--------|------|-------|
| id | TEXT (UUID) | |
| label | TEXT | User-defined label string |
| feature_vector | BLOB | Centroid of all examples for this label |
| example_count | INTEGER | |
| created | TEXT (ISO 8601) | |
| modified | TEXT (ISO 8601) | |
| source | TEXT | "local" or "shared" or "import" |

---

## 10. Build Sequence

This is the execution order. Each phase is a gate — don't start the next
until the previous is validated.

### Phase 1: Infrastructure (no classifier)
- Cluster grouping from Tessera display list elements
- Feature vector computation (12 dimensions)
- SQLite storage (schema, read/write)
- Grid system (lazy cell evaluation)
- Label palette data model (open string labels, counts per class)
- All constants as named placeholders

### Phase 2: Debug Visualization
- Feature vector overlay on cluster click (readable numbers)
- Stroke weight histogram visualization
- Cluster boundary visualization (highlight grouped elements)
- This enables the seeding run — no classifier needed yet

### Phase 3: Seeding Run
- Founder opens 3-4 real AEC PDFs (Revit + AutoCAD mix)
- Labels 10+ examples per class using the UI
- Reads feature vector values via debug overlay
- Updates named constants to real values
- Validates stroke weight calibration across different drawings

### Phase 4: Classifier
- k-NN wired against real seeding data
- Confidence calculation
- Per-class activation at seeding threshold
- Confidence color overlay (per-cluster, opacity = confidence)
- Background worker for progressive rendering

### Phase 5: Active Learning
- Confusable negative surfacing after each label
- Two-keystroke rejection + reclassification (N / N+label)
- Suggestion queue sorted by lowest confidence first

### Phase 6: Completeness + Discovery
- Completeness metric HUD (% understood per page)
- Discovery queue (top 10 unknown clusters by feature distance)
- End-of-session toast ("47 elements classified, confidence up 12%")

---

## Tasks

### Phase 1 — Infrastructure

- [ ] TASK-COR-001: Define `Cluster` struct — element indices, bounds, feature vector, cluster ID.
- [ ] TASK-COR-002: Implement stroke weight histogram analysis — input: float array of widths, output: StrokeWeightBands with peak detection. Unit tests with synthetic data.
- [ ] TASK-COR-003: Implement ceiling grid pre-pass — input: horizontal/vertical positions, output: is_grid + cell dims. Unit tests.
- [ ] TASK-COR-004: Implement spatial clustering — proximity grouping with stroke weight coherence and fill containment. Input: Tessera element array. Output: cluster list.
- [ ] TASK-COR-005: Implement feature vector extraction — compute 12 features per cluster. Named constants as placeholders.
- [ ] TASK-COR-006: Create SQLite schema — `labeled_clusters`, `pending_suggestions`, `pattern_library` tables.
- [ ] TASK-COR-007: Implement SQLite read/write — insert label, query examples by label, count per class, export to file.
- [ ] TASK-COR-008: Implement grid system — page dimensions → cell grid, lazy evaluation, cell ↔ cluster mapping.

### Phase 2 — Debug Visualization

- [ ] TASK-COR-009: Implement feature vector overlay data — click cluster → return feature vector as structured data for Rapida to render.
- [ ] TASK-COR-010: Implement stroke weight histogram data — return histogram + detected bands as overlay data.
- [ ] TASK-COR-011: Implement cluster boundary overlay data — return bounding rects of all clusters in viewport.

### Phase 4 — Classifier

- [ ] TASK-COR-012: Define `IClassifier` interface — predict(), add_example(), label_counts().
- [ ] TASK-COR-013: Implement k-NN classifier — cosine similarity, weighted voting, per-class activation threshold.
- [ ] TASK-COR-014: Implement confidence color overlay data — per-cluster color (class color at confidence opacity).
- [ ] TASK-COR-015: Implement background classification worker — progressive per-region, updates overlay as regions complete.

### Phase 5 — Active Learning

- [ ] TASK-COR-016: Implement confusable negative selection — find K most similar unlabeled clusters after each label event.
- [ ] TASK-COR-017: Implement suggestion storage — pending_suggestions table, confirm/reject/reclassify.

### Phase 6 — Completeness

- [ ] TASK-COR-018: Implement completeness metric — count understood vs total clusters per page.
- [ ] TASK-COR-019: Implement discovery queue — top N unknown clusters sorted by feature space distance from all labeled examples.

---

<!-- SECTION: TASKS TO DO -->
<!-- SECTION: RUN LOG -->
