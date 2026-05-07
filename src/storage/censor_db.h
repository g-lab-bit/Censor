/*
 * censor_db.h — SQLite storage layer for Censor.
 *
 * Three tables per SPEC-censor-core.md §9:
 *   labeled_clusters   — user-confirmed labels + feature vectors
 *   pending_suggestions — classifier predictions awaiting user action
 *   pattern_library    — centroid vectors per label (derived index)
 *
 * Schema is created on first open. All key operations are keyed by
 * file_hash so the database survives file renames.
 *
 * Not thread-safe. Caller must serialise access.
 */

#ifndef CENSOR_DB_H
#define CENSOR_DB_H

#include "censor_types.h"

#include <string>
#include <vector>

/* Forward-declare sqlite3 so callers of censor_db.h don't need sqlite3.h. */
struct sqlite3;

namespace censor {

/* ---------------------------------------------------------------------------
 * LabeledCluster — one row from labeled_clusters.
 * -------------------------------------------------------------------------*/
struct LabeledCluster {
    std::string   id;
    FeatureVector feature_vector;
    std::string   label;
    std::string   source_file;     /* file hash */
    int           page_number = 0;
    std::string   timestamp;       /* ISO 8601 */
    float         confidence  = 0.0f;
    bool          is_negative = false;
};

/* ---------------------------------------------------------------------------
 * PendingSuggestion — one row from pending_suggestions.
 * -------------------------------------------------------------------------*/
enum class SuggestionState {
    Pending   = -1,
    Rejected  = 0,
    Confirmed = 1,
};

struct PendingSuggestion {
    std::string     id;
    std::string     cluster_ref;     /* file_hash:page:cluster_id */
    std::string     predicted_label;
    float           confidence     = 0.0f;
    SuggestionState state          = SuggestionState::Pending;
    std::string     corrected_label; /* non-empty when rejected with reclassify */
};

/* ---------------------------------------------------------------------------
 * PatternEntry — one row from pattern_library.
 * -------------------------------------------------------------------------*/
struct PatternEntry {
    std::string   id;
    std::string   label;
    FeatureVector feature_vector; /* centroid of all examples */
    int           example_count = 0;
    std::string   created;
    std::string   modified;
    std::string   source; /* "local", "shared", or "import" */
};

/* ---------------------------------------------------------------------------
 * CensorDb — RAII wrapper around an SQLite connection.
 * -------------------------------------------------------------------------*/
class __attribute__((visibility("default"))) CensorDb {
public:
    CensorDb() = default;
    ~CensorDb();

    CensorDb(const CensorDb&)            = delete;
    CensorDb& operator=(const CensorDb&) = delete;

    /* Open (or create) the database at `path`. Returns false on failure. */
    bool open(const std::string& path);
    void close();
    bool is_open() const { return db_ != nullptr; }

    /* --- labeled_clusters ------------------------------------------------*/
    bool insert_label(const LabeledCluster& lc);
    std::vector<LabeledCluster> query_examples_by_label(const std::string& label) const;
    /* Returns {label -> count} for every distinct label in labeled_clusters. */
    std::vector<std::pair<std::string, int>> count_per_class() const;

    /* --- pending_suggestions --------------------------------------------*/
    bool insert_suggestion(const PendingSuggestion& ps);
    bool confirm_suggestion(const std::string& id);
    bool reject_suggestion(const std::string& id, const std::string& corrected_label);

    /* --- pattern_library ------------------------------------------------*/
    bool upsert_pattern(const PatternEntry& pe);
    std::vector<PatternEntry> all_patterns() const;

private:
    bool exec(const char* sql) const;
    bool create_schema();

    sqlite3* db_ = nullptr;
};

} /* namespace censor */

#endif /* CENSOR_DB_H */
