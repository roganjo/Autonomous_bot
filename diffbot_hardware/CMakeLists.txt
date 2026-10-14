cmake_minimum_required(VERSION 3.8)
project(diffbot_hardware)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(hardware_interface REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# ── Shared library (the hardware interface plugin) ────────────────────────────
add_library(diffbot_hardware SHARED
  src/diffbot_hardware.cpp
  src/serial_port.cpp
)

target_include_directories(diffbot_hardware PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries(diffbot_hardware
  ${Boost_LIBRARIES}
)

ament_target_dependencies(diffbot_hardware
  hardware_interface
  pluginlib
  rclcpp
  rclcpp_lifecycle
)

# Export the plugin
pluginlib_export_plugin_description_file(hardware_interface diffbot_hardware.xml)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS diffbot_hardware
  DESTINATION lib
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY
  config
  launch
  urdf
  DESTINATION share/${PROJECT_NAME}
)

install(FILES diffbot_hardware.xml
  DESTINATION share/${PROJECT_NAME}
)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(diffbot_hardware)
ament_export_dependencies(hardware_interface pluginlib rclcpp rclcpp_lifecycle)
ament_package()
