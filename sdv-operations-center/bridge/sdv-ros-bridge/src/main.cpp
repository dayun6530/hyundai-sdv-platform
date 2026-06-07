#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "sdv/ros_bridge/BridgeNode.hpp"

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<sdv::ros_bridge::BridgeNode>();
    rclcpp::spin(node);
  } catch (const std::exception& ex) {
    RCLCPP_FATAL(rclcpp::get_logger("sdv_ros_bridge"), "Fatal: %s", ex.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
