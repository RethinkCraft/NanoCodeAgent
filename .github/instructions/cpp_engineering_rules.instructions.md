---
description: "Use when writing, editing, or reviewing C++ source files, headers, CMakeLists, or build scripts. Adds C++-specific details that refine the repository-wide rules in AGENTS.md."
applyTo: "**/*.{cpp,hpp,h,c,cxx,cmake,sh}"
---
# C++ Engineering Rules

Apply `AGENTS.md` first. Then apply the following C++-specific details for files matched by this instruction.

## Testing Details

- Use GoogleTest (`gtest`).
- Place tests in the `tests/` directory, mirroring the `src/` structure where practical.
- Use `gtest_discover_tests()` in `tests/CMakeLists.txt`.
- Reset global states such as `getopt_long`'s `optind` in fixture `SetUp()` methods when needed.

## Build And Dependency Details

- Keep third-party dependencies under `3rd-party/` and wire them through existing submodule and CMake patterns.
- When touching dependency wiring in `CMakeLists.txt`, keep the submodule existence pre-check that fails with `FATAL_ERROR` when a required dependency is missing.

## Logging And CLI Details

- Use the wrapper macros defined in `include/logger.hpp` (`LOG_INFO`, `LOG_DEBUG`, `LOG_ERROR`).
- Reserve `std::cout` or `printf` for CLI `--help` and `--version` style output only.

## Reminder

- These instructions refine repository-wide coding standards. They are not a task skill.
