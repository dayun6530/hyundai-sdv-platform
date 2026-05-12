cmake_minimum_required(VERSION 3.16)
project(SDV_Dashboard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets)

find_package(Qt6 QUIET COMPONENTS Mqtt)
if(TARGET Qt6::Mqtt)
    set(SDV_HAVE_QT_MQTT TRUE)
else()
    set(SDV_HAVE_QT_MQTT FALSE)
    message(STATUS "Qt6 Mqtt not found — dashboard will use HTTP polling only for fleet state (install Qt MQTT for live flag/HUD sync).")
endif()

set(SDV_DASH_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/DashboardDataHub.cpp
    src/VehicleHUDView.cpp
    src/FeatureStatusWidget.cpp
    src/OTADeploymentWidget.cpp
    src/ABTestingWidget.cpp
    src/ShadowCompareWidget.cpp
    src/HealthMonitorWidget.cpp
    src/EventTimelineWidget.cpp
)

add_executable(SDV_Dashboard ${SDV_DASH_SOURCES})

target_include_directories(SDV_Dashboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(SDV_Dashboard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)

if(SDV_HAVE_QT_MQTT)
    target_link_libraries(SDV_Dashboard PRIVATE Qt6::Mqtt)
    target_compile_definitions(SDV_Dashboard PRIVATE SDV_HAVE_QT_MQTT=1)
endif()

if(MSVC)
    target_compile_definitions(SDV_Dashboard PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(GNUInstallDirs)
install(TARGETS SDV_Dashboard RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
