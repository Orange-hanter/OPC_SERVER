#pragma once

#include <open62541/client.h>
#include <open62541/client_config_default.h>

#include <memory>

// UA_Client_new already calls UA_ClientConfig_setDefault. Do not call it again.
// SignAndEncrypt / X.509 handshake under Valgrind exceeds the 5s default timeout
// (nightly saw BadTimeout after ~16s of OpenSSL channel setup).
inline constexpr UA_UInt32 kOpcUaClientTimeoutMs = 60'000;

struct UaClientDeleter {
    void operator()(UA_Client* client) const noexcept {
        if (client != nullptr) {
            UA_Client_delete(client);
        }
    }
};

using UniqueUaClient = std::unique_ptr<UA_Client, UaClientDeleter>;

inline UniqueUaClient make_ua_client(UA_UInt32 timeout_ms = kOpcUaClientTimeoutMs) {
    UniqueUaClient client{UA_Client_new()};
    if (client) {
        UA_Client_getConfig(client.get())->timeout = timeout_ms;
    }
    return client;
}
