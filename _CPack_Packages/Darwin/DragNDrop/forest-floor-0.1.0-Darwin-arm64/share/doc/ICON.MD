# Desktop Icon and Usable Drum Machine Checklist

Status key:
- `[ ]` not started
- `[-]` in progress
- `[x]` complete

## Purpose
Turn the current desktop host stub into a double-clickable desktop application that is usable as a real drum machine.

## Verified Baseline (2026-02-10)
- [x] Desktop target builds and runs as a CLI stub (`apps/desktop/src/main.cpp`).
- [x] Engine can render in-memory buffers (`packages/engine-cpp`).
- [x] Sequencer, MIDI mapping, and preset model logic exist as Rust libraries (`packages/control-rs`, `packages/midi-rs`, `packages/presets-rs`).
- [x] Plugin host lifecycle/routing/automation scaffolding exists (`packages/plugin-host`).
- [x] Desktop app bundle/icon metadata exists.
- [ ] Real audio device callback backend exists.
- [ ] Graphical UI exists.
- [ ] Hardware MIDI device I/O exists.
- [ ] End-to-end runtime wiring (UI/control -> engine/audio -> output) exists.
- [ ] Disk-based sample loading pipeline exists.

## Phase 1 - App Bundle and Clickable Icon
- [x] Convert desktop target to a platform app bundle target (macOS `.app` first).
- [x] Add bundle metadata (`Info.plist`, bundle id, version, display name).
- [x] Add application icons (`.icns`/`.ico`) and wire into build/package flow.
- [x] Update release packaging to emit installable desktop artifacts, not only CLI archives.

Definition of done:
- [-] App launches via Finder/Desktop icon (no terminal required).
- [-] Correct app name/icon appears in Finder and Dock.

Verification notes (2026-02-10):
- [x] `cmake --build --preset macos-arm64-dev` emits `build/apps/desktop/Forest Floor.app`.
- [x] Bundle metadata validated via `plutil -p build/apps/desktop/Forest Floor.app/Contents/Info.plist`.
- [x] `cpack --config build/CPackConfig.cmake` emits macOS `.dmg` with `Forest Floor.app` staged in package content.
- [ ] Manual Finder/Dock visual verification still pending on a local desktop session.

## Phase 2 - Real-Time Audio Backend
- [ ] Introduce an audio backend abstraction in desktop host.
- [ ] Implement macOS backend (CoreAudio or equivalent) to drive continuous callbacks.
- [ ] Feed callback buffers into `Engine::process` and route to device outputs.
- [ ] Wire device id/sample rate/buffer size settings to runtime backend config.
- [ ] Track backend-level XRuns/underruns and expose in diagnostics.

Definition of done:
- [ ] Continuous playback runs for at least 5 minutes without crash.
- [ ] Audio output is audible from desktop app path (not synthetic one-shot CLI run).

## Phase 3 - Sequencer and Transport Runtime Wiring
- [ ] Add runtime bridge from control sequencer events to engine trigger path.
- [ ] Drive sequencer timing from actual audio timeline/callback sample clock.
- [ ] Implement transport start/stop/tempo controls in live runtime.
- [ ] Ensure choke groups, swing, and step velocity affect audible output.

Definition of done:
- [ ] 16-step pattern loops correctly at selectable BPM.
- [ ] Changing tempo while running updates playback timing correctly.

## Phase 4 - MIDI Device Input and Learn
- [ ] Add platform MIDI device discovery/input stream integration.
- [ ] Route incoming MIDI bytes through `midi-rs` parsing and note mapping.
- [ ] Route mapped note-on to engine track triggers.
- [ ] Route CC learn/bindings to engine parameter updates.

Definition of done:
- [ ] External pad controller can trigger tracks in real time.
- [ ] Gain/cutoff/decay learn mappings function during live playback.

## Phase 5 - Usable UI Surface
- [ ] Add desktop window and event loop.
- [ ] Implement minimum controls: transport, BPM, 16-step grid, 8 track pads.
- [ ] Implement per-track parameter controls (gain/pan/filter/decay/pitch/choke).
- [ ] Show runtime state feedback (playhead step, active triggers, device status).

Definition of done:
- [ ] User can create and audition a basic drum loop from UI only.
- [ ] No terminal interaction is required for normal operation.

## Phase 6 - Samples, Projects, and Recovery
- [ ] Add disk sample loading (for example WAV/AIFF) into track assignments.
- [ ] Add project save/load actions backed by `presets-rs` formats.
- [ ] Ship default starter samples/kit in `assets`.
- [ ] Ensure crash/runtime diagnostics are accessible from app UX or docs path.

Definition of done:
- [ ] Save -> close -> reopen restores kit/pattern and playback behavior.
- [ ] Starter content is available on first launch.

## Phase 7 - Productization and Release Gate
- [ ] Add end-to-end desktop smoke test path in CI (launch + short render/session).
- [ ] Add soak regression for audio callback stability and XRun thresholds.
- [ ] Add signed desktop artifact flow (codesign/notarize where applicable).
- [ ] Update user docs with install + first-run instructions for desktop app.

Definition of done:
- [ ] Release artifacts install and launch as desktop apps.
- [ ] CI gates include desktop runtime checks, not only module tests.

## Exit Criteria for "Working"
- [ ] A non-developer can double-click the app icon and hear sound within 2 minutes.
- [ ] The app supports basic beat making: load sounds, program 16 steps, play/stop, tweak core parameters, save and reopen.
- [ ] Build/release pipeline produces signed, installable desktop artifacts.
