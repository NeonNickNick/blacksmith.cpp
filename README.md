# blacksmith.cpp

A C++20 mirror of Blacksmith, with optional Python bindings built using
[pybind11](https://pybind11.readthedocs.io/).

## Development dependencies

The project is developed in [Visual Studio Code](https://code.visualstudio.com/)
with the following tools:

- [CMake](https://cmake.org/) for project configuration
- [Ninja](https://ninja-build.org/) as the build system
- [clangd](https://clangd.llvm.org/) for C++ language support in VS Code
- Visual Studio with the MSVC C++ x64 toolchain, used as the compiler
- [uv](https://docs.astral.sh/uv/) for Python environments and packaging

The `build.cmd` script handles the build workflow and initializes the MSVC
environment through Visual Studio. Install the Visual Studio C++ build tools
and make CMake, Ninja, and uv available on `PATH`.
