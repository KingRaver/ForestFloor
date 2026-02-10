# Roadmap

## Phase 0 - Foundations
- [x] Finalize architecture docs and contracts.
- [x] Create monorepo scaffolding.
- [x] Add CI pipeline for macOS and Windows.
- [x] Establish shared event and parameter ABI types.

Definition of done:
- Build and test pipelines run on macOS and Windows.
- Core modules compile with placeholder implementations.

## Phase 1 - Core Drum Machine MVP
- 16-step sequencer, 8 tracks.
- Sample playback engine and transport.
- Audio device selection and latency settings.
- MIDI note input for pad triggering.

Definition of done:
- Stable playback at target latency without XRuns in baseline profile.

## Phase 2 - Instrument Control Depth
- Per-track controls: gain, pan, filter, envelope, pitch.
- Choke groups and swing.
- Save/load kits and patterns.
- MIDI Learn for initial parameter set.

Definition of done:
- End-to-end project save/load with deterministic playback recall.

## Phase 3 - Extensibility
- Plugin SDK v1 for instruments/effects/MIDI nodes.
- Host-side plugin validation and sandbox strategy.
- Expanded automation and routing features.

Definition of done:
- At least one internal plugin and one external plugin loaded via SDK v1.

## Phase 4 - Production Readiness
- Performance optimization and profiling tooling.
- Installer/signing/release automation.
- Crash reporting and diagnostics.

Definition of done:
- Repeatable signed builds and release checklist completed.
