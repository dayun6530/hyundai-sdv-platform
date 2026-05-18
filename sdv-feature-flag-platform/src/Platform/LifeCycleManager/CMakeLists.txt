# Shared fleet version helpers (Feature.cpp + *Shadow executables that compile Feature.cpp separately).
add_library(FleetVersionResolveLib STATIC src/FleetVersionResolve.cpp)

target_include_directories(FleetVersionResolveLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(LifeCycleManagerLib
    src/Feature.cpp
    src/Flag.cpp
    src/LogData.cpp
)

target_include_directories(LifeCycleManagerLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(LifeCycleManagerLib
    PUBLIC
        EventBus
        FleetVersionResolveLib
)

find_package(nlohmann_json 3.2.0 QUIET CONFIG)
if(nlohmann_json_FOUND)
    target_link_libraries(LifeCycleManagerLib PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(LifeCycleManagerLib PUBLIC SDV_LIFECYCLE_HAS_NJSON=1)
else()
    target_compile_definitions(LifeCycleManagerLib PUBLIC SDV_LIFECYCLE_HAS_NJSON=0)
endif()

if(TARGET unleash_sdk)
    target_link_libraries(LifeCycleManagerLib PUBLIC unleash_sdk)
    target_compile_definitions(LifeCycleManagerLib PUBLIC SDV_WITH_UNLEASH=1)
endif()
