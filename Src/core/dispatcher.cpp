#include "core/dispatcher.hpp"
#include "core/translator.hpp"
#include "domain/tag_value_util.hpp"

#include <functional>
#include <iterator>
#include <optional>
#include <vector>

namespace opc::core {
namespace {

int regs_for(const project::Tag& tag) {
    if (tag.quantity.has_value()) {
        return *tag.quantity;
    }
    switch (tag.type) {
    case project::TagType::Bool:
    case project::TagType::UInt16:
    case project::TagType::Int16:
        return 1;
    case project::TagType::UInt32:
    case project::TagType::Int32:
    case project::TagType::Float32:
        return 2;
    case project::TagType::Float64:
        return 4;
    }
    return 1;
}

domain::Error transport_error(domain::Error err) {
    if (err.component.empty()) {
        err.component = "core.dispatcher";
    }
    return err;
}

void publish_quality(ports::ITagStore* store,
                     domain::TagId id,
                     domain::Quality quality,
                     domain::QualityReason reason,
                     domain::TimestampMs now,
                     std::optional<domain::ScalarValue> value = std::nullopt) {
    if (store == nullptr) {
        return;
    }
    auto previous = store->get(id);
    auto next = domain::with_quality(previous, quality, reason, now);
    if (value.has_value()) {
        next.value = *value;
        next.source_ts = now;
    }
    store->publish(id, next);
}

}  // namespace

Dispatcher::Dispatcher(Dependencies deps) : deps_(std::move(deps)) {}

std::unique_ptr<ports::ISpan> Dispatcher::start_span(std::string_view name) const {
    if (deps_.tracer == nullptr) {
        return nullptr;
    }
    return deps_.tracer->start_span(name);
}

void Dispatcher::bind_transport(std::string endpoint_id, ports::IModbusTransport* transport) {
    std::lock_guard lock(state_mutex_);
    transports_[std::move(endpoint_id)] = transport;
}

domain::Result<void> Dispatcher::poll_tag(const TagBinding& binding,
                                          ports::IModbusTransport& transport,
                                          domain::TimestampMs now) {
    if (deps_.tag_store == nullptr || deps_.clock == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "missing tag_store/clock", "core.dispatcher", false});
    }

    const auto& tag = binding.tag;
    const auto qty = static_cast<std::uint16_t>(regs_for(tag));
    const auto addr = static_cast<std::uint16_t>(tag.address);
    const auto t0 = deps_.clock->now_ms();
    const auto observe_rtt = [&]() {
        if (deps_.metrics != nullptr) {
            deps_.metrics->histogram_observe(
                "modbus_poll_rtt_ms", static_cast<double>(deps_.clock->now_ms() - t0));
        }
    };

    domain::Result<std::vector<std::uint16_t>> regs{std::unexpected(domain::Error{})};
    if (tag.area == project::Area::Holding) {
        regs = transport.read_holding_registers(binding.unit_id, addr, qty);
    } else if (tag.area == project::Area::Input) {
        regs = transport.read_input_registers(binding.unit_id, addr, qty);
    } else if (tag.area == project::Area::Coil) {
        auto coils = transport.read_coils(binding.unit_id, addr, qty);
        if (!coils) {
            observe_rtt();
            publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad,
                            domain::QualityReason::NoCommunication, now);
            if (deps_.metrics != nullptr) {
                deps_.metrics->counter_add("modbus_poll_errors_total");
            }
            return std::unexpected(transport_error(coils.error()));
        }
        std::vector<std::uint16_t> as_regs;
        as_regs.reserve(coils->size());
        for (bool b : *coils) {
            as_regs.push_back(b ? 1 : 0);
        }
        regs = std::move(as_regs);
    } else {
        auto discs = transport.read_discrete_inputs(binding.unit_id, addr, qty);
        if (!discs) {
            observe_rtt();
            publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad,
                            domain::QualityReason::NoCommunication, now);
            if (deps_.metrics != nullptr) {
                deps_.metrics->counter_add("modbus_poll_errors_total");
            }
            return std::unexpected(transport_error(discs.error()));
        }
        std::vector<std::uint16_t> as_regs;
        for (bool b : *discs) {
            as_regs.push_back(b ? 1 : 0);
        }
        regs = std::move(as_regs);
    }

    observe_rtt();

    if (!regs) {
        const auto reason = regs.error().code == domain::ErrorCode::Timeout
                                ? domain::QualityReason::Timeout
                            : regs.error().code == domain::ErrorCode::ModbusException
                                ? domain::QualityReason::ModbusException
                                : domain::QualityReason::NoCommunication;
        publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad, reason, now);
        if (deps_.metrics != nullptr) {
            deps_.metrics->counter_add("modbus_poll_errors_total");
        }
        return std::unexpected(transport_error(regs.error()));
    }

    auto decoded = Translator::decode(tag, *regs);
    if (!decoded) {
        publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad,
                        domain::QualityReason::DecodingError, now);
        return std::unexpected(decoded.error());
    }
    domain::TagValue value;
    value.value = *decoded;
    value.quality = domain::Quality::Good;
    value.reason = domain::QualityReason::None;
    value.server_ts = now;
    value.source_ts = now;
    deps_.tag_store->publish(binding.id, value);
    return {};
}

