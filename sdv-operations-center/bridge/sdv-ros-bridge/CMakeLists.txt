cmake_minimum_required(VERSION 3.16)
project(sdv_ros_bridge VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(autoware_vehicle_msgs REQUIRED)
find_package(autoware_planning_msgs REQUIRED)
find_package(autoware_perception_msgs REQUIRED)
find_package(sdv_ops_data_core REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

add_executable(sdv_ros_bridge_node
  src/main.cpp
  src/BridgeNode.cpp
  src/RosMessageNormalizer.cpp
  src/TelemetryWebSocketServer.cpp
)

target_include_directories(sdv_ros_bridge_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(sdv_ros_bridge_node
  rclcpp
  nav_msgs
  autoware_vehicle_msgs
  autoware_planning_msgs
  autoware_perception_msgs
)

target_link_libraries(sdv_ros_bridge_node
  sdv_ops_data_core
  nlohmann_json::nlohmann_json
  Boost::system
)

install(TARGETS sdv_ros_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
