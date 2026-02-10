# Contributing

## Working Principles
- Respect real-time safety constraints.
- Preserve module boundaries.
- Document decisions that change public contracts.

## Branch and PR Workflow
1. Create a focused branch.
2. Keep changes scoped to a clear module objective.
3. Include tests for behavior changes.
4. Update docs/specs when contracts change.

Local commands and checks are documented in `docs/development/WORKFLOW.md`.
Before opening a PR, run `./tools/scripts/dev-check.sh --clean`.

## Required PR Contents
- Problem statement.
- Design summary.
- Test evidence.
- Risk notes and rollback plan.

## Contract Change Policy
- Any change to event/parameter/plugin contracts requires:
  - ADR update in `docs/project/DECISIONS.md`.
  - Versioning notes in relevant spec files.

## Real-Time Review Checklist
- No allocations on RT path.
- No blocking or lock contention in audio callback.
- Bounded per-block CPU behavior.
