# Integration of clang-tidy and clangd into UMD

This document describes clang-tidy integration to the UMD project for static code analysis and improved code quality.

## Overview

Clang-tidy is a clang-based C++ "linter" tool that provides static analysis to find bugs, performance issues, and style violations. This integration enables automated code quality checks during the build  and development (if used with the clangd extension) process.

### 1. CMake Integration

A new build option `TT_UMD_ENABLE_CLANG_TIDY` has been added to `CMakeLists.txt` that allows enabling clang-tidy checks during compilation:

When enabled, the build system will:
- Search for `clang-tidy` or `clang-tidy-17` executable
- Configure CMake to run clang-tidy on each source file during compilation using the project-specific configuration file `.clang-tidy`
- Enable real-time analysis through clangd in VSCode using the `.clang-tidy` configuration for immediate feedback during development

### 2. VSCode Configuration

The `.vscode/default.settings.json` contains configuration that allows the integration of clangd and clang-tidy tools in the IDE itself.

#### clangd Integration
- **clangd.path**: Set to `clangd-17` to use a specific version
- **clangd.arguments**: Configured with optimal settings:
  - `--compile-commands-dir=${workspaceFolder}/build`: Use build directory for compilation database
  - `--header-insertion=never`: Disable automatic header insertion
  - `--enable-config`: Enable clangd configuration files

#### IntelliSense Configuration
- **C_Cpp.default.intelliSenseMode**: Set to `clang-x64` for consistency
- **C_Cpp.intelliSenseEngine**: Disabled to avoid conflicts with clangd

### 3. Configuration Files

The project uses two seprate configuration files: 
- `.clang-tidy` for build-time static analysis configuration (which will be used on the CI)
- `.clangd` for real-time analysis integrated with clangd in VSCode. This file merges configurations from `.clang-tidy` with additional settings defined in `.clangd`. Rules defined in `.clangd` will not generate build failures, unlike those in `.clang-tidy`

## Usage

### Building with clang-tidy

Clang-tidy is enabled by default, to override the default behavior the CMake variable `TT_UMD_ENABLE_CLANG_TIDY` can be used.

To disable clang-tidy during the build process:

```bash
cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON -DTT_UMD_ENABLE_CLANG_TIDY=OFF
```

Note: `.clang-tidy` and `.clangd` depend on the compilation database `compile_commands.json`. This compilation database is generated once the flag `CMAKE_EXPORT_COMPILE_COMMANDS` is set to `1` (but this is by default set to true in the UMD codebase).

Note: The flag `TT_UMD_BUILD_TESTS` isn't necessary, but it's almost always used, see the general `README.md` for more information.

### VSCode Setup

1. Install the clangd extension for VS Code
2. Ensure `clangd-17` is available in your PATH
3. The workspace settings will automatically configure clangd to work with the project

## Requirements

- **clang-tidy**: Version 17 recommended (clang-tidy-17)
- **clangd**: Version 17 recommended for VS Code integration
- **CMake**: Version 3.10 or higher

## Troubleshooting

### Common Issues

1. **Clang-tidy not found**: Install clang-tidy-17 or ensure it's in your PATH
2. **VS Code conflicts**: Disable C/C++ IntelliSense if using clangd

### Performance Considerations

- Clang-tidy analysis adds compilation time
- Use `TT_UMD_ENABLE_CLANG_TIDY=OFF` for faster development builds
