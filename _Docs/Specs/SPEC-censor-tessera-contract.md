# SPEC: Censor ↔ Tessera API Contract
# Domain: Censor (consumer), Tessera (provider)
# Phase: v0.1
# Status: Draft

---

## Purpose

Define the API contract between Censor (vector element classifier plugin) and
Tessera (rendering engine). Censor needs read-only access to Tessera's display
list — the vector primitives, stroke properties, fill colors, and spatial index
that the classifier's feature extraction depends on.

Tessera already has all the data internally (DisplayListElement, PathSegment,
StrokeStyle, R-tree). This contract defines how to expose it through a stable
C API boundary so Censor can ship as a separate DLL.

---

## 1. Design Principles

- **Read-only.** Censor never writes to Tessera's display list.
- **C API boundary.** Both sides can ship as independent DLLs. No C++ types
  cross the boundary. No shared headers beyond this contract.
- **Iterator pattern.** Censor iterates elements — does not get a copy of the
  entire display list. Keeps memory footprint bounded.
- **Viewport queries.** Censor can query elements in a rect (leverages Tessera's
  existing R-tree). The grid system queries one cell at a time.
- **Lazy.** Tessera builds the display list on first access per page (already
  does this). Censor triggers it by requesting elements.

---

## 2. New Tessera Public API Functions

### 2.1 Element Query

```c
// Opaque handle to a query result set
typedef struct TesseraQueryResult TesseraQueryResult;

// Query all elements on a page within a viewport rect.
// Pass NULL viewport to get all elements on the page.
// Returns opaque result handle. Caller must free with tessera_query_free().
TESSERA_API TesseraQueryResult* tessera_query_elements(
    TesseraDoc* doc,
    int page,
    const TesseraRect* viewport,  // NULL = entire page
    TesseraError* err
);

// Number of elements in result set.
TESSERA_API int tessera_query_count(const TesseraQueryResult* result);

// Free query result.
TESSERA_API void tessera_query_free(TesseraQueryResult* result);
```

### 2.2 Element Accessors

```c
// Element type enum (matches internal ElementType)
typedef enum {
    TESSERA_ELEM_PATH = 0,
    TESSERA_ELEM_TEXT = 1,
    TESSERA_ELEM_IMAGE = 2,
    TESSERA_ELEM_FORM_XOBJ = 3,
    TESSERA_ELEM_SHADING = 4,
} TesseraElementType;

// Bounding rect
typedef struct {
    float x0, y0, x1, y1;
} TesseraRect;

// Color
typedef struct {
    float r, g, b, a;
} TesseraColor;

// Get element type at index.
TESSERA_API TesseraElementType tessera_query_elem_type(
    const TesseraQueryResult* result, int index);

// Get element bounding rect at index.
TESSERA_API TesseraRect tessera_query_elem_bounds(
    const TesseraQueryResult* result, int index);

// Get fill color. Returns {0,0,0,0} if element has no fill.
TESSERA_API TesseraColor tessera_query_elem_fill_color(
    const TesseraQueryResult* result, int index);

// Get stroke color. Returns {0,0,0,0} if element has no stroke.
TESSERA_API TesseraColor tessera_query_elem_stroke_color(
    const TesseraQueryResult* result, int index);

// Get stroke width. Returns 0 if element has no stroke.
TESSERA_API float tessera_query_elem_stroke_width(
    const TesseraQueryResult* result, int index);

// Get affine transform [a,b,c,d,e,f].
TESSERA_API void tessera_query_elem_transform(
    const TesseraQueryResult* result, int index, float out[6]);

// Get handler flags (FLAG_SHX_CLUSTER, FLAG_REDUNDANT, etc.)
TESSERA_API uint32_t tessera_query_elem_flags(
    const TesseraQueryResult* result, int index);
```

### 2.3 Path Geometry Access

