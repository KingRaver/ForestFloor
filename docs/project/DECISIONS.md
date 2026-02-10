# Architecture Decisions (ADR Log)

## How to Use
Record significant architecture choices here to preserve intent and tradeoffs.

Template:
- ADR: `ADR-XXXX`
- Date: `YYYY-MM-DD`
- Status: `Proposed | Accepted | Superseded`
- Context
- Decision
- Alternatives considered
- Consequences

---

## ADR-0001
- Date: 2026-02-10
- Status: Accepted
- Context
  - Forest Floor needs real-time reliability and long-term modular growth.
- Decision
  - Use C++ for real-time engine and DSP hot path.
  - Use Rust for sequencer/control/mapping/state orchestration.
  - Keep UI replaceable behind stable control APIs.
- Alternatives considered
  - All C++ stack.
  - All Rust stack.
- Consequences
  - Cross-language boundary adds interface design complexity.
  - Strong separation improves maintainability and testability.

## ADR-0002
- Date: 2026-02-10
- Status: Accepted
- Context
  - Forest Floor needs a stable, language-neutral contract for engine/control/plugin boundaries.
- Decision
  - Define canonical ABI structs in C header `packages/abi/include/ff/abi/contracts.h`.
  - Mirror the same layouts in `packages/abi-rs/src/lib.rs` for Rust modules.
  - Require synchronized updates across both packages for ABI changes.
- Alternatives considered
  - Generate bindings from one source-of-truth build step.
  - Keep ad-hoc per-module contracts without a shared ABI package.
- Consequences
  - Manual sync discipline is required between C and Rust mirrors.
  - Contract drift risk is reduced by centralization and explicit versioning.

## ADR-0003
- Date: 2026-02-10
- Status: Accepted
- Context
  - Project modules span C++ real-time engine code and Rust control-plane code.
- Decision
  - Use root CMake for C++ packages and root Cargo workspace for Rust packages.
  - Keep both build systems first-class in CI across macOS and Windows.
- Alternatives considered
  - Single build orchestration layer with custom wrappers.
  - Build only one language stack initially.
- Consequences
  - Two toolchains increase setup requirements.
  - Language boundaries stay clear and independently testable.
