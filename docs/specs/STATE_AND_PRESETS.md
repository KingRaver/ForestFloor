# State and Presets Specification

## Persistent Objects
- Project: full session graph, transport, kits, mappings.
- Kit: track-to-sound assignments and per-track settings.
- Pattern: sequencer event data and automation lanes.
- MIDI profile: device-specific mapping bindings.

## Storage Principles
- Human-inspectable formats for top-level project metadata.
- Binary sidecar blobs allowed for large sample/index data.
- Schema version included in every persisted object.

## Current Reference Format (Phase 2)
- `presets-rs` currently uses deterministic text serialization for:
  - Kits (`FF_KIT_V1`)
  - Patterns (`FF_PATTERN_V1`)
  - Projects (`FF_PROJECT_V1`)
- The format is intentionally stable for save/load roundtrip tests and offline development.
- Recall output from saved projects currently includes:
  - deterministic sequencer event replay payloads
  - deterministic engine parameter update payloads (numeric ABI IDs)

## Suggested Layout
- `projects/<name>/project.json`
- `projects/<name>/kits/*.json`
- `projects/<name>/patterns/*.json`
- `projects/<name>/assets/` for copied or referenced media manifests.

## Versioning and Migration
- Strict monotonic schema versions.
- Migrations are deterministic and tested.
- Forward compatibility is best effort; backward compatibility is guaranteed within major version.

## Sample Referencing
- Store both logical sample IDs and file references.
- Missing file strategy: warn, keep placeholder, allow relink.
- Never silently drop missing sample references.

## Autosave and Recovery
- Atomic write via temp file + rename.
- Journal last known good state.
- On crash restart, prompt with recovery candidate.
