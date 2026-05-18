# DeploymentManager is an independent HPC-side module owned by `sdv-ota-manager`.
# It builds a standalone CLI executable (`DeploymentMgr`) and is intentionally kept
# free of direct linking dependencies on Feature Flag Platform internals.
#
# This directory can be built:
# - Standalone: configure with `-S sdv-ota-manager/platform/DeploymentManager -B <build>`
# - Integrated: included as an external subdirectory from the platform build for compatibility

# Standalone build hygiene:
# - When this directory is the CMake top-level, declare a minimal project so CMake doesn't warn.
# - When included via add_subdirectory(), do not override the parent project's global settings.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SDVDeploymentManager LANGUAGES CXX)
endif()

# Prefer target-scoped standard settings to avoid clobbering parent projects.
# If the parent already set a standard, we keep it; otherwise default to C++17 for this module.
if(NOT DEFINED CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD STREQUAL "")
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(DeploymentMgr
    src/DeploymentHistory.cpp
    src/ConfigUpdater.cpp
    src/DeploymentManager.cpp
    src/VehicleRegistry.cpp
    src/VersionManager.cpp
    src/StateManager.cpp
    src/CampaignManager.cpp
    src/CampaignDocument.cpp
    src/CampaignValidation.cpp
    src/CampaignRunManager.cpp
    src/CampaignRunRecorder.cpp
    src/TargetingResolver.cpp
    src/DeploymentManagerMain.cpp
)

target_include_directories(DeploymentMgr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(DeploymentMgr PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(DeploymentMgr PRIVATE stdc++fs)
endif()

set_target_properties(DeploymentMgr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