void Dispatcher::poll_tag_async(TagBinding binding,
                                ports::IModbusTransport& transport,
                                domain::TimestampMs now,
                                ports::ModbusCompletion<void> done) {
    if (!done) {
        return;
    }
    if (deps_.tag_store == nullptr || deps_.clock == nullptr) {
        done(std::unexpected(domain::Error{
            domain::ErrorCode::Internal, "missing tag_store/clock", "core.dispatcher", false}));
        return;
    }

    const auto qty = static_cast<std::uint16_t>(regs_for(binding.tag));
    const auto addr = static_cast<std::uint16_t>(binding.tag.address);
    const auto t0 = deps_.clock->now_ms();
    auto finish_regs = [this, binding, now, t0, done = std::move(done)](
                           domain::Result<std::vector<std::uint16_t>> regs) mutable {
        if (deps_.metrics != nullptr) {
            deps_.metrics->histogram_observe(
                "modbus_poll_rtt_ms", static_cast<double>(deps_.clock->now_ms() - t0));
        }
        if (!regs) {
            const auto reason = regs.error().code == domain::ErrorCode::Timeout
                                    ? domain::QualityReason::Timeout
                                : regs.error().code == domain::ErrorCode::ModbusException
                                    ? domain::QualityReason::ModbusException
                                    : domain::QualityReason::NoCommunication;
            publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad, reason, now);
            if (deps_.metrics != nullptr) {
                deps_.metrics->counter_add("modbus_poll_errors_total");
            }
            done(std::unexpected(transport_error(regs.error())));
            return;
        }
        auto decoded = Translator::decode(binding.tag, *regs);
        if (!decoded) {
            publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad,
                            domain::QualityReason::DecodingError, now);
            done(std::unexpected(decoded.error()));
            return;
        }
        domain::TagValue value;
        value.value = *decoded;
        value.quality = domain::Quality::Good;
        value.reason = domain::QualityReason::None;
        value.server_ts = now;
        value.source_ts = now;
        deps_.tag_store->publish(binding.id, value);
        done({});
    };

    if (binding.tag.area == project::Area::Holding) {
        transport.async_read_holding_registers(binding.unit_id, addr, qty, std::move(finish_regs));
        return;
    }
    if (binding.tag.area == project::Area::Input) {
        transport.async_read_input_registers(binding.unit_id, addr, qty, std::move(finish_regs));
        return;
    }
    if (binding.tag.area == project::Area::Coil) {
        transport.async_read_coils(
            binding.unit_id, addr, qty,
            [finish_regs = std::move(finish_regs)](domain::Result<std::vector<bool>> coils) mutable {
                if (!coils) {
                    finish_regs(std::unexpected(coils.error()));
                    return;
                }
                std::vector<std::uint16_t> as_regs;
                as_regs.reserve(coils->size());
                for (bool b : *coils) {
                    as_regs.push_back(b ? 1 : 0);
                }
                finish_regs(std::move(as_regs));
            });
        return;
    }
    transport.async_read_discrete_inputs(
        binding.unit_id, addr, qty,
        [finish_regs = std::move(finish_regs)](domain::Result<std::vector<bool>> discs) mutable {
            if (!discs) {
                finish_regs(std::unexpected(discs.error()));
                return;
            }
            std::vector<std::uint16_t> as_regs;
            as_regs.reserve(discs->size());
            for (bool b : *discs) {
                as_regs.push_back(b ? 1 : 0);
            }
            finish_regs(std::move(as_regs));
        });
}

