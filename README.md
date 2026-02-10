# Forest Floor

Forest Floor is a modular, standalone drum machine platform designed for long-term composability.

## Goals
- Real-time reliable audio and MIDI behavior.
- Clear module boundaries so features can be added without rewrites.
- Plugin-ready architecture for instruments, effects, and MIDI processors.
- Monorepo workflow with shared specs, tests, and release standards.

## Core Design Direction
- C++ for DSP hot paths and real-time audio callback processing.
- Rust for control plane logic: sequencing, MIDI mapping, state, presets, and orchestration.
- UI as a replaceable client over stable engine/control interfaces.

## Documentation Index
- Overview: `docs/00-overview.md`
- Developer workflow: `docs/development/WORKFLOW.md`
- Build guide: `docs/build/BUILD.md`
- Architecture: `docs/architecture/ARCHITECTURE.md`
- Realtime constraints: `docs/architecture/THREADING_AND_REALTIME.md`
- Module ownership: `docs/architecture/MODULES.md`
- Plugin contract: `docs/specs/PLUGIN_SDK.md`
- Parameters: `docs/specs/PARAMETERS.md`
- Events: `docs/specs/EVENTS.md`
- State and presets: `docs/specs/STATE_AND_PRESETS.md`
- Roadmap: `docs/project/ROADMAP.md`
- ADR log: `docs/project/DECISIONS.md`
- Testing strategy: `docs/quality/TESTING.md`
- Contribution rules: `docs/community/CONTRIBUTING.md`
- Release checklist: `docs/release/RELEASE_CHECKLIST.md`

## Status
Documentation baseline, core Phase 0-4 infrastructure, and release automation are implemented.
