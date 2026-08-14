add_library(opc_adapters
  asio_reactor.cpp
  modbus_tcp_transport.cpp
  modbus_udp_transport.cpp
  modbus_protocol.cpp
  ua_pki.cpp
  opc_ua_server.cpp
  ring_historian.cpp
  sqlite_historian.cpp
  frame_log.cpp
  frame_replay.cpp
  memory_metrics.cpp
  spdlog_log.cpp
  otel_metrics.cpp
)
add_library(opc::adapters ALIAS opc_adapters)

target_link_libraries(opc_adapters
  PUBLIC
    opc::project
    SQLite::SQLite3
    spdlog::spdlog
    opentelemetry_metrics
    opentelemetry_exporter_ostream_metrics
    opentelemetry_resources
    opentelemetry_common
  PRIVATE
    opc::asio
    "${OPC_OPEN62541_TARGET}"
)
if(OPC_WITH_OTLP)
  target_compile_definitions(opc_adapters PUBLIC OPC_WITH_OTLP=1)
  target_link_libraries(opc_adapters PUBLIC opentelemetry_exporter_otlp_http_metric)
endif()
target_include_directories(opc_adapters PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Src>"
)
opc_apply_project_options(opc_adapters)
