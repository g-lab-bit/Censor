# SPEC: Censor Plugin Architecture
# Domain: Censor (plugin), Rapida (host)
# Phase: v0.1
# Status: Draft

---

## Purpose

Censor ships as an independent DLL/shared library that Rapida loads dynamically.
This spec defines the plugin boundary — how Rapida discovers, loads, and
communicates with Censor without depending on it at compile time.

Censor can be sold, shipped, or updated independently of Rapida.

---

## 1. Plugin Identity

- **Name:** Censor
- **Namespace:** `censor_` (all public C API functions)
- **Build target:** `censor.dll` (Windows), `libcensor.so` (Linux), `libcensor.dylib` (macOS)
- **Language:** C++ internally, C API at boundary (same pattern as librapida and Tessera)
- **License:** Proprietary (separate from Rapida)

---

## 2. Plugin Lifecycle API

```c
// Plugin initialization. Called once after Rapida loads the DLL.
// Returns 0 on success, non-zero on error.
CENSOR_API int censor_init(const CensorConfig* config);

// Plugin shutdown. Called before Rapida unloads the DLL.
CENSOR_API void censor_shutdown(void);

// Plugin version.
CENSOR_API const char* censor_version(void);

// Minimum Tessera query API version required.
CENSOR_API int censor_required_tessera_version(void);
```

### CensorConfig

```c
typedef struct {
    const char* data_dir;       // Path to Censor's SQLite storage directory
    const char* pattern_dir;    // Path to pattern library directory (import/export)
    int enable_debug_overlay;   // 1 = debug visualization available
} CensorConfig;
```

---

## 3. Document Attachment

Censor attaches to an open document. Rapida owns the TesseraDoc handle and
passes it to Censor. Censor holds a read-only reference.

```c
// Opaque handle to a Censor session (one per open document).
typedef struct CensorSession CensorSession;

// Attach Censor to an open document.
// tessera_doc: the TesseraDoc* that Rapida already holds.
// Returns session handle. Caller must free with censor_detach().
CENSOR_API CensorSession* censor_attach(
    void* tessera_doc,    // TesseraDoc* (void* to avoid header dependency)
    const char* file_id,  // Stable identifier for this file (hash or path)
    int* err_code
);

// Detach Censor from document. Flushes pending labels to SQLite.
CENSOR_API void censor_detach(CensorSession* session);
```

---

## 4. Rapida Integration Points

### 4.1 Rapida → Censor (host calls plugin)

| Event | Censor API call | Notes |
|-------|----------------|-------|
| Document opened | `censor_attach()` | Pass TesseraDoc handle |
| Document closed | `censor_detach()` | Flush labels to SQLite |
| User activates classifier mode | `censor_set_mode(session, CENSOR_MODE_ACTIVE)` | Enables grid overlay |
| User deactivates classifier mode | `censor_set_mode(session, CENSOR_MODE_PASSIVE)` | Hides grid, keeps data |
| User clicks grid cell | `censor_activate_cell(session, page, cell_x, cell_y)` | Triggers lazy clustering |
| User labels cluster | `censor_label_cluster(session, cluster_id, label, is_negative)` | Stores in SQLite |
| User rejects suggestion | `censor_reject_suggestion(session, suggestion_id, corrected_label)` | N or N+label |
| User confirms suggestion | `censor_confirm_suggestion(session, suggestion_id)` | Y |
| Viewport changes | `censor_set_viewport(session, page, rect)` | Updates overlay region |
| Render frame | `censor_get_overlay(session, page, rect, ...)` | Returns overlay data for rendering |

### 4.2 Censor → Rapida (plugin calls host via callbacks)

```c
// Callback table that Rapida provides to Censor at init.
typedef struct {
    // Request Tessera query for a page + viewport.
    // Censor calls this instead of linking Tessera directly.
    void* (*query_elements)(void* tessera_doc, int page,
                            const float* viewport_rect);

    // Notify Rapida that overlay data changed (trigger re-render).
    void (*invalidate_overlay)(int page);

    // Show toast notification to user.
    void (*show_toast)(const char* message);

} CensorHostCallbacks;

CENSOR_API void censor_set_host_callbacks(const CensorHostCallbacks* callbacks);
```

