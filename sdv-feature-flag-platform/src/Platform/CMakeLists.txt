add_subdirectory(FeatureCommunication/EventBus)
add_subdirectory(LifeCycleManager)
add_subdirectory(CenterCommunication)
# DeploymentManager is owned by sdv-ota-manager (kept build-compatible).
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../sdv-ota-manager/platform/DeploymentManager DeploymentManager)
