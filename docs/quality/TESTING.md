# Testing Strategy

## Objectives
- Prevent audio regressions.
- Guarantee deterministic sequencing behavior.
- Protect state schema compatibility.

## Test Layers
- Unit tests
  - Rust: sequencer, mapping logic, migrations.
  - C++: DSP units, voice allocation, smoothing behavior.
  - Rust parser fuzz-style safety coverage for preset/state loaders (`presets-rs` deterministic fuzz input test).
- Integration tests
  - Control plane -> engine event flow.
  - Project load/save roundtrip consistency.
  - Shared ABI fixture checks across Rust and C++ for recall parameter updates.
  - Plugin host dynamic-load fixtures (valid, isolated, missing-symbol) for symbol validation paths.
  - Plugin host SDK v1 lifecycle wiring tests (internal + external plugin activation/process/reset/deactivate).
  - Plugin host routing graph and automation interpolation dispatch tests.
- Real-time regression tests
  - Stress render regression via `ff_engine_nonunit_tests` (`ctest -L nonunit`).
  - Runtime-bound proxy checks to catch severe callback cost regressions.
- Golden audio tests
  - Deterministic fixed-scene render reference comparison in `ff_engine_nonunit_tests`.

## Non-Functional Checks
- Fuzz state file parsing (deterministic fuzz corpus generated in tests).
- Validate plugin load rejection paths.
- Check for allocation in RT process paths where instrumentation is available.

## CI Expectations
- Run all unit and integration tests on every PR.
- Run stress and golden non-unit regression on scheduled CI (`non-unit-regression`) and manual dispatch.
- Use `./tools/scripts/dev-check.sh --clean --with-non-unit` for release-candidate validation.
