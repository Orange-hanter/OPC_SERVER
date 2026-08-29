#include <catch2/catch_test_macros.hpp>

#include "adapters/opc_ua_server.hpp"
#include "adapters/ua_pki.hpp"
#include "app/cli_options.hpp"
#include "ports/i_log.hpp"
#include "project/load.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/config.h>
#ifdef UA_ENABLE_ENCRYPTION
#include <open62541/plugin/pki_default.h>
#endif

#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using opc::adapters::OpcUaServer;
using opc::ports::NullLog;

namespace {

std::uint16_t free_tcp_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t len = sizeof(addr);
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const auto port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

std::shared_ptr<opc::project::Project> x509_project(std::uint16_t port,
                                                    const char* mode,
                                                    bool allow_cert,
                                                    bool allow_none_cert,
                                                    bool allow_anonymous) {
    const bool none_mode = std::string_view(mode) == "None";
    const std::string json = R"({
      "schemaVersion": 1,
      "name": "ua-x509",
      "opcua": {
        "endpointUrl": "opc.tcp://127.0.0.1:)" +
                             std::to_string(port) + R"(",
        "applicationName": "OPC_SERVER X509",
        "securityPolicy": ")" +
                             std::string(none_mode ? "None" : "Basic256Sha256") + R"(",
        "securityMode": ")" +
                             std::string(mode) + R"(",
        "namespaceUri": "urn:opc-server:ua-x509",
        "allowAnonymous": )" +
                             std::string(allow_anonymous ? "true" : "false") + R"(,
        "allowCertificateIdentity": )" +
                             std::string(allow_cert ? "true" : "false") + R"(,
        "allowNoneCertificate": )" +
                             std::string(allow_none_cert ? "true" : "false") + R"(
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 1502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "Temp", "nodePath": "Plant/Temp", "area": "holding",
           "address": 0, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 50, "priority": "fast", "deviceId": "d1", "tagNames": ["Temp"]}
      ]
    })";
    auto loaded = opc::project::load_json_text(json, "x509.json");
    REQUIRE(loaded.ok);
    return std::make_shared<opc::project::Project>(std::move(loaded.project));
}

}  // namespace

TEST_CASE("parse_cli recognizes X509 identity flags", "[app][cli][x509]") {
    const char* argv[] = {"OPC_SERVER", "--ua-allow-certificate-identity",
                          "--ua-allow-none-certificate", "--ua-deny-anonymous"};
    auto opts = opc::app::parse_cli(4, argv);
    REQUIRE(opts.errors.empty());
    CHECK(opts.ua_allow_certificate_identity);
    CHECK(opts.ua_allow_none_certificate);
    CHECK(opts.ua_deny_anonymous);
}

TEST_CASE("project load parses allowCertificateIdentity fail-closed anonymous",
          "[project][opcua][x509]") {
    constexpr std::string_view kJson = R"({
      "schemaVersion": 1,
      "name": "x509-load",
      "opcua": {
        "endpointUrl": "opc.tcp://0.0.0.0:4840",
        "allowCertificateIdentity": true
      },
      "endpoints": [
        {"id": "ep1", "host": "127.0.0.1", "port": 502, "transport": "tcp"}
      ],
      "devices": [
        {"id": "d1", "endpointId": "ep1", "unitId": 1, "tags": [
          {"name": "A", "area": "holding", "address": 0, "type": "uint16", "group": "g1"}
        ]}
      ],
      "pollGroups": [
        {"id": "g1", "periodMs": 100, "priority": "fast", "deviceId": "d1", "tagNames": ["A"]}
      ]
    })";
    const auto loaded = opc::project::load_json_text(kJson, "x509-load.json");
    REQUIRE(loaded.ok);
    CHECK(loaded.project.opcua.allow_certificate_identity);
    CHECK_FALSE(loaded.project.opcua.allow_anonymous);
    CHECK_FALSE(loaded.project.opcua.allow_none_certificate);
}

TEST_CASE("OpcUaServer refuses X509IdentityToken over None without allowNoneCertificate",
          "[opcua][x509]") {
    const auto port = free_tcp_port();
    auto project = x509_project(port, "None", /*allow_cert=*/true, /*allow_none_cert=*/false,
                                /*allow_anonymous=*/false);
    NullLog log;
    OpcUaServer server{&log};
    auto started = server.start(project);
    REQUIRE_FALSE(started);
    CHECK(started.error().message.find("allowNoneCertificate") != std::string::npos);
}

