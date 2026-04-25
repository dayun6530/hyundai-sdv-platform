cmake_minimum_required(VERSION 3.16)
project(sdv-feature-flag-platform
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "SDV Automotive Feature Flag Platform"
)

if(NOT UNIX)
    message(FATAL_ERROR "This platform only supports Linux builds")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(src/Platform)
add_subdirectory(src/Feature)
add_subdirectory(tests)

add_executable(SdvPlatform src/Platform/LifeCycleManager/src/LifeCycleMgr.cpp)
target_include_directories(SdvPlatform PRIVATE src/Platform/LifeCycleManager/include)

# Generalized experiment groups: car_1 .. car_10 (choose config by argv[0]).
foreach(i RANGE 1 10)
    add_executable(car_${i} src/Platform/LifeCycleManager/src/LifeCycleMgr.cpp)
    target_include_directories(car_${i} PRIVATE src/Platform/LifeCycleManager/include)
endforeach()

add_executable(EventBusHost src/Platform/FeatureCommunication/EventBus/src/EventBusHost.cpp)
target_link_libraries(EventBusHost PRIVATE EventBus)

add_executable(WatchDog src/Platform/LifeCycleManager/src/WatchDog.cpp)
target_link_libraries(WatchDog PRIVATE LifeCycleManagerLib)

add_executable(FeatureEvaluator src/Platform/LifeCycleManager/src/FeatureEvaluator.cpp)
target_link_libraries(FeatureEvaluator PRIVATE LifeCycleManagerLib)

add_executable(DataCollector src/Platform/CenterCommunication/src/DataCollector.cpp)
target_link_libraries(DataCollector PRIVATE CenterCommunicationLib)

add_executable(SignalReceiver src/Platform/CenterCommunication/src/SignalReceiver.cpp)
target_link_libraries(SignalReceiver PRIVATE CenterCommunicationLib)

add_executable(SignalSender src/Platform/CenterCommunication/src/SignalSender.cpp)
target_link_libraries(SignalSender PRIVATE CenterCommunicationLib)

# Symlink compile_commands.json to repo root for clangd/Cursor (best-effort; needs Unix + existing build dir).
if(CMAKE_EXPORT_COMPILE_COMMANDS AND UNIX)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_SOURCE_DIR}/compile_commands.json"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ERROR_QUIET)
endif()
