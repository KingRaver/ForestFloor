# PATCHES01 Audit Plan

Date: 2026-02-10
Scope: Phase 0 through Phase 3 verification follow-up
Source: Implementation and test audit across C++ and Rust modules

## Objective
Track and execute remediation patches for all identified gaps, with explicit traceability from finding to patch and clear done criteria.

## Findings Register (Complete)

### F-01 [P0] Plugin trust boundary bypass during load
Status: [x] Closed
Summary:
- Untrusted dynamic library code can execute in-host process before effective rejection/isolation semantics are established.
Evidence:
- `packages/plugin-host/src/host.cpp` (`DynamicLibrary::open`, `loadPluginBinary`)
- `docs/community/SECURITY.md` (external plugins are untrusted by default)
Impact:
- Malicious plugin payload may execute even when later rejected.
Mapped patch:
- Patch 01

### F-02 [P1] MIDI note index panic risk on malformed input
Status: [x] Closed
Summary:
- Note mapping indexes a 128-slot array directly from raw note bytes without a hard bounds gate.
Evidence:
- `packages/midi-rs/src/lib.rs` (`NoteMap::bind_note`, `NoteMap::resolve_track`, `parse_midi_message`)
Impact:
- Malformed input can trigger panic/DoS in caller path.
Mapped patch:
- Patch 02

### F-03 [P1] Null plugin instance activation safety gap
Status: [x] Closed
Summary:
- Plugin activation path does not explicitly reject null return from `ff_create`.
Evidence:
- `packages/plugin-host/src/host.cpp` (`activatePlugin`)
Impact:
- Invalid instance lifecycle handling and potential undefined plugin call flow.
Mapped patch:
- Patch 03

### F-04 [P2] ABI layout checks are insufficiently strict
Status: [x] Closed
Summary:
- ABI tests use weak size assertions (for example lower-bound checks), with no complete C/Rust layout lockstep assertions.
Evidence:
- `packages/abi-rs/src/lib.rs` (current ABI tests)
- `packages/abi/include/ff/abi/contracts.h` (no compile-time struct size/offset assertions)
Impact:
- Silent ABI drift risk across language boundaries.
Mapped patch:
- Patch 04

### F-05 [P2] Parser-level semantic bounds are not fully fail-closed
Status: [x] Closed
Summary:
- Some semantic validity checks happen later in recall mapping/clamping, not always at parse boundary.
Evidence:
- `packages/presets-rs/src/lib.rs` (load/deserialization paths)
- `packages/control-rs/src/lib.rs` (late-stage range handling during recall map)
Impact:
- Invalid state may enter runtime pipeline before deterministic rejection.
Mapped patch:
- Patch 05

### F-06 [P2] Security/reliability automation below documented test strategy
Status: [x] Closed
Summary:
- Fuzz, stress/XRun, golden-audio, and RT allocation instrumentation are documented but not wired into executable CI workflows.
Evidence:
- `docs/quality/TESTING.md` (declared expectations)
- `.github/workflows/ci.yml` + `tools/scripts/dev-check.sh` (currently unit/integration only)
Impact:
- Higher risk of release-time regressions and unverified non-functional guarantees.
Mapped patch:
- Patch 06

### F-07 [P2] Roadmap Phase 1 definition of done is not complete
Status: [x] Closed
Summary:
- Phase 1 feature work remains in-progress, but measurable validation support for latency/XRun-oriented regression is now wired into non-unit automation.
Evidence:
- `docs/project/ROADMAP.md` (Phase 1 marked `[-]`)
Impact:
- Project should not be represented as fully complete through Phase 3 at production standard.
Mapped patch:
- Patch 06 (test/program maturity support) + separate feature completion work outside this patch file.

## Patch Register (All Mentioned Patches)

