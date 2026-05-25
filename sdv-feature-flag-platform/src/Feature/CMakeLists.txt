set(FEATURE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(LCM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Platform/LifeCycleManager/src)
set(LCM_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Platform/LifeCycleManager/include)

# *Shadow binaries compile Feature.cpp again with SDV_SHADOW_ENTRY_MAIN (no LifeCycleManagerLib — avoids duplicate symbols).
# Link FleetVersionResolveLib only (fleet version logging helpers used from Feature.cpp).
macro(configure_shadow_executable target_name shadow_logical_name primary_logical_name)
    add_executable(${target_name}
        ${LCM_SRC_DIR}/Feature.cpp
        ${LCM_SRC_DIR}/Flag.cpp
        ${LCM_SRC_DIR}/LogData.cpp
        ${LCM_SRC_DIR}/TelemetryRuntimeConfig.cpp)
    target_include_directories(${target_name} PRIVATE ${FEATURE_INCLUDE} ${LCM_INC_DIR})
    target_compile_definitions(${target_name} PRIVATE
        SDV_SHADOW_ENTRY_MAIN=1
        "SDV_SHADOW_LOGICAL_NAME=\"${shadow_logical_name}\""
        "SDV_SHADOW_PRIMARY_NAME=\"${primary_logical_name}\"")
    target_link_libraries(${target_name} PRIVATE EventBus FleetVersionResolveLib)
    if(TARGET nlohmann_json::nlohmann_json)
        target_link_libraries(${target_name} PRIVATE nlohmann_json::nlohmann_json)
    endif()
    set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endmacro()

add_executable(NavigationFeature src/NavigationFeature.cpp)
target_include_directories(NavigationFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(NavigationFeature PRIVATE LifeCycleManagerLib)
set_target_properties(NavigationFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

configure_shadow_executable(NavigationFeatureShadow NavigationFeatureShadow NavigationFeature)

add_executable(AdasFeature src/AdasFeature.cpp)
target_include_directories(AdasFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(AdasFeature PRIVATE LifeCycleManagerLib)
set_target_properties(AdasFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

configure_shadow_executable(AdasFeatureShadow AdasFeatureShadow AdasFeature)

add_executable(EvEnergyFeature src/EvEnergyFeature.cpp)
target_include_directories(EvEnergyFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(EvEnergyFeature PRIVATE LifeCycleManagerLib)
set_target_properties(EvEnergyFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

configure_shadow_executable(EvEnergyFeatureShadow EvEnergyFeatureShadow EvEnergyFeature)
