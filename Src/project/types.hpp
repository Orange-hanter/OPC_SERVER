#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opc::project {

enum class Transport { Tcp, Udp };
enum class SecurityPolicy { None, Basic256Sha256 };
enum class SecurityMode { None, Sign, SignAndEncrypt };
enum class Area { Holding, Input, Coil, Discrete };
enum class TagType { Bool, UInt16, Int16, UInt32, Int32, Float32, Float64 };
enum class Priority { Fast, Normal, Slow };

struct OpcUaUser {
    std::string username;
    std::string password;
};

struct OpcUaSettings {
    std::string endpoint_url{"opc.tcp://0.0.0.0:4840"};
    std::string application_name;
    SecurityPolicy security_policy{SecurityPolicy::None};
    SecurityMode security_mode{SecurityMode::None};
    std::string namespace_uri;
    /// Empty = keep open62541 default (anonymous). Non-empty installs username token AC.
    std::vector<OpcUaUser> users;
    /// Default true when no users; load sets false when users / certificate identity present and field omitted.
    bool allow_anonymous{true};
    /// Lab only: allow username/password over SecurityMode None (plaintext credentials).
    bool allow_none_password{false};
    /// Advertise and accept X509IdentityToken (verified via sessionPKI / --ua-trust).
    bool allow_certificate_identity{false};
    /// Lab only: allow X509IdentityToken over SecurityMode None.
    bool allow_none_certificate{false};
};

struct Endpoint {
    std::string id;
    std::string host;
    std::uint16_t port{502};
    Transport transport{Transport::Tcp};
    int connect_timeout_ms{3000};
    int response_timeout_ms{1000};
    int reconnect_delay_ms{2000};
};

struct Tag {
    std::string name;
    std::string node_path;
    Area area{Area::Holding};
    int address{0};
    TagType type{TagType::UInt16};
    std::optional<int> quantity;
    std::string byte_order;
    double scale{1.0};
    double offset{0.0};
    std::string unit;
    bool writable{false};
    std::string group;
    std::string description;
};

struct DeviceProfile {
    std::string id;
    std::string name;
    std::string vendor;
    std::string description;
    std::vector<Tag> tags;
};

struct Device {
    std::string id;
    std::string endpoint_id;
    int unit_id{1};
    std::string profile_id;
    std::string description;
    std::vector<Tag> tags;
};

struct RegisterBlock {
    Area area{Area::Holding};
    int start{0};
    int count{1};
    std::string description;
};

struct PollGroup {
    std::string id;
    int period_ms{1000};
    Priority priority{Priority::Normal};
    std::string device_id;
    std::vector<RegisterBlock> blocks;
    std::vector<std::string> tag_names;
};

struct Project {
    int schema_version{1};
    std::string name;
    std::string description;
    int address_base{0};
    OpcUaSettings opcua;
    std::vector<Endpoint> endpoints;
    std::vector<DeviceProfile> device_profiles;
    std::vector<Device> devices;
    std::vector<PollGroup> poll_groups;
};

struct Diagnostic {
    enum class Severity { Error, Warning } severity{Severity::Error};
    std::string path;
    std::string message;
};

struct LoadResult {
    bool ok{false};
    Project project;
    std::vector<Diagnostic> diagnostics;
};

}  // namespace opc::project
