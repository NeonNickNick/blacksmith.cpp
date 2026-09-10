# clang-tidy integration for build-time include checking.
#
# When enabled, the build runs clang-tidy with misc-include-cleaner on every
# source file.  Missing direct includes (symbols used via transitive includes)
# will FAIL the build — not just warn.
#
# Controlled by: -DENABLE_CLANG_TIDY=ON/OFF   (default: ON if clang-tidy found)
#
# The same check runs in clangd for in-editor error squiggles (see ../.clangd).

find_program(CLANG_TIDY_EXE
  NAMES clang-tidy
  PATHS
    "$ENV{USERPROFILE}/.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin"
  PATH_SUFFIXES bin
)

if(CLANG_TIDY_EXE)
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  set(CLANG_TIDY_AVAILABLE TRUE)
else()
  message(WARNING "clang-tidy not found — build-time include checking disabled")
  set(CLANG_TIDY_AVAILABLE FALSE)
endif()

option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" ${CLANG_TIDY_AVAILABLE})

if(ENABLE_CLANG_TIDY AND CLANG_TIDY_AVAILABLE)
  # Only run the include-cleaner check at build time.
  # (Other clang-tidy checks still run in the IDE via clangd.)
  #
  # --warnings-as-errors  →  makes missing includes FAIL the build
  # --extra-arg            →  passes --driver-mode=cl to clang-tidy's parser so
  #                           it understands MSVC-style compile flags
  set(CLANG_TIDY_COMMAND
    "${CLANG_TIDY_EXE}"
    "-checks=-*,misc-include-cleaner"
    "--warnings-as-errors=misc-include-cleaner"
    "--extra-arg=--driver-mode=cl"
  )

  set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")

  message(STATUS "Build-time include checking: ENABLED (build will fail on missing includes)")
else()
  set(CMAKE_CXX_CLANG_TIDY "")
  message(STATUS "Build-time include checking: DISABLED")
endif()
