set(FEATURE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(LCM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Platform/LifeCycleManager/src)
set(LCM_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Platform/LifeCycleManager/include)

# *Shadow binaries compile Feature.cpp again with SDV_SHADOW_ENTRY_MAIN (no LifeCycleManagerLib — avoids duplicate symbols).
macro(configure_shadow_executable target_name shadow_logical_name primary_logical_name)
    add_executable(${target_name}
        ${LCM_SRC_DIR}/Feature.cpp
        ${LCM_SRC_DIR}/Flag.cpp
        ${LCM_SRC_DIR}/LogData.cpp)
    target_include_directories(${target_name} PRIVATE ${FEATURE_INCLUDE} ${LCM_INC_DIR})
    target_compile_definitions(${target_name} PRIVATE
        SDV_SHADOW_ENTRY_MAIN=1
        "SDV_SHADOW_LOGICAL_NAME=\"${shadow_logical_name}\""
        "SDV_SHADOW_PRIMARY_NAME=\"${primary_logical_name}\"")
    target_link_libraries(${target_name} PRIVATE EventBus)
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

# --- Car (EA): A|B line features — link domain objects without duplicate main ---
set(CAR_DOMAIN_SRCS
    src/AdasFeature.cpp
    src/NavigationFeature.cpp
    src/EvEnergyFeature.cpp
    src/car/Car.cpp
)

function(add_car_line_executable target_name entry_cpp)
    add_executable(${target_name} ${entry_cpp} ${CAR_DOMAIN_SRCS})
    target_include_directories(${target_name} PRIVATE ${FEATURE_INCLUDE})
    target_link_libraries(${target_name} PRIVATE LifeCycleManagerLib)
    target_compile_definitions(${target_name} PRIVATE SDV_FEATURE_NO_MAIN)
    set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

add_car_line_executable(AdasFeature_A src/car/A/AdasFeature_A.cpp)
add_car_line_executable(AdasFeature_B src/car/B/AdasFeature_B.cpp)
add_car_line_executable(NavigationFeature_A src/car/A/NavigationFeature_A.cpp)
add_car_line_executable(NavigationFeature_B src/car/B/NavigationFeature_B.cpp)
add_car_line_executable(EvEnergyFeature_A src/car/A/EvEnergyFeature_A.cpp)
add_car_line_executable(EvEnergyFeature_B src/car/B/EvEnergyFeature_B.cpp)

macro(add_car_shadow shadow_target primary_literal shadow_literal)
    configure_shadow_executable(${shadow_target} ${shadow_literal} ${primary_literal})
endmacro()

add_car_shadow(AdasFeature_A_Shadow AdasFeature_A AdasFeature_A_Shadow)
add_car_shadow(AdasFeature_B_Shadow AdasFeature_B AdasFeature_B_Shadow)
add_car_shadow(NavigationFeature_A_Shadow NavigationFeature_A NavigationFeature_A_Shadow)
add_car_shadow(NavigationFeature_B_Shadow NavigationFeature_B NavigationFeature_B_Shadow)
add_car_shadow(EvEnergyFeature_A_Shadow EvEnergyFeature_A EvEnergyFeature_A_Shadow)
add_car_shadow(EvEnergyFeature_B_Shadow EvEnergyFeature_B EvEnergyFeature_B_Shadow)
