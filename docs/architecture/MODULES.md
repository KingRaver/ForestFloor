# Modules and Boundaries

## Planned Modules
- `apps/desktop`
  - Desktop host shell, windowing, packaging.
- `packages/engine-cpp`
  - Real-time graph host and audio callback integration.
- `packages/dsp-cpp`
  - DSP kernels, samplers, effects, utility signal functions.
- `packages/control-rs`
  - Sequencer, transport, command routing, undo/redo.
- `packages/midi-rs`
  - MIDI device discovery, parser, mapping, learn state machine.
- `packages/presets-rs`
  - Project/kit/pattern schemas, migrations, persistence.
- `packages/plugin-host`
  - SDK loader, plugin validation, capability negotiation.
- `packages/abi`
  - Language-neutral C ABI event and parameter contracts.
- `packages/abi-rs`
  - Rust mirror types for ABI-safe control-plane integration.

## Dependency Policy
- `abi` is the contract source for cross-language boundaries.
- `abi-rs` mirrors `abi` and should be updated in the same change set.
- `dsp-cpp` depends on no higher-level modules.
- `engine-cpp` may depend on `dsp-cpp`.
- Rust modules do not depend on UI internals.
- UI depends only on stable APIs from control/host modules.

## Public Surface Rules
- Each package must define a small explicit public API.
- Internal details stay private to package boundaries.
- Breaking API changes require ADR entry and version bump.

## Add-Module Checklist
1. Define ownership and non-ownership responsibilities.
2. Define inbound/outbound interfaces.
3. Define real-time constraints.
4. Add unit tests and integration test hooks.
5. Document compatibility expectations.
