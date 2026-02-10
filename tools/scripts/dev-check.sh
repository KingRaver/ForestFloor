#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./tools/scripts/dev-check.sh [options]

Options:
  --tools-only  Check toolchain and architecture only.
  --skip-cpp    Skip C++ configure/build/test checks.
  --skip-rust   Skip Rust workspace tests.
  --with-non-unit  Include non-unit regression checks (stress/golden).
  --clean       Remove local build outputs before running checks.
  -h, --help    Show this help message.
USAGE
}

log() {
  printf '[dev-check] %s\n' "$1"
}

die() {
  printf '[dev-check] ERROR: %s\n' "$1" >&2
  exit 1
}

require_cmd() {
  local cmd="$1"
  command -v "$cmd" >/dev/null 2>&1 || die "Missing required command: $cmd"
}

RUN_CPP=1
RUN_RUST=1
TOOLS_ONLY=0
CLEAN=0
RUN_NON_UNIT=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tools-only)
      TOOLS_ONLY=1
      ;;
    --skip-cpp)
      RUN_CPP=0
      ;;
    --skip-rust)
      RUN_RUST=0
      ;;
    --clean)
      CLEAN=1
      ;;
    --with-non-unit)
      RUN_NON_UNIT=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      die "Unknown option: $1"
      ;;
  esac
  shift
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# Keep tool discovery deterministic on macOS ARM.
export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:$HOME/.cargo/bin:$PATH"
if [[ -f "$HOME/.cargo/env" ]]; then
  # shellcheck disable=SC1090
  . "$HOME/.cargo/env"
fi

log "Repository root: $REPO_ROOT"

for cmd in cmake ninja ctest cargo rustc; do
  require_cmd "$cmd"
done

log "Tool versions"
cmake --version | head -n 1
ninja --version
cargo -V
rustc -vV | sed -n '1,6p'

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
log "Host: $HOST_OS $HOST_ARCH"

# On Windows (MSYS2/Git Bash), MSVC's link.exe must precede Git's /usr/bin/link
# (POSIX hard-link utility) in PATH, otherwise Rust's linker invocation fails.
if [[ "$HOST_OS" == MINGW* || "$HOST_OS" == MSYS* ]]; then
  CL_PATH="$(command -v cl 2>/dev/null || true)"
  if [[ -n "$CL_PATH" ]]; then
    MSVC_BIN_DIR="$(dirname "$CL_PATH")"
    export PATH="$MSVC_BIN_DIR:$PATH"
    log "MSVC bin: $MSVC_BIN_DIR"
  fi
fi

if [[ "$HOST_OS" == "Darwin" && "$HOST_ARCH" == "arm64" ]]; then
  require_cmd file

  for bin in "$(command -v cmake)" "$(command -v ninja)" "$(command -v cargo)" "$(command -v rustc)"; do
    file_output="$(file "$bin")"
    if [[ "$file_output" != *"arm64"* ]]; then
      die "Expected arm64 binary, got: $file_output"
    fi
  done

  rust_host_line="$(rustc -vV | grep '^host: ' || true)"
  if [[ "$rust_host_line" != "host: aarch64-apple-darwin" ]]; then
    die "Rust host must be aarch64-apple-darwin, got: ${rust_host_line:-unknown}"
  fi
fi

if [[ "$TOOLS_ONLY" -eq 1 ]]; then
  log "Toolchain checks passed."
  exit 0
fi

if [[ "$CLEAN" -eq 1 ]]; then
  log "Cleaning local build outputs (build, target)."
  rm -rf build target
fi

if [[ "$RUN_CPP" -eq 1 ]]; then
  if [[ "$HOST_OS" == "Darwin" && "$HOST_ARCH" == "arm64" ]] && [[ -f "$REPO_ROOT/CMakePresets.json" ]] && cmake --list-presets >/dev/null 2>&1; then
    PRESETS="$(cmake --list-presets)"
    SELECTED_PRESET=""

    if [[ -x "/opt/homebrew/opt/llvm/bin/clang++" ]] && grep -q 'macos-arm64-llvm' <<<"$PRESETS"; then
      SELECTED_PRESET="macos-arm64-llvm"
    elif grep -q 'macos-arm64-dev' <<<"$PRESETS"; then
      SELECTED_PRESET="macos-arm64-dev"
    fi

    if [[ -n "$SELECTED_PRESET" ]]; then
      log "Configuring C++ with preset: $SELECTED_PRESET"
      cmake --preset "$SELECTED_PRESET"
      log "Building C++ with preset: $SELECTED_PRESET"
      cmake --build --preset "$SELECTED_PRESET"
    else
      log "No compatible macOS ARM preset found; falling back to generic configure/build."
      cmake -S . -B build -G Ninja
      cmake --build build
    fi
  elif [[ "$HOST_OS" == MINGW* || "$HOST_OS" == MSYS* ]]; then
    CMAKE_EXTRA_ARGS=()
    if command -v cl >/dev/null 2>&1; then
      log "Configuring C++ (Windows MSVC)"
      CMAKE_EXTRA_ARGS+=(-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl)
    else
      log "Configuring C++ (Windows MinGW)"
    fi
    cmake -S . -B build -G Ninja "${CMAKE_EXTRA_ARGS[@]}"
    log "Building C++ (Windows)"
    cmake --build build
  else
    log "Configuring C++ (generic)"
    cmake -S . -B build -G Ninja
    log "Building C++ (generic)"
    cmake --build build
  fi

  log "Running C++ unit/integration tests"
  ctest --test-dir build --output-on-failure -LE nonunit

  if [[ "$RUN_NON_UNIT" -eq 1 ]]; then
    log "Running C++ non-unit regression tests (stress/golden)"
    ctest --test-dir build --output-on-failure -L nonunit
  fi
fi

if [[ "$RUN_RUST" -eq 1 ]]; then
  log "Running Rust tests"
  cargo test --workspace
fi

log "All checks passed."
