# Testing Strategy

## Objectives
- Prevent audio regressions.
- Guarantee deterministic sequencing behavior.
- Protect state schema compatibility.

## Test Layers
- Unit tests
  - Rust: sequencer, mapping logic, migrations.
  - C++: DSP units, voice allocation, smoothing behavior.
- Integration tests
  - Control plane -> engine event flow.
  - Project load/save roundtrip consistency.
  - Shared ABI fixture checks across Rust and C++ for recall parameter updates.
  - Plugin host dynamic-load fixtures (valid, isolated, missing-symbol) for symbol validation paths.
  - Plugin host SDK v1 lifecycle wiring tests (internal + external plugin activation/process/reset/deactivate).
  - Plugin host routing graph and automation interpolation dispatch tests.
- Real-time regression tests
  - Callback duration distribution.
  - XRun detection under stress scenarios.
- Golden audio tests
  - Render fixed scenes and compare against toleranced reference outputs.

## Non-Functional Checks
- Fuzz state file parsing.
- Validate plugin load rejection paths.
- Check for allocation in RT process paths where instrumentation is available.

## CI Expectations
- Run all unit and integration tests on every PR.
- Run stress and golden tests on scheduled builds and release candidates.
