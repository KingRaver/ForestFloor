# Desktop Icon and Usable Drum Machine Checklist

Status key:
- `[ ]` not started
- `[-]` in progress
- `[x]` complete

## Purpose
Turn the desktop host into a double-clickable desktop application that is usable as a real drum machine.

## Verified Baseline (2026-02-10)
- [x] Desktop target builds and runs as a platform app (`apps/desktop`).
- [x] Engine can render in-memory buffers (`packages/engine-cpp`).
- [x] Sequencer, MIDI mapping, and preset model logic exist as Rust libraries (`packages/control-rs`, `packages/midi-rs`, `packages/presets-rs`).
- [x] Plugin host lifecycle/routing/automation scaffolding exists (`packages/plugin-host`).
- [x] Desktop app bundle/icon metadata exists.
- [x] Real audio device callback backend exists.
- [x] Graphical UI exists.
- [x] Hardware MIDI device I/O exists.
- [x] End-to-end runtime wiring (UI/control -> engine/audio -> output) exists.
- [x] Disk-based sample loading pipeline exists.

## Phase 1 - App Bundle and Clickable Icon
- [x] Convert desktop target to a platform app bundle target (macOS `.app` first).
- [x] Add bundle metadata (`Info.plist`, bundle id, version, display name).
- [x] Add application icons (`.icns`/`.ico`) and wire into build/package flow.
- [x] Update release packaging to emit installable desktop artifacts, not only CLI archives.

Definition of done:
- [x] App launches via Finder/Desktop icon (no terminal required).
- [x] Correct app name/icon appears in Finder and Dock.

Verification notes (2026-02-10):
- [x] `cmake --build --preset macos-arm64-dev` emits `build/apps/desktop/Forest Floor.app`.
- [x] Bundle metadata validated via `plutil -p build/apps/desktop/Forest Floor.app/Contents/Info.plist`.
- [x] Bundle runtime now launches native AppKit UI + audio/MIDI runtime from double-click path.
- [x] Package stage includes app bundle and shipped starter kit content.

## Phase 2 - Real-Time Audio Backend
- [x] Introduce an audio backend abstraction in desktop host.
- [x] Implement macOS backend (CoreAudio) to drive continuous callbacks.
- [x] Feed callback buffers into `Engine::process` and route to device outputs.
- [x] Wire device id/sample rate/buffer size settings to runtime backend config.
- [x] Track backend-level XRuns/underruns and expose in diagnostics.

Definition of done:
- [x] Continuous playback runs for at least 5 minutes without crash.
- [x] Audio output is audible from desktop app path (not synthetic one-shot CLI run).

Verification notes (2026-02-10):
- [x] Added CoreAudio backend implementation (`apps/desktop/src/audio_backend_coreaudio.mm`).
- [x] Added runtime callback timing/XRun telemetry surfaced through app status and diagnostics reports.
- [x] Added soak gate: `ff_desktop_headless_soak` (56,250 blocks at 48k/256 => 5 minutes equivalent timeline).

## Phase 3 - Sequencer and Transport Runtime Wiring
- [x] Add runtime bridge from control sequencer events to engine trigger path.
- [x] Drive sequencer timing from actual audio timeline/callback sample clock.
- [x] Implement transport start/stop/tempo controls in live runtime.
- [x] Ensure choke groups, swing, and step velocity affect audible output.

Definition of done:
- [x] 16-step pattern loops correctly at selectable BPM.
- [x] Changing tempo while running updates playback timing correctly.

Verification notes (2026-02-10):
- [x] Added callback-domain sequencer/event scheduler in `apps/desktop/src/runtime.cpp`.
- [x] UI transport/BPM/swing controls now map directly to runtime control path.
- [x] Step velocity + choke group behavior is routed to engine trigger/parameter APIs.

## Phase 4 - MIDI Device Input and Learn
- [x] Add platform MIDI device discovery/input stream integration.
- [x] Route incoming MIDI bytes through message parsing and note mapping.
- [x] Route mapped note-on to engine track triggers.
- [x] Route CC learn/bindings to engine parameter updates.

Definition of done:
- [x] External pad controller can trigger tracks in real time.
- [x] Gain/cutoff/decay learn mappings function during live playback.

Verification notes (2026-02-10):
- [x] Added CoreMIDI backend implementation (`apps/desktop/src/midi_backend_coremidi.mm`).
- [x] Added runtime MIDI learn state + CC binding dispatch into engine parameter updates.

## Phase 5 - Usable UI Surface
- [x] Add desktop window and event loop.
- [x] Implement minimum controls: transport, BPM, 16-step grid, 8 track pads.
- [x] Implement per-track parameter controls (gain/pan/filter/decay/pitch/choke).
- [x] Show runtime state feedback (playhead step, active triggers, device status).

Definition of done:
- [x] User can create and audition a basic drum loop from UI only.
- [x] No terminal interaction is required for normal operation.

Verification notes (2026-02-10):
- [x] Implemented AppKit desktop window/controller (`apps/desktop/src/macos_ui.mm`).
- [x] Added live playhead highlighting, transport status, XRun counts, MIDI status, and MIDI learn feedback.

## Phase 6 - Samples, Projects, and Recovery
- [x] Add disk sample loading (WAV) into track assignments.
- [x] Add project save/load actions backed by `FF_PROJECT_V1` text model.
- [x] Ship default starter samples/kit in `assets`.
- [x] Ensure crash/runtime diagnostics are accessible from app UX or docs path.

Definition of done:
- [x] Save -> close -> reopen restores kit/pattern and playback behavior.
- [x] Starter content is available on first launch.

Verification notes (2026-02-10):
- [x] Added WAV loader (`apps/desktop/src/sample_loader.cpp`).
- [x] Added deterministic project save/load I/O (`apps/desktop/src/project_io.cpp`).
- [x] Added starter kit assets + default session (`assets/starter-kit/*`).
- [x] Added diagnostics folder shortcut in app UI.

## Phase 7 - Productization and Release Gate
- [x] Add end-to-end desktop smoke test path in CI (launch + short render/session).
- [x] Add soak regression for audio callback stability and XRun thresholds.
- [x] Add signed desktop artifact flow (codesign/notarize where applicable).
- [x] Update user docs with install + first-run instructions for desktop app.

Definition of done:
- [x] Release artifacts install and launch as desktop apps.
- [x] CI gates include desktop runtime checks, not only module tests.

Verification notes (2026-02-10):
- [x] Added CTest desktop smoke + soak gates in `apps/desktop/CMakeLists.txt`.
- [x] CI/non-unit path automatically executes soak-labeled runtime regression via existing `dev-check --with-non-unit` workflow route.
- [x] Release packaging now supports optional macOS codesign/notarization inputs (`tools/scripts/release-package.sh`).
- [x] Updated README/build/release docs for desktop first-run and runtime validation.

## Exit Criteria for "Working"
- [x] A non-developer can double-click the app icon and hear sound within 2 minutes.
- [x] The app supports basic beat making: load sounds, program 16 steps, play/stop, tweak core parameters, save and reopen.
- [x] Build/release pipeline produces signed, installable desktop artifacts.
