# blacksmith.cpp

A C++20 mirror of Blacksmith, with optional Python bindings built using
[pybind11](https://pybind11.readthedocs.io/).

## Build the Python extension

The Python binding build uses the project-local virtual environment at
`python/.venv`. The commands below use the Windows `x64-release` CMake preset
and require CMake, Ninja, a Visual Studio C++ toolchain, and [uv](https://docs.astral.sh/uv/).
Run them from a Visual Studio Developer PowerShell or a shell where the MSVC
compiler is available.

From the repository root, create the Python environment and install pybind11:

```powershell
cd python
uv venv
uv pip install pybind11
cd blacksmith
..\.venv\Scripts\Activate.ps1
cd ..\..
```

Activate the environment from `python/blacksmith` before configuring and
building the C++ project with Python bindings enabled (they are enabled by
default):

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release --config Release
```

The build writes the compiled Python extension, a `.pyd` file, into
`python/blacksmith` (for example, `core.cp311-win_amd64.pyd`). The preset uses
Python 3.11, matching the Python projects' declared version range.

## Build and use the wheel

Build the `blacksmith` wheel from its package directory first:

```powershell
cd python/blacksmith
uv build --wheel
```

Then return to the `python` directory and sync the application environment.
The root Python project's `pyproject.toml` points to the wheel under
`blacksmith/dist`, so running `uv sync` from `python` installs the package from
that location:

```powershell
cd ..
uv sync
uv run optimize
```

Run `uv sync` in `python` after the wheel has been built; this project depends
on that wheel by its local path.

Rebuild the wheel after rebuilding the C++ extension so it contains the latest
`.pyd` file. The package's `dist` directory and generated build files are
ignored by Git.
