cmake_minimum_required(VERSION 3.16)
project(SDV_Dashboard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets WebChannel)

find_package(Qt6 QUIET COMPONENTS StateMachine)
if(TARGET Qt6::StateMachine)
    set(SDV_HAVE_QT_STATEMACHINE TRUE)
else()
    set(SDV_HAVE_QT_STATEMACHINE FALSE)
    message(STATUS "Qt6 StateMachine not installed — OTA uses equivalent transition-table state machine.")
endif()

find_package(Qt6 QUIET COMPONENTS WebSockets)
if(TARGET Qt6::WebSockets)
    set(SDV_HAVE_QT_WEBSOCKETS TRUE)
else()
    set(SDV_HAVE_QT_WEBSOCKETS FALSE)
    message(WARNING "Qt6 WebSockets not found — ROS bridge ingest disabled. Install Qt WebSockets via Qt Maintenance Tool.")
endif()

find_package(Qt6 QUIET COMPONENTS Mqtt)
if(TARGET Qt6::Mqtt)
    set(SDV_HAVE_QT_MQTT TRUE)
else()
    set(SDV_HAVE_QT_MQTT FALSE)
    message(STATUS "Qt6 Mqtt not found — dashboard will use HTTP polling only for fleet state (install Qt MQTT for live flag/HUD sync).")
endif()

set(SDV_OPS_CORE_SOURCES
    src/ops/domain/TelemetryParser.cpp
    src/ops/ingest/SignalManager.cpp
    src/ops/viewmodels/EgoViewModel.cpp
    src/ops/viewmodels/TrajectoryViewModel.cpp
    src/ops/viewmodels/PerceptionViewModel.cpp
)

set(SDV_DASH_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/DashboardDataHub.cpp
    src/VehicleHUDView.cpp
    src/FeatureStatusWidget.cpp
    src/OTADeploymentWidget.cpp
    src/modules/ota/OtaTypes.cpp
    src/modules/ota/OtaStateMachine.cpp
    src/modules/ota/OtaDeploymentService.cpp
    src/modules/ota/RollbackController.cpp
    src/modules/ota/OtaDeploymentViewModel.cpp
    src/ABTestingWidget.cpp
    src/ShadowCompareWidget.cpp
    src/HealthMonitorWidget.cpp
    src/EventTimelineWidget.cpp
    src/ui/DashboardTheme.cpp
    src/ui/UiAnimation.cpp
    src/ui/OtaFlowDiagramWidget.cpp
    src/ui/GlassPanel.cpp
    src/ui/NavSidebar.cpp
    src/ui/TopBar.cpp
    src/ui/CockpitHudOverlay.cpp
    src/ui/CockpitInstrumentDock.cpp
    src/ui/CockpitMapHud.cpp
    src/ui/SteeringWheelOverlay.cpp
    src/ui/CockpitPanel.cpp
    src/mapbox/MapboxBridge.cpp
    src/mapbox/MapboxTokenLoader.cpp
    src/mapbox/MapboxWebConfig.cpp
    src/mapbox/MiniMapView.cpp
    src/mapbox/MapboxCockpitView.cpp
    src/carla/CarlaCameraReceiver.cpp
    src/carla/CarlaCockpitView.cpp
    src/carla/FsdPaintKit.cpp
    src/carla/CinematicOverlay.cpp
    src/carla/HudOverlayWidget.cpp
    src/carla/LaneOverlayRenderer.cpp
    src/carla/NavigationOverlayRenderer.cpp
    src/carla/PerceptionOverlayRenderer.cpp
    src/carla/integration/CarlaApiAdapter.cpp
    src/carla/integration/CarlaTelemetrySerializer.cpp
    src/carla/integration/PerceptionExtractor.cpp
    src/carla/integration/LaneExtractor.cpp
    src/carla/integration/HudViewModel.cpp
    src/carla/integration/CarlaSimAdapter.cpp
    src/carla/integration/CarlaWebSocketAdapter.cpp
    src/carla/integration/CarlaWsPublisher.cpp
    src/carla/integration/CarlaDataBridge.cpp
    src/carla/integration/CarlaIntegrationController.cpp
    src/carla/control/CarlaVehicleController.cpp
    src/carla/control/CarlaControlBridge.cpp
    src/carla/control/CarlaControlManager.cpp
    src/carla/control/CarlaControlPanel.cpp
    src/carla/TrafficLightOverlayRenderer.cpp
    src/twin/TwinSnapshotParser.cpp
    src/twin/DigitalTwinController.cpp
    src/twin/adapters/CarlaTwinAdapters.cpp
    src/twin/adapters/AutowareTwinAdapters.cpp
    src/mapbox/mapbox.qrc
    ${SDV_OPS_CORE_SOURCES}
    src/examples/OpsIngestIntegrationExample.cpp
)

if(SDV_HAVE_QT_WEBSOCKETS)
    list(APPEND SDV_DASH_SOURCES
        src/ops/ingest/RosBridgeClient.cpp
        src/ops/OpsTelemetryPipeline.cpp
    )
else()
    list(APPEND SDV_DASH_SOURCES src/ops/OpsTelemetryPipelineStub.cpp)
endif()

add_executable(SDV_Dashboard ${SDV_DASH_SOURCES})
target_sources(SDV_Dashboard PRIVATE
    src/ops/OpsTelemetryPipeline.h
    src/mapbox/MapboxBridge.h
    src/modules/ota/OtaStateMachine.h
    src/modules/ota/OtaDeploymentService.h
    src/modules/ota/OtaDeploymentViewModel.h
)

target_include_directories(SDV_Dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(SDV_Dashboard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)

if(SDV_HAVE_QT_STATEMACHINE)
    target_link_libraries(SDV_Dashboard PRIVATE Qt6::StateMachine)
    target_compile_definitions(SDV_Dashboard PRIVATE SDV_HAVE_QT_STATEMACHINE=1)
endif()

if(SDV_HAVE_QT_WEBSOCKETS)
    target_link_libraries(SDV_Dashboard PRIVATE Qt6::WebSockets)
    target_compile_definitions(SDV_Dashboard PRIVATE SDV_HAVE_QT_WEBSOCKETS=1)
endif()

add_executable(ops_ingest_smoke tests/ops_ingest_smoke.cpp ${SDV_OPS_CORE_SOURCES})
target_include_directories(ops_ingest_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ops_ingest_smoke PRIVATE Qt6::Core)

if(SDV_HAVE_QT_WEBSOCKETS)
    add_executable(ops_ws_bridge_smoke
        tests/ops_ws_bridge_smoke.cpp
        src/ops/ingest/RosBridgeClient.cpp
        src/ops/OpsTelemetryPipeline.cpp
        ${SDV_OPS_CORE_SOURCES}
    )
    target_include_directories(ops_ws_bridge_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_sources(ops_ws_bridge_smoke PRIVATE src/ops/OpsTelemetryPipeline.h)
    target_link_libraries(ops_ws_bridge_smoke PRIVATE Qt6::Core Qt6::WebSockets)
    target_compile_definitions(ops_ws_bridge_smoke PRIVATE SDV_HAVE_QT_WEBSOCKETS=1)
endif()

if(SDV_HAVE_QT_MQTT)
    target_link_libraries(SDV_Dashboard PRIVATE Qt6::Mqtt)
    target_compile_definitions(SDV_Dashboard PRIVATE SDV_HAVE_QT_MQTT=1)
endif()

if(MSVC)
    target_compile_definitions(SDV_Dashboard PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(GNUInstallDirs)
install(TARGETS SDV_Dashboard RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
