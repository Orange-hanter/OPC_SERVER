#include "adapters/sqlite_historian.hpp"

#include <cstddef>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <variant>

#include <sqlite3.h>

namespace opc::adapters {
namespace {

domain::Error sql_err(std::string message) {
    return domain::Error{domain::ErrorCode::Internal, std::move(message), "adapters.sqlite_historian", false};
}

const char* value_type_name(const domain::ScalarValue& v) {
    return std::visit(
        [](const auto& x) -> const char* {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return "bool";
            } else if constexpr (std::is_same_v<T, std::uint16_t>) {
                return "u16";
            } else if constexpr (std::is_same_v<T, std::int16_t>) {
                return "i16";
            } else if constexpr (std::is_same_v<T, std::uint32_t>) {
                return "u32";
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                return "i32";
            } else if constexpr (std::is_same_v<T, float>) {
                return "f32";
            } else if constexpr (std::is_same_v<T, double>) {
                return "f64";
            } else {
                return "unknown";
            }
        },
        v);
}

std::string value_to_text(const domain::ScalarValue& v) {
    return std::visit(
        [](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, bool>) {
                return x ? "1" : "0";
            } else {
                std::ostringstream oss;
                oss << x;
                return oss.str();
            }
        },
        v);
}

domain::Result<domain::ScalarValue> text_to_value(std::string_view type, std::string_view text) {
    try {
        if (type == "null" || type.empty()) {
            return domain::ScalarValue{std::monostate{}};
        }
        if (type == "bool") {
            return domain::ScalarValue{text == "1" || text == "true"};
        }
        if (type == "u16") {
            return domain::ScalarValue{static_cast<std::uint16_t>(std::stoul(std::string(text)))};
        }
        if (type == "i16") {
            return domain::ScalarValue{static_cast<std::int16_t>(std::stoi(std::string(text)))};
        }
        if (type == "u32") {
            return domain::ScalarValue{static_cast<std::uint32_t>(std::stoul(std::string(text)))};
        }
        if (type == "i32") {
            return domain::ScalarValue{static_cast<std::int32_t>(std::stol(std::string(text)))};
        }
        if (type == "f32") {
            return domain::ScalarValue{std::stof(std::string(text))};
        }
        if (type == "f64") {
            return domain::ScalarValue{std::stod(std::string(text))};
        }
    } catch (...) {
        return std::unexpected(sql_err("failed to parse cold value"));
    }
    return std::unexpected(sql_err(std::string("unknown value type: ") + std::string(type)));
}

}  // namespace

SqliteHistorian::SqliteHistorian(std::string db_path,
                                 std::size_t hot_capacity,
                                 ports::IMetrics* metrics)
    : db_path_(std::move(db_path)),
      metrics_(metrics),
      hot_(std::make_unique<RingHistorian>(hot_capacity, metrics)),
      pending_cap_(hot_capacity == 0 ? 1 : hot_capacity) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open(db_path_.c_str(), &raw);
    if (rc != SQLITE_OK) {
        open_error_ = sql_err(std::string("sqlite3_open failed: ") +
                              (raw != nullptr ? sqlite3_errmsg(raw) : "unknown"));
        if (raw != nullptr) {
            sqlite3_close(raw);
        }
        return;
    }
    db_ = raw;
    if (auto schema = ensure_schema(); !schema) {
        open_error_ = schema.error();
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

SqliteHistorian::~SqliteHistorian() {
    if (db_ != nullptr) {
        try {
            [[maybe_unused]] const auto flush_result = flush();
        } catch (...) {
            // Destructors must not throw; pending samples remain unflushed.
            pending_.clear();
        }
        [[maybe_unused]] const int close_result = sqlite3_close(db_);
        db_ = nullptr;
    }
}

domain::Result<void> SqliteHistorian::ensure_schema() {
    static const char* kSql =
        "CREATE TABLE IF NOT EXISTS samples ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  tag_id INTEGER NOT NULL,"
        "  server_ts INTEGER NOT NULL,"
        "  source_ts INTEGER NOT NULL,"
        "  quality INTEGER NOT NULL,"
        "  reason INTEGER NOT NULL,"
        "  epoch INTEGER NOT NULL,"
        "  value_type TEXT NOT NULL,"
        "  value_text TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(server_ts);";
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, kSql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err != nullptr ? err : "schema failed";
        sqlite3_free(err);
        return std::unexpected(sql_err(std::move(msg)));
    }
    return {};
}

