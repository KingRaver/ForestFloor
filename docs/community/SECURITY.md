# Security Policy

## Scope
This policy covers Forest Floor source, build pipeline, release artifacts, and plugin loading model.

## Reporting
- Report vulnerabilities privately to maintainers.
- Do not disclose details publicly until a fix is available.

## Dependency Hygiene
- Pin dependency versions where possible.
- Review dependency updates before merge.
- Block known-vulnerable packages in CI.

## Release Integrity
- Build in CI from tagged commits.
- Sign release artifacts.
- Publish checksums with releases.

## Plugin Trust Model
- External plugins are untrusted by default.
- Host requires explicit trusted plugin roots before dynamic loading.
- Host validates SDK compatibility after a plugin passes trust gate admission.
- Fail closed on malformed metadata/state.
- Dynamic library load (`dlopen`/`LoadLibrary`) is not a sandbox boundary.

## Security Priorities
1. Protect users from malicious project/plugin payloads.
2. Prevent crash or hang from malformed inputs.
3. Keep recovery path for corrupted state files.
