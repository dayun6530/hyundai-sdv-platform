set(UNIT_TEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)

    if(TARGET EventBus)
        target_compile_options(EventBus PRIVATE --coverage -O0 -g)
        target_link_options(EventBus PRIVATE --coverage)
    endif()
    if(TARGET LifeCycleManagerLib)
        target_compile_options(LifeCycleManagerLib PRIVATE --coverage -O0 -g)
        target_link_options(LifeCycleManagerLib PRIVATE --coverage)
    endif()
    if(TARGET CenterCommunicationLib)
        target_compile_options(CenterCommunicationLib PRIVATE --coverage -O0 -g)
        target_link_options(CenterCommunicationLib PRIVATE --coverage)
    endif()
endif()

function(add_sqa_unit_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_include_directories(${target_name}
        PRIVATE
            ${UNIT_TEST_INCLUDE_DIR}
            ${CMAKE_SOURCE_DIR}/tests/mockup
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src/Feature/include
            ${CMAKE_SOURCE_DIR}/src/Platform/LifeCycleManager/include
            ${CMAKE_SOURCE_DIR}/src/Platform/FeatureCommunication/EventBus/include
            ${CMAKE_SOURCE_DIR}/src/Platform/CenterCommunication/include
            ${CMAKE_BINARY_DIR}/src/Platform/FeatureCommunication/EventBus
            ${CMAKE_BINARY_DIR}/src/Platform/FeatureCommunication/EventBus/proto
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_sqa_unit_test(
    unit_event
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/communications/eventbus/EventTest.cpp
)
target_link_libraries(unit_event PRIVATE EventBus)

add_sqa_unit_test(
    unit_event_queue
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/communications/eventbus/EventQueueTest.cpp
)
target_link_libraries(unit_event_queue PRIVATE EventBus)

add_sqa_unit_test(
    unit_event_bus_interface
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/communications/eventbus/EventBusInterfaceTest.cpp
)
target_link_libraries(unit_event_bus_interface PRIVATE EventBus)

add_sqa_unit_test(
    unit_event_bus_proxy
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/communications/eventbus/EventBusProxyTest.cpp
)
target_link_libraries(unit_event_bus_proxy PRIVATE EventBus)

add_sqa_unit_test(
    unit_event_bus_host
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/communications/eventbus/EventBusHostTest.cpp
)
target_link_libraries(unit_event_bus_host PRIVATE EventBus)

add_sqa_unit_test(
    unit_feature_base
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/lifecyclemgr/FeatureTest.cpp
)
target_link_libraries(unit_feature_base PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_lifecycle_mgr
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/lifecyclemgr/LifeCycleMgrTest.cpp
)
target_link_libraries(unit_lifecycle_mgr PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_watchdog
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/lifecyclemgr/WatchDogTest.cpp
)
target_link_libraries(unit_watchdog PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_adas_feature
    ${CMAKE_CURRENT_SOURCE_DIR}/Feature/AdasFeatureTest.cpp
)
target_link_libraries(unit_adas_feature PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_ev_energy_feature
    ${CMAKE_CURRENT_SOURCE_DIR}/Feature/EvEnergyFeatureTest.cpp
)
target_link_libraries(unit_ev_energy_feature PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_navigation_feature
    ${CMAKE_CURRENT_SOURCE_DIR}/Feature/NavigationFeatureTest.cpp
)
target_link_libraries(unit_navigation_feature PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_flag
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/lifecyclemgr/FlagTest.cpp
)
target_link_libraries(unit_flag PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_log_data
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/lifecyclemgr/LogDataTest.cpp
)
target_link_libraries(unit_log_data PRIVATE LifeCycleManagerLib)

add_sqa_unit_test(
    unit_feature_evaluator
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/lifecyclemgr/FeatureEvaluatorTest.cpp
)
target_link_libraries(unit_feature_evaluator PRIVATE EventBus LifeCycleManagerLib)

add_sqa_unit_test(
    unit_signal
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/centercommunication/SignalTest.cpp
)
target_link_libraries(unit_signal PRIVATE CenterCommunicationLib)

add_sqa_unit_test(
    unit_signal_receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/centercommunication/SignalReceiverTest.cpp
)
target_sources(unit_signal_receiver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Platform/CenterCommunication/src/SignalReceiver.cpp
)
target_compile_definitions(unit_signal_receiver PRIVATE SDV_CENTERCOMM_NO_MAIN=1)
target_link_libraries(unit_signal_receiver PRIVATE CenterCommunicationLib EventBus)

add_sqa_unit_test(
    unit_signal_sender
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/centercommunication/SignalSenderTest.cpp
)
target_sources(unit_signal_sender PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Platform/CenterCommunication/src/SignalSender.cpp
)
target_compile_definitions(unit_signal_sender PRIVATE SDV_CENTERCOMM_NO_MAIN=1)
target_link_libraries(unit_signal_sender PRIVATE CenterCommunicationLib EventBus)

add_sqa_unit_test(
    unit_data_collector
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform/centercommunication/DataCollectorTest.cpp
)
target_link_libraries(unit_data_collector PRIVATE CenterCommunicationLib)
