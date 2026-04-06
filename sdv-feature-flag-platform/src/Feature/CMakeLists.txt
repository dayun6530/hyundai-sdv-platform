set(FEATURE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(NavigationFeature src/NavigationFeature.cpp)
target_include_directories(NavigationFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(NavigationFeature PRIVATE LifeCycleManagerLib)
set_target_properties(NavigationFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(NavigationFeatureShadow ../Platform/LifeCycleManager/src/ShadowReceiverMain.cpp)
target_include_directories(NavigationFeatureShadow PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(NavigationFeatureShadow PRIVATE LifeCycleManagerLib)
target_compile_definitions(NavigationFeatureShadow PRIVATE SHADOW_NAME="NavigationFeatureShadow" PRIMARY_NAME="NavigationFeature")
set_target_properties(NavigationFeatureShadow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(AdasFeature src/AdasFeature.cpp)
target_include_directories(AdasFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(AdasFeature PRIVATE LifeCycleManagerLib)
set_target_properties(AdasFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(AdasFeatureShadow ../Platform/LifeCycleManager/src/ShadowReceiverMain.cpp)
target_include_directories(AdasFeatureShadow PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(AdasFeatureShadow PRIVATE LifeCycleManagerLib)
target_compile_definitions(AdasFeatureShadow PRIVATE SHADOW_NAME="AdasFeatureShadow" PRIMARY_NAME="AdasFeature")
set_target_properties(AdasFeatureShadow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(EvEnergyFeature src/EvEnergyFeature.cpp)
target_include_directories(EvEnergyFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(EvEnergyFeature PRIVATE LifeCycleManagerLib)
set_target_properties(EvEnergyFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(EvEnergyFeatureShadow ../Platform/LifeCycleManager/src/ShadowReceiverMain.cpp)
target_include_directories(EvEnergyFeatureShadow PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(EvEnergyFeatureShadow PRIVATE LifeCycleManagerLib)
target_compile_definitions(EvEnergyFeatureShadow PRIVATE SHADOW_NAME="EvEnergyFeatureShadow" PRIMARY_NAME="EvEnergyFeature")
set_target_properties(EvEnergyFeatureShadow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

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
    add_executable(${shadow_target} ../Platform/LifeCycleManager/src/ShadowReceiverMain.cpp)
    target_include_directories(${shadow_target} PRIVATE ${FEATURE_INCLUDE})
    target_link_libraries(${shadow_target} PRIVATE LifeCycleManagerLib)
    target_compile_definitions(${shadow_target} PRIVATE
        SHADOW_NAME="${shadow_literal}" PRIMARY_NAME="${primary_literal}")
    set_target_properties(${shadow_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endmacro()

add_car_shadow(AdasFeature_A_Shadow AdasFeature_A AdasFeature_A_Shadow)
add_car_shadow(AdasFeature_B_Shadow AdasFeature_B AdasFeature_B_Shadow)
add_car_shadow(NavigationFeature_A_Shadow NavigationFeature_A NavigationFeature_A_Shadow)
add_car_shadow(NavigationFeature_B_Shadow NavigationFeature_B NavigationFeature_B_Shadow)
add_car_shadow(EvEnergyFeature_A_Shadow EvEnergyFeature_A EvEnergyFeature_A_Shadow)
add_car_shadow(EvEnergyFeature_B_Shadow EvEnergyFeature_B EvEnergyFeature_B_Shadow)
