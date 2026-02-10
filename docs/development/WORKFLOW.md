# Developer Workflow

## Purpose
Practical runbook for local toolchain checks, build/test commands, and day-to-day development loops.

## One-Command Check
Default command for local readiness and pre-PR verification:

```bash
./tools/scripts/dev-check.sh
```

Useful flags:
- `--clean`: remove `build/` and `target/` before checks.
- `--tools-only`: verify tools/architecture only.
- `--skip-cpp`: run only Rust checks.
- `--skip-rust`: run only C++ checks.

CI parity:
- GitHub Actions runs the same command in `/Users/jeffspirlock/ForestFloor/.github/workflows/ci.yml`.

## Local Toolchain Check
Run this once per machine setup and after tool upgrades.

```bash
which cmake ninja cargo rustc
file "$(which cmake)" "$(which ninja)" "$(which cargo)" "$(which rustc)"
rustc -vV
```

Expected on Apple Silicon:
- `cmake` and `ninja` from `/opt/homebrew/bin`
- `cargo` and `rustc` from `$HOME/.cargo/bin`
- Binary architecture `arm64`
- Rust host `aarch64-apple-darwin`

## Daily Build/Test Commands
From repo root:

```bash
# C++ configure/build (Apple Silicon preset)
cmake --preset macos-arm64-dev
cmake --build --preset macos-arm64-dev

# C++ tests (currently none registered yet)
ctest --test-dir build --output-on-failure

# Rust build/tests
cargo build --workspace
cargo test --workspace
```

## Fast Inner Loop
Use this while actively iterating:

```bash
# Build only
cmake --build --preset macos-arm64-dev

# Test a single Rust crate
cargo test -p control-rs

# Test a specific Rust test
cargo test -p midi-rs bind_cc_replaces_existing_mapping
```

## Clean Rebuild
Use when compiler/toolchain settings changed:

```bash
rm -rf build target
cmake --preset macos-arm64-dev
cmake --build --preset macos-arm64-dev
cargo test --workspace
```

## Tooling Notes
- CMake preset is defined in `CMakePresets.json`.
- C++ builds use Homebrew LLVM (`/opt/homebrew/opt/llvm/bin/clang++`) on macOS ARM.
- If shell paths look wrong, start a new terminal and verify `~/.zprofile` loads `/opt/homebrew` and `~/.cargo/env`.

## Troubleshooting
- `cstddef` or `iostream` not found:
  - confirm preset usage (`cmake --preset macos-arm64-dev`)
  - ensure Homebrew LLVM is installed: `brew install llvm`
- `cmake`/`ninja` resolve to `/usr/local`:
  - fix shell PATH priority to `/opt/homebrew/bin` first.
- `rustc` host is not `aarch64-apple-darwin`:
  - reinstall rustup/toolchain for native host and remove x86_64 toolchains.

## Pre-PR Command Checklist
Run before opening a PR:

```bash
./tools/scripts/dev-check.sh --clean
```
