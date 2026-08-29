#include "adapters/ua_pki.hpp"

#include "ports/i_log.hpp"

#include <open62541/config.h>
#ifdef UA_ENABLE_ENCRYPTION
#include <open62541/plugin/create_certificate.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/types.h>
#endif

#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace opc::adapters {
namespace {

void log_msg(ports::ILog* log, ports::LogLevel level, std::string_view msg) {
    if (log != nullptr) {
        log->log(level, "adapters.opcua.pki", msg);
    }
}

domain::Result<std::vector<std::uint8_t>> read_all_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(domain::Error{domain::ErrorCode::InvalidArgument,
                                             "cannot read PKI file: " + path, "adapters.opcua.pki", false});
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

bool ua_encryption_built() {
#ifdef UA_ENABLE_ENCRYPTION
    return true;
#else
    return false;
#endif
}

const char* ua_security_policy_uri(project::SecurityPolicy policy) {
    switch (policy) {
    case project::SecurityPolicy::Basic256Sha256:
        return "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256";
    case project::SecurityPolicy::None:
        return "http://opcfoundation.org/UA/SecurityPolicy#None";
    }
    return "http://opcfoundation.org/UA/SecurityPolicy#None";
}

int ua_message_security_mode(project::SecurityMode mode) {
    switch (mode) {
    case project::SecurityMode::Sign:
        return 2;  // UA_MESSAGESECURITYMODE_SIGN
    case project::SecurityMode::SignAndEncrypt:
        return 3;  // UA_MESSAGESECURITYMODE_SIGNANDENCRYPT
    case project::SecurityMode::None:
        return 1;  // UA_MESSAGESECURITYMODE_NONE
    }
    return 1;
}

domain::Result<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>>
load_or_create_application_cert(std::string_view application_uri,
                                const OpcUaSecurityOptions& options,
                                ports::ILog* log) {
    if (!options.certificate_path.empty() && !options.private_key_path.empty()) {
        auto cert = read_all_bytes(options.certificate_path);
        if (!cert) {
            return std::unexpected(cert.error());
        }
        auto key = read_all_bytes(options.private_key_path);
        if (!key) {
            return std::unexpected(key.error());
        }
        return std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>{std::move(*cert),
                                                                               std::move(*key)};
    }
    if (!options.generate_if_missing) {
        return std::unexpected(domain::Error{
            domain::ErrorCode::InvalidArgument,
            "Sign/Encrypt requires --ua-cert and --ua-key (or enable self-signed generation)",
            "adapters.opcua.pki",
            false});
    }
#ifdef UA_ENABLE_ENCRYPTION
    const std::string uri =
        application_uri.empty() ? std::string{"urn:opc-server:application"} : std::string{application_uri};
    UA_String subject[3] = {UA_STRING_STATIC("C=RU"), UA_STRING_STATIC("O=OPC_SERVER"),
                            UA_STRING_STATIC("CN=OPC_SERVER")};
    const std::string san_uri = "URI:" + uri;
    UA_String subject_alt[2] = {UA_STRING_STATIC("DNS:localhost"),
                                UA_STRING_ALLOC(san_uri.c_str())};
    UA_ByteString key = UA_BYTESTRING_NULL;
    UA_ByteString cert = UA_BYTESTRING_NULL;
    UA_KeyValueMap* kvm = UA_KeyValueMap_new();
    UA_UInt16 expires = 365;
    UA_UInt16 bits = 2048;
    UA_KeyValueMap_setScalar(kvm, UA_QUALIFIEDNAME(0, "expires-in-days"), &expires, &UA_TYPES[UA_TYPES_UINT16]);
    UA_KeyValueMap_setScalar(kvm, UA_QUALIFIEDNAME(0, "key-size-bits"), &bits, &UA_TYPES[UA_TYPES_UINT16]);
    const auto status =
        UA_CreateCertificate(UA_Log_Stdout, subject, 3, subject_alt, 2, UA_CERTIFICATEFORMAT_DER, kvm, &key,
                             &cert);
    UA_String_clear(&subject_alt[1]);
    UA_KeyValueMap_delete(kvm);
    if (status != UA_STATUSCODE_GOOD) {
        return std::unexpected(domain::Error{domain::ErrorCode::Internal,
                                             std::string("UA_CreateCertificate failed: ") +
                                                 UA_StatusCode_name(status),
                                             "adapters.opcua.pki",
                                             false});
    }
    log_msg(log, ports::LogLevel::Info, "generated self-signed OPC UA application certificate");
    std::vector<std::uint8_t> cert_bytes(cert.data, cert.data + cert.length);
    std::vector<std::uint8_t> key_bytes(key.data, key.data + key.length);
    UA_ByteString_clear(&cert);
    UA_ByteString_clear(&key);
    return std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>{std::move(cert_bytes),
                                                                           std::move(key_bytes)};
#else
    (void)application_uri;
    (void)log;
    return std::unexpected(domain::Error{domain::ErrorCode::NotImplemented,
                                         "open62541 was built without UA_ENABLE_ENCRYPTION",
                                         "adapters.opcua.pki", false});
#endif
}

}  // namespace opc::adapters
