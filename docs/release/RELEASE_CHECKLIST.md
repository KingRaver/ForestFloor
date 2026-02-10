# Release Checklist

## Objective
Produce repeatable signed installer artifacts and publish them with integrity metadata.

## Preconditions
- Version selected (semantic version, no leading `v` in script arguments).
- `FF_RELEASE_SIGNING_KEY_PEM` secret configured in GitHub Actions for release workflow.
- Local toolchain healthy (`cmake`, `ninja`, `cpack`, `openssl`, Rust toolchain).

## Local Dry Run (Recommended)
Run release automation smoke test with ephemeral signing key:

```bash
./tools/scripts/release-check.sh
```

## Local Candidate Packaging
Run full validation + signed package generation:

```bash
./tools/scripts/release-package.sh \
  --version 0.4.0 \
  --require-signing \
  --signing-key /absolute/path/to/release-signing-key.pem
```

Optional macOS production signing/notarization:

```bash
./tools/scripts/release-package.sh \
  --version 0.4.0 \
  --require-signing \
  --signing-key /absolute/path/to/release-signing-key.pem \
  --codesign-identity "Developer ID Application: Example Corp (TEAMID)" \
  --notary-profile forest-floor-notary
```

Expected outputs in `dist/`:
- `forest-floor-<version>-<system>-<arch>.<ext>`
- `forest-floor-<version>-checksums.sha256`
- `forest-floor-<version>-signing-public.pem`
- `forest-floor-<version>-<system>-<arch>.<ext>.sig`
- `forest-floor-<version>-manifest.txt`

## CI Release Execution
1. Push annotated tag `v<version>` to trigger `.github/workflows/release.yml`.
2. Confirm both matrix jobs complete (`macos-latest`, `windows-latest`).
3. Confirm publish job attaches all generated artifacts to GitHub release.

## Integrity Verification
For each artifact listed in checksum file:
1. Validate SHA-256 checksum against `*-checksums.sha256`.
2. Verify detached signature with `*-signing-public.pem`.
3. On macOS release candidates, verify code signature/notarization (`codesign --verify` and `spctl --assess`).

## Diagnostics and Profiling Gate
- Ensure `forest_floor_desktop` emits runtime diagnostics file in diagnostics directory.
- Ensure `ff_engine_profile` smoke/integration test remains green in CI.

## Sign-Off
- Roadmap status updated for current phase.
- ADR updated for any release-contract/process changes.
- Testing evidence captured in PR notes.