domain::Result<void> Dispatcher::poll_group(const project::PollGroup& group,
                                            ports::IModbusTransport& transport,
                                            domain::TimestampMs now) {
    const auto* device = deps_.index.device(group.device_id);
    if (device == nullptr) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::NotFound, "device missing", "core.dispatcher", false});
    }

    std::vector<TagBinding> to_poll;
    if (!group.tag_names.empty()) {
        for (const auto& name : group.tag_names) {
            auto binding = deps_.index.find_by_name(name);
            if (!binding) {
                return std::unexpected(domain::Error{domain::ErrorCode::NotFound,
                                                     "tag not found: " + name,
                                                     "core.dispatcher",
                                                     false});
            }
            to_poll.push_back(*binding);
        }
    } else {
        for (const auto& binding : deps_.index.tags()) {
            if (binding.device_id == group.device_id &&
                (binding.tag.group.empty() || binding.tag.group == group.id)) {
                to_poll.push_back(binding);
            }
        }
        if (to_poll.empty() && !group.blocks.empty()) {
            for (const auto& binding : deps_.index.tags()) {
                if (binding.device_id != group.device_id) {
                    continue;
                }
                for (const auto& block : group.blocks) {
                    const int end = block.start + block.count;
                    const int tag_regs = regs_for(binding.tag);
                    if (binding.tag.area == block.area && binding.tag.address >= block.start &&
                        binding.tag.address + tag_regs <= end) {
                        to_poll.push_back(binding);
                        break;
                    }
                }
            }
        }
    }

    domain::Result<void> first_error = {};
    for (const auto& binding : to_poll) {
        auto r = poll_tag(binding, transport, now);
        if (!r && first_error.has_value()) {
            first_error = std::unexpected(r.error());
        }
    }
    return first_error;
}

domain::Result<void> Dispatcher::poll_due(std::string_view endpoint_id, domain::TimestampMs now) {
    auto span = start_span("modbus.poll");
    if (span) {
        span->set_attribute("endpoint_id", endpoint_id);
    }
    ports::IModbusTransport* transport = nullptr;
    {
        std::lock_guard lock(state_mutex_);
        auto it = transports_.find(std::string(endpoint_id));
        if (it == transports_.end() || it->second == nullptr) {
            if (span) {
                span->set_error("transport not bound");
            }
            return std::unexpected(domain::Error{
                domain::ErrorCode::NotFound, "transport not bound", "core.dispatcher", false});
        }
        transport = it->second;
    }
    if (!transport->is_connected()) {
        const auto* ep = deps_.index.endpoint(endpoint_id);
        if (ep == nullptr) {
            if (span) {
                span->set_error("endpoint missing");
            }
            return std::unexpected(domain::Error{
                domain::ErrorCode::NotFound, "endpoint missing", "core.dispatcher", false});
        }
        auto conn = transport->connect({ep->host, ep->port});
        if (!conn) {
            mark_endpoint_bad(endpoint_id, domain::QualityReason::NoCommunication, now);
            if (span) {
                span->set_error(conn.error().message);
            }
            return std::unexpected(conn.error());
        }
    }

    if (auto wr = flush_writes(endpoint_id); !wr) {
        if (span) {
            span->set_error(wr.error().message);
        }
        return wr;
    }

    domain::Result<void> first_error = {};
    const auto groups = deps_.index.groups_for_endpoint(endpoint_id);
    for (const auto* group : groups) {
        const std::string key = std::string(endpoint_id) + "|" + group->id;
        domain::TimestampMs last{0};
        {
            std::lock_guard lock(state_mutex_);
            if (last_poll_ms_.contains(key)) {
                last = last_poll_ms_[key];
            }
        }
        if (last != 0 && (now - last) < group->period_ms) {
            continue;
        }
        auto r = poll_group(*group, *transport, now);
        {
            std::lock_guard lock(state_mutex_);
            last_poll_ms_[key] = now;
        }
        if (!r) {
            if (deps_.metrics != nullptr) {
                deps_.metrics->counter_add("modbus_poll_overruns");
            }
            if (first_error.has_value()) {
                first_error = std::unexpected(r.error());
            }
        }
    }
    if (span && !first_error) {
        span->set_error(first_error.error().message);
    }
    return first_error;
}

