#pragma once

#include "adapters/ring_historian.hpp"
#include "ports/i_historian.hpp"
#include "ports/i_metrics.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace opc::adapters {

/// Hot ring + cold SQLite historian. `flush()` appends pending samples to the DB.
class SqliteHistorian final : public ports::IHistorian {
public:
    /// Opens (or creates) `db_path`. Throws nothing — check `open_error()` after construction.
    SqliteHistorian(std::string db_path,
                    std::size_t hot_capacity = 4096,
                    ports::IMetrics* metrics = nullptr);
    ~SqliteHistorian() override;

    SqliteHistorian(const SqliteHistorian&) = delete;
    SqliteHistorian& operator=(const SqliteHistorian&) = delete;

    [[nodiscard]] const std::optional<domain::Error>& open_error() const { return open_error_; }

    void record(domain::TagId id, const domain::TagValue& value) override;
    domain::Result<void> flush() override;
    [[nodiscard]] std::vector<ports::HistorianSample> recent(std::size_t max) const override;
    [[nodiscard]] std::uint64_t dropped() const override;

    [[nodiscard]] RingHistorian& hot() { return *hot_; }
    [[nodiscard]] const RingHistorian& hot() const { return *hot_; }

    /// Load cold samples oldest-first (empty if DB unavailable).
    [[nodiscard]] domain::Result<std::vector<ports::HistorianSample>>
    load_cold(std::size_t max) const;

private:
    domain::Result<void> ensure_schema();
    domain::Result<void> insert_batch(const std::vector<ports::HistorianSample>& batch);

    std::string db_path_;
    ports::IMetrics* metrics_{nullptr};
    std::unique_ptr<RingHistorian> hot_;
    sqlite3* db_{nullptr};
    std::optional<domain::Error> open_error_;
    mutable std::mutex mutex_;
    std::vector<ports::HistorianSample> pending_;
    std::size_t pending_cap_{0};
};

}  // namespace opc::adapters