### Patch 01: Plugin Load Boundary Hardening (P0)
Status: [x]
Findings addressed:
- [x] F-01
Target files:
- `packages/plugin-host/src/host.cpp`
- `packages/plugin-host/include/ff/plugin_host/host.hpp`
- `docs/specs/PLUGIN_SDK.md`
- `docs/community/SECURITY.md`
Checklist:
- [x] Define and implement a true pre-execution validation path where possible.
- [x] If true pre-execution validation is impossible for dynamic libs, explicitly model this in API semantics and docs.
- [x] Prevent any "validated safe" interpretation prior to trust gate.
- [x] Add trust gate policy (for example allowlist/signing/explicit opt-in) before dynamic load.
- [x] Add regression tests for admission semantics and rejection behavior.
Done when:
- [x] Threat model and runtime semantics match real loader behavior.
- [x] Tests prove tightened admission contract.

### Patch 02: MIDI Input Bounds Safety (P1)
Status: [x]
Findings addressed:
- [x] F-02
Target files:
- `packages/midi-rs/src/lib.rs`
Checklist:
- [x] Add hard note bounds checks before any note array indexing.
- [x] Reject/sanitize malformed MIDI note/value bytes consistently.
- [x] Add no-panic malformed-input tests.
Done when:
- [x] No panic path remains from malformed note input.
- [x] New bounds tests pass in workspace tests.

### Patch 03: Plugin Activation Null-Instance Safety (P1)
Status: [x]
Findings addressed:
- [x] F-03
Target files:
- `packages/plugin-host/src/host.cpp`
- `packages/plugin-host/tests/host_tests.cpp`
Checklist:
- [x] Fail activation when `ff_create` returns null.
- [x] Ensure no lifecycle call path runs on invalid/null instance.
- [x] Add regression tests for null-create and cleanup behavior.
Done when:
- [x] Activation failure is explicit and safe.
- [x] Counters/state remain consistent in failure scenarios.

### Patch 04: ABI Contract Assertion Hardening (P2)
Status: [x]
Findings addressed:
- [x] F-04
Target files:
- `packages/abi/include/ff/abi/contracts.h`
- `packages/abi-rs/src/lib.rs`
Checklist:
- [x] Add strict size assertions for ABI structs in C and Rust.
- [x] Add field-offset assertions for critical ABI structs in Rust tests.
- [x] Replace broad lower-bound checks with exact layout checks.
Done when:
- [x] ABI drift causes deterministic test/build failure cross-language.

### Patch 05: Preset Parser Semantic Bound Enforcement (P2)
Status: [x]
Findings addressed:
- [x] F-05
Target files:
- `packages/presets-rs/src/lib.rs`
- `packages/control-rs/src/lib.rs`
Checklist:
- [x] Enforce semantic bounds at parse boundary (track indices, velocity ranges, choke-group ranges).
- [x] Reject malformed persisted payloads with deterministic errors.
- [x] Add negative tests for malformed payload cases.
Done when:
- [x] Parser rejects invalid semantic values before runtime mapping.
- [x] Error behavior is stable and test-covered.

### Patch 06: Security and Reliability Test Program Completion (P2)
Status: [x]
Findings addressed:
- [x] F-06
- [x] F-07 (supporting non-functional completion path)
Target files:
- `tools/scripts/dev-check.sh`
- `.github/workflows/ci.yml`
- `docs/quality/TESTING.md`
- `docs/project/ROADMAP.md` (status updates once complete)
Checklist:
- [x] Add parser fuzz coverage for persistence inputs.
- [x] Add stress/XRun validation path for realtime behavior.
- [x] Add at least one golden-audio deterministic render test.
- [x] Wire these checks into CI schedule/release gates and document cadence.
- [x] Update roadmap/test docs to match implemented automation.
Done when:
- [x] Documented strategy aligns with executable automated checks.
- [x] Phase 1 DoD prerequisites have measurable validation support.

## Patch Execution Order
1. Patch 01
2. Patch 02
3. Patch 03
4. Patch 04
5. Patch 05
6. Patch 06

## Global Verification Checklist (Per Patch)
- [x] Code changes complete and reviewed.
- [x] New tests added and passing.
- [x] Existing tests remain green via `./tools/scripts/dev-check.sh --clean`.
- [x] Relevant docs/specs updated.
- [x] ADR updated in `docs/project/DECISIONS.md` when contract/architecture behavior changes.
