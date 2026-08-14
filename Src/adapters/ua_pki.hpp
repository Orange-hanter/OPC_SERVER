#pragma once

#include "domain/types.hpp"
#include "ports/i_log.hpp"
#include "project/types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opc::adapters {

struct OpcUaSecurityOptions {
    std::string certificate_path;
    std::string private_key_path;
    std::vector<std::string> trust_list;
    std::vector<std::string> revocation_list;  // CRL / reject list files
    bool generate_if_missing{true};
    /// Lab only. Industrial Sign/Encrypt defaults to false (no AcceptAll).
    bool accept_untrusted{false};
};

[[nodiscard]] bool ua_encryption_built();

[[nodiscard]] const char* ua_security_policy_uri(project::SecurityPolicy policy);
[[nodiscard]] int ua_message_security_mode(project::SecurityMode mode);

[[nodiscard]] domain::Result<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>>
load_or_create_application_cert(std::string_view application_uri,
                                const OpcUaSecurityOptions& options,
                                ports::ILog* log = nullptr);

}  // namespace opc::adapters
