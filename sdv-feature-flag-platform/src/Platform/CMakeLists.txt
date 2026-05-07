add_subdirectory(FeatureCommunication/EventBus)
add_subdirectory(LifeCycleManager)
add_subdirectory(CenterCommunication)

# -----------------------------------------------------------------------------
# DeploymentManager (HPC module boundary)
#
# NOTE:
# - DeploymentManager is owned by `sdv-ota-manager` and modeled as an independent
#   HPC module, NOT as an internal Feature Flag Platform component.
# - We include it here only for build compatibility and local demo workflows
#   (e.g. building `DeploymentMgr` alongside platform binaries).
# - When integrating with a real vehicle stack, DeploymentManager should be built
#   and delivered as a separate module/artifact within the same HPC.
# -----------------------------------------------------------------------------
option(SDV_BUILD_DEPLOYMENT_MANAGER
    "Build sdv-ota-manager's DeploymentMgr (independent HPC module) alongside the platform for compatibility"
    ON
)

if(SDV_BUILD_DEPLOYMENT_MANAGER)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../../../sdv-ota-manager/platform/DeploymentManager
        sdv_ota_manager__DeploymentManager
    )
endif()
