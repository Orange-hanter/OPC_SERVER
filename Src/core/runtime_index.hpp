#pragma once

#include "domain/types.hpp"
#include "project/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opc::core {

struct TagBinding {
    domain::TagId id{0};
    std::string device_id;
    std::string endpoint_id;
    std::uint8_t unit_id{1};
    project::Tag tag;
};

/// Immutable index built once from Project (ADR-0005).
class RuntimeIndex {
public:
    static RuntimeIndex build(std::shared_ptr<const project::Project> project);

    [[nodiscard]] const project::Project& project() const { return *project_; }
    [[nodiscard]] std::shared_ptr<const project::Project> project_ptr() const { return project_; }

    [[nodiscard]] std::optional<TagBinding> find_by_id(domain::TagId id) const;
    [[nodiscard]] std::optional<TagBinding> find_by_name(std::string_view name) const;
    [[nodiscard]] const std::vector<TagBinding>& tags() const { return tags_; }

    [[nodiscard]] const project::Endpoint* endpoint(std::string_view id) const;
    [[nodiscard]] const project::Device* device(std::string_view id) const;
    [[nodiscard]] std::vector<const project::PollGroup*> groups_for_endpoint(
        std::string_view endpoint_id) const;

private:
    std::shared_ptr<const project::Project> project_;
    std::vector<TagBinding> tags_;
    std::unordered_map<domain::TagId, std::size_t> by_id_;
    std::unordered_map<std::string, std::size_t> by_name_;
};

}  // namespace opc::core
