# Threading and Real-Time Rules

## Non-Negotiable Audio Thread Rules
- No heap allocation.
- No blocking syscalls.
- No mutex locks or waits.
- No filesystem, network, or device enumeration.
- No logging sinks that can block.

## Thread Ownership
- Audio RT thread owns:
  - Node graph execution.
  - Parameter smoothing application.
  - Meter accumulation for deferred publish.
- Control thread owns:
  - Transport and sequencer state transitions.
  - Event generation and scheduling.
- Loader thread owns:
  - File read, decode, and sample preparation.
- UI thread owns:
  - Presentation state and user interactions.

## Approved Cross-Thread Mechanisms
- SPSC lock-free ring buffer for event delivery.
- Atomic or double-buffer parameter tables.
- Immutable snapshots for project state handoff.

## Forbidden Cross-Thread Mechanisms
- Shared mutable containers with coarse locks.
- Calling UI APIs from RT path.
- Direct RT reads from disk-backed objects.

## Timing Semantics
- Engine processes fixed-size blocks.
- Events carry sample offset within current block.
- Transport position represented in samples for deterministic replay.

## Performance Budgets
- Target callback utilization: <= 50% average, <= 75% peak.
- XRuns should be treated as release blockers.
- Per-node budget tracked in profiling builds.

## Recovery Strategy
- On overload: prioritize continuity over fidelity.
- Drop non-critical telemetry first.
- Apply bounded voice stealing before hard clipping.
