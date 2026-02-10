#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./tools/scripts/release-package.sh [options]

Options:
  --version <value>      Release version (required, example: 0.4.0)
  --output-dir <path>    Output directory for installers/manifests (default: dist)
  --build-dir <path>     CMake build directory (default: build-release)
  --signing-key <path>   PEM private key used for detached signatures
  --require-signing      Fail if signing key is not provided
  --skip-checks          Skip ./tools/scripts/dev-check.sh --clean --with-non-unit
  --skip-build           Skip configure/build and package existing build directory
  -h, --help             Show this help
USAGE
}

log() {
  printf '[release-package] %s\n' "$1"
}

die() {
  printf '[release-package] ERROR: %s\n' "$1" >&2
  exit 1
}

require_cmd() {
  local cmd="$1"
  command -v "$cmd" >/dev/null 2>&1 || die "Missing required command: $cmd"
}

sha256_file() {
  local file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi

  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi

  die "Missing SHA256 tool (sha256sum or shasum)."
}

VERSION=""
OUTPUT_DIR="dist"
BUILD_DIR="build-release"
SIGNING_KEY="${FF_RELEASE_SIGNING_KEY_PATH:-}"
REQUIRE_SIGNING=0
SKIP_CHECKS=0
SKIP_BUILD=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      [[ $# -ge 2 ]] || die "Missing value for --version"
      VERSION="$2"
      shift 2
      ;;
    --output-dir)
      [[ $# -ge 2 ]] || die "Missing value for --output-dir"
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --build-dir)
      [[ $# -ge 2 ]] || die "Missing value for --build-dir"
      BUILD_DIR="$2"
      shift 2
      ;;
    --signing-key)
      [[ $# -ge 2 ]] || die "Missing value for --signing-key"
      SIGNING_KEY="$2"
      shift 2
      ;;
    --require-signing)
      REQUIRE_SIGNING=1
      shift
      ;;
    --skip-checks)
      SKIP_CHECKS=1
      shift
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
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
done

[[ -n "$VERSION" ]] || die "Missing required --version value."

require_cmd cmake
require_cmd ninja
require_cmd cpack
require_cmd openssl

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# Keep tool discovery deterministic on macOS ARM.
export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:$HOME/.cargo/bin:$PATH"
if [[ -f "$HOME/.cargo/env" ]]; then
  # shellcheck disable=SC1090
  . "$HOME/.cargo/env"
fi

if [[ "$SKIP_CHECKS" -eq 0 ]]; then
  log "Running full pre-release validation checks"
  ./tools/scripts/dev-check.sh --clean --with-non-unit
fi

mkdir -p "$OUTPUT_DIR"

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  HOST_OS="$(uname -s)"
  HOST_ARCH="$(uname -m)"
  CMAKE_EXTRA_ARGS=()
  if [[ "$HOST_OS" == "Darwin" && "$HOST_ARCH" == "arm64" ]]; then
    if [[ -x "/opt/homebrew/opt/llvm/bin/clang++" ]]; then
      CMAKE_EXTRA_ARGS+=(
        -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
      )
      log "Using Homebrew LLVM toolchain for release build"
    fi
  fi

  log "Configuring release build: $BUILD_DIR"
  cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFF_BUILD_DESKTOP=ON \
    -DFF_RELEASE_VERSION="$VERSION" \
    "${CMAKE_EXTRA_ARGS[@]}"

  log "Building release target: forest_floor_desktop"
  cmake --build "$BUILD_DIR" --target forest_floor_desktop
fi

[[ -f "$BUILD_DIR/CPackConfig.cmake" ]] || die "Missing $BUILD_DIR/CPackConfig.cmake. Build/package configuration did not complete."

log "Cleaning previous release artifacts for version $VERSION"
rm -f "$OUTPUT_DIR"/forest-floor-"$VERSION"-* \
      "$OUTPUT_DIR"/forest-floor-"$VERSION"-checksums.sha256 \
      "$OUTPUT_DIR"/forest-floor-"$VERSION"-manifest.txt

log "Generating installer/package artifacts via CPack"
cpack --config "$BUILD_DIR/CPackConfig.cmake" -B "$OUTPUT_DIR"

ARTIFACTS=()
while IFS= read -r artifact; do
  ARTIFACTS+=("$artifact")
done < <(
  find "$OUTPUT_DIR" -maxdepth 1 -type f \
    -name "forest-floor-${VERSION}-*" \
    ! -name "*.sha256" ! -name "*.sig" ! -name "*.pub.pem" ! -name "*-manifest.txt" \
    | sort
)

[[ "${#ARTIFACTS[@]}" -gt 0 ]] || die "No release artifacts generated for version $VERSION."

CHECKSUM_FILE="$OUTPUT_DIR/forest-floor-${VERSION}-checksums.sha256"
: >"$CHECKSUM_FILE"
for artifact in "${ARTIFACTS[@]}"; do
  checksum="$(sha256_file "$artifact")"
  printf '%s  %s\n' "$checksum" "$(basename "$artifact")" >>"$CHECKSUM_FILE"
done
log "Wrote checksums: $CHECKSUM_FILE"

SIGNED=0
PUBLIC_KEY_PATH=""
if [[ -n "$SIGNING_KEY" ]]; then
  [[ -f "$SIGNING_KEY" ]] || die "Signing key not found: $SIGNING_KEY"
  PUBLIC_KEY_PATH="$OUTPUT_DIR/forest-floor-${VERSION}-signing-public.pem"
  openssl pkey -in "$SIGNING_KEY" -pubout -out "$PUBLIC_KEY_PATH" >/dev/null 2>&1

  for artifact in "${ARTIFACTS[@]}"; do
    signature_path="${artifact}.sig"
    openssl dgst -sha256 -sign "$SIGNING_KEY" -out "$signature_path" "$artifact"
    openssl dgst -sha256 -verify "$PUBLIC_KEY_PATH" -signature "$signature_path" "$artifact" >/dev/null
  done
  SIGNED=1
  log "Created detached signatures for ${#ARTIFACTS[@]} artifact(s)"
elif [[ "$REQUIRE_SIGNING" -eq 1 ]]; then
  die "Signing is required but no --signing-key (or FF_RELEASE_SIGNING_KEY_PATH) was provided."
fi

MANIFEST_FILE="$OUTPUT_DIR/forest-floor-${VERSION}-manifest.txt"
{
  printf 'version=%s\n' "$VERSION"
  printf 'generated_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'host_os=%s\n' "$(uname -s)"
  printf 'host_arch=%s\n' "$(uname -m)"
  printf 'artifacts=%s\n' "${#ARTIFACTS[@]}"
  printf 'checksums_file=%s\n' "$(basename "$CHECKSUM_FILE")"
  if [[ "$SIGNED" -eq 1 ]]; then
    printf 'signed=yes\n'
    printf 'public_key=%s\n' "$(basename "$PUBLIC_KEY_PATH")"
  else
    printf 'signed=no\n'
  fi
  printf 'artifact_files=\n'
  for artifact in "${ARTIFACTS[@]}"; do
    printf '  - %s\n' "$(basename "$artifact")"
  done
} >"$MANIFEST_FILE"

log "Wrote manifest: $MANIFEST_FILE"
log "Release packaging complete."
