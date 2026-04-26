cmake_minimum_required(VERSION 3.16)
project(unleash_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer CONFIG packages when available, but fall back to CMake's built-in Find modules.
find_package(nlohmann_json CONFIG REQUIRED)

find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
  find_package(CURL REQUIRED)
endif()

# ---- SDK library
file(GLOB_RECURSE UNLEASH_SDK_SOURCES
     CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(unleash_sdk STATIC ${UNLEASH_SDK_SOURCES})

target_include_directories(unleash_sdk
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(unleash_sdk
  PUBLIC
    UNLEASH_SDK_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(unleash_sdk
  PUBLIC
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

# ---- Examples
option(UNLEASH_BUILD_EXAMPLES "Build SDK examples" ON)

if(UNLEASH_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---- Tests
option(UNLEASH_BUILD_TESTS "Build unit tests" ON)

if(UNLEASH_BUILD_TESTS)
  enable_testing()

  find_package(GTest CONFIG REQUIRED)

  file(GLOB_RECURSE UNLEASH_TEST_SOURCES
       CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
  )

  add_executable(unleash_tests ${UNLEASH_TEST_SOURCES})

  target_include_directories(unleash_tests
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_link_libraries(unleash_tests
    PRIVATE
      unleash_sdk
      GTest::gtest_main
  )

if(WIN32)
    target_link_libraries(unleash_tests PRIVATE ws2_32)
endif()

  include(GoogleTest)
  gtest_discover_tests(unleash_tests)
endif()