namespace {

[[nodiscard]] std::vector<TagBinding> collect_group_tags(const RuntimeIndex& index,
                                                         const project::PollGroup& group) {
    std::vector<TagBinding> to_poll;
    if (!group.tag_names.empty()) {
        for (const auto& name : group.tag_names) {
            auto binding = index.find_by_name(name);
            if (binding) {
                to_poll.push_back(*binding);
            }
        }
        return to_poll;
    }
    for (const auto& binding : index.tags()) {
        if (binding.device_id == group.device_id &&
            (binding.tag.group.empty() || binding.tag.group == group.id)) {
            to_poll.push_back(binding);
        }
    }
    if (to_poll.empty() && !group.blocks.empty()) {
        for (const auto& binding : index.tags()) {
            if (binding.device_id != group.device_id) {
                continue;
            }
            for (const auto& block : group.blocks) {
                const int end = block.start + block.count;
                const int tag_regs = regs_for(binding.tag);
                if (binding.tag.area == block.area && binding.tag.address >= block.start &&
                    binding.tag.address + tag_regs <= end) {
                    to_poll.push_back(binding);
                    break;
                }
            }
        }
    }
    return to_poll;
}

}  // namespace

void Dispatcher::poll_due_async(std::string_view endpoint_id,
                                domain::TimestampMs now,
                                ports::ModbusCompletion<void> done) {
    if (!done) {
        return;
    }
    auto span = start_span("modbus.poll");
    if (span) {
        span->set_attribute("endpoint_id", endpoint_id);
        span->set_attribute("async", "true");
    }

    ports::IModbusTransport* transport = nullptr;
    {
        std::lock_guard lock(state_mutex_);
        auto it = transports_.find(std::string(endpoint_id));
        if (it == transports_.end() || it->second == nullptr) {
            if (span) {
                span->set_error("transport not bound");
            }
            done(std::unexpected(domain::Error{
                domain::ErrorCode::NotFound, "transport not bound", "core.dispatcher", false}));
            return;
        }
        transport = it->second;
    }

    auto shared_span = std::shared_ptr<ports::ISpan>(std::move(span));
    auto shared_done =
        std::make_shared<ports::ModbusCompletion<void>>(std::move(done));
    auto first_error = std::make_shared<std::optional<domain::Error>>();

    auto finish = [shared_span, shared_done, first_error](domain::Result<void> result) {
        if (shared_span && !result) {
            shared_span->set_error(result.error().message);
        }
        if (*shared_done) {
            (*shared_done)(std::move(result));
        }
    };

    auto run_polls = [this, endpoint_id = std::string(endpoint_id), now, transport, finish,
                      first_error]() {
        if (auto wr = flush_writes(endpoint_id); !wr) {
            finish(std::unexpected(wr.error()));
            return;
        }

        auto tags = std::make_shared<std::vector<TagBinding>>();
        const auto groups = deps_.index.groups_for_endpoint(endpoint_id);
        for (const auto* group : groups) {
            if (group == nullptr) {
                continue;
            }
            const std::string key = endpoint_id + "|" + group->id;
            domain::TimestampMs last{0};
            {
                std::lock_guard lock(state_mutex_);
                if (last_poll_ms_.contains(key)) {
                    last = last_poll_ms_[key];
                }
            }
            if (last != 0 && (now - last) < group->period_ms) {
                continue;
            }
            auto batch = collect_group_tags(deps_.index, *group);
            tags->insert(tags->end(), batch.begin(), batch.end());
            {
                std::lock_guard lock(state_mutex_);
                last_poll_ms_[key] = now;
            }
        }

        auto index = std::make_shared<std::size_t>(0);
        auto step = std::make_shared<std::function<void()>>();
        *step = [this, transport, now, tags, index, step, finish, first_error]() {
            if (*index >= tags->size()) {
                if (*first_error) {
                    finish(std::unexpected(**first_error));
                } else {
                    finish({});
                }
                return;
            }
            TagBinding binding = (*tags)[(*index)++];
            poll_tag_async(std::move(binding), *transport, now,
                           [step, finish, first_error](domain::Result<void> r) {
                               if (!r && !*first_error) {
                                   *first_error = r.error();
                               }
                               (*step)();
                           });
        };
        (*step)();
    };

    if (!transport->is_connected()) {
        const auto* ep = deps_.index.endpoint(endpoint_id);
        if (ep == nullptr) {
            finish(std::unexpected(domain::Error{
                domain::ErrorCode::NotFound, "endpoint missing", "core.dispatcher", false}));
            return;
        }
        transport->async_connect(
            ports::EndpointAddress{.host = ep->host, .port = ep->port},
            [this, endpoint_id = std::string(endpoint_id), now, finish,
             run_polls](domain::Result<void> conn) mutable {
                if (!conn) {
                    mark_endpoint_bad(endpoint_id, domain::QualityReason::NoCommunication, now);
                    finish(std::unexpected(conn.error()));
                    return;
                }
                run_polls();
            });
        return;
    }
    run_polls();
}

