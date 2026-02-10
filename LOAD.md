# How to Start the Forest Floor Drum Machine

## Prerequisites

### macOS (Apple Silicon)

Install the required toolchains via Homebrew:

```bash
brew install cmake ninja llvm
```

Install Rust via rustup:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

Verify everything is native ARM64:

```bash
which cmake ninja cargo rustc
file "$(which cmake)" "$(which ninja)" "$(which cargo)" "$(which rustc)"
rustc -vV
```

Expected:
- `cmake` and `ninja` resolve to `/opt/homebrew/bin`
- `cargo` and `rustc` resolve to `$HOME/.cargo/bin`
- All binaries are `Mach-O 64-bit executable arm64`
- Rust host is `aarch64-apple-darwin`

### macOS (Intel) / Linux

Same tools as above. Use your system package manager instead of Homebrew where appropriate. The CMake presets are macOS ARM64-specific, so use the generic configure commands shown below.

### Windows

- Visual Studio with MSVC v143
- CMake 3.28+
- Ninja 1.11+
- Rust stable (via rustup)

### Shell PATH

Ensure your shell profile (`~/.zprofile` or `~/.bashrc`) includes:

```bash
export PATH="/opt/homebrew/bin:$HOME/.cargo/bin:$PATH"
```

## Build

### Step 1: Build the C++ Engine and Desktop Host

**macOS Apple Silicon (system Clang):**

```bash
cmake --preset macos-arm64-dev
cmake --build --preset macos-arm64-dev
```

**macOS Apple Silicon (Homebrew LLVM):**

```bash
cmake --preset macos-arm64-llvm
cmake --build --preset macos-arm64-llvm
```

**Generic (other platforms):**

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

### Step 2: Build the Rust Control Plane

```bash
cargo build --workspace
```

### Step 3: Run Tests

```bash
ctest --test-dir build --output-on-failure
cargo test --workspace
```

Or use the one-command validation script:

```bash
./tools/scripts/dev-check.sh
```

Add `--clean` to wipe build artifacts first. Add `--with-non-unit` for stress and golden regression tests.

## Run the Desktop Host

After building on macOS, the app bundle is at `build/apps/desktop/Forest Floor.app`.

```bash
open "./build/apps/desktop/Forest Floor.app"
```

If you need terminal output or environment variable overrides, run the bundle executable directly:

```bash
"./build/apps/desktop/Forest Floor.app/Contents/MacOS/Forest Floor"
```

### What It Does

The desktop host stub initializes the full engine and plugin pipeline:

1. Creates an `Engine` instance with master gain at 0.8.
2. Enables profiling and loads a sample into track 0.
3. Starts the transport and triggers MIDI note 36 (track 0) at velocity 127.
4. Processes one 256-frame audio block through the engine.
5. Registers an internal sampler plugin via the Plugin Host SDK.
6. Sets up audio routing: host input -> internal sampler -> master output.
7. Activates, processes, resets, and deactivates the internal plugin.
8. Prints a session summary and writes a diagnostics report.

### Expected Output

```
Forest Floor desktop host stub
Registered plugins: 1
Routes: 2
Transport running: yes
Diagnostics directory: ./diagnostics
```

A runtime report is written to the `diagnostics/` directory.

### Loading an External Plugin

Set the `FF_DESKTOP_PLUGIN_PATH` environment variable to the path of a shared library that implements the Forest Floor Plugin SDK:

```bash
FF_DESKTOP_PLUGIN_PATH=/path/to/my_plugin.dylib \
  "./build/apps/desktop/Forest Floor.app/Contents/MacOS/Forest Floor"
```

The host will trust the plugin's parent directory, attempt to load it, and route audio through it if loading succeeds. The output will include the external plugin's load status.

## Diagnostics

Runtime and crash reports are written to the directory specified by the `FF_DIAGNOSTICS_DIR` environment variable. If unset, the default is `./diagnostics`.

```bash
FF_DIAGNOSTICS_DIR=/tmp/ff-diag \
  "./build/apps/desktop/Forest Floor.app/Contents/MacOS/Forest Floor"
ls ./diagnostics/
```

## Troubleshooting

| Problem | Fix |
|---|---|
| `cstddef` or `iostream` not found | Use a CMake preset (`cmake --preset macos-arm64-dev`) or install Homebrew LLVM (`brew install llvm`) |
| `cmake`/`ninja` resolve to `/usr/local` | Fix shell PATH priority to `/opt/homebrew/bin` first |
| `rustc` host is not `aarch64-apple-darwin` | Reinstall rustup for native host: `rustup toolchain install stable` |
| Build fails on Windows | Ensure MSVC v143 is on PATH (`ilammy/msvc-dev-cmd` in CI) |
| Missing `openssl` for release scripts | Install via Homebrew: `brew install openssl` |

## Quick Reference

| Action | Command |
|---|---|
| Full validation | `./tools/scripts/dev-check.sh --clean` |
| Build C++ only | `cmake --build --preset macos-arm64-dev` |
| Build Rust only | `cargo build --workspace` |
| Run C++ tests | `ctest --test-dir build --output-on-failure` |
| Run Rust tests | `cargo test --workspace` |
| Run single Rust crate | `cargo test -p control-rs` |
| Run desktop host | `open "./build/apps/desktop/Forest Floor.app"` |
| Release package | `./tools/scripts/release-package.sh --version 0.1.0` |
| Release smoke check | `./tools/scripts/release-check.sh` |
