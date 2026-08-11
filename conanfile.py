from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class OpcServerConan(ConanFile):
    name = "opc-server"
    version = "0.1.0"
    package_type = "application"

    license = "MIT"
    url = "https://github.com/Orange-hanter/OPC_SERVER"
    description = "Industrial Modbus to OPC UA gateway"

    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "Src/*",
        "Lib/Json/*",
        "tools/*",
        "tests/*",
        "DOCs/examples/*",
        "DOCs/schemas/*",
        "LICENSE",
    )
    default_options = {"open62541/*:shared": False}

    def requirements(self):
        self.requires("open62541/1.5.0")

    def build_requirements(self):
        self.test_requires("catch2/3.15.1")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure(
            variables={
                "OPC_DEPENDENCY_PROVIDER": "CONAN",
                "BUILD_TESTING": True,
            }
        )
        cmake.build()
        cmake.test(cli_args=["--output-on-failure"])

    def package(self):
        CMake(self).install()
