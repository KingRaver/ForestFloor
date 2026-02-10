# Roadmap

Status key:
- `[ ]` not started
- `[-]` in progress
- `[x]` complete

## Phase 0 - Foundations
- [x] Finalize architecture docs and contracts.
- [x] Create monorepo scaffolding.
- [x] Add CI pipeline for macOS and Windows.
- [x] Establish shared event and parameter ABI types.

Definition of done:
- [x] Build and test pipelines run on macOS and Windows.
- [x] Core modules compile with placeholder implementations.

## Phase 1 - Core Drum Machine MVP
- [-] 16-step sequencer, 8 tracks (core sequencing logic implemented in `control-rs`).
- [-] Sample playback engine and transport (core one-shot playback and transport state implemented in `engine-cpp`).
- [-] Audio device selection and latency settings (device config model implemented; backend integration pending).
- [-] MIDI note input for pad triggering (note mapping and note-on trigger path implemented).

Definition of done:
- [-] Stable playback at target latency without XRuns in baseline profile (stress/golden regression harness implemented; real backend integration + device-level XRun profiling pending).

## Phase 2 - Instrument Control Depth
- [x] Per-track controls: gain, pan, filter, envelope, pitch (implemented in `engine-cpp` + `presets-rs` models).
- [x] Choke groups and swing (implemented in `control-rs` and `engine-cpp` behavior/tests).
- [x] Save/load kits and patterns (deterministic text persistence implemented in `presets-rs`).
- [x] MIDI Learn for initial parameter set (implemented in `midi-rs` for gain/cutoff/decay targets).

Definition of done:
- [x] End-to-end project save/load with deterministic playback recall (deterministic event replay + deterministic engine recall parameter payload generation).

## Phase 3 - Extensibility
- [x] Plugin SDK v1 for instruments/effects/MIDI nodes (dynamic metadata/lifecycle symbol loading, internal plugin registration, activation/process/reset/deactivate wiring implemented in `plugin-host`).
- [x] Host-side plugin validation and sandbox strategy (validation policy, isolation queue/session state, and dynamic fixture-based validation tests implemented in `plugin-host`).
- [x] Expanded automation and routing features (host routing graph and automation lanes with interpolation + dispatch implemented and tested in `plugin-host`).

Definition of done:
- [x] At least one internal plugin and one external plugin loaded via SDK v1.

## Phase 4 - Production Readiness
- [ ] Performance optimization and profiling tooling.
- [ ] Installer/signing/release automation.
- [ ] Crash reporting and diagnostics.

Definition of done:
- [ ] Repeatable signed builds and release checklist completed.
