# Copilot Instructions for tt-umd

## Project Overview

tt-umd (Tenstorrent User Mode Driver) is a C++17 library that provides user-space access to Tenstorrent AI accelerator hardware. It supports Wormhole and Blackhole chip architectures.

## Build Verification

The environment is pre-configured with dependencies via `copilot-setup-steps.yml`.
To rebuild after making changes:

```bash
cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON -DTT_UMD_ENABLE_CLANG_TIDY=OFF
cmake --build build
```

**Important**:
- Always use cmake/ninja to build. Do NOT compile files directly with g++ (dependencies like fmt are managed by CPM and require cmake).
- Do NOT modify `CMakeLists.txt`.
- Do NOT run security scans (CodeQL, etc.) - CI handles this separately.

## Pre-commit Integration

**All agents should use pre-commit hooks when making changes to ensure code quality and consistency.**

### Installing Pre-commit

If pre-commit is not already installed, install it with:
```bash
pip install pre-commit
```

### Configuring Pre-commit for the Repository

After installing, configure the git hooks:
```bash
pre-commit install
```

This will set up automatic checks before each commit. The hooks are defined in `.pre-commit-config.yaml` and include:
- **gersemi**: CMake file formatting
- **clang-format**: C++ code formatting
- **black**: Python code formatting
- **yamllint**: YAML file linting (for `.github/` directory)
- **check-copyright**: Copyright header verification
- **check-cpp-comment-periods**: Ensures C++ comments end with periods

### Using Pre-commit

**Automatic execution**: Pre-commit hooks run automatically before each commit after installation.

**Manual execution**: Run all hooks on all files:
```bash
pre-commit run --all-files
```

**Run on specific files**: 
```bash
pre-commit run --files <file1> <file2>
```

**Best Practices**:
1. Install pre-commit at the start of your work session
2. Run `pre-commit run --all-files` before making changes to establish a baseline
3. Let hooks run automatically before commits to catch issues early
4. If hooks fail, fix the issues and re-commit (many hooks auto-fix formatting)

## Code Style Guidelines

- **C++ Standard**: C++17
- **Formatting**: clang-format (config in `.clang-format`)
- **Linting**: clang-tidy (config in `.clang-tidy`)

Follow the existing style in the file you're modifying.

## When Fixing Static Analysis Issues

**Workflow** (do exactly these steps, nothing more):
1. Read the file containing the issue
2. Apply the minimal fix
3. Build with cmake/ninja to verify it compiles
4. Create the PR

**Guidelines**:
1. Make minimal, targeted changes to fix the specific issue
2. Follow existing code patterns in the surrounding code
3. If a fix requires architectural changes, skip it
4. Prefer safe, conservative fixes over clever ones

### Common Issues and Fixes

**Virtual method call during construction**
`Call to virtual method 'X' during construction bypasses virtual dispatch`

Consider:
1. Use fully-qualified call: `ClassName::method()` to make intent explicit
2. Mark method/class `final` only if it's truly not meant to be extended

**Null pointer dereference**
- Add null check before dereferencing
- Use early return pattern

**Uninitialized variable**
- Initialize at declaration

**Dead store**
- Remove the unused assignment, or use the value

**Division by zero**
- Add check before division: `if (divisor != 0) { ... }`
- Use ternary with default: `divisor != 0 ? x / divisor : default_value`
- Assert if zero is a programming error: `TT_ASSERT(divisor != 0)`
- Use `std::max(divisor, 1)` if 1 is a safe minimum