**Why callbacks instead of direct Tessera linking:** Censor doesn't link Tessera.
Rapida is the intermediary. Censor asks Rapida for vector data, Rapida calls
Tessera, passes the result back. This keeps the dependency tree clean:
`Rapida → Tessera` and `Rapida → Censor`, never `Censor → Tessera` directly.

---

## 5. Overlay Data Format

Censor produces overlay data that Rapida renders on the canvas. Censor does
not render anything itself — it returns structured data that Rapida's renderer
draws.

```c
typedef struct {
    float bounds[4];        // x0, y0, x1, y1 of cluster
    float color[4];         // RGBA (class color at confidence opacity)
    const char* label;      // Predicted or assigned label (NULL if unknown)
    float confidence;       // 0.0–1.0
    int cluster_id;         // Stable ID for interaction
    int is_user_labeled;    // 1 = user applied this label directly
} CensorOverlayEntry;

typedef struct {
    const CensorOverlayEntry* entries;
    int count;
    float completeness;     // 0.0–1.0 — % of elements classified on this page
} CensorOverlayData;

// Get overlay for current viewport. Data valid until next call or detach.
CENSOR_API CensorOverlayData censor_get_overlay(
    CensorSession* session,
    int page,
    const float* viewport_rect  // x0, y0, x1, y1
);
```

---

## 6. File Organization (Censor Repo)

```
Censor/
  include/
    censor.h              # Public C API
  src/
    plugin.cpp            # init/shutdown/version
    session.cpp           # attach/detach, document-level state
    clustering/           # Spatial clustering, feature extraction
    classifier/           # k-NN, confidence, active learning
    storage/              # SQLite read/write
    overlay/              # Overlay data generation
  tests/
    unit/
    integration/
  _Docs/
    Specs/
    LessonsLearned.md
    RunLogs/
  tools/
    mcp_server/           # MCP server for Claude Code development
  CMakeLists.txt
```

---

## 7. Build System

- CMake, C++17 (match Tessera)
- No Tessera dependency at build time — only uses `censor.h` types
- SQLite vendored (amalgamation, single .c file)
- Google Test for tests
- Ships as single DLL with no external dependencies
- Rapida loads via `LoadLibrary` / `dlopen` at runtime
- Function pointers resolved via `GetProcAddress` / `dlsym`

---

## 8. Storage Location

- SQLite database: `%APPDATA%/Censor/censor.db` (Windows)
- Pattern library export: `%APPDATA%/Censor/patterns/`
- Per-file labels keyed by file hash (survives file rename/move)
- Database schema defined in SPEC-censor-core.md §12

---

## Tasks

- [ ] TASK-PLG-001: Create CMake scaffold — C++17, censor.dll target, GoogleTest, SQLite amalgamation.
- [ ] TASK-PLG-002: Define `censor.h` public API header — all types and function declarations from §2-§5.
- [ ] TASK-PLG-003: Implement `censor_init()` / `censor_shutdown()` / `censor_version()` — config parsing, SQLite open/close.
- [ ] TASK-PLG-004: Implement `censor_attach()` / `censor_detach()` — session lifecycle, flush on detach.
- [ ] TASK-PLG-005: Implement `censor_set_host_callbacks()` — store callback table, validate non-null.
- [ ] TASK-PLG-006: Implement `censor_get_overlay()` — stub returning empty overlay data. Proves the render pipeline works end-to-end before classifier exists.
- [ ] TASK-PLG-007: Write Rapida-side dynamic loader — `LoadLibrary("censor.dll")`, resolve function pointers, graceful fallback if DLL missing.
- [ ] TASK-PLG-008: Wire Rapida host callbacks — implement `query_elements` (calls Tessera), `invalidate_overlay` (triggers canvas redraw), `show_toast`.
- [ ] TASK-PLG-009: Integration test — Rapida loads Censor DLL, attaches to document, requests empty overlay, detaches. No classifier logic yet.

---

<!-- SECTION: TASKS TO DO -->
<!-- SECTION: RUN LOG -->
