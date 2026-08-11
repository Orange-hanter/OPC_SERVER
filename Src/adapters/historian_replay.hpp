#pragma once

#include "ports/i_historian.hpp"
#include "ports/i_tag_store.hpp"

#include <cstddef>

namespace opc::adapters {

/// Replay historian samples into a TagStore (debug / incident analysis; no field I/O).
inline void replay_samples(ports::ITagStore& store,
                           const std::vector<ports::HistorianSample>& samples) {
    for (const auto& sample : samples) {
        store.publish(sample.id, sample.value);
    }
}

inline void replay_recent(ports::ITagStore& store, const ports::IHistorian& historian, std::size_t max) {
    auto samples = historian.recent(max);
    // recent() is newest-first; replay oldest-first for chronological restore.
    for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
        store.publish(it->id, it->value);
    }
}

}  // namespace opc::adapters
