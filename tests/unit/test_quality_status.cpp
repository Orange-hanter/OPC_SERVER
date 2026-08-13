#include <catch2/catch_test_macros.hpp>

#include "adapters/opc_ua_server.hpp"
#include "domain/types.hpp"

#include <open62541/types.h>

using opc::adapters::OpcUaServer;
using opc::domain::Error;
using opc::domain::ErrorCode;
using opc::domain::Quality;
using opc::domain::QualityReason;

TEST_CASE("Quality maps to OPC UA StatusCode per information model", "[unit][opcua][quality]") {
    CHECK(OpcUaServer::quality_to_status(Quality::Good, QualityReason::None) == UA_STATUSCODE_GOOD);
    CHECK(OpcUaServer::quality_to_status(Quality::Uncertain, QualityReason::Stale) ==
          UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE);
    CHECK(OpcUaServer::quality_to_status(Quality::Bad, QualityReason::NoCommunication) ==
          UA_STATUSCODE_BADNOCOMMUNICATION);
    CHECK(OpcUaServer::quality_to_status(Quality::Bad, QualityReason::Timeout) ==
          UA_STATUSCODE_BADNOCOMMUNICATION);
    CHECK(OpcUaServer::quality_to_status(Quality::Bad, QualityReason::DecodingError) ==
          UA_STATUSCODE_BADDECODINGERROR);
    CHECK(OpcUaServer::quality_to_status(Quality::Bad, QualityReason::ModbusException) ==
          UA_STATUSCODE_BADDEVICEFAILURE);
    CHECK(OpcUaServer::quality_to_status(Quality::Bad, QualityReason::WriteRejected) ==
          UA_STATUSCODE_BADWRITENOTSUPPORTED);
    CHECK(OpcUaServer::quality_to_status(Quality::Bad, QualityReason::OutOfRange) ==
          UA_STATUSCODE_BADOUTOFRANGE);
}

TEST_CASE("Write errors map to UA StatusCodes", "[unit][opcua][quality]") {
    Error denied{ErrorCode::Permission, "tag not writable", "core.dispatcher", false};
    CHECK(OpcUaServer::map_error_to_status(denied) == UA_STATUSCODE_BADNOTWRITABLE);

    Error full{ErrorCode::QueueFull, "full", "core.dispatcher", true};
    CHECK(OpcUaServer::map_error_to_status(full) == UA_STATUSCODE_BADRESOURCEUNAVAILABLE);

    Error type{ErrorCode::InvalidArgument, "bad", "core.dispatcher", false};
    CHECK(OpcUaServer::map_error_to_status(type) == UA_STATUSCODE_BADTYPEMISMATCH);
}
