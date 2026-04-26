from conan import ConanFile

class UnleashSdkConan(ConanFile):
    name = "unleash_sdk"
    version = "0.1.0"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"

    requires = (
        "nlohmann_json/3.11.3",
        "libcurl/8.5.0",
    )

    # GTest is linked into tests
    test_requires = "gtest/1.14.0"

    # CMake as a tool is fine here (optional if you already have a recent cmake)
    tool_requires = "cmake/3.28.1"

    default_options = {
        "libcurl/*:shared": False,
    }