domain::Result<void> Dispatcher::enqueue_write(domain::TagId tag_id, domain::ScalarValue value) {
    auto binding = deps_.index.find_by_id(tag_id);
    if (!binding) {
        return std::unexpected(
            domain::Error{domain::ErrorCode::NotFound, "unknown tag", "core.dispatcher", false});
    }
    if (!binding->tag.writable) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::Permission, "tag not writable", "core.dispatcher", false});
    }
    {
        std::lock_guard lock(write_mutex_);
        auto& queue = write_queues_[binding->endpoint_id];
        if (queue.size() >= kMaxWriteQueueDepth) {
            if (deps_.metrics != nullptr) {
                deps_.metrics->counter_add("modbus_write_queue_overflow_total");
            }
            return std::unexpected(domain::Error{domain::ErrorCode::QueueFull,
                                                 "write queue full for endpoint " + binding->endpoint_id,
                                                 "core.dispatcher",
                                                 true});
        }
        queue.push_back(PendingWrite{tag_id, std::move(value)});
        if (deps_.metrics != nullptr) {
            deps_.metrics->gauge_set("modbus_write_queue_depth", static_cast<double>(queue.size()));
        }
    }
    return {};
}

domain::Result<void> Dispatcher::flush_writes(std::string_view endpoint_id) {
    std::vector<PendingWrite> batch;
    {
        std::lock_guard lock(write_mutex_);
        auto q_it = write_queues_.find(std::string(endpoint_id));
        if (q_it == write_queues_.end() || q_it->second.empty()) {
            return {};
        }
        batch.swap(q_it->second);
    }
    auto span = start_span("modbus.write");
    if (span) {
        span->set_attribute("endpoint_id", endpoint_id);
        span->set_attribute("write_count", static_cast<std::int64_t>(batch.size()));
    }
    ports::IModbusTransport* transport = nullptr;
    {
        std::lock_guard lock(state_mutex_);
        auto t_it = transports_.find(std::string(endpoint_id));
        if (t_it != transports_.end()) {
            transport = t_it->second;
        }
    }
    if (transport == nullptr) {
        std::lock_guard lock(write_mutex_);
        auto& queue = write_queues_[std::string(endpoint_id)];
        queue.insert(queue.begin(), std::make_move_iterator(batch.begin()),
                     std::make_move_iterator(batch.end()));
        if (span) {
            span->set_error("transport not bound");
        }
        return std::unexpected(domain::Error{
            domain::ErrorCode::NotFound, "transport not bound", "core.dispatcher", false});
    }
    const auto now = deps_.clock != nullptr ? deps_.clock->now_ms() : domain::TimestampMs{0};

    auto requeue_from = [&](std::size_t index) {
        std::lock_guard lock(write_mutex_);
        auto& queue = write_queues_[std::string(endpoint_id)];
        queue.insert(queue.begin(),
                     std::make_move_iterator(batch.begin() + static_cast<std::ptrdiff_t>(index)),
                     std::make_move_iterator(batch.end()));
    };

    for (std::size_t i = 0; i < batch.size();) {
        auto binding = deps_.index.find_by_id(batch[i].tag_id);
        if (!binding) {
            ++i;
            continue;
        }
        auto encoded = Translator::encode(binding->tag, batch[i].value);
        if (!encoded) {
            publish_quality(deps_.tag_store, batch[i].tag_id, domain::Quality::Bad,
                            domain::QualityReason::DecodingError, now);
            requeue_from(i + 1);
            if (span) {
                span->set_error(encoded.error().message);
            }
            return std::unexpected(encoded.error());
        }

        domain::Result<void> wr = {};
        const auto addr = static_cast<std::uint16_t>(binding->tag.address);
        std::size_t consumed = 1;

        if (binding->tag.area == project::Area::Coil) {
            std::vector<std::uint8_t> bits;
            bits.reserve(encoded->size());
            for (auto reg : *encoded) {
                bits.push_back(reg != 0 ? 1 : 0);
            }
            auto next_addr = static_cast<int>(addr) + static_cast<int>(bits.size());
            std::size_t j = i + 1;
            for (; j < batch.size(); ++j) {
                auto next = deps_.index.find_by_id(batch[j].tag_id);
                if (!next || next->tag.area != project::Area::Coil ||
                    next->unit_id != binding->unit_id || next->tag.address != next_addr) {
                    break;
                }
                auto next_encoded = Translator::encode(next->tag, batch[j].value);
                if (!next_encoded) {
                    publish_quality(deps_.tag_store, batch[j].tag_id, domain::Quality::Bad,
                                    domain::QualityReason::DecodingError, now);
                    requeue_from(j + 1);
                    if (span) {
                        span->set_error(next_encoded.error().message);
                    }
                    return std::unexpected(next_encoded.error());
                }
                for (auto reg : *next_encoded) {
                    bits.push_back(reg != 0 ? 1 : 0);
                }
                next_addr += static_cast<int>((*next_encoded).size());
            }
            consumed = j - i;
            if (bits.size() == 1) {
                wr = transport->write_single_coil(binding->unit_id, addr, bits[0] != 0);
            } else {
                wr = transport->write_multiple_coils(binding->unit_id, addr, bits);
            }
        } else if (encoded->size() == 1) {
            wr = transport->write_single_register(binding->unit_id, addr, (*encoded)[0]);
        } else {
            wr = transport->write_multiple_registers(binding->unit_id, addr, *encoded);
        }

        if (!wr) {
            publish_quality(deps_.tag_store, batch[i].tag_id, domain::Quality::Bad,
                            domain::QualityReason::WriteRejected, now);
            requeue_from(i + 1);
            if (span) {
                span->set_error(wr.error().message);
            }
            return std::unexpected(wr.error());
        }
        for (std::size_t k = 0; k < consumed; ++k) {
            publish_quality(deps_.tag_store, batch[i + k].tag_id, domain::Quality::Good,
                            domain::QualityReason::None, now, batch[i + k].value);
        }
        i += consumed;
    }
    return {};
}

void Dispatcher::mark_endpoint_bad(std::string_view endpoint_id,
                                   domain::QualityReason reason,
                                   domain::TimestampMs now) {
    for (const auto& binding : deps_.index.tags()) {
        if (binding.endpoint_id != endpoint_id) {
            continue;
        }
        publish_quality(deps_.tag_store, binding.id, domain::Quality::Bad, reason, now);
    }
}

}  // namespace opc::core
