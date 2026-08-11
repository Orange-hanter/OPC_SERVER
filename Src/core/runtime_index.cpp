#include "core/runtime_index.hpp"

namespace opc::core {

RuntimeIndex RuntimeIndex::build(std::shared_ptr<const project::Project> project) {
    RuntimeIndex index;
    index.project_ = std::move(project);
    domain::TagId next = 1;
    for (const auto& device : index.project_->devices) {
        for (const auto& tag : device.tags) {
            TagBinding binding;
            binding.id = next++;
            binding.device_id = device.id;
            binding.endpoint_id = device.endpoint_id;
            binding.unit_id = static_cast<std::uint8_t>(device.unit_id);
            binding.tag = tag;
            const auto pos = index.tags_.size();
            index.by_id_.emplace(binding.id, pos);
            index.by_name_.emplace(binding.tag.name, pos);
            index.tags_.push_back(std::move(binding));
        }
    }
    return index;
}

std::optional<TagBinding> RuntimeIndex::find_by_id(domain::TagId id) const {
    const auto it = by_id_.find(id);
    if (it == by_id_.end()) {
        return std::nullopt;
    }
    return tags_[it->second];
}

std::optional<TagBinding> RuntimeIndex::find_by_name(std::string_view name) const {
    const auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) {
        return std::nullopt;
    }
    return tags_[it->second];
}

const project::Endpoint* RuntimeIndex::endpoint(std::string_view id) const {
    for (const auto& ep : project_->endpoints) {
        if (ep.id == id) {
            return &ep;
        }
    }
    return nullptr;
}

const project::Device* RuntimeIndex::device(std::string_view id) const {
    for (const auto& d : project_->devices) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

std::vector<const project::PollGroup*>
RuntimeIndex::groups_for_endpoint(std::string_view endpoint_id) const {
    std::vector<const project::PollGroup*> out;
    for (const auto& group : project_->poll_groups) {
        const auto* dev = device(group.device_id);
        if (dev != nullptr && dev->endpoint_id == endpoint_id) {
            out.push_back(&group);
        }
    }
    return out;
}

}  // namespace opc::core
