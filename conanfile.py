import re
from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class ManualDoubleSidedPrinter(ConanFile):
    name = "manual-double-sided-printer"
    package_type = "application"
    settings = "os", "arch", "compiler", "build_type"
    exports = "CMakeLists.txt"
    exports_sources = "CMakeLists.txt", "src/*", "tests/*", "Assets/app.ico"

    def set_version(self):
        project = Path(self.recipe_folder, "CMakeLists.txt").read_text(encoding="utf-8")
        self.version = re.search(r"project\(ManualDoubleSidedPrinter VERSION (\S+)", project)[1]

    def layout(self):
        cmake_layout(self)

    def validate(self):
        if self.settings.os != "Windows" or self.settings.compiler != "msvc":
            raise ConanInvalidConfiguration("The desktop application requires Windows and MSVC.")

    def generate(self):
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_info(self):
        self.cpp_info.bindirs = ["."]
