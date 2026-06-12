/*
 * censor_db.cpp — SQLite storage layer implementation.
 */

#include "storage/censor_db.h"

#include <sqlite3.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace censor {

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

static std::string now_iso8601()
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    /* ce-3hj — std::gmtime returns a shared static buffer (a data race when
     * two CensorDb instances run on different threads; also a C4996 hard
     * error under MSVC /WX). Use the per-caller-buffer variants. */
    std::tm tm_buf{};
#if defined(_MSC_VER)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static void pack_fv(const FeatureVector& fv, std::vector<uint8_t>& out)
{
    out.resize(sizeof(fv.dims));
    std::memcpy(out.data(), fv.dims, sizeof(fv.dims));
}

static FeatureVector unpack_fv(const void* blob, int bytes)
{
    FeatureVector fv{};
    if (blob && bytes == static_cast<int>(sizeof(fv.dims)))
        std::memcpy(fv.dims, blob, sizeof(fv.dims));
    return fv;
}

/* ---------------------------------------------------------------------------
 * Open / close
 * -------------------------------------------------------------------------*/

bool CensorDb::open(const std::string& path)
{
    if (db_) close();
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;",  nullptr, nullptr, nullptr);
    return create_schema();
}

void CensorDb::close()
{
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

CensorDb::~CensorDb() { close(); }

bool CensorDb::exec(const char* sql) const
{
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    return rc == SQLITE_OK;
}

bool CensorDb::create_schema()
{
    return exec(
        "CREATE TABLE IF NOT EXISTS labeled_clusters ("
        "  id             TEXT PRIMARY KEY,"
        "  feature_vector BLOB NOT NULL,"
        "  label          TEXT NOT NULL,"
        "  source_file    TEXT NOT NULL,"
        "  page_number    INTEGER NOT NULL DEFAULT 0,"
        "  timestamp      TEXT NOT NULL,"
        "  confidence     REAL NOT NULL DEFAULT 0.0,"
        "  is_negative    INTEGER NOT NULL DEFAULT 0"
        ");"

        "CREATE TABLE IF NOT EXISTS pending_suggestions ("
        "  id              TEXT PRIMARY KEY,"
        "  cluster_ref     TEXT NOT NULL,"
        "  predicted_label TEXT NOT NULL,"
        "  confidence      REAL NOT NULL DEFAULT 0.0,"
        "  confirmed       INTEGER,"          /* NULL=pending, 1=confirmed, 0=rejected */
        "  corrected_label TEXT"
        ");"

        "CREATE TABLE IF NOT EXISTS pattern_library ("
        "  id             TEXT PRIMARY KEY,"
        "  label          TEXT NOT NULL UNIQUE,"
        "  feature_vector BLOB NOT NULL,"
        "  example_count  INTEGER NOT NULL DEFAULT 0,"
        "  created        TEXT NOT NULL,"
        "  modified       TEXT NOT NULL,"
        "  source         TEXT NOT NULL DEFAULT 'local'"
        ");"
    );
}

/* ---------------------------------------------------------------------------
 * labeled_clusters
 * -------------------------------------------------------------------------*/

bool CensorDb::insert_label(const LabeledCluster& lc)
{
    const char* sql =
        "INSERT INTO labeled_clusters"
        " (id, feature_vector, label, source_file, page_number, timestamp, confidence, is_negative)"
        " VALUES (?,?,?,?,?,?,?,?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    std::vector<uint8_t> blob;
    pack_fv(lc.feature_vector, blob);

    std::string ts = lc.timestamp.empty() ? now_iso8601() : lc.timestamp;

    sqlite3_bind_text(stmt, 1, lc.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, lc.label.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, lc.source_file.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 5, lc.page_number);
    sqlite3_bind_text(stmt, 6, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 7, static_cast<double>(lc.confidence));
    sqlite3_bind_int(stmt, 8, lc.is_negative ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<LabeledCluster> CensorDb::query_examples_by_label(const std::string& label) const
{
    const char* sql =
        "SELECT id, feature_vector, label, source_file, page_number, timestamp, confidence, is_negative"
        " FROM labeled_clusters WHERE label=? ORDER BY timestamp;";

    sqlite3_stmt* stmt = nullptr;
    std::vector<LabeledCluster> results;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    sqlite3_bind_text(stmt, 1, label.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LabeledCluster lc;
        auto to_str = [&](int col) -> std::string {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return p ? p : "";
        };
        lc.id           = to_str(0);
        lc.feature_vector = unpack_fv(
            sqlite3_column_blob(stmt, 1),
            sqlite3_column_bytes(stmt, 1));
        lc.label        = to_str(2);
        lc.source_file  = to_str(3);
        lc.page_number  = sqlite3_column_int(stmt, 4);
        lc.timestamp    = to_str(5);
        lc.confidence   = static_cast<float>(sqlite3_column_double(stmt, 6));
        lc.is_negative  = sqlite3_column_int(stmt, 7) != 0;
        results.push_back(std::move(lc));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::pair<std::string, int>> CensorDb::count_per_class() const
{
    const char* sql =
        "SELECT label, COUNT(*) FROM labeled_clusters GROUP BY label ORDER BY label;";

    sqlite3_stmt* stmt = nullptr;
    std::vector<std::pair<std::string, int>> results;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* lbl = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int cnt = sqlite3_column_int(stmt, 1);
        results.emplace_back(lbl ? lbl : "", cnt);
    }
    sqlite3_finalize(stmt);
    return results;
}

/* ---------------------------------------------------------------------------
 * pending_suggestions
 * -------------------------------------------------------------------------*/

bool CensorDb::insert_suggestion(const PendingSuggestion& ps)
{
    const char* sql =
        "INSERT INTO pending_suggestions"
        " (id, cluster_ref, predicted_label, confidence, confirmed, corrected_label)"
        " VALUES (?,?,?,?,?,?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, ps.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ps.cluster_ref.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ps.predicted_label.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, static_cast<double>(ps.confidence));

    if (ps.state == SuggestionState::Pending)
        sqlite3_bind_null(stmt, 5);
    else
        sqlite3_bind_int(stmt, 5, ps.state == SuggestionState::Confirmed ? 1 : 0);

    if (ps.corrected_label.empty())
        sqlite3_bind_null(stmt, 6);
    else
        sqlite3_bind_text(stmt, 6, ps.corrected_label.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CensorDb::confirm_suggestion(const std::string& id)
{
    const char* sql = "UPDATE pending_suggestions SET confirmed=1 WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool CensorDb::reject_suggestion(const std::string& id, const std::string& corrected_label)
{
    const char* sql =
        "UPDATE pending_suggestions SET confirmed=0, corrected_label=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, corrected_label.empty() ? nullptr : corrected_label.c_str(),
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

/* ---------------------------------------------------------------------------
 * pattern_library
 * -------------------------------------------------------------------------*/

bool CensorDb::upsert_pattern(const PatternEntry& pe)
{
    std::string now = now_iso8601();
    const char* sql =
        "INSERT INTO pattern_library (id, label, feature_vector, example_count, created, modified, source)"
        " VALUES (?,?,?,?,?,?,?)"
        " ON CONFLICT(label) DO UPDATE SET"
        "   feature_vector=excluded.feature_vector,"
        "   example_count=excluded.example_count,"
        "   modified=excluded.modified,"
        "   source=excluded.source;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    std::vector<uint8_t> blob;
    pack_fv(pe.feature_vector, blob);

    std::string created  = pe.created.empty()  ? now : pe.created;
    std::string modified = pe.modified.empty() ? now : pe.modified;
    std::string source   = pe.source.empty()   ? "local" : pe.source;

    sqlite3_bind_text(stmt, 1, pe.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pe.label.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, pe.example_count);
    sqlite3_bind_text(stmt, 5, created.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, modified.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, source.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<PatternEntry> CensorDb::all_patterns() const
{
    const char* sql =
        "SELECT id, label, feature_vector, example_count, created, modified, source"
        " FROM pattern_library ORDER BY label;";

    sqlite3_stmt* stmt = nullptr;
    std::vector<PatternEntry> results;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PatternEntry pe;
        auto to_str = [&](int col) -> std::string {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return p ? p : "";
        };
        pe.id             = to_str(0);
        pe.label          = to_str(1);
        pe.feature_vector = unpack_fv(
            sqlite3_column_blob(stmt, 2),
            sqlite3_column_bytes(stmt, 2));
        pe.example_count  = sqlite3_column_int(stmt, 3);
        pe.created        = to_str(4);
        pe.modified       = to_str(5);
        pe.source         = to_str(6);
        results.push_back(std::move(pe));
    }
    sqlite3_finalize(stmt);
    return results;
}

} /* namespace censor */