#ifdef UA_ENABLE_ENCRYPTION
#if defined(UA_ENABLE_ENCRYPTION_OPENSSL) || defined(UA_ENABLE_ENCRYPTION_MBEDTLS)
TEST_CASE("OpcUaServer X509IdentityToken accepts trusted user cert and rejects untrusted",
          "[opcua][x509][encryption]") {
    REQUIRE(opc::adapters::ua_encryption_built());
    const auto port = free_tcp_port();
    auto project = x509_project(port, "SignAndEncrypt", /*allow_cert=*/true,
                                /*allow_none_cert=*/false, /*allow_anonymous=*/false);

    auto trusted = opc::adapters::load_or_create_application_cert(
        "urn:opc-server:ua-x509-client", {}, nullptr);
    REQUIRE(trusted);
    auto untrusted = opc::adapters::load_or_create_application_cert(
        "urn:opc-server:ua-x509-untrusted", {}, nullptr);
    REQUIRE(untrusted);

    const auto dir =
        std::filesystem::temp_directory_path() / ("opc-x509-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const auto trust_path = dir / "trusted-client.der";
    write_bytes(trust_path, trusted->first);

    NullLog log;
    opc::adapters::OpcUaSecurityOptions pki{
        .trust_list = {trust_path.string()},
        .accept_untrusted = false,
    };
    OpcUaServer server{&log, nullptr, std::move(pki)};
    REQUIRE(server.start(project));
    server.serve_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    {
        UA_Client* client = UA_Client_new();
        REQUIRE(client != nullptr);
        UA_ClientConfig* cc = UA_Client_getConfig(client);
        UA_ClientConfig_setDefault(cc);
        UA_ByteString cert{};
        cert.length = trusted->first.size();
        cert.data = trusted->first.data();
        UA_ByteString key{};
        key.length = trusted->second.size();
        key.data = trusted->second.data();
        UA_String_clear(&cc->clientDescription.applicationUri);
        cc->clientDescription.applicationUri = UA_STRING_ALLOC("urn:opc-server:ua-x509-client");
        REQUIRE(UA_ClientConfig_setDefaultEncryption(cc, cert, key, nullptr, 0, nullptr, 0) ==
                UA_STATUSCODE_GOOD);
        cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        UA_String_clear(&cc->securityPolicyUri);
        cc->securityPolicyUri =
            UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
        UA_CertificateVerification_AcceptAll(&cc->certificateVerification);
        REQUIRE(UA_ClientConfig_setAuthenticationCert(cc, cert, key) == UA_STATUSCODE_GOOD);
        REQUIRE(UA_Client_connect(client, server.endpoint_url().c_str()) == UA_STATUSCODE_GOOD);

        UA_Variant value;
        UA_Variant_init(&value);
        const UA_NodeId node = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
        REQUIRE(UA_Client_readValueAttribute(client, node, &value) == UA_STATUSCODE_GOOD);
        UA_Variant_clear(&value);
        UA_Client_disconnect(client);
        UA_Client_delete(client);
    }

    {
        UA_Client* client = UA_Client_new();
        REQUIRE(client != nullptr);
        UA_ClientConfig* cc = UA_Client_getConfig(client);
        UA_ClientConfig_setDefault(cc);
        UA_ByteString channel_cert{};
        channel_cert.length = trusted->first.size();
        channel_cert.data = trusted->first.data();
        UA_ByteString channel_key{};
        channel_key.length = trusted->second.size();
        channel_key.data = trusted->second.data();
        UA_ByteString auth_cert{};
        auth_cert.length = untrusted->first.size();
        auth_cert.data = untrusted->first.data();
        UA_ByteString auth_key{};
        auth_key.length = untrusted->second.size();
        auth_key.data = untrusted->second.data();
        UA_String_clear(&cc->clientDescription.applicationUri);
        cc->clientDescription.applicationUri = UA_STRING_ALLOC("urn:opc-server:ua-x509-client");
        REQUIRE(UA_ClientConfig_setDefaultEncryption(cc, channel_cert, channel_key, nullptr, 0,
                                                     nullptr, 0) == UA_STATUSCODE_GOOD);
        cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        UA_String_clear(&cc->securityPolicyUri);
        cc->securityPolicyUri =
            UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
        UA_CertificateVerification_AcceptAll(&cc->certificateVerification);
        REQUIRE(UA_ClientConfig_setAuthenticationCert(cc, auth_cert, auth_key) ==
                UA_STATUSCODE_GOOD);
        const auto bad = UA_Client_connect(client, server.endpoint_url().c_str());
        CHECK(bad != UA_STATUSCODE_GOOD);
        UA_Client_delete(client);
    }

    {
        UA_Client* anon = UA_Client_new();
        REQUIRE(anon != nullptr);
        UA_ClientConfig* cc = UA_Client_getConfig(anon);
        UA_ClientConfig_setDefault(cc);
        UA_ByteString cert{};
        cert.length = trusted->first.size();
        cert.data = trusted->first.data();
        UA_ByteString key{};
        key.length = trusted->second.size();
        key.data = trusted->second.data();
        UA_String_clear(&cc->clientDescription.applicationUri);
        cc->clientDescription.applicationUri = UA_STRING_ALLOC("urn:opc-server:ua-x509-client");
        REQUIRE(UA_ClientConfig_setDefaultEncryption(cc, cert, key, nullptr, 0, nullptr, 0) ==
                UA_STATUSCODE_GOOD);
        cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        UA_String_clear(&cc->securityPolicyUri);
        cc->securityPolicyUri =
            UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
        UA_CertificateVerification_AcceptAll(&cc->certificateVerification);
        const auto denied = UA_Client_connect(anon, server.endpoint_url().c_str());
        CHECK(denied != UA_STATUSCODE_GOOD);
        UA_Client_delete(anon);
    }

    server.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
#endif
#else
TEST_CASE("X509IdentityToken fails closed without encryption build", "[opcua][x509]") {
    CHECK_FALSE(opc::adapters::ua_encryption_built());
    const auto port = free_tcp_port();
    auto project = x509_project(port, "SignAndEncrypt", true, false, false);
    NullLog log;
    OpcUaServer server{&log, nullptr};
    auto started = server.start(project);
    REQUIRE_FALSE(started);
}
#endif
