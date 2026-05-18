add_library(CenterCommunicationLib
    src/Signal.cpp
    src/telemetry/HttpPostJson.cpp
    src/telemetry/TelemetryRecord.cpp
    src/telemetry/TelemetryExporter.cpp
    src/telemetry/OtlpLogsExporter.cpp
    src/telemetry/OperationalDbExporter.cpp
)

target_include_directories(CenterCommunicationLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(CenterCommunicationLib PUBLIC nlohmann_json::nlohmann_json)
endif()

find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
    find_package(CURL QUIET)
endif()
if(CURL_FOUND)
    target_link_libraries(CenterCommunicationLib PUBLIC CURL::libcurl)
    target_compile_definitions(CenterCommunicationLib PUBLIC SDV_CENTERCOMM_HAS_LIBCURL=1)
else()
    message(WARNING
        "libcurl development files were not found. "
        "DataCollector will use its raw-socket HTTP fallback. "
        "Install libcurl4-openssl-dev to enable libcurl transport.")
endif()

# --- Optional: MQTT vehicle agent (Paho MQTT C++ + nlohmann/json + spdlog) ---
option(SDV_ENABLE_MQTT_VEHICLE_AGENT "Build MqttVehicleAgent (requires Paho MQTT C++, spdlog, nlohmann/json)" ON)

if(SDV_ENABLE_MQTT_VEHICLE_AGENT)
    find_package(spdlog CONFIG QUIET)
    find_package(PahoMqttCpp CONFIG QUIET)

    if(NOT spdlog_FOUND OR NOT nlohmann_json_FOUND OR NOT PahoMqttCpp_FOUND)
        message(WARNING
            "SDV_ENABLE_MQTT_VEHICLE_AGENT is ON but dependencies were not found. "
            "Disabling MqttVehicleAgent build. "
            "(need: spdlog, nlohmann_json, PahoMqttCpp)")
    else()
        target_sources(CenterCommunicationLib PRIVATE
            src/MqttVehicleAgent.cpp
        )

        # Paho MQTT C++ target name differs by distribution; support common variants.
        if(TARGET PahoMqttCpp::paho-mqttpp3)
            target_link_libraries(CenterCommunicationLib PUBLIC PahoMqttCpp::paho-mqttpp3)
        elseif(TARGET paho-mqttpp3)
            target_link_libraries(CenterCommunicationLib PUBLIC paho-mqttpp3)
        else()
            message(FATAL_ERROR "PahoMqttCpp found but no known imported target is available.")
        endif()

        target_link_libraries(CenterCommunicationLib PUBLIC
            spdlog::spdlog
            nlohmann_json::nlohmann_json
        )

        target_compile_definitions(CenterCommunicationLib PUBLIC SDV_ENABLE_MQTT_VEHICLE_AGENT=1)

        add_executable(MqttVehicleAgentHost
            src/MqttVehicleAgentHost.cpp
        )
        target_include_directories(MqttVehicleAgentHost PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(MqttVehicleAgentHost PRIVATE CenterCommunicationLib EventBus)
        set_target_properties(MqttVehicleAgentHost PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    endif()
endif()
