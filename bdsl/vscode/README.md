# Blacksmith BDSL

`.bdsl` syntax highlighting, diagnostics for unsaved buffers, document formatting,
format on save, snippets, and **BDSL: Compile Current File to C++**.

Use `[skillset]` for the skill set block and `__N__` for the active skill parameter.
The TextMate grammar defines separate scopes for blocks, actions, flow, parameters,
magic fields, marks, resources, types, enums, functions, numbers, strings and comments.
For the extension's explicit syntax colors, choose **BDSL Dark** or **BDSL Light**
from **Preferences: Color Theme**. Other themes use their standard scope colors.

The packaged extension includes `bdslc`. For a source checkout, build the root
CMake project's `bdslc` target first or set `bdsl.compilerPath` to the native executable. There
are no npm runtime dependencies. Press F5 with this directory open in VSCode to
start an Extension Development Host.

Diagnostics and formatting use the same C++ parser as code generation. Invalid
documents are diagnosed and left untouched by the formatter. The compiler must
run on the extension host's OS and architecture (including SSH/WSL hosts).
The extension requires a trusted workspace and reports missing/crashed tools in
the Problems panel. Compilation uses the current buffer and a save dialog.

Full Chinese syntax: [SYNTAX.md](docs/SYNTAX.md) in the packaged extension;
build and integration guide: `../README.md` in the source distribution.
