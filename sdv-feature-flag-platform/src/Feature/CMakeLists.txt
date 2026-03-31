set(FEATURE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(NavigationFeature src/NavigationFeature.cpp)
target_include_directories(NavigationFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(NavigationFeature PRIVATE LifeCycleManagerLib)
set_target_properties(NavigationFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(AdasFeature src/AdasFeature.cpp)
target_include_directories(AdasFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(AdasFeature PRIVATE LifeCycleManagerLib)
set_target_properties(AdasFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(EvEnergyFeature src/EvEnergyFeature.cpp)
target_include_directories(EvEnergyFeature PRIVATE ${FEATURE_INCLUDE})
target_link_libraries(EvEnergyFeature PRIVATE LifeCycleManagerLib)
set_target_properties(EvEnergyFeature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
