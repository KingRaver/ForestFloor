#!/usr/bin/env bash
set -euo pipefail

log() {
  printf '[release-check] %s\n' "$1"
}

die() {
  printf '[release-check] ERROR: %s\n' "$1" >&2
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

require_cmd openssl
require_cmd cmake
require_cmd cpack
require_cmd ninja

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

VERSION="0.0.0-smoke"
KEY_PATH="$TMP_DIR/release-signing-key.pem"
DIST_DIR="$TMP_DIR/dist"
BUILD_DIR="$TMP_DIR/build-release"

log "Generating ephemeral signing key"
openssl genrsa -out "$KEY_PATH" 2048 >/dev/null 2>&1

log "Running release package script in smoke mode"
./tools/scripts/release-package.sh \
  --version "$VERSION" \
  --output-dir "$DIST_DIR" \
  --build-dir "$BUILD_DIR" \
  --skip-checks \
  --require-signing \
  --signing-key "$KEY_PATH"

CHECKSUM_FILE="$DIST_DIR/forest-floor-${VERSION}-checksums.sha256"
MANIFEST_FILE="$DIST_DIR/forest-floor-${VERSION}-manifest.txt"
PUBLIC_KEY_FILE="$DIST_DIR/forest-floor-${VERSION}-signing-public.pem"

[[ -f "$CHECKSUM_FILE" ]] || die "Missing checksum file: $CHECKSUM_FILE"
[[ -f "$MANIFEST_FILE" ]] || die "Missing manifest file: $MANIFEST_FILE"
[[ -f "$PUBLIC_KEY_FILE" ]] || die "Missing public key file: $PUBLIC_KEY_FILE"

log "Verifying checksums and detached signatures"
while read -r expected file_name; do
  [[ -n "$expected" ]] || continue
  artifact="$DIST_DIR/$file_name"
  signature="$artifact.sig"
  [[ -f "$artifact" ]] || die "Missing artifact listed in checksums: $artifact"
  [[ -f "$signature" ]] || die "Missing detached signature: $signature"

  actual="$(sha256_file "$artifact")"
  [[ "$expected" == "$actual" ]] || die "Checksum mismatch for $artifact"

  openssl dgst -sha256 -verify "$PUBLIC_KEY_FILE" -signature "$signature" "$artifact" >/dev/null
done <"$CHECKSUM_FILE"

log "Release automation smoke check passed"
