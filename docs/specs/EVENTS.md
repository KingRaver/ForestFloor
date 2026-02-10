# Event Specification

Canonical ABI definitions:
- C: `packages/abi/include/ff/abi/contracts.h`
- Rust mirror: `packages/abi-rs/src/lib.rs`

## Event Categories
- Note events: on/off with velocity.
- Trigger events: sequencer step hits and ratchets.
- Transport events: play, stop, locate, tempo change.
- Clock events: pulse, start, continue, stop.
- Control events: discrete command actions.

## Core Event Envelope
Fields:
- `event_type`
- `timeline_sample`
- `block_offset`
- `source`
- `payload`

## Time Semantics
- `timeline_sample` identifies absolute musical timeline position.
- `block_offset` identifies exact offset within current audio block.
- Host normalizes incoming MIDI timestamps to engine sample time.

## Ordering Guarantees
- Stable ordering by `timeline_sample`, then `block_offset`, then insertion order.
- Deterministic replay required for identical input streams.

## Queue Semantics
- Event queue is bounded and lock-free.
- Overflow policy:
  1. Drop lowest-priority non-critical events.
  2. Emit overflow diagnostic on telemetry channel.
  3. Never block RT thread.

## Priority Classes
- Critical: note on/off, transport start/stop.
- Normal: automation and controller events.
- Low: diagnostics and non-musical metadata.
