# Architecture

## System Shape
Forest Floor is a layered system with a strict real-time boundary.

## Layer Responsibilities
- UI Layer
  - Presents controls, sequencer grid, routing views.
  - Never owns audio timing.
- Rust Control Plane
  - Sequencer, transport logic, MIDI Learn, preset/project orchestration.
  - Emits timestamped events and parameter updates.
- C++ Engine Layer
  - Owns audio callback and graph execution.
  - Applies parameter smoothing and executes nodes each block.
- C++ DSP Layer
  - Voice/sample playback, filters, dynamics, effects.

## Directional Dependency Rule
Allowed flow only:
1. UI -> Control Plane
2. Control Plane -> Engine
3. Engine -> DSP

No reverse ownership dependencies are allowed. Upstream communication uses event channels only.

## Runtime Threads
- Audio RT thread: executes graph processing.
- MIDI input thread: receives device data and forwards to control plane.
- Control thread: sequencer state updates and command routing.
- Loader thread(s): sample decoding and disk IO.
- UI thread: render and user input.

## Communication Contracts
- Shared ABI: versioned C structs in `packages/abi/include/ff/abi/contracts.h` with Rust mirror in `packages/abi-rs/src/lib.rs`.
- Event queue: discrete timestamped commands from control plane to engine.
- Parameter snapshot: lock-free, double-buffered normalized values.
- Telemetry queue: non-RT diagnostics out of engine.

## Extension Strategy
Plugins are node modules loaded via versioned SDK contracts. Node types:
- Instrument node: event in, audio out.
- Effect node: audio in/out plus parameters.
- MIDI node: event in/out transformations.
- Utility node: analysis/routing with bounded RT cost.

## Failure Containment
- Node crashes must isolate to host process boundaries where possible.
- Invalid plugin state should fail load, not crash audio callback.
- Overload handling should degrade gracefully (voice stealing, quality tier fallback).
