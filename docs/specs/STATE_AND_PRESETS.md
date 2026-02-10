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
