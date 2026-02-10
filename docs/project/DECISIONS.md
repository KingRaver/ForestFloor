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

## ADR-0004
- Date: 2026-02-10
- Status: Accepted
- Context
  - Phase 2 requires kit/pattern/project save-load with deterministic recall checks.
  - Development environments may run without outbound network access for fetching new crates.
- Decision
  - Implement persistence in `presets-rs` using a deterministic, human-inspectable text format:
    - `FF_KIT_V1`
    - `FF_PATTERN_V1`
    - `FF_PROJECT_V1`
  - Keep serialization dependency-free (std only) to avoid external crate fetch requirements.
- Alternatives considered
  - JSON via `serde`/`serde_json`.
  - Binary-only format.
- Consequences
  - Additional custom parser maintenance burden.
  - Deterministic roundtrip behavior is explicit and testable in offline environments.

## ADR-0005
- Date: 2026-02-10
- Status: Accepted
- Context
  - Phase 2 requires deterministic project recall across Rust control logic and C++ engine application.
  - String parameter IDs are useful for UI/mapping, but RT-safe engine updates need compact numeric IDs.
- Decision
  - Introduce a stable numeric track-parameter ID scheme in the shared ABI:
    - base `0x1000`
    - stride `0x10`
    - slots for gain/pan/filter/envelope/pitch/choke
  - Use this scheme for recall-generated `FfParameterUpdate` payloads and engine-side `applyParameterUpdate`.
- Alternatives considered
  - Keep string IDs through the control->engine boundary.
  - Hardcode non-versioned parameter IDs in engine only.
- Consequences
  - ABI constants must remain synchronized between C and Rust.
  - Cross-language recall behavior is deterministic and directly testable.

## ADR-0006
- Date: 2026-02-10
- Status: Accepted
- Context
  - Phase 3 requires a plugin SDK baseline and host-side validation before loading external nodes.
  - Unsafe plugins must not silently enter the real-time path.
- Decision
  - Implement host-side validation on `PluginDescriptor` + `PluginBinaryInfo`.
  - Reject plugins on incompatible SDK major, missing lifecycle entrypoints, non-RT-safe process, or dynamic allocation on process path.
  - Accept plugins with isolation warnings and mark them `requires_isolation`.
- Alternatives considered
  - Trust plugin self-declaration with no validation.
  - Reject all warning-class plugins outright.
- Consequences
  - Loader policy is explicit and testable in `plugin-host` unit tests.
  - Isolation execution model remains a follow-up implementation.

## ADR-0007
- Date: 2026-02-10
- Status: Accepted
- Context
  - Control plane emits ABI parameter updates in Rust, while engine applies them in C++.
  - Contract drift across language boundaries must be caught early.
- Decision
  - Add a shared interop fixture (`fixtures/interop/phase2_engine_recall_updates.csv`) consumed by:
    - Rust recall tests (`control-rs`)
    - C++ engine batch-apply tests (`engine-cpp`)
  - Add engine batch update API using `ff_parameter_update_t` arrays.
- Alternatives considered
  - Keep separate per-language tests with no shared fixture.
  - Build full Rust<->C++ runtime FFI harness immediately.
- Consequences
  - Cross-language parameter contract is validated in CI without new external build dependencies.
  - Runtime FFI harness can be layered later without losing contract coverage.

## ADR-0008
- Date: 2026-02-10
- Status: Accepted
- Context
  - Phase 3 needs actual plugin binary loading and symbol validation, not metadata-only registration.
  - Isolation-marked plugins need a host execution path separate from in-process loading semantics.
- Decision
  - Add dynamic binary loading in `plugin-host`:
    - resolve metadata symbols `ff_plugin_get_descriptor_v1` and `ff_plugin_get_binary_info_v1`
    - resolve required lifecycle symbols (`ff_create`, `ff_prepare`, `ff_process`, `ff_reset`, `ff_destroy`)
    - merge metadata-declared entrypoints with actual symbol presence before validation
  - Add isolation session state model:
    - pending isolation queue
    - running isolation sessions via `startIsolationSession`
- Alternatives considered
  - Continue metadata-only registration for Phase 3.
  - Require full process sandbox runtime before introducing any isolation state model.
- Consequences
  - Host now validates real binaries via fixture modules in CI.
  - Isolation execution is explicit in host state, while IPC/process sandbox execution remains follow-up work.

## ADR-0009
- Date: 2026-02-10
- Status: Accepted
- Context
  - Phase 3 requires complete SDK wiring, not only binary admission checks.
  - Extensibility also requires host-managed routing and automation dispatch primitives.
- Decision
  - Extend `plugin-host` with SDK v1 runtime lifecycle APIs:
    - internal plugin registration with lifecycle callbacks
    - external plugin binary loading via metadata/lifecycle symbols
    - activation/process/reset/deactivation runtime controls
  - Add host routing graph and automation lane services:
    - route endpoints (`host.input`, plugin ids, `host.master`)
    - automation lanes with deterministic point interpolation to parameter updates
  - Validate with integration tests that load one internal plugin and one external plugin and drive full lifecycle + routing/automation behavior.
- Alternatives considered
  - Keep lifecycle execution outside `plugin-host` and only expose metadata.
  - Implement routing/automation only in control plane with no host contract.
- Consequences
  - Phase 3 definition-of-done is testable in one module.
  - Future isolated execution backends can reuse existing routing/automation and lifecycle surfaces.

## ADR-0010
- Date: 2026-02-10
- Status: Accepted
- Context
  - External plugin binaries are untrusted, but dynamic loading (`dlopen`/`LoadLibrary`) may execute plugin-controlled code before metadata validation completes.
  - Validation results alone cannot represent a pre-execution safety boundary.
- Decision
  - Add an explicit trust gate in `plugin-host` before dynamic load:
    - host must register trusted plugin root directories via `addTrustedPluginRoot(...)`
    - `loadPluginBinary(...)` rejects plugin paths outside trusted roots before opening the library
  - Clarify docs that validation is admission policy after trust gating, not a sandbox boundary.
- Alternatives considered
  - Keep metadata validation only and treat it as sufficient pre-load safety.
  - Always load any path and rely solely on post-load validation/isolation warnings.
- Consequences
  - Plugin loading now requires explicit trust configuration by host/application code.
  - Risk of accidental loading from arbitrary paths is reduced, but runtime process isolation remains a follow-up security layer.

## ADR-0011
- Date: 2026-02-10
- Status: Accepted
- Context
  - Persistence loaders accepted some semantically invalid values (for example out-of-range track indices/choke groups/step velocities) and only rejected or clamped later in recall paths.
  - Test strategy documented fuzz/stress/golden coverage but automated workflow did not run dedicated non-unit regression checks.
- Decision
  - Harden persistence parsing in `presets-rs` to fail closed at load boundary for:
    - track assignment and control track index bounds
    - choke group semantic range (`0..15`)
    - step velocity semantic range (`0..127`)
  - Add deterministic parser fuzz-safety tests for kit/pattern/project loaders.
  - Add C++ non-unit engine regression tests (`ff_engine_nonunit_tests`) covering:
    - stress render finite-output/runtime-bounded proxy checks
    - deterministic golden render reference comparison
  - Add `dev-check --with-non-unit` and schedule CI non-unit regression workflow execution.
- Alternatives considered
  - Keep late-stage clamping/rejection in control/engine only.
  - Rely on ad-hoc manual stress/golden checks with no CI schedule.
- Consequences
  - Invalid persisted state is rejected earlier and more deterministically.
  - Non-unit regressions are monitored on a scheduled CI cadence without slowing default PR checks.
