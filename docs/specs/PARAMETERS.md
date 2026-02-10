# Parameter Specification

Canonical ABI definitions:
- C: `packages/abi/include/ff/abi/contracts.h`
- Rust mirror: `packages/abi-rs/src/lib.rs`

## Goals
- Stable parameter identity across sessions.
- Uniform automation and MIDI mapping behavior.
- Language-neutral normalized representation.

## Parameter Identity
- `parameter_id` is immutable and globally unique.
- Suggested format: `module.node.parameter`.

## Canonical Definition
Each parameter declares:
- ID
- Display name
- Unit
- Normalized range `[0.0, 1.0]`
- Mapping curve (linear, log, exponential, custom)
- Default value
- Smoothing mode
- Automatable flag
- MIDI mappable flag

## Runtime Value Model
- Control plane sends normalized values.
- Engine performs de-normalization and smoothing.
- Smoothing modes: `none`, `linear`, `one_pole`.

## Automation Rules
- Automation points are timestamped in sample-domain timeline.
- Conflicts resolve by priority: transport automation > direct UI > MIDI CC.
- Last-writer-wins within equal priority and timestamp.

## MIDI Mapping Rules
- CC binding targets parameter ID only.
- Optional pickup/soft-takeover policies per mapping.
- Mapping profile scoped by MIDI device identifier.

## Serialization
- Persist normalized values plus schema version.
- Do not persist transient smoothed state.
