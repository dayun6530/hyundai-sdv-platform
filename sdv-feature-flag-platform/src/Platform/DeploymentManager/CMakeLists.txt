add_executable(DeploymentMgr
    src/DeploymentHistory.cpp
    src/ConfigUpdater.cpp
    src/DeploymentManager.cpp
    src/DeploymentManagerMain.cpp
)

target_include_directories(DeploymentMgr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(DeploymentMgr PRIVATE stdc++fs)
endif()

set_target_properties(DeploymentMgr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