```c
// Path segment type
typedef enum {
    TESSERA_SEG_MOVE_TO = 0,
    TESSERA_SEG_LINE_TO = 1,
    TESSERA_SEG_BEZIER_TO = 2,
} TesseraSegmentType;

// Path segment
typedef struct {
    TesseraSegmentType type;
    float x, y;           // endpoint
    float cx1, cy1;       // control point 1 (bezier only)
    float cx2, cy2;       // control point 2 (bezier only)
    int close;            // 1 = subpath closes after this segment
} TesseraPathSegment;

// Get path segment count for element at index.
// Returns 0 for non-path elements.
TESSERA_API int tessera_query_elem_path_count(
    const TesseraQueryResult* result, int index);

// Get path segment at segment_index for element at index.
TESSERA_API TesseraPathSegment tessera_query_elem_path_segment(
    const TesseraQueryResult* result, int index, int segment_index);

// Bulk copy all path segments for element at index into caller's buffer.
// Buffer must hold at least tessera_query_elem_path_count() segments.
// Returns number of segments copied.
TESSERA_API int tessera_query_elem_path_segments(
    const TesseraQueryResult* result, int index,
    TesseraPathSegment* out_segments, int out_capacity);
```

### 2.4 Text Access

```c
// Get font name for text element. Returns "" for non-text.
// Writes into caller's buffer. Returns required length.
TESSERA_API int tessera_query_elem_font_name(
    const TesseraQueryResult* result, int index,
    char* out_buf, int buf_size);

// Get font size for text element. Returns 0 for non-text.
TESSERA_API float tessera_query_elem_font_size(
    const TesseraQueryResult* result, int index);
```

---

## 3. Versioning

- Contract version stored as `TESSERA_QUERY_API_VERSION` constant (start at 1).
- `tessera_query_api_version()` function returns current version.
- Censor checks version at init. Refuses to load if version < minimum required.
- New fields added as new accessor functions — never change existing function
  signatures. Forward compatible.

---

## 4. Threading

- `tessera_query_elements()` may trigger display list build if page not yet
  prepared. This is a potentially slow operation on first call per page.
- Once built, query result is read-only. Multiple threads can read the same
  `TesseraQueryResult` concurrently.
- Each `TesseraQueryResult` is an independent snapshot — Censor can hold
  multiple open simultaneously.

---

## 5. Memory

- `TesseraQueryResult` holds pointers into Tessera's internal display list.
  The display list is pinned in memory while any query result referencing it
  is alive. Censor must call `tessera_query_free()` when done.
- Path segments are not copied until `tessera_query_elem_path_segments()` is
  called. Index-based access (`tessera_query_elem_path_segment()`) reads
  directly from the internal buffer.

---

## 6. What This Contract Does NOT Include

- Write access to display list (Censor is read-only)
- Tile rendering (Censor doesn't render — Rapida handles that)
- Document lifecycle (Rapida owns TesseraDoc, passes it to Censor)
- OCG / layer information (deferred — Tessera doesn't expose layers yet)
- Transparency group parameters (deferred to Tessera v0.3)

---

## 7. Known Limitations (v0.1)

- **Bezier control points zeroed.** PDFium public API doesn't expose them.
  Tessera v0.2 linearizes curves. Censor's feature extraction works on
  endpoints — acceptable for v1 classification accuracy. 94.3% of AEC
  paths are straight lines anyway (corpus data).
- **Text content not available.** Tessera deferred `FPDFTextObj_GetText()`
  (task te-el7). Font name and size available. Censor can classify text
  elements by font/size/position without content for v1.
- **Form XObjects flattened.** Tessera renders Form XObjects to bitmap and
  emits as fill_image. Censor sees them as images, not vector geometry.
  Acceptable — Form XObjects are rare in AEC files (<1% per corpus data).

---

## Tasks

- [ ] TASK-CON-001: Define TesseraRect, TesseraColor, TesseraElementType, TesseraPathSegment structs in new `tessera_query.h` public header.
- [ ] TASK-CON-002: Implement `tessera_query_elements()` — viewport rect query against existing R-tree, return opaque result handle wrapping vector of element pointers.
- [ ] TASK-CON-003: Implement element property accessors (type, bounds, colors, stroke_width, transform, flags).
- [ ] TASK-CON-004: Implement path segment accessors (count, single segment, bulk copy).
- [ ] TASK-CON-005: Implement text accessors (font_name, font_size).
- [ ] TASK-CON-006: Implement `tessera_query_api_version()` and version constant.
- [ ] TASK-CON-007: Write unit tests — query empty page, query populated page, viewport filtering, path segment access, bulk copy, thread safety with concurrent reads.

---

<!-- SECTION: TASKS TO DO -->
<!-- SECTION: RUN LOG -->