void SqliteHistorian::record(domain::TagId id, const domain::TagValue& value) {
    hot_->record(id, value);
    std::lock_guard lock(mutex_);
    pending_.push_back(ports::HistorianSample{.id = id, .value = value});
}

domain::Result<void> SqliteHistorian::insert_batch(const std::vector<ports::HistorianSample>& batch) {
    if (db_ == nullptr) {
        return std::unexpected(open_error_.value_or(sql_err("database not open")));
    }
    if (batch.empty()) {
        return {};
    }
    char* err = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err != nullptr ? err : "BEGIN failed";
        sqlite3_free(err);
        return std::unexpected(sql_err(std::move(msg)));
    }

    sqlite3_stmt* stmt = nullptr;
    static const char* kInsert =
        "INSERT INTO samples(tag_id, server_ts, source_ts, quality, reason, epoch, value_type, value_text)"
        " VALUES(?,?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(db_, kInsert, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return std::unexpected(sql_err(sqlite3_errmsg(db_)));
    }

    for (const auto& sample : batch) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int64(stmt, 1, sample.id);
        sqlite3_bind_int64(stmt, 2, sample.value.server_ts);
        sqlite3_bind_int64(stmt, 3, sample.value.source_ts);
        sqlite3_bind_int(stmt, 4, static_cast<int>(sample.value.quality));
        sqlite3_bind_int(stmt, 5, static_cast<int>(sample.value.reason));
        sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(sample.value.epoch));
        const auto* type = value_type_name(sample.value.value);
        const auto text = value_to_text(sample.value.value);
        sqlite3_bind_text(stmt, 7, type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, text.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            const std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return std::unexpected(sql_err(msg));
        }
    }
    sqlite3_finalize(stmt);

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err != nullptr ? err : "COMMIT failed";
        sqlite3_free(err);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return std::unexpected(sql_err(std::move(msg)));
    }
    return {};
}

domain::Result<void> SqliteHistorian::flush() {
    std::vector<ports::HistorianSample> batch;
    {
        std::lock_guard lock(mutex_);
        batch.swap(pending_);
    }
    auto result = insert_batch(batch);
    if (!result) {
        std::lock_guard lock(mutex_);
        pending_.insert(pending_.begin(), batch.begin(), batch.end());
        const std::size_t max_pending = pending_cap_ * 4;
        if (pending_.size() > max_pending) {
            const std::size_t drop = pending_.size() - max_pending;
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(drop));
            cold_pending_dropped_ += drop;
            if (metrics_ != nullptr) {
                metrics_->counter_add("historian.cold_pending_dropped", static_cast<double>(drop));
            }
        }
        return result;
    }
    if (metrics_ != nullptr) {
        metrics_->counter_add("historian.cold_flushed", static_cast<double>(batch.size()));
    }
    return {};
}

std::vector<ports::HistorianSample> SqliteHistorian::recent(std::size_t max) const {
    return hot_->recent(max);
}

std::uint64_t SqliteHistorian::dropped() const {
    const auto hot_dropped = hot_->dropped();
    std::lock_guard lock(mutex_);
    return hot_dropped + cold_pending_dropped_;
}

domain::Result<std::vector<ports::HistorianSample>>
SqliteHistorian::load_cold(std::size_t max) const {
    if (db_ == nullptr) {
        return std::unexpected(open_error_.value_or(sql_err("database not open")));
    }
    sqlite3_stmt* stmt = nullptr;
    const std::string sql =
        "SELECT tag_id, server_ts, source_ts, quality, reason, epoch, value_type, value_text"
        " FROM samples ORDER BY id ASC LIMIT " +
        std::to_string(max) + ";";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(sql_err(sqlite3_errmsg(db_)));
    }
    std::vector<ports::HistorianSample> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ports::HistorianSample sample;
        sample.id = static_cast<domain::TagId>(sqlite3_column_int64(stmt, 0));
        sample.value.server_ts = sqlite3_column_int64(stmt, 1);
        sample.value.source_ts = sqlite3_column_int64(stmt, 2);
        sample.value.quality = static_cast<domain::Quality>(sqlite3_column_int(stmt, 3));
        sample.value.reason = static_cast<domain::QualityReason>(sqlite3_column_int(stmt, 4));
        sample.value.epoch = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 5));
        const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        auto parsed = text_to_value(type != nullptr ? type : "", text != nullptr ? text : "");
        if (!parsed) {
            sqlite3_finalize(stmt);
            return std::unexpected(parsed.error());
        }
        sample.value.value = *parsed;
        out.push_back(std::move(sample));
    }
    sqlite3_finalize(stmt);
    return out;
}

}  // namespace opc::adapters
